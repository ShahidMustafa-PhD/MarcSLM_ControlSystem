#include "scanstreamingmanager.h"
#include "io/streamingmarcreader.h"
#include "opcserver_lib/opcserverua.h"
#include <sstream>
#include <iomanip>
#include <cmath>
#include <chrono>
#include <QDebug>
#include <QThread>

using namespace std::chrono_literals;

// ============================================================================
// Constructor / Destructor
// ============================================================================

ScanStreamingManager::ScanStreamingManager(QObject* parent)
    : QObject(parent),
      mMaxQueue(4),
      mStopRequested(false),
      mPLCPrepared(false)
{
    // Configure scanner with production defaults
    mScannerConfig.cardNumber = 1;
    mScannerConfig.listMemory = 10000;
    mScannerConfig.markSpeed = 250.0;
    mScannerConfig.jumpSpeed = 1000.0;
    mScannerConfig.laserMode = 1;
    mScannerConfig.analogOutValue = 640;
    mScannerConfig.analogOutStandby = 0;
}

ScanStreamingManager::~ScanStreamingManager() {
    stopProcess();
}

// ============================================================================
// MAIN THREAD: Configuration Loading
// ============================================================================

bool ScanStreamingManager::loadScanConfig(const std::wstring& configJsonPath) {
    try {
        // Convert wstring to string for BuildStyleLibrary
        std::string path(configJsonPath.begin(), configJsonPath.end());
        
        if (!mBuildStyles.loadFromJson(path)) {
            emit error(QString::fromStdString("- Failed to parse buildStyles from: " + path));
            return false;
        }

        std::ostringstream ss;
        ss << "Loaded " << mBuildStyles.count() << " buildStyles from config.json";
        emit statusMessage(QString::fromStdString(ss.str()));
        emit configLoaded(QString::fromStdString(path));
        
        return true;
    } catch (const std::exception& e) {
        std::ostringstream ss;
        ss << "Config load error: " << e.what();
        emit error(QString::fromStdString(ss.str()));
        return false;
    }
}

// ============================================================================
// PUBLIC INTERFACE - MAIN THREAD
// ============================================================================

bool ScanStreamingManager::startProcess(const std::wstring& marcPath, const std::wstring& configJsonPath) {
    // Sanity check: ensure no threads already running
    if (mProducerThread.joinable() || mConsumerThread.joinable()) {
        emit error("Process already running");
        return false;
    }

    // Validate paths
    if (marcPath.empty()) {
        emit error("ERROR: MARC file path is empty");
        return false;
    }

    if (configJsonPath.empty()) {
        emit error("ERROR: JSON configuration file path is empty");
        return false;
    }

    // Validate OPC manager is available (required for production mode)
    if (!mOPCManager) {
        emit error("ERROR: OPC Manager not initialized. Call setOPCManager() first or ensure SLMWorkerManager is active.");
        return false;
    }

    // Reset state
    mStopRequested = false;
    mEmergencyStopFlag = false;
    mPLCPrepared = false;
    mOPCInitialized = false;
    mLayersProduced = 0;
    mLayersConsumed = 0;
    mTotalLayers = 0;
    mCurrentLayerNumber = 0;
    mProcessMode = ProcessMode::Production;  // PRODUCTION MODE
    
    {
        std::lock_guard<std::mutex> lk(mMutex);
        mQueue.clear();
    }

    // ========== STORE CONFIG PATH FOR CONSUMER THREAD ==========
    //
    // Consumer thread will load BuildStyleLibrary from this path.
    // This ensures configuration is loaded in the correct thread context.
    //
    mConfigJsonPath = configJsonPath;

    emit statusMessage("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    emit statusMessage("INDUSTRIAL SLM STARTUP SEQUENCE");
    emit statusMessage("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    emit statusMessage("STEP 1: Verifying OPC Manager is ready...");

    // ========== INDUSTRIAL PRACTICE: OPC MUST BE READY BEFORE ANYTHING ELSE ==========
    // The OPC server manages physical recoater, platform, and laser timing
    // No scanning can happen without OPC layer synchronization
    
    // Wait for OPC to be ready (with timeout)
    const int OPC_READY_TIMEOUT_MS = 5000;
    {
        std::unique_lock<std::mutex> lk(mMutex);
        
        // OPC manager should already be initialized by SLMWorkerManager
        // If not, wait briefly for it to initialize
        if (!mOPCManager->isInitialized()) {
            emit statusMessage("- Waiting for OPC Manager to initialize...");
            
            bool opc_ready = mCvOPCReady.wait_for(lk,
                std::chrono::milliseconds(OPC_READY_TIMEOUT_MS),
                [this] { return mOPCManager && mOPCManager->isInitialized(); }
            );
            
            if (!opc_ready) {
                emit error("ERROR: OPC Manager failed to initialize within timeout");
                return false;
            }
        }
        
        mOPCInitialized = true;
    }

    emit statusMessage("- STEP 1 COMPLETE: OPC Manager is ready");
    emit statusMessage("- STEP 2: Starting Consumer thread (owns Scanner, loads config.json)...");

    // ========== INDUSTRIAL PRACTICE: Consumer thread owns Scanner ==========
    // Consumer initializes Scanner in its own thread
    // Consumer loads BuildStyleLibrary from JSON config file
    // Consumer waits for OPC layer-ready signal before executing each layer
    mConsumerThread = std::thread(&ScanStreamingManager::consumerThreadFunc, this);
    
    // Small delay to let consumer initialize scanner
    std::this_thread::sleep_for(100ms);

    emit statusMessage("- STEP 2 COMPLETE: Consumer thread running");
    emit statusMessage("- STEP 3: Starting Producer thread (reads MARC file)...");

    // ========== INDUSTRIAL PRACTICE: Producer streams MARC AFTER consumer ready ==========
    // Producer reads layers sequentially with low memory footprint
    // Producer waits on bounded queue if consumer is slow
    mProducerThread = std::thread(&ScanStreamingManager::producerThreadFunc, this, marcPath);

    emit statusMessage("- STEP 3 COMPLETE: Producer thread streaming MARC file");
    emit statusMessage("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    emit statusMessage("PRODUCTION SLM MODE ACTIVATED");
    emit statusMessage("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    emit statusMessage("- Streaming .marc file with parameter segments");
    emit statusMessage("- BuildStyle parameters loaded from config.json");
    emit statusMessage("- OPC layer synchronization enabled");
    emit statusMessage("- RTC5 scanner executing with parameter switching");
    emit statusMessage("━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    
    return true;
}

// ========== NEW: TEST PROCESS MODE ==========
bool ScanStreamingManager::startTestProcess(float testLayerThickness, size_t testLayerCount) {
    // Sanity check: ensure no threads already running
    if (mProducerThread.joinable() || mConsumerThread.joinable() || mTestProducerThread.joinable()) {
        emit error("Process already running");
        return false;
    }

    // Validate test parameters
    if (testLayerThickness <= 0.0f || testLayerThickness > 0.5f) {
        emit error("Invalid test layer thickness (must be > 0 and <= 0.5 mm)");
        return false;
    }

    if (testLayerCount == 0 || testLayerCount > 100) {
        emit error("Invalid test layer count (must be 1-100)");
        return false;
    }

    // Validate OPC manager is available
    if (!mOPCManager) {
        emit error("- OPC Manager not initialized. Call setOPCManager() first or ensure SLMWorkerManager is active.");
        return false;
    }

    // Reset state
    mStopRequested = false;
    mEmergencyStopFlag = false;
    mPLCPrepared = false;
    mOPCInitialized = false;
    mLayersProduced = 0;
    mLayersConsumed = 0;
    mTotalLayers = testLayerCount;
    mCurrentLayerNumber = 0;
    mProcessMode = ProcessMode::Test;  // TEST MODE
    
    {
        std::lock_guard<std::mutex> lk(mMutex);
        mQueue.clear();
    }

    emit statusMessage("- TEST MODE STARTUP SEQUENCE");
    emit statusMessage("- STEP 1: Verifying OPC Manager is ready...");

    // ========== TEST MODE: Verify OPC available (but won't synchronize on laser state) =========
    {
        std::unique_lock<std::mutex> lk(mMutex);
        
        if (!mOPCManager->isInitialized()) {
            emit statusMessage("- Waiting for OPC Manager to initialize...");
            
            const int OPC_READY_TIMEOUT_MS = 5000;
            bool opc_ready = mCvOPCReady.wait_for(lk,
                std::chrono::milliseconds(OPC_READY_TIMEOUT_MS),
                [this] { return mOPCManager && mOPCManager->isInitialized(); }
            );
            
            if (!opc_ready) {
                emit error("- OPC Manager not ready, but continuing in TEST mode (no PLC sync)");
                // Don't fail, proceed in test mode without OPC
            }
        }
        
        mOPCInitialized = (mOPCManager && mOPCManager->isInitialized());
    }

    emit statusMessage("- STEP 1 COMPLETE: OPC status verified");
    emit statusMessage("- STEP 2: Starting Consumer thread (synthetic test mode)...");

    // ========== TEST MODE: Consumer-only, no producer thread =========
    // Consumer will generate synthetic test square patterns
    // No MARC file is read
    // Laser is OFF for pilot marking
    mConsumerThread = std::thread(&ScanStreamingManager::consumerThreadFunc, this);
    
    // Small delay to let consumer initialize scanner
    std::this_thread::sleep_for(500ms);

    // ========== TEST MODE: Enqueue synthetic layers directly from producer thread =========
    // FIX: Store test producer thread for proper joining during stopProcess()
    // This prevents detached thread crash in destructor
    mTestProducerThread = std::thread(&ScanStreamingManager::producerTestThreadFunc, this,
                                       testLayerThickness, testLayerCount);

    std::ostringstream ss;
    ss << "- STEP 2 COMPLETE: Consumer thread ready";
    emit statusMessage(QString::fromStdString(ss.str()));
    
    ss.str("");
    ss << "- TEST MODE ACTIVATED";
    emit statusMessage(QString::fromStdString(ss.str()));
    ss.str("");
    ss << "   - Generating " << testLayerCount << " synthetic layers";
    emit statusMessage(QString::fromStdString(ss.str()));
    ss.str("");
    ss << "   - Layer thickness: " << testLayerThickness << " mm";
    emit statusMessage(QString::fromStdString(ss.str()));
    ss.str("");
    ss << "   - Laser OFF (pilot marking)";
    emit statusMessage(QString::fromStdString(ss.str()));
    ss.str("");
    ss << "   - Simple square geometry (5mm x 5mm)";
    emit statusMessage(QString::fromStdString(ss.str()));
    
    return true;
}

void ScanStreamingManager::stopProcess() {
    qDebug() << "ScanStreamingManager::stopProcess() - Initiating graceful shutdown";
    
    mStopRequested = true;
    
    // Wake all waiting threads to allow them to check mStopRequested
    mCvProducerNotFull.notify_all();
    mCvConsumerNotEmpty.notify_all();
    mCvPLCNotified.notify_all();
    mCvOPCReady.notify_all();

    // ========== FIX: Join test producer thread (was detached, caused crash) ==========
    if (mTestProducerThread.joinable()) {
        qDebug() << "Waiting for test producer thread to finish...";
        mTestProducerThread.join();
        qDebug() << "Test producer thread finished";
    }

    // Wait for threads to finish gracefully
    if (mProducerThread.joinable()) {
        qDebug() << "Waiting for producer thread to finish...";
        mProducerThread.join();
        qDebug() << "Producer thread finished";
    }
    
    if (mConsumerThread.joinable()) {
        qDebug() << "Waiting for consumer thread to finish...";
        mConsumerThread.join();
        qDebug() << "Consumer thread finished";
    }

    emit statusMessage("- Streaming process stopped (all threads shut down gracefully)");
}

// ========== NEW: Emergency stop ==========
void ScanStreamingManager::emergencyStop() {
    qDebug() << "ScanStreamingManager::emergencyStop() - EMERGENCY STOP ACTIVATED";
    
    mEmergencyStopFlag = true;
    mStopRequested = true;
    
    // Wake all waiting threads
    mCvProducerNotFull.notify_all();
    mCvConsumerNotEmpty.notify_all();
    mCvPLCNotified.notify_all();
    mCvOPCReady.notify_all();

    // ========== FIX: Join test producer thread (was detached, caused crash) ==========
    if (mTestProducerThread.joinable()) {
        qDebug() << "Waiting for test producer thread to finish...";
        mTestProducerThread.join();
        qDebug() << "Test producer thread finished";
    }

    // Wait for threads to finish (gracefully, no timeout in C++17)
    if (mProducerThread.joinable()) {
        mProducerThread.join();
    }
    
    if (mConsumerThread.joinable()) {
        mConsumerThread.join();
    }

    emit statusMessage("- EMERGENCY STOP: Laser disabled, all operations halted");
}

// ========== Notify completion ==========
void ScanStreamingManager::notifyLayerExecutionComplete(uint32_t layerNumber) {
    // ========== BIDIRECTIONAL OPC SYNCHRONIZATION ==========
    //
    // This method completes the bidirectional handshake with OPC/PLC.
    // Called from consumer thread after laser scanning completes for a layer.
    //
    // Handshake Flow:
    //   1. Scanner requests layer: writeLayerParameters() → LaySurface=TRUE
    //   2. PLC creates layer: recoater, platform → LaySurface_Done=TRUE
    //   3. Scanner executes layer: laser scan → turns laser OFF
    //   4. Scanner signals complete: THIS METHOD → LaySurface=FALSE
    //   5. Loop repeats for next layer
    //
    // Thread Safety:
    //   This is called from Consumer thread.
    //   mOPCManager pointer is stable (set by GUI thread before consumer starts).
    //   OPCServerManager methods are thread-safe (COM synchronization).
    //
    // Industrial Practice:
    //   All production SLM systems require bidirectional handshake.
    //   Without this, PLC may start next layer while Scanner still busy.
    //
    
    if (!mOPCManager) {
        qWarning() << "Cannot notify layer completion - OPC manager not set";
        return;
    }

    if (!mOPCManager->isInitialized()) {
        qWarning() << "Cannot notify layer completion - OPC not initialized";
        return;
    }

    // ========== SIGNAL OPC: LAYER EXECUTION COMPLETE ==========
    //
    // This writes LaySurface=FALSE to PLC.
    // PLC interprets this as: "Scanner finished layer, ready for next"
    //
    if (!mOPCManager->writeLayerExecutionComplete(static_cast<int>(layerNumber))) {
        qWarning() << "Failed to notify OPC of layer" << layerNumber << "completion";
        // Non-fatal error: continue with next layer anyway
    } else {
        qDebug() << "✅ Layer" << layerNumber << "execution complete signal sent to OPC";
    }
}

// ========== Notify PLC Prepared (called from ProcessController / OPC worker) ==========
void ScanStreamingManager::notifyPLCPrepared() {
    mPLCPrepared = true;
    mCvPLCNotified.notify_one();
}

// ============================================================================
// CONSUMER THREAD - Execute commands on Scanner with parameter switching
// ============================================================================

void ScanStreamingManager::consumerThreadFunc() {
    try {
        qDebug() << "Consumer thread started";
        
        std::ostringstream ss;
        
        // ============================================================================
        // CRITICAL: Scanner is owned ONLY by this thread
        // ============================================================================
        // This thread creates, initializes, uses, and destroys the Scanner instance.
        // No other thread may access the Scanner.
        // This follows ScanLab RTC5 industrial standard: device ownership per thread
        // ============================================================================
        
        Scanner scanner;
        
        // ============================================================================
        // PHASE 1: Load BuildStyleLibrary from JSON Configuration File
        // ============================================================================
        // INDUSTRIAL PRACTICE: Load configuration in consumer thread
        // This ensures thread-safe access to mBuildStyles during layer execution
        
        if (!mConfigJsonPath.empty()) {
            // Convert wstring to string for BuildStyleLibrary
            std::string configPath(mConfigJsonPath.begin(), mConfigJsonPath.end());
            
            emit statusMessage(QString("Consumer: Loading BuildStyle parameters from config.json..."));
            
            if (!mBuildStyles.loadFromJson(configPath)) {
                ss.str("");
                ss << "- CRITICAL: Failed to parse buildStyles from: " << configPath;
                emit error(QString::fromStdString(ss.str()));
                mStopRequested = true;
                return;
            }

            ss.str("");
            ss << "- Consumer: Loaded " << mBuildStyles.count() << " buildStyles from config.json";
            emit statusMessage(QString::fromStdString(ss.str()));
            emit configLoaded(QString::fromStdString(configPath));
            
            // Validate that we have at least one build style
            if (mBuildStyles.isEmpty()) {
                emit statusMessage("- WARNING: No buildStyles loaded from config.json. Using defaults only.");
            }
        } else {
            emit statusMessage("- WARNING: No config.json path provided. Using default parameters only.");
        }
        
        // ============================================================================
        // PHASE 2: Scanner Initialization with Comprehensive Checks
        // ============================================================================
        
        // Initialize scanner with production config
        try {
            // Pre-check: Ensure RTC5 DLL can be acquired safely
            if (!RTC5DLLManager::instance().acquireDLL()) {
                emit error("CRITICAL: Failed to acquire RTC5 DLL in consumer thread");
                mStopRequested = true;
                return;
            }
            
            if (!scanner.initialize(mScannerConfig)) {
                emit error("CRITICAL: Scanner initialization failed in consumer thread");
                mStopRequested = true;
                return;
            }
        } catch (const std::exception& e) {
            qDebug() << "Exception during scanner initialization:" << QString::fromStdString(e.what());
            mStopRequested = true;
            return;
        } catch (...) {
            qDebug() << "Unknown exception during scanner initialization";
            mStopRequested = true;
            return;
        }
        
        // Post-initialization verification
        if (!scanner.isInitialized()) {
            ss.str("");
            ss << "- CRITICAL: Scanner initialization reported success but isInitialized() returned false\n"
               << "  This indicates an internal consistency error in the Scanner class";
            emit error(QString::fromStdString(ss.str()));
            mStopRequested = true;
            return;
        }
        
        emit statusMessage("- Scanner initialization complete");

        // ============================================================================
        // PHASE 3: OPC Manager Validation
        // ============================================================================
        // Verify OPC manager is available for layer creation synchronization
        
        if (mProcessMode == ProcessMode::Production && !mOPCManager) {
            emit error("ERROR: OPC Manager not initialized. Call setOPCManager() first or ensure SLMWorkerManager is active.");
            mStopRequested = true;
            return;
        }
        
        // ============================================================================
        // PHASE 4: Main Consumer Loop - Process Enqueued Layers
        // ============================================================================
        
        emit statusMessage("- Consumer thread ready: awaiting layers from producer...");
        
        size_t layerNumber = 0;

        while (!mStopRequested) {  // FIX: Check stop flag at loop entry (atomic read)
            std::shared_ptr<marc::RTCCommandBlock> block;

            // ====== WAIT FOR PRODUCER TO ENQUEUE A BLOCK ======
            {
                std::unique_lock<std::mutex> lk(mMutex);
                mCvConsumerNotEmpty.wait(lk, [this] {
                    return mStopRequested || !mQueue.empty();  // Check both conditions
                });
                if (mStopRequested) break;  // Exit cleanly if stop requested

                // Pop from front of queue
                if (mQueue.empty()) continue;
                block = mQueue.front();
                mQueue.pop_front();
                lk.unlock();

                // Notify producer that queue has space
                mCvProducerNotFull.notify_one();
            }

            if (!block) continue;

            layerNumber = block->layerNumber;
            mCurrentLayerNumber = layerNumber;

            // ========== INDUSTRIAL PRACTICE: OPC LAYER SYNCHRONIZATION ==========
            // LAYER EXECUTION SEQUENCE:
            // 1. OPC calls writeLayerParameters(layerNumber, deltaValue, deltaValue)
            //    - deltaValue = layerThickness in microns
            //    - Initiates recoater, platform, laser timing in PLC
            // 2. OPC signals "layer prepared" when recoater finishes
            // 3. Consumer executes scan vectors
            // 4. Consumer turns laser OFF
            // 5. Repeat for next layer
            
            if (mProcessMode == ProcessMode::Production) {
                // ========== PRODUCTION MODE: Wait for OPC to prepare layer ==========`
                std::unique_lock<std::mutex> lk(mMutex);
                
                ss.str("");
                ss << "Layer " << layerNumber << ": Requesting OPC layer preparation...";
                emit statusMessage(QString::fromStdString(ss.str()));

                // Tell OPC to prepare this layer
                if (mOPCManager && mOPCManager->isInitialized()) {
                    float layerThickness = block->layerThickness;
                    int deltaValue = static_cast<int>(layerThickness * 1000.0f);  // mm to microns
                    
                    // Call OPC to initiate recoater, platform, and timing sequence
                    try {
                        if (!mOPCManager->writeLayerParameters(
                            static_cast<int>(1),//(layerNumber),// one layer at a time!
                            deltaValue,
                            deltaValue))
                        {
                            ss.str("");
                            ss << "Layer " << layerNumber << ": OPC layer setup failed, continuing anyway";
                            emit statusMessage(QString::fromStdString(ss.str()));
                        }
                    } catch (const std::exception& e) {
                        ss.str("");
                        ss << "Layer " << layerNumber << ": Exception during OPC write: " << e.what();
                        emit error(QString::fromStdString(ss.str()));
                    }
                }
                
                // Now wait for OPC to signal "layer prepared"
                ss.str("");
                ss << "Layer " << layerNumber << ": Waiting for recoater/platform to prepare...";
                emit statusMessage(QString::fromStdString(ss.str()));

                // Block consumer until OPC says "layer prepared"
                mCvPLCNotified.wait(lk, [this] {
                    return mStopRequested || mPLCPrepared.load();
                });
                
                if (mStopRequested) break;  // Exit if stop requested while waiting

                // Reset PLC flag for next layer
                mPLCPrepared = false;
                
                ss.str("");
                ss << "Layer " << layerNumber << ": - Recoater/platform ready, starting laser scan...";
                emit statusMessage(QString::fromStdString(ss.str()));
            } else {
                // ========== TEST MODE: No OPC synchronization ==========`
                ss.str("");
                ss << "Layer " << layerNumber << " (TEST MODE: no OPC sync, laser OFF)";
                emit statusMessage(QString::fromStdString(ss.str()));
            }

            // ====== CHECK FOR EMERGENCY STOP BEFORE EXECUTION ======
            if (mEmergencyStopFlag.load()) {
                ss.str("");
                ss << "Layer " << layerNumber << ":  EMERGENCY STOP activated, aborting execution";
                emit statusMessage(QString::fromStdString(ss.str()));
                break;
            }

            // ====== VERIFY SCANNER STILL OPERATIONAL BEFORE EXECUTION ======
            if (!scanner.isInitialized()) {
                ss.str("");
                ss << "CRITICAL: Scanner became uninitialized before executing layer " << layerNumber
                   << "\n  The scanner may have been disconnected or powered off";
                emit error(QString::fromStdString(ss.str()));
                mStopRequested = true;
                break;
            }

            // ============================================================================
            // ✅ CRITICAL FIX: PREPARE RTC5 LIST BEFORE QUEUING COMMANDS
            // ============================================================================
            // INDUSTRIAL RTC5 STANDARD: Before queuing any jump/mark commands,
            // the list MUST be explicitly opened and reset to accept new commands.
            //
            // RTC5 List State Machine (per layer):
            // 1. set_start_list(1)     - Open list for writing
            // 2. [queue commands]       - jumpTo/markTo add commands to list
            // 3. set_end_of_list()      - Close list (signals end of command sequence)
            // 4. execute_list(1)        - Hardware executes the closed list
            // 5. wait for completion    - Poll until list execution finishes
            //
            // ERROR SCENARIO (without this fix):
            // - List was closed after previous layer
            // - First command of new layer tries to write to closed list
            // - RTC5 rejects command → "command failed at index 0"
            //
            // SOLUTION: Explicitly restart list before queuing commands
            if (!scanner.prepareListForLayer()) {
                ss.str("");
                ss << "CRITICAL: Failed to prepare RTC5 list for layer " << layerNumber;
                emit error(QString::fromStdString(ss.str()));
                mStopRequested = true;
                break;
            }

            ss.str("");
            ss << "Layer " << layerNumber << ": Executing scanner commands...";
            emit statusMessage(QString::fromStdString(ss.str()));

            // ========== PARAMETER SWITCHING: CRITICAL SECTION =========
            const marc::RTCCommandBlock::ParameterSegment* currentSegment = nullptr;
            bool executionError = false;
            size_t commandsInCurrentBatch = 0;
            const size_t MAX_COMMANDS_PER_BATCH = mScannerConfig.listMemory - 10;  // Safety margin

            for (size_t i = 0; i < block->commands.size() && !mStopRequested; ++i) {
                const auto& cmd = block->commands[i];

                // ========== DEMO3 LESSON: Check if list buffer is getting full ==========
                // Demo3 monitors ListLevel and executes list when near capacity
                // This prevents buffer overflow and ensures smooth dual buffering
                if (scanner.getCurrentListLevel() >= MAX_COMMANDS_PER_BATCH) {
                    ss.str("");
                    ss << "  Layer " << layerNumber << ": List buffer near full ("
                       << scanner.getCurrentListLevel() << " commands), executing batch...";
                    emit statusMessage(QString::fromStdString(ss.str()));
                    
                    // Execute current batch
                    try {
                        if (!scanner.executeList()) {
                            ss.str("");
                            ss << "Failed to execute command batch at index " << i;
                            emit error(QString::fromStdString(ss.str()));
                            executionError = true;
                            break;
                        }
                        
                        // Wait for batch to complete
                        if (!scanner.waitForListCompletion(30000)) {  // 30s timeout per batch
                            ss.str("");
                            ss << "Batch execution timeout at command index " << i;
                            emit error(QString::fromStdString(ss.str()));
                            executionError = true;
                            break;
                        }
                        
                        // Prepare next batch buffer (Demo3's auto_change already swapped buffers)
                        if (!scanner.prepareListForLayer()) {
                            ss.str("");
                            ss << "Failed to prepare next batch buffer at command " << i;
                            emit error(QString::fromStdString(ss.str()));
                            executionError = true;
                            break;
                        }
                        
                        commandsInCurrentBatch = 0;
                        
                    } catch (const std::exception& e) {
                        ss.str("");
                        ss << "Exception during batch execution: " << e.what();
                        emit error(QString::fromStdString(ss.str()));
                        executionError = true;
                        break;
                    }
                }

                // CHECK IF WE ENTERED A NEW PARAMETER SEGMENT
                const auto* nextSegment = block->getSegmentFor(i);
                if (nextSegment && nextSegment != currentSegment) {
                    currentSegment = nextSegment;
                    
                    // ====== APPLY NEW LASER PARAMETERS ======
                    try {
                        if (!scanner.applySegmentParameters(currentSegment->laserPower,
                                                            currentSegment->laserSpeed,
                                                            currentSegment->jumpSpeed)) {
                            ss.str("");
                            ss << "   Failed to apply parameters for buildStyle " << currentSegment->buildStyleId
                               << " at command index " << i;
                            emit error(QString::fromStdString(ss.str()));
                            executionError = true;
                            break;
                        }

                        ss.str("");
                        ss << "  - Applied buildStyle " << currentSegment->buildStyleId
                           << " (power=" << currentSegment->laserPower << "W"
                           << ", markSpeed=" << currentSegment->laserSpeed << "mm/s"
                           << ", jumpSpeed=" << currentSegment->jumpSpeed << "mm/s)";
                        emit statusMessage(QString::fromStdString(ss.str()));
                    } catch (const std::exception& e) {
                        ss.str("");
                        ss << "Exception applying segment parameters: " << e.what();
                        emit error(QString::fromStdString(ss.str()));
                        executionError = true;
                        break;
                    }
                }

                // ====== EXECUTE INDIVIDUAL COMMAND ======
                bool success = false;

                if (cmd.type == marc::RTCCommandBlock::Command::Jump) {
                    success = scanner.jumpTo(Scanner::Point(cmd.x, cmd.y));
                } else if (cmd.type == marc::RTCCommandBlock::Command::Mark) {
                    success = scanner.markTo(Scanner::Point(cmd.x, cmd.y));
                } else if (cmd.type == marc::RTCCommandBlock::Command::Delay) {
                    // Delays are handled by the RTC card, but for simplicity in this refactor,
                    // we can use a sleep. For high-performance applications, this should be
                    // replaced with a proper RTC delay command.
                    std::this_thread::sleep_for(std::chrono::milliseconds(cmd.delayMs));
                    success = true;
                } else {
                    // SetPower, SetSpeed, SetFocus handled by applySegmentParameters()
                    success = true;
                }

                if (!success) {
                    ss.str("");
                    ss << "Failed to execute command at index " << i;
                    emit error(QString::fromStdString(ss.str()));
                    executionError = true;
                    break;
                }
            }

            if (executionError || mStopRequested) {
                if (executionError) {
                    emit statusMessage(QString::fromStdString(
                        "Layer " + std::to_string(layerNumber) + " execution encountered errors"));
                }
                mStopRequested = true;
                break;
            }

            // ====== EXECUTE FINAL BATCH FOR THIS LAYER ======
            // Demo3 Pattern: Close and execute remaining commands in buffer
            ss.str("");
            ss << "Layer " << layerNumber << ": Executing final batch ("
               << scanner.getCurrentListLevel() << " commands)...";
            emit statusMessage(QString::fromStdString(ss.str()));

            // ====== SMALL DELAY BEFORE EXECUTING LIST (RTC5 SYNCHRONIZATION) ======
            // Added delay to ensure RTC5 DSP is ready for command execution
            std::this_thread::sleep_for(std::chrono::milliseconds(2000));

            // ====== EXECUTE THE ACCUMULATED COMMAND LIST ON RTC5 ======
            if (!scanner.executeList()) {
                ss.str("");
                ss << "Scanner executeList() failed for layer " << layerNumber;
                emit error(QString::fromStdString(ss.str()));
                mStopRequested = true;
                break;
            }

            // ====== WAIT FOR SCANNER TO FINISH EXECUTING THIS LIST ======
            const int SCANNER_TIMEOUT_MS = 100000;  // 100 second timeout
            try {
                if (!scanner.waitForListCompletion(SCANNER_TIMEOUT_MS)) {
                    ss.str("");
                    ss << "Scanner list did not complete within timeout (" << SCANNER_TIMEOUT_MS << "ms)"
                       << " for layer " << layerNumber
                       << "\n  The scanner may be stuck or hardware may be offline";
                    emit error(QString::fromStdString(ss.str()));
                    mStopRequested = true;
                    break;
                }
            } catch (const std::exception& e) {
                ss.str("");
                ss << "Exception waiting for list completion for layer " << layerNumber << ": " << e.what();
                emit error(QString::fromStdString(ss.str()));
                mStopRequested = true;
                break;
            }

            // ========== LASER OFF AFTER LAYER EXECUTION ==========`
            // Industrial SLM standard: disable laser after each layer to prevent drift
            try {
                scanner.disableLaser();
                ss.str("");
                ss << "Layer " << layerNumber << ": Execution complete, laser OFF";
                emit statusMessage(QString::fromStdString(ss.str()));
            } catch (const std::exception& e) {
                qWarning() << "Failed to disable laser after layer:" << e.what();
            }

            // ====== LAYER EXECUTION COMPLETE ======
            ++mLayersConsumed;
            emit layerExecuted(static_cast<uint32_t>(layerNumber));
            emit progress(static_cast<int>(mLayersConsumed.load()), 
                         static_cast<int>(mTotalLayers.load()));
            
            // ========== BIDIRECTIONAL OPC SYNCHRONIZATION: NOTIFY LAYER COMPLETE ========= =
            //
            // CRITICAL: Signal OPC that Scanner has finished executing this layer.
            // This completes the bidirectional handshake loop:
            //
            // Full Per-Layer Cycle:
            // ─────────────────────────────────────────────────────────────────────────
            // STEP 1: Scanner Requests Layer Creation
            //   • Consumer calls mOPCManager->writeLayerParameters(layerNumber, deltaValue, deltaValue)
            //   • OPC writes: Lay_Stacks, Step_Source, Step_Sink, LaySurface=TRUE
            //   • PLC starts recoater/platform movement
            //
            // STEP 2: PLC Signals Layer Ready
            //   • PLC completes recoater/platform movement
            //   • PLC sets: LaySurface_Done=TRUE
            //
            // STEP 3: ProcessController Detects Layer Ready (Polling)
            //   • GUI thread polls OPC data every 500ms
            //   • onOPCDataUpdated() detects LaySurface_Done=TRUE
            //   • Calls handlePowderSurfaceComplete()
            //   • Calls ScanStreamingManager::notifyPLCPrepared()
            //     └─ Sets mPLCPrepared = true
            //     └─ Notifies mCvPLCNotified condition_variable
            //
            // STEP 4: Consumer Executes Layer
            //   • Consumer thread wakes from condition_variable wait
            //   • Executes scanner commands (jump/mark)
            //   • Turns laser OFF
            //
            // STEP 5: Scanner Notifies OPC Execution Complete (THIS STEP)
            //   • Consumer calls notifyLayerExecutionComplete(layerNumber)
            //   • This calls mOPCManager->writeLayerExecutionComplete(layerNumber)
            //   • OPC writes: LaySurface=FALSE
            //   • PLC receives signal: "Scanner finished, ready for next layer"
            //
            // STEP 6: Loop Repeats
            //   • Next layer dequeued
            //   • Scanner requests next layer creation (back to STEP 1)
            //
            // WHY THIS IS CRITICAL:
            // ─────────────────────────────────────────────────────────────────────────
            // Without this signal, PLC doesn't know when Scanner finishes.
            // This can cause:
            //   • Race conditions (PLC starts next layer while Scanner still working)
            //   • Timing errors (layer creation out of sync with laser execution)
            //   • Safety issues (laser still ON when recoater moves)
            //
            // Industrial Standard:
            //   All production SLM systems implement bidirectional handshake.
            //   OPC → Scanner: "Layer ready"
            //   Scanner → OPC: "Layer done"
            //   This ensures perfect synchronization between mechanical (recoater)
            //   and optical (laser) subsystems.
            //
            if (mProcessMode == ProcessMode::Production) {
                // Only notify OPC in production mode (test mode has no PLC sync)
                notifyLayerExecutionComplete(static_cast<uint32_t>(layerNumber));
            }
        }

        // ============================================================================
        // PHASE 5: Shutdown scanner gracefully
        // ============================================================================
        try {
            // ========== EMERGENCY STOP: Disable laser immediately ==========
            if (mEmergencyStopFlag.load()) {
                scanner.disableLaser();
                ss.str("");
                ss << "Emergency: Laser disabled";
                emit statusMessage(QString::fromStdString(ss.str()));
            }
            
            scanner.shutdown();
            emit statusMessage("Scanner shutdown complete (consumer thread finished)");
        } catch (const std::exception& e) {
            ss.str("");
            ss << "Exception during scanner shutdown: " << e.what();
            emit statusMessage(QString::fromStdString(ss.str()));
        }

        // ========== SAFE SIGNAL EMISSION: Emit finished only after thread-local cleanup ==========
        // By this point:
        // - Scanner is shut down (local variable destructed)
        // - Thread is about to exit naturally
        // - All thread-local state is clean
        // - finished() signal will be caught by GUI via Qt::QueuedConnection
        //   (signal queued, not invoked immediately from thread context)
        emit finished();

    } catch (const std::exception& e) {
        // CRITICAL FIX: Catch exceptions in main thread function
        // This prevents unhandled exceptions from terminating thread without cleanup
        std::ostringstream ss;
        ss << "CRITICAL: Unhandled exception in consumer thread: " << e.what();
        emit error(QString::fromStdString(ss.str()));
        emit finished();  // Still signal completion so GUI thread can clean up
    } catch (...) {
        // Catch any exception that std::exception doesn't cover
        emit error("CRITICAL: Unknown exception in consumer thread");
        emit finished();  // Still signal completion
    }
}

// ============================================================================
// PRODUCER THREAD - Stream MARC file and convert layers with parameters
// ============================================================================

void ScanStreamingManager::producerThreadFunc(const std::wstring& marcPath) {
    try {
        // Open MARC file for streaming (no full load)
        marc::StreamingMarcReader reader(marcPath);
        mTotalLayers = reader.totalLayers();
        
        if (mTotalLayers == 0) {
            emit error("MARC file contains no layers");
            mStopRequested = true;
            return;
        }

        std::ostringstream ss;
        ss << "Loading " << mTotalLayers << " layers from file (streaming mode)";
        emit statusMessage(QString::fromStdString(ss.str()));

        // Stream layers one at a time
        while (reader.hasNextLayer() && !mStopRequested) {
            // Read one layer from file (low memory footprint)
            marc::Layer layer;
            try {
                layer = reader.readNextLayer();
            } catch (const std::exception& e) {
                ss.str("");
                ss << "Error reading layer " << reader.currentLayerIndex() << ": " << e.what();
                emit error(QString::fromStdString(ss.str()));
                mStopRequested = true;
                break;
            }

            // Convert to command block WITH PARAMETER SEGMENTS
            auto block = std::make_shared<marc::RTCCommandBlock>();
            block->layerNumber = layer.layerNumber;
            block->layerHeight = layer.layerHeight;
            block->layerThickness = layer.layerThickness;
            block->hatchCount = layer.hatches.size();
            block->polylineCount = layer.polylines.size();
            block->polygonCount = layer.polygons.size();

            if (!convertLayerToBlock(layer, *block)) {
                ss.str("");
                ss << "Conversion failed for layer " << layer.layerNumber;
                emit error(QString::fromStdString(ss.str()));
                mStopRequested = true;
                break;
            }

            // Enqueue block (bounded queue: wait if full)
            {
                std::unique_lock<std::mutex> lk(mMutex);
                // Block producer if queue is full
                mCvProducerNotFull.wait(lk, [this] {
                    return mStopRequested || mQueue.size() < mMaxQueue;
                });
                if (mStopRequested) break;  // FIX: Exit gracefully if stop requested

                mQueue.push_back(block);
                ++mLayersProduced;

                ss.str("");
                ss << "Layer " << layer.layerNumber << " enqueued ("
                   << mLayersProduced << "/" << mTotalLayers << ") with "
                   << block->parameterSegments.size() << " parameter segments";
                emit statusMessage(QString::fromStdString(ss.str()));
            }
            // Notify consumer that queue has data
            mCvConsumerNotEmpty.notify_one();
            
            emit progress(static_cast<int>(mLayersProduced.load()), 
                         static_cast<int>(mTotalLayers.load()));
        }

        if (!mStopRequested) {
            emit statusMessage("- Producer finished streaming all layers");
        }
    } catch (const std::exception& e) {
        // FIX: Catch exceptions in producer thread
        std::ostringstream ss;
        ss << "Producer exception: " << e.what();
        emit error(QString::fromStdString(ss.str()));
    } catch (...) {
        // Catch any unhandled exception
        emit error("Producer: Unknown exception occurred");
    }
}

// ============================================================================
// PRODUCER THREAD (TEST MODE) - Generate synthetic layers for testing
// ============================================================================

void ScanStreamingManager::producerTestThreadFunc(float layerThickness, size_t layerCount) {
    try {
        std::ostringstream ss;
        ss << "Test producer: Generating " << layerCount << " synthetic layers @ " 
           << layerThickness << " mm";
        emit statusMessage(QString::fromStdString(ss.str()));

        // Generate synthetic layers for testing
        for (size_t i = 0; i < layerCount && !mStopRequested; ++i) {
            // Create a synthetic RTCCommandBlock (no geometry, just marks)
            auto block = std::make_shared<marc::RTCCommandBlock>();
            block->layerNumber = i + 1;
            block->layerHeight = static_cast<float>(i) * layerThickness;
            block->layerThickness = layerThickness;
            
            // Add a simple test pattern: 10mm square
            // This allows testing without a real MARC file
            block->hatchCount = 1;
            block->polylineCount = 0;
            block->polygonCount = 0;
            
            // Create a 10mm square test pattern (4 sides, 20 marks total)
            const long TEST_SIZE = 5000;  // 5mm in bits
            
            // Side 1: left to right bottom
            for (int j = -TEST_SIZE; j <= TEST_SIZE; j += 1000) {
                marc::RTCCommandBlock::Command jump{
                    marc::RTCCommandBlock::Command::Jump,
                    static_cast<long>(j),
                    -TEST_SIZE};
                marc::RTCCommandBlock::Command mark{
                    marc::RTCCommandBlock::Command::Mark,
                    static_cast<long>(j),
                    static_cast<long>(-TEST_SIZE + 500)};
                block->commands.push_back(jump);
                block->commands.push_back(mark);
            }
            
            // Create parameter segment with fixed pilot mode values (do not use getStyle(8))
            marc::BuildStyle pilotStyle;
            pilotStyle.id = 0; // or a special pilot/test id
            pilotStyle.laserPower = 0.0;      // Pilot mode: laser off
            pilotStyle.laserSpeed = 200.0;     // Mark speed (mm/s)
            pilotStyle.jumpSpeed = 1200.0;    // Jump speed (mm/s)
            pilotStyle.laserMode = 0;         // Default mode
            pilotStyle.laserFocus = 0.0;      // Default focus
            // Apply pilot style to the block
            applyBuildStyle(&pilotStyle, *block, 0);
            
            // Enqueue block (bounded queue: wait if full)
            {
                std::unique_lock<std::mutex> lk(mMutex);
                mCvProducerNotFull.wait(lk, [this] {
                    return mStopRequested || mQueue.size() < mMaxQueue;
                });
                if (mStopRequested) break;  // FIX: Exit gracefully if stop requested

                mQueue.push_back(block);
                ++mLayersProduced;

                ss.str("");
                ss << "Test Layer " << block->layerNumber << " generated ("
                   << mLayersProduced << "/" << layerCount << ")";
                emit statusMessage(QString::fromStdString(ss.str()));
            }
            mCvConsumerNotEmpty.notify_one();
            
            emit progress(static_cast<int>(mLayersProduced.load()), 
                         static_cast<int>(layerCount));
        }

        if (!mStopRequested) {
            emit statusMessage("- Test producer finished generating all synthetic layers");
        }
    } catch (const std::exception& e) {
        // FIX: Catch exceptions in test producer thread
        std::ostringstream ss;
        ss << "Test producer exception: " << e.what();
        emit error(QString::fromStdString(ss.str()));
    } catch (...) {
        // Catch any unhandled exception
        emit error("Test producer: Unknown exception occurred");
    }
}

// ============================================================================
// ========== CONVERSION LOGIC IMPLEMENTATIONS ===========
// ============================================================================

bool ScanStreamingManager::convertLayerToBlock(const marc::Layer& L, marc::RTCCommandBlock& out) {
    try {
        size_t cmdStartIdx = 0;

        for (const auto& h : L.hatches) {
            if (!convertHatch(h, out, cmdStartIdx)) return false;
        }
        for (const auto& p : L.polylines) {
            if (!convertPolyline(p, out, cmdStartIdx)) return false;
        }
        for (const auto& pg : L.polygons) {
            if (!convertPolygon(pg, out, cmdStartIdx)) return false;
        }
        return true;
    } catch (const std::exception& e) {
        emit error(QString::fromStdString(std::string("convertLayerToBlock exception: ") + e.what()));
        return false;
    }
}

bool ScanStreamingManager::convertHatch(const marc::Hatch& h, marc::RTCCommandBlock& out, size_t& cmdStartIdx) {
    try {
        cmdStartIdx = out.commands.size();

        const marc::BuildStyle* style = mBuildStyles.getStyle(h.tag.type);
        if (!style) style = mBuildStyles.getStyle(8);

        for (const auto& line : h.lines) {
            marc::RTCCommandBlock::Command jump{marc::RTCCommandBlock::Command::Jump,
                static_cast<long>(mmToBits(static_cast<double>(line.a.x))),
                static_cast<long>(mmToBits(static_cast<double>(line.a.y)))};
            marc::RTCCommandBlock::Command mark{marc::RTCCommandBlock::Command::Mark,
                static_cast<long>(mmToBits(static_cast<double>(line.b.x))),
                static_cast<long>(mmToBits(static_cast<double>(line.b.y)))};
            out.commands.push_back(jump);
            out.commands.push_back(mark);
        }

        if (style) applyBuildStyle(style, out, cmdStartIdx);
        return true;
    } catch (...) {
        return false;
    }
}

bool ScanStreamingManager::convertPolyline(const marc::Polyline& p, marc::RTCCommandBlock& out, size_t& cmdStartIdx) {
    try {
        if (p.points.empty()) return true;
        cmdStartIdx = out.commands.size();

        const marc::BuildStyle* style = mBuildStyles.getStyle(p.tag.type);
        if (!style) style = mBuildStyles.getStyle(8);

        // Jump to first point
        out.commands.push_back(marc::RTCCommandBlock::Command{marc::RTCCommandBlock::Command::Jump,
            static_cast<long>(mmToBits(static_cast<double>(p.points[0].x))),
            static_cast<long>(mmToBits(static_cast<double>(p.points[0].y)))});

        for (size_t i = 1; i < p.points.size(); ++i) {
            out.commands.push_back(marc::RTCCommandBlock::Command{marc::RTCCommandBlock::Command::Mark,
                static_cast<long>(mmToBits(static_cast<double>(p.points[i].x))),
                static_cast<long>(mmToBits(static_cast<double>(p.points[i].y)))});
        }

        if (style) applyBuildStyle(style, out, cmdStartIdx);
        return true;
    } catch (...) {
        return false;
    }
}

bool ScanStreamingManager::convertPolygon(const marc::Polygon& p, marc::RTCCommandBlock& out, size_t& cmdStartIdx) {
    try {
        if (p.points.empty()) return true;
        cmdStartIdx = out.commands.size();

        const marc::BuildStyle* style = mBuildStyles.getStyle(p.tag.type);
        if (!style) style = mBuildStyles.getStyle(8);

        out.commands.push_back(marc::RTCCommandBlock::Command{marc::RTCCommandBlock::Command::Jump,
            static_cast<long>(mmToBits(static_cast<double>(p.points[0].x))),
            static_cast<long>(mmToBits(static_cast<double>(p.points[0].y)))});

        for (size_t i = 1; i < p.points.size(); ++i) {
            out.commands.push_back(marc::RTCCommandBlock::Command{marc::RTCCommandBlock::Command::Mark,
                static_cast<long>(mmToBits(static_cast<double>(p.points[i].x))),
                static_cast<long>(mmToBits(static_cast<double>(p.points[i].y)))});
        }

        // Close loop
        out.commands.push_back(marc::RTCCommandBlock::Command{marc::RTCCommandBlock::Command::Mark,
            static_cast<long>(mmToBits(static_cast<double>(p.points[0].x))),
            static_cast<long>(mmToBits(static_cast<double>(p.points[0].y)))});

        if (style) applyBuildStyle(style, out, cmdStartIdx);
        return true;
    } catch (...) {
        return false;
    }
}

void ScanStreamingManager::applyBuildStyle(const marc::BuildStyle* style,
                                           marc::RTCCommandBlock& out,
                                           size_t cmdStartIdx) {
    if (!style) return;

    size_t cmdEndIdx = out.commands.size();
    if (cmdEndIdx == 0) return;
    cmdEndIdx = cmdEndIdx - 1;

    out.addParameterSegment(
        style->id,
        style->laserPower,
        style->laserSpeed,
        style->jumpSpeed,
        style->laserMode,
        style->laserFocus
    );

    // Ensure segment covers intended commands
    if (!out.parameterSegments.empty()) {
        auto &seg = out.parameterSegments.back();
        seg.startCmd = cmdStartIdx;
        seg.endCmd = cmdEndIdx;
    }
}

long ScanStreamingManager::mmToBits(double mm) const {
    double bits = mm * mCalib.bitsPerMM();
    long mx = mCalib.maxBits;
    if (bits > mx) bits = mx;
    if (bits < -mx) bits = -mx;
    return static_cast<long>(std::lround(bits));
}
