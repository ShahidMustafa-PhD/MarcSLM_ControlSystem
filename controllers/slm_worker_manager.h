// ============================================================================
// slm_worker_manager.h
// ============================================================================
//
// INDUSTRIAL SLM WORKER ARCHITECTURE
//
// DESIGN PRINCIPLE:
// ???????????????????????????????????????????????????????????????????????????
// Only OPC server requires a dedicated worker thread because:
//  1. OPC is a blocking COM interface (must be in single dedicated thread)
//  2. Scanner is owned by ScanStreamingManager's consumer thread
//  3. Consumer thread already manages Scanner lifecycle (init/use/destroy)
//  4. ScanStreamingManager producer/consumer is sufficient threading model
//
// THREAD TOPOLOGY:
// ?????????????????????????????????????????????????????????????????????????
// GUI Thread (Main)
//  ?? ProcessController
//      ?? Starts SLMWorkerManager (OPC only)
//      ?? Starts ScanStreamingManager (Producer/Consumer)
//
// OPC Worker Thread (std::thread)
//  ?? OPCWorker (QObject, thread-affinity bound)
//      ?? OPCServerManager (owns COM interfaces)
//
// Producer Thread (ScanStreamingManager)
//  ?? Reads MARC, converts to RTCCommandBlock, enqueues
//
// Consumer Thread (ScanStreamingManager)
//  ?? Owns Scanner (created here, destroyed here)
//  ?? Executes commands, waits for OPC completion signal
//
// COMMUNICATION FLOW:
// ?????????????????????????????????????????????????????????????????????????
// 1. OPC detects layer ready (powder surface complete)
//    ?? OPCWorker::layerReadyForScanning() signal
//
// 2. ProcessController receives signal
//    ?? Calls ScanStreamingManager::notifyPLCPrepared()
//
// 3. Consumer thread wakes and executes layer
//    ?? Scans geometry on Scanner device
//
// 4. Consumer finishes layer
//    ?? Calls OPCManager->notifyLayerExecutionComplete()
//
// 5. OPC processes next layer (loop repeats)
//
// ADVANTAGES OF THIS DESIGN:
// ?????????????????????????????????????????????????????????????????????????
// ? Single OPC worker thread (minimal threads)
// ? Scanner owned by consumer thread (no shared device access)
// ? Clear producer/consumer model (producer reads MARC, consumer executes)
// ? Proper layering (ScanStreamingManager handles streaming logic)
// ? No redundant threads (ScannerWorker was unnecessary)
// ? Bidirectional coordination (OPC?Scanner?OPC handshake)
// ? Industrial-grade (follows RTC5/ScanLab best practices)
//
// ============================================================================

#ifndef SLM_WORKER_MANAGER_H
#define SLM_WORKER_MANAGER_H

#include <QObject>
#include <QString>
#include <memory>
#include <thread>
#include <mutex>
#include <condition_variable>
#include <atomic>

class OPCServerManagerUA;
class Scanner;
class ScanStreamingManager;

// ============================================================================
// OPCWorker - Manages OPC UA server in dedicated thread
// ============================================================================
//
// PURPOSE:
// ???????????????????????????????????????????????????????????????????????
// Encapsulates OPC UA server initialization and operations. OPC UA client
// can be safely accessed from worker thread, so this worker thread provides
// exclusive OPC UA ownership.
//
// LIFECYCLE:
// ???????????????????????????????????????????????????????????????????????
// [GUI Thread]
//   ?? SLMWorkerManager::startWorkers()
//       ?? Create std::thread ? OPCWorker runs initialize()
//           ?? Create OPCServerManagerUA
//           ?? Initialize OPC UA (connect to OPC UA server endpoint)
//           ?? Emit initialized(true)
//           ?? Wait on condition_variable for shutdown signal
//
// SIGNALS EMITTED TO GUI:
// ???????????????????????????????????????????????????????????????????????
// • initialized(bool success)      - Emitted after init complete
// • layerReadyForScanning()        - OPC UA detected layer ready (powder done)
// • layerCompleted(int)            - OPC UA notified layer execution done
// • error(QString)                 - Connection/operation error
//
// THREAD SAFETY:
// ???????????????????????????????????????????????????????????????????????
// ? OPCServerManagerUA pointer stable after initialization
// ? Can be read from GUI thread (atomic pointer access)
// ? All operations are Qt::QueuedConnection (safe cross-thread signals)
//

class OPCWorker : public QObject {
    Q_OBJECT

public:
    explicit OPCWorker(QObject* parent = nullptr);
    ~OPCWorker();

    // Thread-safe read of OPC UA manager (pointer is stable after init)
    OPCServerManagerUA* getOPCManager() const { return mOPCManager.get(); }

public slots:
    // ========== Initialization (called in worker thread) ==========
    //
    // Creates OPCServerManagerUA and initializes OPC UA connection.
    // Emits initialized(true) on success, initialized(false) on failure.
    // Then enters wait loop until shutdown signal received.
    //
    void initialize();

    // ========== Shutdown (called in worker thread) ==========
    //
    // Stops OPC UA operation, destroys OPCServerManagerUA, exits thread.
    // Called by SLMWorkerManager when stopWorkers() is invoked.
    //
    void shutdown();

    // ========== OPC UA Operations (called from GUI thread via Qt::QueuedConnection) ==========
    //
    // These slot implementations allow GUI to invoke OPC UA operations safely.
    // All access to mOPCManager is protected by worker thread affinity.
    //
    void writeStartUp(bool value);
    void writePowderFillParameters(int layers, int deltaSource, int deltaSink);
    void writeBottomLayerParameters(int layers, int deltaSource, int deltaSink);
    void writeLayerParameters(int layerNumber, int deltaValue);

signals:
    // ========== Initialization Status ==========
    void initialized(bool success);

    // ========== Layer Synchronization Signals ==========
    //
    // layerReadyForScanning():
    //   Emitted when OPC UA detects powder surface completion (layer prepared).
    //   ProcessController receives this and calls ScanStreamingManager::notifyPLCPrepared()
    //   which wakes the consumer thread to execute the layer.
    //
    void layerReadyForScanning();

    // ========== Shutdown Status ==========
    void shutdown_complete();

    // ========== Error Reporting ==========
    void error(const QString& message);

private:
    std::unique_ptr<OPCServerManagerUA> mOPCManager;
    bool mInitialized = false;
};

// ============================================================================
// SLMWorkerManager - Manages OPC UA worker thread lifecycle
// ============================================================================
//
// PURPOSE:
// ???????????????????????????????????????????????????????????????????????
// Creates and manages the OPC UA worker thread. Provides clean API for starting
// and stopping OPC UA server, and emits signals for synchronization.
//
// DESIGN DECISION: Only OPC UA gets a worker thread
// ???????????????????????????????????????????????????????????????????????
// Scanner is owned by ScanStreamingManager's consumer thread (NOT here).
/// This eliminates redundant thread and simplifies architecture:
//
// BEFORE (Old Design - Unnecessary):
//   OPC Worker (dedicated thread)
//   Scanner Worker (dedicated thread)  ? REDUNDANT
//   Producer Thread
//   Consumer Thread (owns Scanner)
//   = 4 threads
//
// AFTER (New Design - Clean):
//   OPC UA Worker (dedicated thread)
//   Producer Thread
//   Consumer Thread (owns Scanner)     ? Scanner here, not separate worker
//   = 3 threads
//
// Why Scanner goes with Consumer:
//   1. Consumer already owns device state machine
//   2. Consumer already manages block execution
//   3. No need for separate initialization thread
//   4. Simplifies synchronization (fewer threads)
//   5. Follows RTC5 best practices (device owner thread)
//
// LIFECYCLE:
// ???????????????????????????????????????????????????????????????????????
// [GUI Thread]
//   ?? ProcessController::startProductionSLMProcess()
//       ?? Create SLMWorkerManager()
//       ?? Connect systemReady signal
//       ?? Call startWorkers()
//           ?? Create std::thread ? OPCWorker::initialize()
//           ?? Return immediately
//               ? (async in worker thread)
//               OPC UA initializes
//               ?
//               initialized(true) signal
//               ?
//               systemReady() signal (via onOPCInitialized)
//               ?
//           ProcessController::onSystemReady()
//               ?? Extract OPC UA manager pointer
//               ?? Pass to ScanStreamingManager
//               ?? Call ScanStreamingManager::startProcess()
//                   ?? Create Producer thread
//                   ?? Create Consumer thread
//                       ?? Consumer owns Scanner (created here)
//
class SLMWorkerManager : public QObject {
    Q_OBJECT

public:
    explicit SLMWorkerManager(QObject* parent = nullptr);
    ~SLMWorkerManager();

    // ========== Lifecycle Control (GUI Thread) ==========
    //
    // startWorkers():
    //   Spawns OPC UA worker thread. Returns immediately.
    //   OPC UA initialization happens asynchronously.
    //   When ready, emits systemReady() signal.
    //
    void startWorkers();

    // stopWorkers():
    //   Signals OPC UA worker to stop and wait for thread completion.
    //   Gracefully shuts down OPC UA (disconnect, cleanup).
    //   When complete, OPC UA thread exits and is joined.
    //
    void stopWorkers();

    // emergencyStop():
    //   Immediately terminates OPC UA worker with minimal cleanup.
    //   Used only in life-critical situations (e.g., emergency button).
    //
    void emergencyStop();

    // ========== Device Access (GUI Thread) ==========
    //
    // getOPCManager():
    //   Returns pointer to OPCServerManagerUA.
    //   Thread-safe (uses atomic pointer load).
    //   Can be read from GUI thread after systemReady() signal.
    //
    OPCServerManagerUA* getOPCManager() const;

    // ========== State Queries (GUI Thread) ==========
    //
    bool isOPCInitialized() const { return mOPCInitialized; }
    bool isRunning() const { return mOPCInitialized; }  // Only OPC UA now

    // ========== Thread Affinity Info (Debugging) ==========
    //
    std::thread::id getOPCThreadId() const { return mOPCThreadId.load(); }

public slots:
    // ========== OPC UA Worker Callbacks (GUI Thread) ==========
    //
    // Called via Qt::QueuedConnection when OPC UA worker emits signals.
    // These run in GUI thread and handle OPC UA state changes.
    //
    void onOPCInitialized(bool success);
    void onOPCShutdown();
    void onOPCError(const QString& message);

signals:
    // ========== System Readiness Signal ==========
    //
    // systemReady():
    //   Emitted when OPC UA worker fully initialized.
    //   ProcessController listens to this and proceeds with streaming startup.
    //
    void systemReady();

    // ========== Error Propagation ==========
    //
    // systemError(QString):
    //   Emitted when OPC UA initialization fails.
    //   ProcessController catches this and initiates cleanup.
    //
    void systemError(const QString& message);

private:
    // ========== OPC UA Worker Thread Management ==========
    //
    // mOPCThread:
    //   The std::thread running OPC UA operations.
    //   Created in startWorkers(), joined in stopWorkers().
    //
    std::thread mOPCThread;

    // ========== Thread Synchronization Primitives ==========
    //
    // mOPCMutex + mOPCCv:
    //   Protect OPC UA worker shutdown.
    //   Worker waits on mOPCCv until mOPCRunning.load() == false.
    //
    std::mutex mOPCMutex;
    std::condition_variable mOPCCv;
    std::atomic<bool> mOPCRunning{false};

    // ========== State Tracking ==========
    //
    // mOPCInitialized:
    //   Set to true when OPCWorker::initialize() succeeds.
    //   Checked by ProcessController to decide next steps.
    //
    bool mOPCInitialized = false;
    bool mShuttingDown = false;

    // ========== Thread Metadata ==========
    //
    // mOPCThreadId:
    //   Atomic thread ID for debugging/assertions.
    //   Allows verification that code is running in correct thread.
    //
    std::atomic<std::thread::id> mOPCThreadId{std::thread::id()};

    // ========== Device Pointer ==========
    //
    // mOPCManagerPtr:
    //   Atomic pointer to OPCServerManagerUA (created in worker thread).
    //   Can be safely read from GUI thread after initialization.
    //
    std::atomic<OPCServerManagerUA*> mOPCManagerPtr{nullptr};

    // ========== Worker Thread Function ==========
    //
    // opcThreadFunc():
    //   Main function for std::thread.
    //   Creates OPCWorker (stack-local), calls initialize(), waits.
    //   Called when std::thread(&SLMWorkerManager::opcThreadFunc, this) runs.
    //
    void opcThreadFunc();
};

#endif // SLM_WORKER_MANAGER_H
