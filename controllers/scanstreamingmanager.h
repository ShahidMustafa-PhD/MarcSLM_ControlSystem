#pragma once

#include <QObject>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <deque>
#include <atomic>
#include <memory>
#include <string>

#include "io/readSlices.h"
#include "io/buildstyle.h"
#include "io/rtccommandblock.h"
#include "Scanner.h"

// ============================================================================
// Forward Declarations
// ============================================================================
class OPCServerManagerUA;  // Forward declaration for OPC UA integration

// ============================================================================
// Process Mode Enumeration
// ============================================================================

/**
 * @brief ProcessMode - Distinguishes between production and test workflows
 * 
 * Production: Slice-file driven, reads layerThickness from MARC, creates physical layers via OPC
 * Test: Manual/synthetic, fixed parameters, isolated from production pipeline
 */
enum class ProcessMode {
    Production,  // Slice-file driven SLM process
    Test         // Test-only process without slice file
};

// ============================================================================
// ScanStreamingManager - Producer-Consumer streaming MARC -> RTC execution
// ============================================================================
// 
// INDUSTRIAL SLM THREAD STARTUP SEQUENCE:
// 1. OPC server thread created and started (owns OPCServerManager)
// 2. Wait for OPC to signal "initialized"
// 3. Consumer thread created and started (owns Scanner, waits for OPC signals)
// 4. Producer thread created and started (reads MARC or generates synthetic layers)
// 
// LAYER EXECUTION LOOP (per-layer synchronization):
// 1. Producer enqueues RTCCommandBlock
// 2. Consumer pops block, waits for OPC "layer prepared" signal
// 3. OPC thread calls writeLayerParameters(layerNumber, deltaValue, deltaValue)
// 4. OPC notifies consumer: "layer ready"
// 5. Consumer executes scan vectors with parameter switching
// 6. Consumer turns laser OFF
// 7. Consumer notifies OPC: "layer execution complete" (future: implement)
// 8. Repeat until last layer
//
// THREAD TERMINATION:
// 1. Producer exits when MARC exhausted or stop requested
// 2. Consumer exits when queue empty and no more layers coming
// 3. OPC thread exits after all device cleanup
//
class ScanStreamingManager : public QObject {
    Q_OBJECT

public:
    explicit ScanStreamingManager(QObject* parent = nullptr);
    ~ScanStreamingManager();

    // non-copyable
    ScanStreamingManager(const ScanStreamingManager&) = delete;
    ScanStreamingManager& operator=(const ScanStreamingManager&) = delete;

    // MAIN THREAD INTERFACE
    // Load config.json and initialize BuildStyleLibrary (DEPRECATED - now loaded in consumer thread)
    bool loadScanConfig(const std::wstring& configJsonPath);

    // ========== PRODUCTION MODE ========= =
    // Slice-file driven SLM process with OPC synchronization
    // Thread startup order: OPC ? Consumer ? Producer
    // Consumer thread loads BuildStyleLibrary from configJsonPath before initialization
    bool startProcess(const std::wstring& marcPath, const std::wstring& configJsonPath);
    
    // ========== TEST MODE ========= =
    // Synthetic layer generation without MARC file
    // Consumer generates test patterns, OPC disabled
    bool startTestProcess(float testLayerThickness, size_t testLayerCount);
    
    // Stop gracefully (all threads must exit safely)
    void stopProcess();
    
    // Emergency stop: disable laser immediately, abort all operations
    void emergencyStop();
    
    // Called by GUI when OPC signals "layer prepared"
    void notifyPLCPrepared();

    // Configure queue size (bounded, default 4 layers)
    void setMaxQueuedLayers(size_t sz) { mMaxQueue = (sz < 2 ? 2 : (sz > 10 ? 10 : sz)); }

    // Query scan config status
    bool hasScanConfig() const { return !mBuildStyles.isEmpty(); }
    const marc::BuildStyleLibrary& scanConfig() const { return mBuildStyles; }

    // ========== OPC INTEGRATION ========= =
    // Set OPC UA manager reference (called from SLMWorkerManager)
    void setOPCManager(OPCServerManagerUA* opcMgr) { mOPCManager = opcMgr; }
    
    // Signal OPC UA that layer execution is complete (for future bidirectional sync)
    void notifyLayerExecutionComplete(uint32_t layerNumber);

signals:
    // Emitted on worker thread, caught by GUI via Qt
    void statusMessage(const QString& msg);
    void progress(int layersProcessed, int totalLayers);  // FIX: Was size_t, now int (Qt-safe)
    void finished();
    void error(const QString& message);
    void layerExecuted(uint32_t layerNumber);
    void configLoaded(const QString& configPath);

public slots:
    // Can be connected from GUI actions
    void onPLCLayerPrepared() { notifyPLCPrepared(); }

private:
    // ========== PRODUCER THREAD ==========
    // Streams layers from MARC file and converts to command blocks with parameters
    void producerThreadFunc(const std::wstring& marcPath);
    
    // ========== PRODUCER THREAD (TEST MODE) ========= =
    // Generates synthetic layers for testing (runs in consumer thread in test mode)
    void producerTestThreadFunc(float layerThickness, size_t layerCount);

    // ========== CONSUMER THREAD ==========
    // Owns Scanner, executes command blocks with OPC layer synchronization
    void consumerThreadFunc();

    // ========== CONVERSION LOGIC ==========
    // Convert marc::Layer to RTCCommandBlock with parameter segments
    bool convertLayerToBlock(const marc::Layer& L, marc::RTCCommandBlock& out);
    
    // Convert geometry segment to RTC commands, selecting BuildStyle based on GeometryTag
    bool convertHatch(const marc::Hatch& h, marc::RTCCommandBlock& out, size_t& cmdStartIdx);
    bool convertPolyline(const marc::Polyline& p, marc::RTCCommandBlock& out, size_t& cmdStartIdx);
    bool convertPolygon(const marc::Polygon& p, marc::RTCCommandBlock& out, size_t& cmdStartIdx);

    // Apply laser parameters for geometry segment
    void applyBuildStyle(const marc::BuildStyle* style, marc::RTCCommandBlock& out, size_t cmdStartIdx);
    
    // Convert float mm coordinates to long bits (RTC5 coordinate system)
    long mmToBits(double mm) const;
    
    // ========== THREAD-SAFE QUEUE ==========
    std::mutex mMutex;
    std::condition_variable mCvProducerNotFull;   // wake producer when consumer pops
    std::condition_variable mCvConsumerNotEmpty;  // wake consumer when producer pushes
    std::condition_variable mCvPLCNotified;       // wake consumer when OPC signals "layer prepared"
    std::condition_variable mCvOPCReady;          // wake main thread when OPC initialized
    std::condition_variable mCvLayerRequested;    // NEW: wake producer when consumer requests next layer
    
    std::deque<std::shared_ptr<marc::RTCCommandBlock>> mQueue;
    size_t mMaxQueue{1}; // INDUSTRIAL REFINEMENT: Queue size is 1 for single-piece flow
    
    // ========== CONTROL FLAGS ==========
    std::atomic<bool> mStopRequested{false};
    std::atomic<bool> mPLCPrepared{false};
    std::atomic<bool> mOPCInitialized{false};
    std::atomic<bool> mEmergencyStopFlag{false};
    std::atomic<bool> mProducerFinished{false};      // NEW: signals producer thread has finished all layers
    std::atomic<bool> mLayerRequested{false};        // NEW: signals consumer requests next layer
    
    // ========== PROCESS MODE =========
    ProcessMode mProcessMode{ProcessMode::Production};
    
    // ========== WORKER THREADS =========
    std::thread mProducerThread;
    std::thread mConsumerThread;
    std::thread mTestProducerThread;
    
    // ========== COUNTERS & METADATA ==========
    std::atomic<size_t> mTotalLayers{0};
    std::atomic<size_t> mLayersProduced{0};
    std::atomic<size_t> mLayersConsumed{0};
    std::atomic<uint32_t> mCurrentLayerNumber{0};
    
    // ========== SCAN CONFIGURATION (PARAMETER LIBRARY) =========
    marc::BuildStyleLibrary mBuildStyles;
    std::wstring mConfigJsonPath;  // NEW: Path to JSON configuration file (loaded in consumer thread)
    
    // ========== SCANNER CONFIGURATION =========
    Scanner::Config mScannerConfig;
    
    // ========== COORDINATE CALIBRATION =========
    struct CoordCalib {
        double fieldSizeMM = 163.4;     // f-theta field size
        long maxBits = 524287;          // +/- max (20-bit signed)
        double scaleCorrection = 1.0;   // user calibration
        
        double bitsPerMM() const {
            return (2.0 * static_cast<double>(maxBits)) / fieldSizeMM * scaleCorrection;
        }
    } mCalib;
    
    // ========== OPC INTEGRATION =========
    // Reference to OPC UA manager (owned by SLMWorkerManager OPC worker thread)
    OPCServerManagerUA* mOPCManager{nullptr};
};
