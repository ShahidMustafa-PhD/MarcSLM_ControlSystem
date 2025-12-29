#pragma once

#include "readSlices.h"
#include "Scanner.h"

#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <functional>
#include <chrono>
#include <cmath>

// ============================================================================
// file2RTC Class - Converts MARC files to RTC5 Scanner operations
// ============================================================================
// 
// DESIGN NOTES:
// - Designed for single-threaded scanner usage (Scanner is NOT thread-safe)
// - Worker thread OWNS the Scanner instance exclusively
// - Main thread manages job queue and state via atomics/mutexes
// - Layer data is copied to worker to avoid lifetime issues
//
// USAGE PATTERN:
//   1. Create file2RTC instance
//   2. Call loadFile() to queue layers
//   3. Call start() to spawn worker thread
//   4. Use pause() / resume() / stop() to control execution
//   5. Query state via getState()
//
class file2RTC {
public:
    enum class State { Idle, Loading, Ready, Running, Paused, Stopping, Error };

    struct Calib {
        double fieldSizeMM = 163.4;     // full field size (from f-theta & scan angle)
        long   maxBits = 524287;        // +/- max (20-bit signed -> 2^19-1)
        double scaleCorrection = 1.0;   // user calibration factor
        // convenience: compute bits-per-mm
        double bitsPerMM() const {
            return (2.0 * static_cast<double>(maxBits)) / fieldSizeMM * scaleCorrection;
        }
    };

    file2RTC();
    ~file2RTC();

    // non-copyable
    file2RTC(const file2RTC&) = delete;
    file2RTC& operator=(const file2RTC&) = delete;

    // High level
    bool loadFile(const std::wstring& path);   // load .marc into internal job queue
    bool start();                              // start worker thread if ready
    bool pause();                              // request pause
    bool resume();                             // resume from pause
    bool stop();                               // stop (graceful)
    State getState() const;

    // Calibration
    void setCalibration(const Calib& c);
    Calib getCalibration() const;

    // Callbacks
    void setLogCallback(std::function<void(const std::string&)> cb);
    void setProgressCallback(std::function<void(size_t layerIndex, size_t total)> cb);

private:
    // worker
    void workerThreadFunc();
    void processLayer(const marc::Layer& layer, size_t layerIndex);

    // helpers
    long mmToBits(double mm) const;
    Scanner::Point toScannerPoint(const marc::Point& p) const;
    void log(const std::string& s);

    // state
    mutable std::mutex mMutex;
    std::condition_variable mCv;
    std::thread mWorker;
    std::atomic<State> mState;
    std::atomic<bool> mStopRequested;
    std::atomic<bool> mPauseRequested;

    // job queue (copies of layers)
    std::deque<marc::Layer> mJobQueue;
    size_t mTotalLayers{0};

    // dependencies
    Scanner mScanner;            // owned scanner; only used by worker thread
    Calib mCalib;

    // callbacks
    std::function<void(const std::string&)> mLogCb;
    std::function<void(size_t, size_t)> mProgressCb;
};
