#include "opccontroller.h"
#include <QTextEdit>
#include <QMessageBox>
#include <thread>
#include <chrono>
#include <sstream>

// ============================================================================
// Constructor - Exception-safe initialization
// ============================================================================

OPCController::OPCController(QTextEdit* logWidget, QObject* parent)
    : QObject(parent)
    , mOPCServer(nullptr)
    , mLogWidget(logWidget)
{
    try {
        // ========== Create OPC UA server manager with parent ownership ==========
        // Qt parent-child relationship ensures automatic cleanup
        mOPCServer = new OPCServerManagerUA(this);
        
        if (!mOPCServer) {
            log("ERROR: Failed to allocate OPCServerManagerUA");
            return;
        }

        // ========== Connect signals with Qt::QueuedConnection for thread safety ==========
        // All signals from OPC worker thread are queued, not invoked directly
        // This prevents cross-thread issues during signal/slot delivery
        
        // Connect data updated signal
        if (!connect(mOPCServer, &OPCServerManagerUA::dataUpdated, 
                    this, &OPCController::onOPCDataUpdated,
                    Qt::QueuedConnection)) {
            log("WARNING: dataUpdated signal connection failed");
        }
        
        // Connect connection lost signal
        if (!connect(mOPCServer, &OPCServerManagerUA::connectionLost,
                    this, &OPCController::onOPCConnectionLost,
                    Qt::QueuedConnection)) {
            log("WARNING: connectionLost signal connection failed");
        }
        
        // Connect log message signal
        if (!connect(mOPCServer, &OPCServerManagerUA::logMessage,
                    this, &OPCController::onOPCLogMessage,
                    Qt::QueuedConnection)) {
            log("WARNING: logMessage signal connection failed");
        }
        
    } catch (const std::bad_alloc& e) {
        log(QString("ERROR: Memory allocation failed in OPCController constructor: %1").arg(e.what()));
        mOPCServer = nullptr;
    } catch (const std::exception& e) {
        log(QString("ERROR: Exception in OPCController constructor: %1").arg(e.what()));
        mOPCServer = nullptr;
    } catch (...) {
        log("ERROR: Unknown exception in OPCController constructor");
        mOPCServer = nullptr;
    }
}

// ============================================================================
// Destructor - Exception-safe cleanup
// ============================================================================

OPCController::~OPCController()
{
    try {
        // ========== OPCServerManagerUA will be deleted by Qt parent-child relationship ==========
        // Qt framework calls deleteLater() on children, ensuring safe cleanup
        // mOPCServer pointer becomes invalid after this dtor completes
        mOPCServer = nullptr;
    } catch (const std::exception& e) {
        qCritical("OPCController destructor exception: %s", e.what());
    } catch (...) {
        qCritical("OPCController destructor: Unknown exception");
    }
}

// ============================================================================
// Logging - Thread-safe message output
// ============================================================================

void OPCController::log(const QString& message)
{
    try {
        if (mLogWidget) {
            mLogWidget->append(message);
        }
        emit statusMessage(message);
    } catch (const std::exception& e) {
        qCritical("Exception in log(): %s", e.what());
    } catch (...) {
        qCritical("Unknown exception in log()");
    }
}

// ============================================================================
// Initialization
// ============================================================================

bool OPCController::initialize()
{
    try {
        log("========== OPC UA Initialization Starting ==========");
        log("Connecting to OPC UA Server...");
        log("Waiting for OPC UA server to be ready...");
        
        // ========== Validate that OPC server manager was created successfully ==========
        if (!mOPCServer) {
            log("ERROR: OPC UA manager not initialized (null pointer)");
            log("========== OPC UA Initialization FAILED ==========");
            emit errorMessage("OPC UA manager not initialized");
            return false;
        }

        // ========== Small delay to allow simulator to fully start if just launched ==========
        // This is especially important in development/testing scenarios
        std::this_thread::sleep_for(std::chrono::milliseconds(500));
        
        // ========== Call OPC manager initialization ==========
        // This runs in current thread context (GUI or worker thread)
        // mOPCServer->initialize() contains its own exception handling
        bool initResult = false;
        try {
            initResult = mOPCServer->initialize();
        } catch (const std::exception& e) {
            log(QString("ERROR: Exception during OPC UA manager initialization: %1").arg(e.what()));
            initResult = false;
        }
        
        if (initResult) {
            log("? OPC UA Server initialized successfully");
            log("? Node IDs configured and ready");
            log("========== OPC UA Initialization COMPLETE ==========");
            return true;
        } else {
            log("? Failed to initialize OPC UA Server");
            log("========== OPC UA Initialization FAILED ==========");
            emit errorMessage("Failed to initialize OPC UA Server. Ensure the simulator is running.");
            return false;
        }
    } catch (const std::exception& e) {
        log(QString("CRITICAL: Exception in OPCController::initialize(): %1").arg(e.what()));
        emit errorMessage("OPC UA initialization critical error");
        return false;
    } catch (...) {
        log("CRITICAL: Unknown exception in OPCController::initialize()");
        emit errorMessage("OPC UA initialization unknown error");
        return false;
    }
}

bool OPCController::isInitialized() const
{
    try {
        return mOPCServer && mOPCServer->isInitialized();
    } catch (const std::exception& e) {
        qCritical("Exception in isInitialized(): %s", e.what());
        return false;
    } catch (...) {
        qCritical("Unknown exception in isInitialized()");
        return false;
    }
}

// ============================================================================
// Write Operations - All with exception handling
// ============================================================================

bool OPCController::writeStartUp(bool value)
{
    try {
        if (!isInitialized()) {
            log("ERROR: OPC not initialized");
            return false;
        }
        
        if (!mOPCServer) {
            log("ERROR: OPC server null pointer");
            return false;
        }
        
        if (mOPCServer->writeStartUp(value)) {
            log("? Startup command sent to PLC");
            return true;
        } else {
            log("? Failed to write StartUp command");
            return false;
        }
    } catch (const std::exception& e) {
        log(QString("ERROR: Exception in writeStartUp: %1").arg(e.what()));
        return false;
    } catch (...) {
        log("ERROR: Unknown exception in writeStartUp");
        return false;
    }
}

bool OPCController::writePowderFillParameters(int layers, int deltaSource, int deltaSink)
{
    try {
        if (!isInitialized()) {
            log("ERROR: OPC not initialized");
            return false;
        }
        
        if (!mOPCServer) {
            log("ERROR: OPC server null pointer");
            return false;
        }
        
        if (mOPCServer->writePowderFillParameters(layers, deltaSource, deltaSink)) {
            log(QString("? Powder fill parameters sent: %1 layers, %2/%3 microns")
                .arg(layers).arg(deltaSource).arg(deltaSink));
            return true;
        } else {
            log("? Failed to write powder fill parameters");
            return false;
        }
    } catch (const std::exception& e) {
        log(QString("ERROR: Exception in writePowderFillParameters: %1").arg(e.what()));
        return false;
    } catch (...) {
        log("ERROR: Unknown exception in writePowderFillParameters");
        return false;
    }
}

bool OPCController::writeLayerParameters(int layers, int deltaSource, int deltaSink)
{
    try {
        if (!isInitialized()) {
            log("ERROR: OPC not initialized");
            return false;
        }
        
        if (!mOPCServer) {
            log("ERROR: OPC server null pointer");
            return false;
        }
        
        if (mOPCServer->writeLayerParameters(layers, deltaSource, deltaSink)) {
            log(QString("? Layer parameters sent: %1 layers").arg(layers));
            return true;
        } else {
            log("? Failed to write layer parameters");
            return false;
        }
    } catch (const std::exception& e) {
        log(QString("ERROR: Exception in writeLayerParameters: %1").arg(e.what()));
        return false;
    } catch (...) {
        log("ERROR: Unknown exception in writeLayerParameters");
        return false;
    }
}

bool OPCController::writeBottomLayerParameters(int layers, int deltaSource, int deltaSink)
{
    try {
        if (!isInitialized()) {
            log("ERROR: OPC not initialized");
            return false;
        }
        
        if (!mOPCServer) {
            log("ERROR: OPC server null pointer");
            return false;
        }
        
        if (mOPCServer->writeBottomLayerParameters(layers, deltaSource, deltaSink)) {
            log(QString("? Bottom layer parameters sent: %1 layers").arg(layers));
            return true;
        } else {
            log("? Failed to write bottom layer parameters");
            return false;
        }
    } catch (const std::exception& e) {
        log(QString("ERROR: Exception in writeBottomLayerParameters: %1").arg(e.what()));
        return false;
    } catch (...) {
        log("ERROR: Unknown exception in writeBottomLayerParameters");
        return false;
    }
}

bool OPCController::writeEmergencyStop()
{
    try {
        if (!isInitialized()) {
            log("ERROR: OPC not initialized");
            return false;
        }
        
        if (!mOPCServer) {
            log("ERROR: OPC server null pointer");
            return false;
        }
        
        if (mOPCServer->writeEmergencyStop()) {
            log("?? EMERGENCY STOP signal sent to PLC!");
            return true;
        } else {
            log("? Failed to send emergency stop signal");
            return false;
        }
    } catch (const std::exception& e) {
        log(QString("ERROR: Exception in writeEmergencyStop: %1").arg(e.what()));
        return false;
    } catch (...) {
        log("ERROR: Unknown exception in writeEmergencyStop");
        return false;
    }
}

bool OPCController::writeCylinderPosition(bool isSource, int position)
{
    try {
        if (!isInitialized()) {
            log("ERROR: OPC not initialized");
            return false;
        }
        
        if (!mOPCServer) {
            log("ERROR: OPC server null pointer");
            return false;
        }
        
        if (mOPCServer->writeCylinderPosition(isSource, position)) {
            log(QString("? Cylinder position (%1) set to %2")
                .arg(isSource ? "Source" : "Sink").arg(position));
            return true;
        } else {
            log(QString("? Failed to set cylinder position (%1)")
                .arg(isSource ? "Source" : "Sink"));
            return false;
        }
    } catch (const std::exception& e) {
        log(QString("ERROR: Exception in writeCylinderPosition: %1").arg(e.what()));
        return false;
    } catch (...) {
        log("ERROR: Unknown exception in writeCylinderPosition");
        return false;
    }
}

// ============================================================================
// Read Operations
// ============================================================================

bool OPCController::readData()
{
    try {
        if (!isInitialized()) {
            return false;
        }
        
        if (!mOPCServer) {
            return false;
        }
        
        return mOPCServer->readData(mCurrentData);
    } catch (const std::exception& e) {
        qCritical("Exception in readData(): %s", e.what());
        return false;
    } catch (...) {
        qCritical("Unknown exception in readData()");
        return false;
    }
}

// ============================================================================
// Signal Handlers - Must be exception-safe (called from worker threads)
// ============================================================================

void OPCController::onOPCDataUpdated(const OPCServerManagerUA::OPCData& data)
{
    try {
        mCurrentData = data;
        emit dataUpdated(data);
    } catch (const std::exception& e) {
        qCritical("Exception in onOPCDataUpdated: %s", e.what());
    } catch (...) {
        qCritical("Unknown exception in onOPCDataUpdated");
    }
}

void OPCController::onOPCConnectionLost()
{
    try {
        log("? WARNING: OPC UA Connection Lost!");
        emit connectionLost();
    } catch (const std::exception& e) {
        qCritical("Exception in onOPCConnectionLost: %s", e.what());
    } catch (...) {
        qCritical("Unknown exception in onOPCConnectionLost");
    }
}

void OPCController::onOPCLogMessage(const QString& message)
{
    try {
        // Forward OPC UA server log messages
        log(message);
    } catch (const std::exception& e) {
        qCritical("Exception in onOPCLogMessage: %s", e.what());
    } catch (...) {
        qCritical("Unknown exception in onOPCLogMessage");
    }
}
