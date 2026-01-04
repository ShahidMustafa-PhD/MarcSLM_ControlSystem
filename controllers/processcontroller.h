#ifndef PROCESSCONTROLLER_H
#define PROCESSCONTROLLER_H

#include <QObject>
#include <QTimer>
#include <memory>
#include "opcserver/opcserverua.h"

class OPCController;
class ScannerController;
class ScanStreamingManager;
class SLMWorkerManager;  // Forward declaration
class QTextEdit;

/**
 * @brief ProcessController - Coordinates manufacturing process workflow
 * 
 * INDUSTRIAL SLM THREAD LIFECYCLE MANAGEMENT:
 * 
 * THREAD STARTUP (Production Mode):
 *   1. GUI calls startProductionSLMProcess(marcFilePath)
 *   2. ProcessController creates SLMWorkerManager (if not exists)
 *   3. ProcessController calls mSLMWorkerManager->startWorkers()
 *   4. WAIT: Connect to systemReady() signal from SLMWorkerManager
 *   5. Upon systemReady: OPC thread initialized, OPCServerManager created
 *   6. ProcessController extracts OPC manager pointer from worker
 *   7. ProcessController calls mScanManager->setOPCManager(opcPtr)
 *   8. ProcessController calls mScanManager->startProcess(marcPath)
 *   9. Producer/Consumer threads start (owned by ScanStreamingManager)
 * 
 * THREAD OPERATION:
 *   - OPC Thread: Owns OPCServerManager, signals layer completion
 *   - Consumer Thread (in ScanStreamingManager): Owns Scanner, waits for OPC signal
 *   - Producer Thread (in ScanStreamingManager): Reads MARC, enqueues blocks
 * 
 * THREAD SHUTDOWN (All Paths):
 *   1. ScanStreamingManager::finished() signal emitted when last layer done
 *   2. ProcessController::onScanProcessFinished() slot triggered
 *   3. ProcessController calls mSLMWorkerManager->stopWorkers()
 *   4. Worker threads gracefully shutdown (stop OPC, shutdown Scanner)
 *   5. All threads joined, resources cleaned up
 *   6. GUI returns to Idle state
 * 
 * ERROR PATHS:
 *   - Producer/Consumer error: mScanManager emits error(), stops threads
 *   - OPC initialization error: worker emits error(), returns to main GUI thread
 *   - User stops process: GUI calls stopProcess() -> mScanManager->stopProcess()
 *   - Emergency stop: All paths trigger immediate shutdown without cleanup
 * 
 * Responsibilities:
 * - Process state machine (startup, running, paused, stopped)
 * - Coordination between OPC and Scanner via worker threads
 * - Layer-by-layer execution logic via ScanStreamingManager
 * - Timing and synchronization across thread boundaries
 * - Process monitoring and error handling
 */
class ProcessController : public QObject {
    Q_OBJECT

public:
    enum ProcessState {
        Idle,
        Starting,
        Running,
        Paused,
        Stopping,
        EmergencyStopped
    };
    Q_ENUM(ProcessState)

    explicit ProcessController(OPCController* opcCtrl, 
                              ScannerController* scanCtrl,
                              QTextEdit* logWidget,
                              ScanStreamingManager* scanMgr = nullptr,
                              QObject* parent = nullptr);
    ~ProcessController();

    // Process control
    void startProcess();
    void pauseProcess();
    void resumeProcess();
    void stopProcess();
    void emergencyStop();
    
    // ========== INDUSTRIAL SLM THREAD LIFECYCLE CONTROL ==========
    // Production: Slice-file driven with OPC layer creation (industrial SLM workflow)
    // - Starts OPC worker thread first
    // - Waits for OPC initialization
    // - Then starts producer/consumer scanner threads
    void startProductionSLMProcess(const QString& marcFilePath, const QString& configJsonPath);
    
    // Test: Synthetic layers without OPC (hardware testing only)
    // - No worker threads needed
    // - Direct ScanStreamingManager::startTestProcess
    void startTestSLMProcess(float layerThickness, size_t layerCount);
    
    // State queries
    ProcessState state() const { return mState; }
    bool isRunning() const { return mState == Running; }
    bool isPaused() const { return mState == Paused; }
    
    // Timer configuration
    void setPollingInterval(int milliseconds);
    int pollingInterval() const { return mPollingInterval; }

signals:
    void processStarted();
    void processPaused();
    void processResumed();
    void processStopped();
    void emergencyStopActivated();
    void stateChanged(ProcessState newState);
    
    void layerPreparedByPLC();
    void layerScanned(int layerNumber);
    void statusMessage(const QString& msg);
    void error(const QString& msg);

private slots:
    // Timer-based polling (legacy mode)
    void onTimerTick();
    void onOPCDataUpdated(const OPCServerManagerUA::OPCData& data);
    void onScannerLayerCompleted(int layerNumber);
    
    // ========== INDUSTRIAL SLM THREAD LIFECYCLE SLOTS ==========
    // Called when SLMWorkerManager indicates both OPC and Scanner ready
    void onSystemReady();
    
    // Called when ScanStreamingManager finishes all layers
    void onScanProcessFinished();
    
    // Called when production streaming encounters error
    void onScanProcessError(const QString& message);

private:
    OPCController* mOPCController;
    ScannerController* mScannerController;
    ScanStreamingManager* mScanManager;
    std::unique_ptr<SLMWorkerManager> mSLMWorkerManager;  // NEW: manages OPC/Scanner worker threads
    QTextEdit* mLogWidget;
    QTimer mTimer;
    
    ProcessState mState;
    int mPollingInterval;
    
    // Process tracking
    bool mPreviousPowderSurfaceDone;
    int mCurrentLayerNumber;
    QString mMarcFilePath;  // NEW: stores MARC path during startProductionSLMProcess
    QString mConfigJsonPath;  // NEW: stores JSON config path during startProductionSLMProcess
    
    void setState(ProcessState newState);
    void log(const QString& message);
    void handlePowderSurfaceComplete();
    
    // ========== INDUSTRIAL SLM HELPER METHODS ==========
    // Cleanup and shutdown OPC worker thread (called on any exit path)
    void shutdownOPCWorker();
};

#endif // PROCESSCONTROLLER_H
