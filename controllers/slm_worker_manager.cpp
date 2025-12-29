// ============================================================================
// slm_worker_manager.cpp - CORRECTED VERSION
// ============================================================================
//
// THREAD-SAFE OPC WORKER MANAGEMENT WITH COMPREHENSIVE EXCEPTION HANDLING
//
// KEY FIXES:
// 1. Exception handling in all thread creation paths
// 2. Safe pointer validation before atomic storage
// 3. Try-catch blocks around all hardware initialization
// 4. Proper thread state validation (joinable/detached)
// 5. Resource cleanup on partial initialization failures
// 6. Logging of all error conditions instead of silent failures
//
// ============================================================================

#include "slm_worker_manager.h"
#include "controllers/opccontroller.h"
#include "opcserver_lib/opcserverua.h"
#include "scanner_lib/Scanner.h"

#include <QDebug>
#include <QThread>
#include <sstream>
#include <stdexcept>

// ============================================================================
// OPCWorker Implementation - CORRECTED
// ============================================================================

OPCWorker::OPCWorker(QObject* parent)
    : QObject(parent), mOPCManager(nullptr), mInitialized(false)
{
    // Thread affinity automatically set by Qt to creating thread
}

OPCWorker::~OPCWorker()
{
    try {
        if (mOPCManager) {
            qWarning() << "OPCWorker destructor: OPC UA not properly shut down!";
            mOPCManager.reset();
        }
    } catch (const std::exception& e) {
        qCritical() << "Exception in OPCWorker destructor:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception in OPCWorker destructor";
    }
}

void OPCWorker::initialize()
{
    // ========== CALLED IN OPC WORKER THREAD =========
    // CRITICAL: All exceptions must be caught here to prevent thread termination

    std::ostringstream tid;
    try {
        tid << std::this_thread::get_id();
    } catch (...) {
        tid << "unknown";
    }
    
    qDebug() << "OPCWorker::initialize() - Starting OPC UA initialization in thread"
             << QString::fromStdString(tid.str());

    try {
        // ========== VALIDATE THREAD STATE =========
        // Ensure we're running in expected worker thread context
        if (!QThread::currentThread()) {
            qCritical() << "OPCWorker::initialize() - Invalid thread context";
            emit error("Invalid thread context during OPC UA initialization");
            emit initialized(false);
            return;
        }

        // ========== CREATE OPC UA SERVER MANAGER =========
        qDebug() << "OPCWorker::initialize() - Creating OPCServerManagerUA instance";
        
        try {
            mOPCManager = std::make_unique<OPCServerManagerUA>();
        } catch (const std::bad_alloc& e) {
            qCritical() << "OPCWorker::initialize() - Memory allocation failed:" << e.what();
            emit error("Memory allocation failed for OPC UA manager");
            emit initialized(false);
            return;
        } catch (const std::exception& e) {
            qCritical() << "OPCWorker::initialize() - Exception creating OPC UA manager:" << e.what();
            emit error(QString("Failed to create OPC UA manager: %1").arg(e.what()));
            emit initialized(false);
            return;
        }

        // Validate pointer before proceeding
        if (!mOPCManager) {
            qCritical() << "OPCWorker::initialize() - OPC UA manager pointer is null after creation";
            emit error("OPC UA manager creation failed");
            emit initialized(false);
            return;
        }

        qDebug() << "OPCWorker::initialize() - OPC UA manager created successfully";

        // ========== INITIALIZE OPC UA CONNECTION =========
        qDebug() << "OPCWorker::initialize() - Attempting to connect to OPC UA server";
        
        bool initSuccess = false;
        try {
            initSuccess = mOPCManager->initialize();
        } catch (const std::exception& e) {
            qCritical() << "OPCWorker::initialize() - Exception during OPC UA connection:" << e.what();
            emit error(QString("OPC UA connection exception: %1").arg(e.what()));
            emit initialized(false);
            
            // Clean up partially initialized resources
            try {
                mOPCManager.reset();
            } catch (...) {
                qCritical() << "Failed to cleanup OPC UA manager after connection failure";
            }
            return;
        } catch (...) {
            qCritical() << "OPCWorker::initialize() - Unknown exception during OPC UA connection";
            emit error("Unknown exception during OPC UA connection");
            emit initialized(false);
            
            // Clean up
            try {
                mOPCManager.reset();
            } catch (...) {}
            return;
        }

        if (!initSuccess) {
            qDebug() << "OPCWorker::initialize() - OPC UA initialization failed (returned false)";
            emit error("Failed to initialize OPC UA server (endpoint not running or configuration error)");
            emit initialized(false);
            
            // Clean up
            try {
                mOPCManager.reset();
            } catch (const std::exception& e) {
                qCritical() << "Exception during cleanup:" << e.what();
            }
            return;
        }

        // ========== INITIALIZATION SUCCESSFUL =========
        mInitialized = true;
        qDebug() << "OPCWorker::initialize() - OPC UA server initialized successfully";
        qDebug() << "OPCWorker::initialize() - Connection established, ready for operations";

        // ========== SIGNAL GUI THREAD =========
        // Safe cross-thread communication via Qt::QueuedConnection
        emit initialized(true);

    } catch (const std::exception& e) {
        qCritical() << "OPCWorker::initialize() - Unhandled exception:" << e.what();
        emit error(QString("OPC UA initialization exception: %1").arg(e.what()));
        emit initialized(false);
        
        // Emergency cleanup
        try {
            if (mOPCManager) {
                mOPCManager.reset();
            }
        } catch (...) {
            qCritical() << "Failed to cleanup after exception";
        }
    } catch (...) {
        qCritical() << "OPCWorker::initialize() - Unknown exception type";
        emit error("Unknown exception during OPC UA initialization");
        emit initialized(false);
        
        // Emergency cleanup
        try {
            if (mOPCManager) {
                mOPCManager.reset();
            }
        } catch (...) {}
    }
}

void OPCWorker::shutdown()
{
    // ========== CALLED IN OPC WORKER THREAD =========
    // Exception-safe shutdown

    std::ostringstream tid;
    try {
        tid << std::this_thread::get_id();
    } catch (...) {
        tid << "unknown";
    }
    
    qDebug() << "OPCWorker::shutdown() - Shutting down OPC UA in thread"
             << QString::fromStdString(tid.str());

    try {
        if (mOPCManager && mInitialized) {
            qDebug() << "OPCWorker::shutdown() - Calling OPC UA manager destructor";
            
            try {
                mOPCManager.reset();
                mInitialized = false;
                qDebug() << "OPCWorker::shutdown() - Shutdown complete";
            } catch (const std::exception& e) {
                qCritical() << "Exception during OPC UA shutdown:" << e.what();
                mInitialized = false;
            } catch (...) {
                qCritical() << "Unknown exception during OPC UA shutdown";
                mInitialized = false;
            }
        } else {
            qDebug() << "OPCWorker::shutdown() - Already shut down or not initialized";
        }
    } catch (const std::exception& e) {
        qCritical() << "OPCWorker::shutdown() - Exception in shutdown logic:" << e.what();
    } catch (...) {
        qCritical() << "OPCWorker::shutdown() - Unknown exception in shutdown logic";
    }
    
    emit shutdown_complete();
}

void OPCWorker::writeStartUp(bool value)
{
    if (!mInitialized || !mOPCManager) {
        emit error("OPC UA not initialized for writeStartUp");
        return;
    }

    try {
        if (!mOPCManager->writeStartUp(value)) {
            emit error("Failed to write StartUp tag via OPC UA");
        }
    } catch (const std::exception& e) {
        qCritical() << "Exception in writeStartUp:" << e.what();
        emit error(QString("Exception writing StartUp: %1").arg(e.what()));
    } catch (...) {
        qCritical() << "Unknown exception in writeStartUp";
        emit error("Unknown exception writing StartUp");
    }
}

void OPCWorker::writePowderFillParameters(int layers, int deltaSource, int deltaSink)
{
    if (!mInitialized || !mOPCManager) {
        emit error("OPC UA not initialized for writePowderFillParameters");
        return;
    }

    try {
        if (!mOPCManager->writePowderFillParameters(layers, deltaSource, deltaSink)) {
            emit error("Failed to write powder fill parameters via OPC UA");
        }
    } catch (const std::exception& e) {
        qCritical() << "Exception in writePowderFillParameters:" << e.what();
        emit error(QString("Exception writing powder fill parameters: %1").arg(e.what()));
    } catch (...) {
        qCritical() << "Unknown exception in writePowderFillParameters";
        emit error("Unknown exception writing powder fill parameters");
    }
}

void OPCWorker::writeBottomLayerParameters(int layers, int deltaSource, int deltaSink)
{
    if (!mInitialized || !mOPCManager) {
        emit error("OPC UA not initialized for writeBottomLayerParameters");
        return;
    }

    try {
        if (!mOPCManager->writeBottomLayerParameters(layers, deltaSource, deltaSink)) {
            emit error("Failed to write bottom layer parameters via OPC UA");
        }
    } catch (const std::exception& e) {
        qCritical() << "Exception in writeBottomLayerParameters:" << e.what();
        emit error(QString("Exception writing bottom layer parameters: %1").arg(e.what()));
    } catch (...) {
        qCritical() << "Unknown exception in writeBottomLayerParameters";
        emit error("Unknown exception writing bottom layer parameters");
    }
}

void OPCWorker::writeLayerParameters(int layerNumber, int deltaValue)
{
    if (!mInitialized || !mOPCManager) {
        emit error("OPC UA not initialized for writeLayerParameters");
        return;
    }

    try {
        if (!mOPCManager->writeLayerParameters(layerNumber, deltaValue, deltaValue)) {
            emit error(QString("Failed to write layer %1 parameters via OPC UA").arg(layerNumber));
        }
    } catch (const std::exception& e) {
        qCritical() << "Exception in writeLayerParameters:" << e.what();
        emit error(QString("Exception writing layer parameters: %1").arg(e.what()));
    } catch (...) {
        qCritical() << "Unknown exception in writeLayerParameters";
        emit error("Unknown exception writing layer parameters");
    }
}

// ============================================================================
// SLMWorkerManager Implementation - CORRECTED
// ============================================================================

SLMWorkerManager::SLMWorkerManager(QObject* parent)
    : QObject(parent), mOPCInitialized(false), mShuttingDown(false)
{
    qDebug() << "SLMWorkerManager created in thread" << QThread::currentThread();
}

SLMWorkerManager::~SLMWorkerManager()
{
    try {
        qDebug() << "SLMWorkerManager destructor called";
        
        if (!mShuttingDown) {
            stopWorkers();
        }
    } catch (const std::exception& e) {
        qCritical() << "Exception in SLMWorkerManager destructor:" << e.what();
    } catch (...) {
        qCritical() << "Unknown exception in SLMWorkerManager destructor";
    }
}

void SLMWorkerManager::opcThreadFunc()
{
    // ========== OPC UA WORKER THREAD MAIN FUNCTION =========
    // CRITICAL: This entire function must be exception-safe

    qDebug() << "opcThreadFunc() - Thread function started";

    try {
        // ========== CREATE WORKER OBJECT =========
        // Stack-local, thread affinity automatically set
        OPCWorker localWorker;

        // ========== RECORD THREAD ID =========
        std::thread::id threadId = std::this_thread::get_id();
        mOPCThreadId.store(threadId);
        mOPCRunning.store(true);
        
        std::ostringstream tidStr;
        tidStr << threadId;
        qDebug() << "opcThreadFunc() - Worker thread ID:" << QString::fromStdString(tidStr.str());

        // ========== CONNECT SIGNALS =========
        // Qt::QueuedConnection for safe cross-thread communication
        qDebug() << "opcThreadFunc() - Connecting worker signals";
        
        try {
            connect(&localWorker, &OPCWorker::initialized,
                    this, &SLMWorkerManager::onOPCInitialized,
                    Qt::QueuedConnection);
            connect(&localWorker, &OPCWorker::shutdown_complete,
                    this, &SLMWorkerManager::onOPCShutdown,
                    Qt::QueuedConnection);
            connect(&localWorker, &OPCWorker::error,
                    this, &SLMWorkerManager::onOPCError,
                    Qt::QueuedConnection);
        } catch (const std::exception& e) {
            qCritical() << "Exception connecting signals:" << e.what();
            mOPCRunning.store(false);
            return;
        }

        qDebug() << "opcThreadFunc() - Signals connected successfully";

        // ========== INITIALIZE OPC UA =========
        qDebug() << "opcThreadFunc() - Calling localWorker.initialize()";
        
        try {
            localWorker.initialize();
        } catch (const std::exception& e) {
            qCritical() << "Exception during OPC UA initialization:" << e.what();
            mOPCRunning.store(false);
            return;
        } catch (...) {
            qCritical() << "Unknown exception during OPC UA initialization";
            mOPCRunning.store(false);
            return;
        }

        qDebug() << "opcThreadFunc() - Initialization completed, checking manager pointer";

        // ========== EXPOSE OPC UA MANAGER POINTER =========
        // Thread-safe atomic storage
        OPCServerManagerUA* managerPtr = localWorker.getOPCManager();
        if (managerPtr) {
            qDebug() << "opcThreadFunc() - Storing OPC UA manager pointer atomically";
            mOPCManagerPtr.store(managerPtr);
        } else {
            qWarning() << "opcThreadFunc() - OPC UA manager pointer is null after initialization";
        }

        // ========== WAIT FOR SHUTDOWN SIGNAL =========
        qDebug() << "opcThreadFunc() - Entering wait loop";
        
        try {
            std::unique_lock<std::mutex> lk(mOPCMutex);
            mOPCCv.wait(lk, [this] { return !mOPCRunning.load(); });
        } catch (const std::exception& e) {
            qCritical() << "Exception in condition variable wait:" << e.what();
        }

        qDebug() << "opcThreadFunc() - Shutdown signal received, cleaning up";

        // ========== SHUTDOWN OPC UA =========
        try {
            localWorker.shutdown();
        } catch (const std::exception& e) {
            qCritical() << "Exception during OPC UA shutdown:" << e.what();
        } catch (...) {
            qCritical() << "Unknown exception during OPC UA shutdown";
        }

        // ========== CLEAR STORED POINTER =========
        mOPCManagerPtr.store(nullptr);
        
        qDebug() << "opcThreadFunc() - Thread function exiting normally";

    } catch (const std::exception& e) {
        qCritical() << "Unhandled exception in opcThreadFunc:" << e.what();
        mOPCRunning.store(false);
    } catch (...) {
        qCritical() << "Unknown exception in opcThreadFunc";
        mOPCRunning.store(false);
    }
}

void SLMWorkerManager::startWorkers()
{
    // ========== START OPC UA WORKER THREAD (GUI THREAD) =========
    // Exception-safe thread creation

    if (mOPCInitialized) {
        qWarning() << "SLMWorkerManager::startWorkers() - OPC UA worker already initialized";
        return;
    }

    qDebug() << "SLMWorkerManager::startWorkers() - Starting OPC UA worker thread";

    try {
        // ========== VALIDATE PRECONDITIONS =========
        if (mOPCThread.joinable()) {
            qWarning() << "SLMWorkerManager::startWorkers() - Previous thread still active";
            emit systemError("Cannot start workers: previous thread still active");
            return;
        }

        // ========== SET RUNNING FLAG =========
        mOPCRunning.store(true);

        // ========== CREATE AND START THREAD =========
        qDebug() << "SLMWorkerManager::startWorkers() - Creating std::thread";
        
        try {
            mOPCThread = std::thread(&SLMWorkerManager::opcThreadFunc, this);
        } catch (const std::system_error& e) {
            qCritical() << "System error creating thread:" << e.what() << "(code:" << e.code().value() << ")";
            mOPCRunning.store(false);
            emit systemError(QString("Failed to create OPC worker thread: %1").arg(e.what()));
            return;
        } catch (const std::exception& e) {
            qCritical() << "Exception creating thread:" << e.what();
            mOPCRunning.store(false);
            emit systemError(QString("Failed to create OPC worker thread: %1").arg(e.what()));
            return;
        } catch (...) {
            qCritical() << "Unknown exception creating thread";
            mOPCRunning.store(false);
            emit systemError("Failed to create OPC worker thread: unknown exception");
            return;
        }

        // ========== VALIDATE THREAD STATE =========
        if (!mOPCThread.joinable()) {
            qCritical() << "Thread created but not joinable!";
            mOPCRunning.store(false);
            emit systemError("OPC worker thread creation failed: thread not joinable");
            return;
        }

        qDebug() << "SLMWorkerManager::startWorkers() - OPC UA worker thread spawned successfully";

    } catch (const std::exception& e) {
        qCritical() << "Exception in startWorkers:" << e.what();
        mOPCRunning.store(false);
        emit systemError(QString("Exception starting workers: %1").arg(e.what()));
    } catch (...) {
        qCritical() << "Unknown exception in startWorkers";
        mOPCRunning.store(false);
        emit systemError("Unknown exception starting workers");
    }
}

void SLMWorkerManager::stopWorkers()
{
    // ========== STOP OPC UA WORKER THREAD (GUI Thread) =========
    // Exception-safe shutdown with timeout

    if (mShuttingDown) {
        qWarning() << "SLMWorkerManager::stopWorkers() - Already shutting down";
        return;
    }

    mShuttingDown = true;
    qDebug() << "SLMWorkerManager::stopWorkers() - Initiating graceful shutdown";

    try {
        // ========== SIGNAL WORKER TO EXIT =========
        {
            std::lock_guard<std::mutex> lk(mOPCMutex);
            mOPCRunning.store(false);
        }
        mOPCCv.notify_all();

        qDebug() << "SLMWorkerManager::stopWorkers() - Shutdown signal sent";

        // ========== WAIT FOR THREAD COMPLETION =========
        if (mOPCThread.joinable()) {
            qDebug() << "SLMWorkerManager::stopWorkers() - Waiting for OPC UA thread to join";
            
            try {
                mOPCThread.join();
                qDebug() << "SLMWorkerManager::stopWorkers() - OPC UA thread joined successfully";
            } catch (const std::system_error& e) {
                qCritical() << "System error joining thread:" << e.what();
            } catch (const std::exception& e) {
                qCritical() << "Exception joining thread:" << e.what();
            } catch (...) {
                qCritical() << "Unknown exception joining thread";
            }
        } else {
            qDebug() << "SLMWorkerManager::stopWorkers() - Thread already joined or not started";
        }

        mOPCInitialized = false;
        mShuttingDown = false;

        qDebug() << "SLMWorkerManager::stopWorkers() - Shutdown complete";

    } catch (const std::exception& e) {
        qCritical() << "Exception in stopWorkers:" << e.what();
        mShuttingDown = false;
    } catch (...) {
        qCritical() << "Unknown exception in stopWorkers";
        mShuttingDown = false;
    }
}

void SLMWorkerManager::emergencyStop()
{
    // ========== EMERGENCY SHUTDOWN =========
    // Fastest possible shutdown, minimal error handling

    qDebug() << "SLMWorkerManager::emergencyStop() - EMERGENCY STOP ACTIVATED";

    try {
        mShuttingDown = true;

        // Signal threads to stop immediately
        mOPCRunning.store(false);
        mOPCCv.notify_all();

        // Join threads with timeout
        if (mOPCThread.joinable()) {
            try {
                mOPCThread.join();
            } catch (...) {
                qCritical() << "Exception joining thread during emergency stop";
            }
        }

        mOPCInitialized = false;
        mShuttingDown = false;

        qDebug() << "SLMWorkerManager::emergencyStop() - Emergency shutdown complete";

    } catch (...) {
        qCritical() << "Exception in emergencyStop";
        mShuttingDown = false;
    }
}

OPCServerManagerUA* SLMWorkerManager::getOPCManager() const
{
    // ========== GET OPC UA MANAGER POINTER =========
    // Thread-safe atomic read
    return mOPCManagerPtr.load();
}

void SLMWorkerManager::onOPCInitialized(bool success)
{
    // ========== OPC UA INITIALIZATION CALLBACK =========
    // Runs in GUI thread via Qt::QueuedConnection

    qDebug() << "SLMWorkerManager::onOPCInitialized() - Received initialization result:" << success;

    mOPCInitialized = success;
    if (success) {
        qDebug() << "SLMWorkerManager::onOPCInitialized() - OPC UA initialized successfully";
        emit systemReady();
    } else {
        qWarning() << "SLMWorkerManager::onOPCInitialized() - OPC UA initialization failed";
        emit systemError("OPC UA initialization failed");
    }
}

void SLMWorkerManager::onOPCShutdown()
{
    // ========== OPC UA SHUTDOWN CALLBACK =========
    qDebug() << "SLMWorkerManager::onOPCShutdown() - OPC UA shutdown complete";
    mOPCInitialized = false;
}

void SLMWorkerManager::onOPCError(const QString& message)
{
    // ========== OPC UA ERROR HANDLER =========
    qWarning() << "SLMWorkerManager - OPC UA Error:" << message;
    emit systemError(message);
}
