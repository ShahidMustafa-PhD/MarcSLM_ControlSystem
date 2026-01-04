#include "processcontroller.h"
#include "opccontroller.h"
#include "scannercontroller.h"
#include "scanstreamingmanager.h"
#include "controllers/slm_worker_manager.h"
#include "opcserver/opcserverua.h"
#include <QTextEdit>
#include <QDebug>

ProcessController::ProcessController(OPCController* opcCtrl,
                                     ScannerController* scanCtrl,
                                     QTextEdit* logWidget,
                                     ScanStreamingManager* scanMgr,
                                     QObject* parent)
    : QObject(parent)
    , mOPCController(opcCtrl)
    , mScannerController(scanCtrl)
    , mScanManager(scanMgr)
    , mSLMWorkerManager(nullptr)  // NEW: lazy initialized in startProductionSLMProcess
    , mLogWidget(logWidget)
    , mState(Idle)
    , mPollingInterval(500)  // 500ms default
    , mPreviousPowderSurfaceDone(false)
    , mCurrentLayerNumber(0)
{
    // Connect OPC data updates
    connect(mOPCController, &OPCController::dataUpdated,
            this, &ProcessController::onOPCDataUpdated);
    
    // Connect scanner layer completion
    connect(mScannerController, &ScannerController::layerCompleted,
            this, &ProcessController::onScannerLayerCompleted);
    
    // Setup timer
    mTimer.setInterval(mPollingInterval);
    connect(&mTimer, &QTimer::timeout, this, &ProcessController::onTimerTick);
}

ProcessController::~ProcessController() {
    if (mTimer.isActive()) {
        mTimer.stop();
    }
    // mSLMWorkerManager destroyed automatically (unique_ptr)
}

void ProcessController::log(const QString& message) {
    if (mLogWidget) {
        mLogWidget->append(message);
    }
    emit statusMessage(message);
}

void ProcessController::setState(ProcessState newState) {
    if (mState != newState) {
        mState = newState;
        emit stateChanged(newState);
    }
}

void ProcessController::setPollingInterval(int milliseconds) {
    mPollingInterval = milliseconds;
    if (mTimer.isActive()) {
        mTimer.setInterval(milliseconds);
    }
}

void ProcessController::startProcess() {
    if (mState == Running) {
        log("- Process already running");
        return;
    }
    
    if (!mOPCController->isInitialized()) {
        log("- Cannot start process - OPC not initialized");
        return;
    }
    
    log("- Starting process monitoring...");
    setState(Running);
    
    mTimer.start();
    mCurrentLayerNumber = 0;
    mPreviousPowderSurfaceDone = false;
    
    emit processStarted();
}

void ProcessController::pauseProcess() {
    if (mState != Running) {
        log("- Process is not running");
        return;
    }
    
    log("- Process paused");
    setState(Paused);
    mTimer.stop();
    
    emit processPaused();
}

void ProcessController::resumeProcess() {
    if (mState != Paused) {
        log("- Process is not paused");
        return;
    }
    
    log("- Process resumed");
    setState(Running);
    mTimer.start();
    
    emit processResumed();
}

void ProcessController::stopProcess() {
    if (mState == Idle) {
        log("- Process already stopped");
        return;
    }
    
    log("- Process stopped");
    setState(Idle);
    mTimer.stop();

    // ========== CRITICAL FIX: Check mScanManager exists ==========
    // Stop streaming manager if running
    if (mScanManager) {
        mScanManager->stopProcess();
    }

    // ========== CRITICAL FIX: Check mSLMWorkerManager exists ==========
    // Cleanup OPC worker threads
    shutdownOPCWorker();

    emit processStopped();
}

void ProcessController::emergencyStop() {
    log("-- EMERGENCY STOP ACTIVATED!");
    setState(EmergencyStopped);
    mTimer.stop();

    // Send emergency stop to PLC (if initialized)
    if (mOPCController && mOPCController->isInitialized()) {
        mOPCController->writeEmergencyStop();
    }

    // ========== CRITICAL FIX: Check mScanManager exists ==========
    // Emergency stop streaming manager
    if (mScanManager) {
        mScanManager->emergencyStop();
    }

    // ========== CRITICAL FIX: Check mSLMWorkerManager exists ==========
    // Cleanup OPC worker threads immediately
    if (mSLMWorkerManager) {
        mSLMWorkerManager->emergencyStop();
    }

    emit emergencyStopActivated();
}

// ============================================================================
// INDUSTRIAL SLM PRODUCTION PROCESS (SLICE-FILE DRIVEN WITH OPC)
// ============================================================================

void ProcessController::startProductionSLMProcess(const QString& marcFilePath, const QString& configJsonPath) {
    // ========== DEFENSIVE CHECKS ==========
    //
    // Validate all required pointers before proceeding.
    // This prevents nullptr dereferences when signals are processed asynchronously.
    //
    
    if (mState == Running) {
        log("- Process already running");
        return;
    }

    if (!mScanManager) {
        log("- CRITICAL: ScanStreamingManager not initialized");
        return;
    }

    if (marcFilePath.isEmpty()) {
        log("- No MARC file selected. Open a project with slice file first.");
        return;
    }

    if (configJsonPath.isEmpty()) {
        log("ERROR: No JSON configuration file selected. Attach config.json to project first.");
        return;
    }

    // ========== VALIDATE OPC CONTROLLER ==========
    //
    // OPC controller must be available before creating OPC worker.
    // This is created in MainWindow, so should always exist.
    //
    if (!mOPCController) {
        log("- CRITICAL: OPCController not initialized");
        return;
    }

    log("========================================================================");
    log("INDUSTRIAL SLM PRODUCTION PROCESS STARTING");
    log("========================================================================");
    log(QString("MARC file: %1").arg(marcFilePath));
    log(QString("JSON config: %1").arg(configJsonPath));
    log("Architecture: OPC Worker + Producer/Consumer Threads");
    log("Synchronization: Per-layer handshake");
    log("");

    // ========== STORE PATHS FOR SIGNAL HANDLERS ==========
    //
    // Store as member variables so onSystemReady() can access them.
    // These are called asynchronously via Qt::QueuedConnection.
    //
    mMarcFilePath = marcFilePath;
    mConfigJsonPath = configJsonPath;

    // ========== STEP 1: Create SLMWorkerManager if not exists =========
    //
    // CRITICAL FIX: Check if already created and disconnect old signals
    // to prevent re-entrancy and double connection of signals.
    //
    if (mSLMWorkerManager) {
        // Already created - disconnect old signals to prevent duplicates
        disconnect(mSLMWorkerManager.get(), nullptr, this, nullptr);
    } else {
        // First time - create it
        log("[INIT] Creating SLMWorkerManager (OPC worker only)...");
        mSLMWorkerManager = std::make_unique<SLMWorkerManager>(this);
        log("[INIT] SLMWorkerManager created (OPC only)");
    }

    // ========== CONNECT SYSTEM READINESS SIGNAL ==========
    //
    // Connect fresh (after disconnecting above) to avoid double-connection.
    // Qt::QueuedConnection ensures onSystemReady() runs in GUI thread.
    //
    connect(mSLMWorkerManager.get(), &SLMWorkerManager::systemReady,
            this, &ProcessController::onSystemReady, Qt::QueuedConnection);

    // ========== CONNECT ERROR SIGNAL ==========
    //
    // If OPC initialization fails, catch the error and initiate cleanup.
    //
    connect(mSLMWorkerManager.get(), &SLMWorkerManager::systemError,
            this, &ProcessController::onScanProcessError, Qt::QueuedConnection);

    // ========== STEP 2: Connect ScanStreamingManager signals ==========
    //
    // These signals indicate the state of the streaming process.
    // Use disconnect first to prevent double-connection.
    //
    if (mScanManager) {
        // Cleanup old connections
        disconnect(mScanManager, &ScanStreamingManager::finished, this, nullptr);
        disconnect(mScanManager, QOverload<const QString&>::of(&ScanStreamingManager::error), this, nullptr);

        // ========== FINISHED SIGNAL ==========
        //
        // Emitted when Consumer thread finishes the last layer.
        // ProcessController catches this and calls shutdownOPCWorker().
        //
        connect(mScanManager, &ScanStreamingManager::finished,
                this, &ProcessController::onScanProcessFinished, Qt::QueuedConnection);

        // ========== ERROR SIGNAL ==========
        //
        // Emitted if Producer or Consumer thread encounters error.
        // ProcessController catches this and initiates cleanup.
        //
        connect(mScanManager, &ScanStreamingManager::error,
                this, &ProcessController::onScanProcessError, Qt::QueuedConnection);
    }

    // ========== STEP 3: Start OPC worker thread ==========
    //
    // This spawns the OPC worker thread (std::thread).
    // OPCWorker::initialize() runs in that thread asynchronously.
    // We return immediately below (non-blocking startup).
    //
    log("[STEP 1] Starting OPC worker thread...");
    setState(Starting);

    // DEFENSIVE CHECK: Verify mSLMWorkerManager still exists
    if (!mSLMWorkerManager) {
        log("- CRITICAL: SLMWorkerManager was destroyed unexpectedly");
        onScanProcessError("SLMWorkerManager initialization failed");
        return;
    }

    mSLMWorkerManager->startWorkers();

    log("[STEP 1] OPC worker thread spawned - waiting for initialization...");
    log("[NOTE] Initialization continues asynchronously in worker thread");
    log("[NOTE] GUI remains responsive while OPC initializes");
    log("");

    // ========== RETURN TO EVENT LOOP ==========
    //
    // This method returns immediately. Process startup is asynchronous.
    // When OPC is ready, systemReady() signal fires and onSystemReady() is called.
    // GUI remains responsive during this entire process.
   // startProcess();// added by shahid
}

void ProcessController::onSystemReady() {
    // ========== INDUSTRIAL SLM: OPC READY, START STREAMING ==========
    //
    // At this point, OPC is fully initialized and ready.
    // Now we can start the Producer/Consumer streaming process.
    //

    if (mState != Starting) {
        log("-- ERROR: Got systemReady signal in unexpected state");
        return;
    }

    log("[STEP 1] - OPC worker thread initialized");
    log("[STEP 1] - OPC server ready (COM connection established)");
    log("");

    // ========== CRITICAL FIX: VALIDATE ALL POINTERS ==========
    //
    // Before accessing pointers, verify they still exist.
    // In asynchronous signal handlers, pointers can become invalid.
    //
    
    if (!mSLMWorkerManager) {
        log("-- ERROR: SLMWorkerManager is null (object was destroyed)");
        onScanProcessError("Internal error: SLMWorkerManager destroyed during startup");
        return;
    }

    OPCServerManagerUA* opcManager = mSLMWorkerManager->getOPCManager();
    if (!opcManager) {
        log("-- ERROR: Failed to get OPC manager from worker thread");
        onScanProcessError("OPC initialization failed in worker thread");
        return;
    }

    // ========== CRITICAL FIX: VALIDATE mScanManager ==========
    //
    // mScanManager is created in MainWindow but used here via signal handler.
    // Verify it still exists and is valid.
    //
    if (!mScanManager) {
        log("-- ERROR: ScanStreamingManager is null");
        onScanProcessError("Internal error: ScanStreamingManager not initialized");
        shutdownOPCWorker();
        return;
    }

    // ========== CRITICAL FIX: VALIDATE STORED PATHS ==========
    //
    // mMarcFilePath and mConfigJsonPath are set in startProductionSLMProcess()
    // and accessed here (asynchronously). Validate they're not empty.
    //
    if (mMarcFilePath.isEmpty()) {
        log("-- ERROR: MARC file path is empty");
        onScanProcessError("Internal error: MARC file path not set");
        shutdownOPCWorker();
        return;
    }

    if (mConfigJsonPath.isEmpty()) {
        log("-- ERROR: JSON configuration path is empty");
        onScanProcessError("Internal error: JSON configuration path not set");
        shutdownOPCWorker();
        return;
    }

    // ========== PASS OPC MANAGER TO SCANNING MANAGER ==========
    //
    // Now that we've validated mScanManager exists, pass the OPC pointer.
    //
    log("[STEP 2] Passing OPC manager reference to ScanStreamingManager...");
    mScanManager->setOPCManager(opcManager);
    log("[STEP 2] - OPC manager reference set");
    log("");

    // ========== START PRODUCER/CONSUMER STREAMING ==========
    //
    // Convert QString to wstring for the streaming manager.
    // ScanStreamingManager::startProcess() creates:
    // • Producer thread (reads MARC file)
    // • Consumer thread (owns Scanner, executes commands)
    //
    log("[STEP 3] Starting Producer/Consumer threads...");
    log("[STEP 3] • Producer: Opens MARC file, reads layers sequentially");
    log("[STEP 3] • Consumer: Loads config.json, owns Scanner, executes layers");
    log("");

    std::wstring marcPath = mMarcFilePath.toStdWString();
    std::wstring configPath = mConfigJsonPath.toStdWString();

    // ========== CRITICAL FIX: VALIDATE BEFORE CALLING ==========
    //
    // Final check: mScanManager is still valid before calling its method.
    // Even though we checked above, another thread could theoretically delete it.
    // Defensive programming practice.
    //
    if (!mScanManager) {
        log("-- ERROR: ScanStreamingManager became null before startProcess()");
        onScanProcessError("Internal error: ScanStreamingManager destroyed");
        shutdownOPCWorker();
        setState(Idle);
        return;
    }

    if (mScanManager->startProcess(marcPath, configPath)) {
        setState(Running);
        
        // ========== CRITICAL FIX: START POLLING TIMER ==========
        // We must start the timer to poll OPC for LaySurface_Done.
        // Without this, notifyPLCPrepared() is never called.
        if (!mTimer.isActive()) {
            mTimer.start();
            log("- Polling timer started (500ms interval)");
        }

        log("[STEP 3] - Producer thread started (reading *.marc)");
        log("[STEP 3] - Consumer thread started (owns Scanner, loads config.json)");
        log("");
        log("========================================================================");
        log("PRODUCTION SLM PROCESS ACTIVE");
        log("========================================================================");
        log("Layer synchronization mode:");
        log("  1. Producer enqueues block from MARC");
        log("  2. Consumer waits for OPC layer-ready signal");
        log("  3. GUI polls OPC, detects powder surface complete");
        log("  4. ProcessController calls notifyPLCPrepared()");
        log("  5. Consumer wakes and executes layer on Scanner");
        log("  6. Consumer applies BuildStyle parameters per segment");
        log("  7. Consumer notifies OPC: layer complete");
        log("  8. Repeat for next layer");
        log("========================================================================");
        log("");

        emit processStarted();
    } else {
        log("- FAILED: ScanStreamingManager could not start streaming");
        onScanProcessError("ScanStreamingManager failed to start production process");
        shutdownOPCWorker();
        setState(Idle);
    }
}

// ========== Called when ScanStreamingManager::finished() signal received ==========
//
// This slot is triggered when Consumer thread finishes the last layer.
// At this point:
// • All layers have been scanned
// • Producer thread has exited (MARC exhausted)
// • Consumer thread is about to exit (queue empty)
// • We need to clean up OPC worker
//
void ProcessController::onScanProcessFinished() {
    // ========== PRODUCTION COMPLETE - INITIATE CLEANUP ==========
    //
    // All layers completed successfully.
    // Now we need to shut down the OPC worker thread gracefully.
    //

    if (mState != Running) {
        return;  // Ignore if not running
    }

    log("");
    log("========================================================================");
    log("PRODUCTION SLM PROCESS COMPLETED");
    log("========================================================================");
    log("[CLEANUP] All layers processed successfully");
    log("[CLEANUP] Shutting down OPC worker thread...");

    setState(Idle);

    // ========== SHUTDOWN OPC WORKER ==========
    //
    // Calls SLMWorkerManager::stopWorkers().
    // This signals OPC worker thread to exit and waits for completion.
    // All COM connections are properly cleaned up.
    //
    shutdownOPCWorker();

    log("[CLEANUP] - OPC worker thread shut down");
    log("[CLEANUP] - All threads terminated");
    log("[CLEANUP] - All resources released");
    log("========================================================================");
    log("");

    emit processStopped();
}

// ========== Called when error occurs in OPC or Streaming ==========
//
// This slot handles errors from:
// • SLMWorkerManager::systemError (OPC init failure)
// • ScanStreamingManager::error (Producer/Consumer error)
//
// All error paths must clean up properly to avoid dangling threads.
//
void ProcessController::onScanProcessError(const QString& message) {
    // ========== ERROR HANDLER - CLEANUP ALL THREADS ==========
    //
    // Ensures that on any error, all threads are properly shut down.
    // This is critical for robustness (prevents zombie threads).
    //
    // CRITICAL FIX: Validate pointers before using them in error cleanup.
    //

    log("-- ERROR: " + message);
    log("[CLEANUP] Initiating error recovery...");

    setState(Idle);

    // ========== CRITICAL FIX: VALIDATE mScanManager BEFORE USE ==========
    //
    // If streaming manager was never created, skip this step.
    // This prevents nullptr dereference in error handler.
    //
    if (mScanManager) {
        mScanManager->stopProcess();
        log("[CLEANUP] - Stopped Producer/Consumer threads");
    } else {
        log("[CLEANUP] - ScanStreamingManager not active (already stopped)");
    }

    // ========== CRITICAL FIX: VALIDATE mSLMWorkerManager BEFORE USE ==========
    //
    // If worker manager was never created, skip shutdown.
    // This prevents nullptr dereference or use-after-free.
    //
    shutdownOPCWorker();
    log("[CLEANUP] - Stopped OPC worker thread");

    log("[CLEANUP] - Error recovery complete");
    log("");

    emit error(message);
}

// ========== Helper: Shutdown OPC worker thread ==========
//
// Centralized cleanup method called from all exit paths.
// Ensures that OPC worker is always properly shut down.
//
void ProcessController::shutdownOPCWorker() {
    // ========== CENTRALIZED OPC CLEANUP ==========
    //
    // Called from:
    // • onScanProcessFinished() - normal completion
    // • onScanProcessError() - any error
    // • stopProcess() - user stops process
    // • emergencyStop() - emergency button
    //
    // This ensures OPC worker is cleaned up consistently.
    //
    // CRITICAL FIX: Check if mSLMWorkerManager exists before using it.
    //

    if (!mSLMWorkerManager) {
        // Worker manager was never created or already destroyed
        return;
    }

    qDebug() << "ProcessController::shutdownOPCWorker() - Requesting OPC shutdown...";
    mSLMWorkerManager->stopWorkers();
    qDebug() << "ProcessController::shutdownOPCWorker() - OPC shutdown complete";
}

// ========== LEGACY: TEST PROCESS MODE (NO OPC) ==========
void ProcessController::startTestSLMProcess(float layerThickness, size_t layerCount) {
    if (mState == Running) {
        log("- Process already running");
        return;
    }
    
    if (!mScanManager) {
        log("-- ScanStreamingManager not initialized");
        return;
    }
    
    log(QString("- Starting TEST SLM Process (%1 layers @ %2 mm)").arg(layerCount).arg(layerThickness));
    log("- Mode: Synthetic layers - NO SLICE FILE, NO OPC");
    
    // Connect finish signal (test mode doesn't use worker threads)
    if (mScanManager) {
        disconnect(mScanManager, &ScanStreamingManager::finished, this, nullptr);
        connect(mScanManager, &ScanStreamingManager::finished,
                this, &ProcessController::onScanProcessFinished, Qt::QueuedConnection);
    }
    
    // Start test process (synthetic layers, no OPC, no worker threads)
    if (mScanManager->startTestProcess(layerThickness, layerCount)) {
        setState(Running);
        emit processStarted();
        log("- TEST mode activated: synthetic layers without OPC integration");
    } else {
        log("-- Failed to start test SLM process");
        emit error("Test SLM startup failed");
    }
}

void ProcessController::onTimerTick() {
    if (mState != Running) {
        return;
    }
    
    // ========== CRITICAL FIX: USE WORKER THREAD OPC MANAGER ==========
    // In production mode, the active OPC connection lives in SLMWorkerManager's thread.
    // mOPCController (GUI thread object) is likely uninitialized in this mode.
    // We must poll the active manager.
    
    if (mSLMWorkerManager) {
        OPCServerManagerUA* workerOPC = mSLMWorkerManager->getOPCManager();
        if (workerOPC && workerOPC->isInitialized()) {
            OPCServerManagerUA::OPCData data;
            // Direct read from the worker's manager (thread-safe)
            if (workerOPC->readData(data)) {
                // Manually trigger the update logic
                onOPCDataUpdated(data);
            } else {
                // Reduce log spam
                static int failCount = 0;
                if (++failCount % 20 == 0) log("-- WARNING: OPC read failed (worker thread)");
            }
            return;
        }
    }

    // Legacy/Test Mode fallback: use the local OPCController
    if (mOPCController && mOPCController->isInitialized()) {
        if (!mOPCController->readData()) {
            static int failCount = 0;
            if (++failCount % 20 == 0) log("-- WARNING: OPC read failed (local controller)");
        }
    }
}

void ProcessController::onOPCDataUpdated(const OPCServerManagerUA::OPCData& data) {
    if (mState != Running) {
        return;
    }
    
    // Check for powder surface completion
    bool currentPowderSurfaceDone = (data.powderSurfaceDone != 0);
    
    if (!mPreviousPowderSurfaceDone && currentPowderSurfaceDone) {
        handlePowderSurfaceComplete();
    }
    
    mPreviousPowderSurfaceDone = currentPowderSurfaceDone;
}

void ProcessController::handlePowderSurfaceComplete() {
    log(QString("- Layer Prepared by PLC!"));
    emit layerPreparedByPLC();
    
    // NEW: Notify streaming manager that PLC layer is ready
    if (mScanManager) {
        mScanManager->notifyPLCPrepared();
        log("- Notified streaming manager: PLC layer ready");
    }
    
    // Perform laser scanning if scanner is initialized
    /*if (mScannerController && mScannerController->isInitialized()) {
        int layersProcessed = mScannerController->layersProcessed();
        int maxLayers = mScannerController->maxPilotLayers();
        
        if (layersProcessed < maxLayers) {
            log(QString("- Starting scanner for layer %1").arg(layersProcessed + 1));
            
            if (mScannerController->performLayerScanningTest()) {
                // Success - layerCompleted signal will be emitted by scanner
            } else {
                log("- Scanner operation failed");
            }
        } else {
            log(QString("- All pilot layers completed (%1/%2). Scanning disabled.")
                .arg(layersProcessed).arg(maxLayers));
        }
    } else {
        log("-- Scanner not initialized - skipping laser scan");
        log("Click 'Initialize Scanner' to enable pilot square drawing");
    }*/
}

void ProcessController::onScannerLayerCompleted(int layerNumber) {
    mCurrentLayerNumber = layerNumber;
    log(QString("- Scanner completed layer %1").arg(layerNumber));
    emit layerScanned(layerNumber);
}
