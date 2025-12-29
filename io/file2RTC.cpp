#include "file2RTC.h"
#include <sstream>
#include <iomanip>
#include <cmath>

using namespace std::chrono_literals;

// ============================================================================
// Constructor / Destructor
// ============================================================================

file2RTC::file2RTC()
  : mState(State::Idle),
    mStopRequested(false),
    mPauseRequested(false),
    mCalib()
{
    // leave mScanner uninitialized until worker runs
}

file2RTC::~file2RTC()
{
    stop();
    if (mWorker.joinable()) {
        mWorker.join();
    }
}

// ============================================================================
// Callback Management
// ============================================================================

void file2RTC::setLogCallback(std::function<void(const std::string&)> cb) {
    std::lock_guard<std::mutex> lk(mMutex);
    mLogCb = std::move(cb);
}

void file2RTC::setProgressCallback(std::function<void(size_t, size_t)> cb) {
    std::lock_guard<std::mutex> lk(mMutex);
    mProgressCb = std::move(cb);
}

// ============================================================================
// Configuration
// ============================================================================

void file2RTC::setCalibration(const Calib& c) {
    std::lock_guard<std::mutex> lk(mMutex);
    mCalib = c;
}

file2RTC::Calib file2RTC::getCalibration() const {
    std::lock_guard<std::mutex> lk(mMutex);
    return mCalib;
}

file2RTC::State file2RTC::getState() const {
    return mState.load();
}

// ============================================================================
// File Loading
// ============================================================================

bool file2RTC::loadFile(const std::wstring& path) {
    std::lock_guard<std::mutex> lk(mMutex);

    State current = mState.load();
    if (current == State::Running || current == State::Stopping) {
        log("Cannot load file while running or stopping");
        return false;
    }

    mState = State::Loading;
    mJobQueue.clear();
    mTotalLayers = 0;

    try {
        marc::readSlices reader;
        if (!reader.open(path)) {
            log("Failed to open MARC file: " + std::string(path.begin(), path.end()));
            mState = State::Error;
            return false;
        }

        const auto& layers = reader.layers();
        mTotalLayers = layers.size();
        
        if (mTotalLayers == 0) {
            log("WARNING: File contains no layers");
        }
        
        // FIX: Copy layers to job queue (avoid lifetime issues)
        mJobQueue.insert(mJobQueue.end(), layers.begin(), layers.end());

        std::ostringstream ss;
        ss << "Loaded file '" << std::string(path.begin(), path.end())
           << "' (" << mTotalLayers << " layers)";
        log(ss.str());

        mState = State::Ready;
        return true;
    } catch (const std::exception& e) {
        log(std::string("Exception while loading file: ") + e.what());
        mState = State::Error;
        return false;
    }
}

// ============================================================================
// Execution Control
// ============================================================================

bool file2RTC::start() {
    std::lock_guard<std::mutex> lk(mMutex);
    
    State current = mState.load();
    if (current != State::Ready && current != State::Paused) {
        log("Not ready to start (state: " + std::to_string(static_cast<int>(current)) + ")");
        return false;
    }
    
    if (mWorker.joinable()) {
        log("Worker already running - join previous first");
        return false;
    }
    
    // FIX: Initialize scanner with explicit configuration
    // CRITICAL: Scanner uses Windows API and RTC5 DLL - must be called on worker thread context
    // However, we initialize BEFORE spawning the thread to check early for errors
    Scanner::Config config;
    config.cardNumber = 1;
    config.listMemory = 10000;
    config.markSpeed = 250.0;
    config.jumpSpeed = 1000.0;
    config.laserMode = 1;
    config.analogOutValue = 640;
    config.analogOutStandby = 0;
    
    // Set scanner logging callback
    mScanner.setLogCallback([this](const std::string& msg) { log(msg); });
    
    if (!mScanner.initialize(config)) {
        log("Scanner initialization failed");
        mState = State::Error;
        return false;
    }

    mStopRequested = false;
    mPauseRequested = false;
    mState = State::Running;
    
    // FIX: Spawn worker thread (unlock before thread creation)
    mWorker = std::thread(&file2RTC::workerThreadFunc, this);
    
    return true;
}

bool file2RTC::pause() {
    State current = mState.load();
    if (current != State::Running) {
        return false;
    }
    mPauseRequested = true;
    mState = State::Paused;
    log("Pause requested");
    return true;
}

bool file2RTC::resume() {
    State current = mState.load();
    if (current != State::Paused) {
        return false;
    }
    mPauseRequested = false;
    mCv.notify_all();
    mState = State::Running;
    log("Resuming");
    return true;
}

bool file2RTC::stop() {
    State prev = mState.load();
    if (prev == State::Idle || prev == State::Stopping || prev == State::Error) {
        // Already stopped or stopping
        if (mWorker.joinable()) {
            mWorker.join();
        }
        mState = State::Idle;
        return true;
    }
    
    mState = State::Stopping;
    mStopRequested = true;
    mCv.notify_all();

    if (mWorker.joinable()) {
        // FIX: Avoid deadlock by checking if this is the worker thread
        if (std::this_thread::get_id() != mWorker.get_id()) {
            mWorker.join();
        }
    }

    // shutdown scanner
    mScanner.shutdown();

    mState = State::Idle;
    log("Stopped");
    return true;
}

// ============================================================================
// Worker Thread
// ============================================================================

void file2RTC::workerThreadFunc() {
    log("Worker thread started");

    size_t layerIndex = 0;
    size_t total = mTotalLayers;

    while (!mStopRequested) {
        // Pause handling
        // FIX: Proper pause/resume with condition variable
        if (mPauseRequested) {
            log("Worker paused");
            std::unique_lock<std::mutex> lk(mMutex);
            mCv.wait(lk, [this]{ return !mPauseRequested || mStopRequested; });
            if (mStopRequested) break;
            log("Worker resumed");
        }

        // Get next layer from queue
        marc::Layer layer;
        {
            std::lock_guard<std::mutex> lk(mMutex);
            if (mJobQueue.empty()) break;
            layer = std::move(mJobQueue.front());
            mJobQueue.pop_front();
        }

        // process copy outside lock (NO LOCK DURING SCANNER OPS!)
        try {
            processLayer(layer, layerIndex);
        } catch (const std::exception& e) {
            log(std::string("Error processing layer ") + std::to_string(layerIndex) + 
                std::string(": ") + e.what());
            mState = State::Error;
            break;
        }

        layerIndex++;
        
        // FIX: Call progress callback with proper locking
        {
            std::lock_guard<std::mutex> lk(mMutex);
            if (mProgressCb) {
                mProgressCb(layerIndex, total);
            }
        }
    }

    log("Worker thread finished");
    
    // FIX: Set final state only if not already in Error state
    if (mState != State::Error) {
        mState = (mStopRequested ? State::Idle : State::Ready);
    }
}

// ============================================================================
// Layer Processing
// ============================================================================

void file2RTC::processLayer(const marc::Layer& L, size_t layerIndex) {
    std::ostringstream ss;
    
    // Log layer info
    ss.str("");
    ss << "Processing layer " << layerIndex << " (Z: " << L.layerHeight << " mm, "
       << L.hatches.size() << " hatches, "
       << L.polylines.size() << " polylines, "
       << L.polygons.size() << " polygons)";
    log(ss.str());

    // 1) Handle Z movement if available
    // FIX: layerHeight is in mm, needs conversion to bits
    if (L.layerHeight != 0.0f) {
        // Convert to scanner coordinates
        long zBits = static_cast<long>(std::lround(
            static_cast<double>(L.layerHeight) * mCalib.bitsPerMM()
        ));
        ss.str("");
        ss << "Layer " << layerIndex << " Z bits: " << zBits;
        log(ss.str());
        // NOTE: RTC5 may not support direct Z control via this interface
        // Use manual stepper control if implemented in Scanner class
    }

    // Prepare command list
    // FIX: Ensure scanner is ready for new list
    if (!mScanner.isInitialized()) {
        throw std::runtime_error("Scanner not initialized");
    }

    // 2) Process Hatches
    // FIX: Each line in hatch is a pair of points (a, b)
    for (size_t hIdx = 0; hIdx < L.hatches.size(); ++hIdx) {
        const auto& h = L.hatches[hIdx];
        
        for (size_t lIdx = 0; lIdx < h.lines.size(); ++lIdx) {
            const auto& line = h.lines[lIdx];
            Scanner::Point a = toScannerPoint(line.a);
            Scanner::Point b = toScannerPoint(line.b);

            // FIX: Check point validity before submitting
            if (!a.isValid() || !b.isValid()) {
                ss.str("");
                ss << "WARNING: Invalid point in hatch " << hIdx << " line " << lIdx
                   << " (" << a.x << "," << a.y << ") -> ("
                   << b.x << "," << b.y << ")";
                log(ss.str());
                continue;  // Skip invalid geometry
            }

            // Move laser OFF (jump)
            if (!mScanner.jumpTo(a)) {
                throw std::runtime_error("jumpTo failed in hatch");
            }
            // Mark with laser ON
            if (!mScanner.markTo(b)) {
                throw std::runtime_error("markTo failed in hatch");
            }
        }
    }

    // 3) Process Polylines (connected segments)
    for (size_t pIdx = 0; pIdx < L.polylines.size(); ++pIdx) {
        const auto& pl = L.polylines[pIdx];
        
        if (pl.points.empty()) {
            log("WARNING: Empty polyline");
            continue;
        }
        
        // FIX: Jump to first point with validation
        Scanner::Point p0 = toScannerPoint(pl.points[0]);
        if (!p0.isValid()) {
            log("WARNING: Invalid start point in polyline");
            continue;
        }
        
        if (!mScanner.jumpTo(p0)) {
            throw std::runtime_error("jumpTo failed (polyline start)");
        }
        
        for (size_t i = 1; i < pl.points.size(); ++i) {
            Scanner::Point pi = toScannerPoint(pl.points[i]);
            
            if (!pi.isValid()) {
                ss.str("");
                ss << "WARNING: Invalid point in polyline at index " << i;
                log(ss.str());
                continue;
            }
            
            if (!mScanner.markTo(pi)) {
                throw std::runtime_error("markTo failed (polyline)");
            }
        }
    }

    // 4) Process Polygons (closed loops)
    for (size_t pgIdx = 0; pgIdx < L.polygons.size(); ++pgIdx) {
        const auto& pg = L.polygons[pgIdx];
        
        if (pg.points.empty()) {
            log("WARNING: Empty polygon");
            continue;
        }
        
        // FIX: Jump to first point
        Scanner::Point p0 = toScannerPoint(pg.points[0]);
        if (!p0.isValid()) {
            log("WARNING: Invalid start point in polygon");
            continue;
        }
        
        if (!mScanner.jumpTo(p0)) {
            throw std::runtime_error("jumpTo failed (polygon start)");
        }
        
        // Mark all segments
        for (size_t i = 1; i < pg.points.size(); ++i) {
            Scanner::Point pi = toScannerPoint(pg.points[i]);
            
            if (!pi.isValid()) {
                ss.str("");
                ss << "WARNING: Invalid point in polygon at index " << i;
                log(ss.str());
                continue;
            }
            
            if (!mScanner.markTo(pi)) {
                throw std::runtime_error("markTo failed (polygon segment)");
            }
        }
        
        // FIX: Close the loop by returning to first point
        if (!mScanner.markTo(p0)) {
            throw std::runtime_error("markTo failed (polygon close)");
        }
    }

    // 5) Process Support Circles
    // FIX: Approximate circles with polylines
    for (size_t cIdx = 0; cIdx < L.support_circles.size(); ++cIdx) {
        const auto& c = L.support_circles[cIdx];
        
        // FIX: Validate circle parameters
        if (c.radius <= 0.0f) {
            log("WARNING: Invalid circle radius");
            continue;
        }
        
        constexpr double twoPi = 2.0 * 3.14159265358979323846;
        const unsigned steps = 36; // 36 segments (10° steps)
        
        std::vector<Scanner::Point> pts;
        pts.reserve(steps + 1);
        
        for (unsigned i = 0; i <= steps; ++i) {
            double angle = (static_cast<double>(i) / steps) * twoPi;
            marc::Point mp;
            mp.x = c.center.x + c.radius * static_cast<float>(std::cos(angle));
            mp.y = c.center.y + c.radius * static_cast<float>(std::sin(angle));
            pts.push_back(toScannerPoint(mp));
        }
        
        if (!pts.empty()) {
            Scanner::Point start = pts.front();
            if (!start.isValid()) {
                log("WARNING: Invalid start point for circle");
                continue;
            }
            
            if (!mScanner.jumpTo(start)) {
                throw std::runtime_error("jumpTo failed (circle)");
            }
            
            for (size_t i = 1; i < pts.size(); ++i) {
                if (!pts[i].isValid()) {
                    ss.str("");
                    ss << "WARNING: Invalid point in circle at index " << i;
                    log(ss.str());
                    continue;
                }
                
                if (!mScanner.markTo(pts[i])) {
                    throw std::runtime_error("markTo failed (circle segment)");
                }
            }
        }
    }

    // 6) Execute the accumulated command list
    if (!mScanner.executeList()) {
        throw std::runtime_error("executeList failed");
    }

    // 7) Wait for scanner to finish this layer
    // FIX: Use reasonable timeout (30 seconds per layer)
    if (!mScanner.waitForListCompletion(30000)) {
        throw std::runtime_error("Scanner list did not finish in time");
    }
    
    ss.str("");
    ss << "Layer " << layerIndex << " completed successfully";
    log(ss.str());
}

// ============================================================================
// Coordinate Conversion Helpers
// ============================================================================

long file2RTC::mmToBits(double mm) const {
    // FIX: Use local copy to avoid holding lock during complex calculation
    Calib c = getCalibration();
    double bits = mm * c.bitsPerMM();
    
    // Clamp to valid range (-maxBits .. +maxBits)
    long mx = static_cast<long>(c.maxBits);
    if (bits > mx) bits = mx;
    if (bits < -mx) bits = -mx;
    
    return static_cast<long>(std::lround(bits));
}

Scanner::Point file2RTC::toScannerPoint(const marc::Point& p) const {
    // FIX: Convert marc::Point (float x, y in mm) to Scanner::Point (long x, y in bits)
    long xb = mmToBits(static_cast<double>(p.x));
    long yb = mmToBits(static_cast<double>(p.y));
    
    // FIX: Scanner::Point has validation - use it
    Scanner::Point sp(xb, yb);
    
    return sp;
}

// ============================================================================
// Logging
// ============================================================================

void file2RTC::log(const std::string& s) {
    std::lock_guard<std::mutex> lk(mMutex);
    if (mLogCb) {
        mLogCb(s);
    }
}
