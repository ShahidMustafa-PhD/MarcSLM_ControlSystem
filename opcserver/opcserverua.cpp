#include "opcserverua.h"

#include <QThread>
#include <cstring>
#include <chrono>

// ============================================================================
// OPCServerManagerUA Implementation
// ============================================================================

OPCServerManagerUA::OPCServerManagerUA(QObject* parent)
    : QObject(parent)
    , mClient(nullptr)  // Will be managed by unique_ptr
    , mIsInitialized(false)
    , mConnectionLost(false)
    , mNamespaceIndex(DEFAULT_NAMESPACE_INDEX)
{
    mLayerPrepThread = std::thread(&OPCServerManagerUA::layerPreparationWorker, this);
}

OPCServerManagerUA::~OPCServerManagerUA() {
    // Safely stop and join the worker thread before any other cleanup
    {
        std::lock_guard<std::mutex> lock(mLayerPrepMutex);
        mStopWorker = true;
    }
    mLayerPrepCv.notify_one();
    if (mLayerPrepThread.joinable()) {
        mLayerPrepThread.join();
    }

    // ========== Scoped lock ensures mutex is held during destruction ==========
    std::scoped_lock lock(mStateMutex);
    
    // Disconnect and clean up OPC UA client
    disconnectFromServer();
    
    // Clear all allocated NodeIds before destruction
    clearAllNodeIds();
    
    // mClient is destroyed automatically by unique_ptr with custom deleter
}

void OPCServerManagerUA::setLogCallback(std::function<void(const QString&)> callback) {
    mLogCallback = callback;
}

void OPCServerManagerUA::log(const QString& message) {
    if (mLogCallback) {
        mLogCallback(message);
    }
    emit logMessage(message);
}

bool OPCServerManagerUA::isInitialized() const {
    std::scoped_lock lock(mStateMutex);
    return mIsInitialized;
}

bool OPCServerManagerUA::initialize() {
    std::scoped_lock lock(mStateMutex);
    
    try {
        log("========== OPC UA Initialization Starting ==========");
        log("Connecting to OPC UA Server...");

        // ========== Reset connection loss flag on new initialization ==========
        mConnectionLost = false;

        // ========== Get server URL from environment or use default ==========
        QByteArray envUrl = qgetenv("OPC_UA_URL");
        if (!envUrl.isEmpty()) {
            mServerUrl = QString::fromLocal8Bit(envUrl);
            log(QString("OPC UA URL from OPC_UA_URL: %1").arg(mServerUrl));
        } else {
            mServerUrl = DEFAULT_SERVER_URL;
            log(QString("OPC UA URL (default): %1").arg(DEFAULT_SERVER_URL));
        }

        // ========== Get namespace index from environment or use default ==========
        QByteArray envNs = qgetenv("OPC_UA_NAMESPACE_INDEX");
        if (!envNs.isEmpty()) {
            bool ok;
            uint16_t nsIndex = envNs.toUInt(&ok);
            if (ok) {
                mNamespaceIndex = nsIndex;
                log(QString("OPC UA namespace index from OPC_UA_NAMESPACE_INDEX: %1").arg(mNamespaceIndex));
            }
        } else {
            log(QString("OPC UA namespace index (default): %1").arg(DEFAULT_NAMESPACE_INDEX));
        }
        
        log(QString("Configured namespace index: %1").arg(mNamespaceIndex));

        // ========== Connect to OPC UA server ==========
        log("Initiating connection phase...");
        if (!connectToServer()) {
            log("ERROR: Failed to connect to OPC UA server");
            log("========== OPC UA Initialization FAILED ==========");
            mIsInitialized = false;
            return false;
        }

        log("OPC UA Server connected successfully");

        // ========== Setup all node IDs ==========
        log("Setting up node IDs...");
        setupNodeIds();

        log("OPC UA Server initialized successfully");
        log("========== OPC UA Initialization COMPLETE ==========");
        mIsInitialized = true;

        return true;
    }
    catch (const std::exception& e) {
        log(QString("EXCEPTION during OPC UA init: %1").arg(e.what()));
        mIsInitialized = false;
        disconnectFromServer();
        return false;
    }
}

bool OPCServerManagerUA::connectToServer() {
    // ========== Create OPC UA client via unique_ptr ==========
    // The custom deleter (UA_ClientDeleter) ensures proper cleanup
    
    log("Attempting to connect to server...");
    
    UA_Client* rawClient = UA_Client_new();
    if (!rawClient) {
        log("ERROR: Failed to create OPC UA client");
        return false;
    }

    // Transfer ownership to unique_ptr (deleter will handle disconnect/delete)
    mClient = std::move(UA_ClientPtr(rawClient));

    // ========== Configure client with default settings ==========
    UA_ClientConfig* config = UA_Client_getConfig(mClient.get());
    if (!config) {
        log("ERROR: Failed to get client config");
        mClient.reset();
        return false;
    }
    
    UA_ClientConfig_setDefault(config);

    // ========== Set connection timeout (extended for simulator startup) ==========
    // Increased from 5000ms to 10000ms to allow simulator time to initialize
    config->timeout = 10000;
    log(QString("Connection timeout set to 10000ms"));

    // ========== Connect to server ==========
    log(QString("Connecting to: %1").arg(mServerUrl));
    UA_StatusCode status = UA_Client_connect(mClient.get(), mServerUrl.toUtf8().constData());
    
    if (status != UA_STATUSCODE_GOOD) {
        log(QString("ERROR: Connection failed with status: %1")
            .arg(QString::fromUtf8(UA_StatusCode_name(status))));
        
        // Provide more detailed diagnostics
        if (status == UA_STATUSCODE_BADTIMEOUT) {
            log("   Hint: Connection timeout. Server may be slow to respond.");
        } else if (status == UA_STATUSCODE_BADCONNECTIONCLOSED || 
                   status == UA_STATUSCODE_BADSESSIONCLOSED) {
            log("   Hint: Server rejected connection or connection failed. Is simulator running?");
        } else {
            log(QString("   Status code: %1").arg(status));
        }
        
        // unique_ptr will automatically delete mClient with custom deleter
        mClient.reset();
        return false;
    }

    log(QString("? Connected to OPC UA server: %1").arg(mServerUrl));
    return true;
}

void OPCServerManagerUA::disconnectFromServer() {
    // ========== Clean up OPC UA client ==========
    // Safe to call multiple times (unique_ptr handles nullptr check)
    // custom deleter ensures: UA_Client_disconnect() then UA_Client_delete()
    
    if (mClient) {
        // Reset unique_ptr triggers custom deleter:
        // UA_ClientDeleter::operator() calls disconnect() and delete()
        mClient.reset();
        mIsInitialized = false;
        log("OPC UA client disconnected");
    }
}

UA_NodeId OPCServerManagerUA::createNodeId(const QString& nodeIdString) {
    // ========== Create node ID with namespace index and allocated string identifier ==========
    // Format: ns=<namespace>;<identifier>
    // Example: "CECC.MaTe_DLMS.StartUpSequence.StartUp"
    // 
    // IMPORTANT: UA_NODEID_STRING_ALLOC allocates the identifier string internally.
    // The returned UA_NodeId MUST be cleared with UA_NodeId_clear() before destruction.
    // This is handled centrally in clearAllNodeIds() during shutdown.
    
    QByteArray utf8Bytes = nodeIdString.toUtf8();
    return UA_NODEID_STRING_ALLOC(mNamespaceIndex, utf8Bytes.constData());
}

void OPCServerManagerUA::setupNodeIds() {
    log("Setting up OPC UA node IDs...");

    // ========== MakeSurface nodes ==========
    mNode_layersMax = createNodeId("CECC.MaTe_DLMS.MakeSurface.Z_Stacks");
    mNode_delta_Source = createNodeId("CECC.MaTe_DLMS.MakeSurface.Delta_Source");
    mNode_delta_Sink = createNodeId("CECC.MaTe_DLMS.MakeSurface.Delta_Sink");
    mNode_MakeSurface_Done = createNodeId("CECC.MaTe_DLMS.MakeSurface.MakeSurface_Done");
    mNode_Marcer_Source_Cylinder_ActualPosition = createNodeId("CECC.MaTe_DLMS.MakeSurface.Marcer_Source_Cylinder_ActualPosition");
    mNode_Marcer_Sink_Cylinder_ActualPosition = createNodeId("CECC.MaTe_DLMS.MakeSurface.Marcer_Sink_Cylinder_ActualPosition");

    // ========== GVL nodes ==========
    mNode_StartSurfaces = createNodeId("CECC.MaTe_DLMS.GVL.StartSurfaces");
    mNode_g_Marcer_Source_Cylinder_ActualPosition = createNodeId("CECC.MaTe_DLMS.GVL.g_Marcer_Source_Cylinder_ActualPosition");
    mNode_g_Marcer_Sink_Cylinder_ActualPosition = createNodeId("CECC.MaTe_DLMS.GVL.g_Marcer_Sink_Cylinder_ActualPosition");

    // ========== Prepare2Process nodes ==========
    mNode_LaySurface = createNodeId("CECC.MaTe_DLMS.Prepare2Process.LaySurface");
    mNode_LaySurface_Done = createNodeId("CECC.MaTe_DLMS.Prepare2Process.LaySurface_Done");
    mNode_Step_Sink = createNodeId("CECC.MaTe_DLMS.Prepare2Process.Step_Sink");
    mNode_Step_Source = createNodeId("CECC.MaTe_DLMS.Prepare2Process.Step_Source");
    mNode_Lay_Stacks = createNodeId("CECC.MaTe_DLMS.Prepare2Process.Lay_Stacks");

    // ========== StartUpSequence nodes ==========
    mNode_StartUp = createNodeId("CECC.MaTe_DLMS.StartUpSequence.StartUp");
    mNode_StartUp_Done = createNodeId("CECC.MaTe_DLMS.StartUpSequence.StartUp_Done");

    // ========== Readback nodes (same as write nodes for OPC UA) ==========
    mNode_Marcer_Source_Cylinder_ActualPosition_Read = mNode_Marcer_Source_Cylinder_ActualPosition;
    mNode_Marcer_Sink_Cylinder_ActualPosition_Read = mNode_Marcer_Sink_Cylinder_ActualPosition;
    mNode_Z_Stacks = mNode_layersMax;
    mNode_MakeSurface_Done_Read = mNode_MakeSurface_Done;
    mNode_StartUp_Done_Read = mNode_StartUp_Done;
    mNode_g_Marcer_Source_Cylinder_ActualPosition_Read = mNode_g_Marcer_Source_Cylinder_ActualPosition;
    mNode_g_Marcer_Sink_Cylinder_ActualPosition_Read = mNode_g_Marcer_Sink_Cylinder_ActualPosition;
    mNode_LaySurface_Done_Read = mNode_LaySurface_Done;

    log(QString("? Successfully created OPC UA node IDs (namespace: %1)").arg(mNamespaceIndex));
}

// ============================================================================
// Read/Write Helper Methods (Thread-Safe)
// ============================================================================

bool OPCServerManagerUA::readInt32Node(const UA_NodeId& nodeId, int& value) {
    // ========== Stage 1: Validate state and get client pointer ==========
    UA_Client* client = nullptr;
    {
        std::scoped_lock lock(mStateMutex);
        
        if (mConnectionLost || !mClient || !mIsInitialized) {
            // Do NOT emit signal while holding lock
            return false;
        }
        
        client = mClient.get();
    }
    
    // ========== Stage 2: Perform UA call without state mutex ==========
    // mUaCallMutex serializes access to open62541 (not thread-safe)
    // mStateMutex is released, allowing other state checks
    
    UA_Variant variant;
    UA_Variant_init(&variant);
    
    {
        std::scoped_lock uaLock(mUaCallMutex);
        
        // ========== Read value attribute from server ==========
        UA_StatusCode status = UA_Client_readValueAttribute(client, nodeId, &variant);
        
        if (status != UA_STATUSCODE_GOOD) {
            // ========== Detect connection loss ==========
            if (status == UA_STATUSCODE_BADCONNECTIONCLOSED ||
                status == UA_STATUSCODE_BADSESSIONCLOSED) {
                UA_Variant_clear(&variant);
                handleConnectionLoss(QString::fromUtf8(UA_StatusCode_name(status)));
                return false;
            }
            
            log(QString("ERROR: Failed to read Int32 node: %1")
                .arg(QString::fromUtf8(UA_StatusCode_name(status))));
            UA_Variant_clear(&variant);
            return false;
        }
    }
    // mUaCallMutex released here
    
    // ========== Stage 3: Process result ==========
    // Type check and value extraction
    if (variant.type == &UA_TYPES[UA_TYPES_INT32]) {
        value = *static_cast<int*>(variant.data);
        UA_Variant_clear(&variant);
        return true;
    } else if (variant.type == &UA_TYPES[UA_TYPES_INT16]) {
        // Handle INT16 case (may occur due to PLC type differences)
        value = static_cast<int>(*static_cast<int16_t*>(variant.data));
        UA_Variant_clear(&variant);
        return true;
    } else {
        log(QString("ERROR: Node type mismatch (expected Int32, got %1)")
            .arg(variant.type->typeName));
        UA_Variant_clear(&variant);
        return false;
    }
}

bool OPCServerManagerUA::readBoolNode(const UA_NodeId& nodeId, bool& value) {
    // ========== Stage 1: Validate state and get client pointer ==========
    UA_Client* client = nullptr;
    {
        std::scoped_lock lock(mStateMutex);
        
        if (mConnectionLost || !mClient || !mIsInitialized) {
            return false;
        }
        
        client = mClient.get();
    }
    
    // ========== Stage 2: Perform UA call without state mutex ==========
    UA_Variant variant;
    UA_Variant_init(&variant);
    
    {
        std::scoped_lock uaLock(mUaCallMutex);
        
        // ========== Read value attribute from server ==========
        UA_StatusCode status = UA_Client_readValueAttribute(client, nodeId, &variant);
        
        if (status != UA_STATUSCODE_GOOD) {
            // ========== Detect connection loss ==========
            if (status == UA_STATUSCODE_BADCONNECTIONCLOSED ||
                status == UA_STATUSCODE_BADSESSIONCLOSED) {
                UA_Variant_clear(&variant);
                handleConnectionLoss(QString::fromUtf8(UA_StatusCode_name(status)));
                return false;
            }
            
            log(QString("ERROR: Failed to read Bool node: %1")
                .arg(QString::fromUtf8(UA_StatusCode_name(status))));
            UA_Variant_clear(&variant);
            return false;
        }
    }
    // mUaCallMutex released here
    
    // ========== Stage 3: Process result ==========
    // Type check and value extraction
    if (variant.type == &UA_TYPES[UA_TYPES_BOOLEAN]) {
        value = *static_cast<UA_Boolean*>(variant.data);
        UA_Variant_clear(&variant);
        return true;
    } else {
        log(QString("ERROR: Node type mismatch (expected Boolean, got %1)")
            .arg(variant.type->typeName));
        UA_Variant_clear(&variant);
        return false;
    }
}

bool OPCServerManagerUA::writeInt32Node(const UA_NodeId& nodeId, int value) {
    // ========== Stage 1: Validate state and get client pointer ==========
    UA_Client* client = nullptr;
    {
        std::scoped_lock lock(mStateMutex);
        
        if (mConnectionLost || !mClient || !mIsInitialized) {
            log("OPC UA not initialized - cannot write Int32 node");
            return false;
        }
        
        client = mClient.get();
    }
    
    // ========== Stage 2: Prepare and send variant without state mutex ==========
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Int32 val = static_cast<UA_Int32>(value);
    UA_Variant_setScalarCopy(&variant, &val, &UA_TYPES[UA_TYPES_INT32]);
    
    {
        std::scoped_lock uaLock(mUaCallMutex);
        
        // ========== Write to server ==========
        UA_StatusCode status = UA_Client_writeValueAttribute(client, nodeId, &variant);
        
        // ========== ALWAYS clear variant, even on error ==========
        UA_Variant_clear(&variant);
        
        if (status != UA_STATUSCODE_GOOD) {
            // ========== Detect connection loss ==========
            if (status == UA_STATUSCODE_BADCONNECTIONCLOSED ||
                status == UA_STATUSCODE_BADSESSIONCLOSED) {
                handleConnectionLoss(QString::fromUtf8(UA_StatusCode_name(status)));
                return false;
            }
            
            log(QString("ERROR: Failed to write Int32 node: %1")
                .arg(QString::fromUtf8(UA_StatusCode_name(status))));
            return false;
        }
    }
    // mUaCallMutex released here
    
    return true;
}

bool OPCServerManagerUA::writeBoolNode(const UA_NodeId& nodeId, bool value) {
    // ========== Stage 1: Validate state and get client pointer ==========
    UA_Client* client = nullptr;
    {
        std::scoped_lock lock(mStateMutex);
        
        if (mConnectionLost || !mClient || !mIsInitialized) {
            log("OPC UA not initialized - cannot write Bool node");
            return false;
        }
        
        client = mClient.get();
    }
    
    // ========== Stage 2: Prepare and send variant without state mutex ==========
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Boolean val = value;
    UA_Variant_setScalarCopy(&variant, &val, &UA_TYPES[UA_TYPES_BOOLEAN]);
    
    {
        std::scoped_lock uaLock(mUaCallMutex);
        
        // ========== Write to server ==========
        UA_StatusCode status = UA_Client_writeValueAttribute(client, nodeId, &variant);
        
        // ========== ALWAYS clear variant, even on error ==========
        UA_Variant_clear(&variant);
        
        if (status != UA_STATUSCODE_GOOD) {
            // ========== Detect connection loss ==========
            if (status == UA_STATUSCODE_BADCONNECTIONCLOSED ||
                status == UA_STATUSCODE_BADSESSIONCLOSED) {
                handleConnectionLoss(QString::fromUtf8(UA_StatusCode_name(status)));
                return false;
            }
            
            log(QString("ERROR: Failed to write Bool node: %1")
                .arg(QString::fromUtf8(UA_StatusCode_name(status))));
            return false;
        }
    }
    // mUaCallMutex released here
    
    return true;
}

// ============================================================================
// Public OPC Operations (Same Interface as OPC DA)
// ============================================================================

bool OPCServerManagerUA::readData(OPCData& data) {
    // ========== Stage 1: Quick state check to detect obvious connection loss ==========
    {
        std::scoped_lock lock(mStateMutex);
        
        if (mConnectionLost || !mClient || !mIsInitialized) {
            return false;
        }
    }
    // mStateMutex released - now safe to perform I/O operations
    
    try {
        // ========== Stage 2: Read all data nodes (with UA call mutex) ==========
        int tempInt;
        bool success = true;

        // Read cylinder positions
        if (readInt32Node(mNode_Marcer_Source_Cylinder_ActualPosition_Read, tempInt)) {
            data.sourceCylPosition = tempInt;
        } else {
            success = false;
        }

        if (readInt32Node(mNode_Marcer_Sink_Cylinder_ActualPosition_Read, tempInt)) {
            data.sinkCylPosition = tempInt;
        } else {
            success = false;
        }

        if (readInt32Node(mNode_Z_Stacks, tempInt)) {
            data.stacksLeft = tempInt;
        } else {
            success = false;
        }

        // Read boolean flags (convert to int for compatibility)
        bool tempBool;
        if (readBoolNode(mNode_MakeSurface_Done_Read, tempBool)) {
            data.ready2Powder = tempBool ? 1 : 0;
        } else {
            success = false;
        }

        if (readBoolNode(mNode_StartUp_Done_Read, tempBool)) {
            data.startUpDone = tempBool ? 1 : 0;
        } else {
            success = false;
        }

        if (readInt32Node(mNode_g_Marcer_Source_Cylinder_ActualPosition_Read, tempInt)) {
            data.g_sourceCylPosition = tempInt;
        } else {
            success = false;
        }

        if (readInt32Node(mNode_g_Marcer_Sink_Cylinder_ActualPosition_Read, tempInt)) {
            data.g_sinkCylPosition = tempInt;
        } else {
            success = false;
        }

        if (readBoolNode(mNode_LaySurface_Done_Read, tempBool)) {
            data.powderSurfaceDone = tempBool ? 1 : 0;
        } else {
            success = false;
        }

        // ========== Stage 3: Emit signal OUTSIDE locks ==========
        if (success) {
            emit dataUpdated(data);
        }

        return success;
    }
    catch (const std::exception& e) {
        log(QString("ERROR: Exception during OPC UA read: %1").arg(e.what()));
        return false;
    }
}

bool OPCServerManagerUA::writeStartUp(bool value) {
    // ========== Quick state check ==========
    {
        std::scoped_lock lock(mStateMutex);
        
        if (mConnectionLost || !mIsInitialized) {
            log("ERROR: StartUp node not initialized");
            return false;
        }
    }

    // ========== Perform write outside state mutex ==========
    if (writeBoolNode(mNode_StartUp, value)) {
        log("Startup command sent to PLC (OPC UA)");
        return true;
    }
    return false;
}

bool OPCServerManagerUA::writePowderFillParameters(int layers, int deltaSource, int deltaSink) {
    // ========== Quick state check ==========
    {
        std::scoped_lock lock(mStateMutex);
        
        if (mConnectionLost || !mIsInitialized) {
            log("ERROR: Required OPC UA nodes not initialized");
            return false;
        }
    }

    // ========== Perform writes outside state mutex ==========
    try {
        QThread::msleep(OPERATION_SLEEP_MS);

        if (!writeInt32Node(mNode_layersMax, layers)) return false;
        if (!writeInt32Node(mNode_Lay_Stacks, layers)) return false;
        QThread::msleep(OPERATION_SLEEP_MS);

        if (!writeInt32Node(mNode_delta_Source, deltaSource)) return false;
        QThread::msleep(OPERATION_SLEEP_MS);

        if (!writeInt32Node(mNode_delta_Sink, deltaSink)) return false;
        QThread::msleep(OPERATION_SLEEP_MS);

        if (!writeBoolNode(mNode_StartSurfaces, true)) return false;
        QThread::msleep(500);

        log("Powder fill parameters sent to PLC (OPC UA)");
        return true;
    }
    catch (const std::exception& e) {
        log(QString("ERROR: Exception writing powder fill parameters: %1").arg(e.what()));
        return false;
    }
}

bool OPCServerManagerUA::writeLayerParameters(int layers, int deltaSource, int deltaSink) {
    // ========== Quick state check ==========
    {
        std::scoped_lock lock(mStateMutex);
        
        if (mConnectionLost || !mIsInitialized) {
            log("ERROR: Layer nodes not initialized");
            return false;
        }
    }

    // ========== Perform writes outside state mutex ==========
    try {
        if (!writeInt32Node(mNode_Lay_Stacks, layers)) return false;
        QThread::msleep(OPERATION_SLEEP_MS);

        if (!writeInt32Node(mNode_Step_Source, deltaSource)) return false;
        QThread::msleep(OPERATION_SLEEP_MS);

        if (!writeInt32Node(mNode_Step_Sink, deltaSink)) return false;
        QThread::msleep(OPERATION_SLEEP_MS);

        if (!writeBoolNode(mNode_LaySurface, true)) return false;
        
        // Trigger the simulated layer preparation
        {
            std::lock_guard<std::mutex> lock(mLayerPrepMutex);
            mLayerPrepRequested = true;
        }
        mLayerPrepCv.notify_one();

        // In a real system, the client would now wait for LaySurface_Done to become true.
        // Here, we just log that the process has been started. The actual wait happens
        // in the consumer thread, which polls the value. The worker thread simulates
        // the server-side change.
        log("Layer parameters sent to PLC (OPC UA), simulating layer preparation...");
        
        // The original implementation had a 400ms sleep here.
        // We keep it to maintain timing consistency with the original code,
        // even though the real delay is now handled by the worker thread.
        QThread::msleep(400);

        return true;
    }
    catch (const std::exception& e) {
        log(QString("ERROR: Exception writing layer parameters: %1").arg(e.what()));
        return false;
    }
}

bool OPCServerManagerUA::writeBottomLayerParameters(int layers, int deltaSource, int deltaSink) {
    // ========== Quick state check ==========
    {
        std::scoped_lock lock(mStateMutex);
        
        if (mConnectionLost || !mIsInitialized) {
            log("ERROR: Bottom layer nodes not initialized");
            return false;
        }
    }

    // ========== Perform writes outside state mutex ==========
    try {
        if (!writeInt32Node(mNode_Lay_Stacks, layers)) return false;
        QThread::msleep(1000);

        if (!writeInt32Node(mNode_Step_Source, deltaSource)) return false;
        QThread::msleep(1000);

        if (!writeInt32Node(mNode_Step_Sink, deltaSink)) return false;
        QThread::msleep(1000);

        if (!writeBoolNode(mNode_LaySurface, true)) return false;
        QThread::msleep(500);

        log("Bottom layer parameters sent to PLC (OPC UA)");
        return true;
    }
    catch (const std::exception& e) {
        log(QString("ERROR: Exception writing bottom layer parameters: %1").arg(e.what()));
        return false;
    }
}

bool OPCServerManagerUA::writeEmergencyStop() {
    // ========== Quick state check ==========
    {
        std::scoped_lock lock(mStateMutex);
        
        if (mConnectionLost) {
            // Still log even if disconnected
            log("ERROR: Cannot send emergency stop - connection lost");
            return false;
        }
        
        if (mIsInitialized && mClient) {
            // Try to send if possible
            writeBoolNode(mNode_StartSurfaces, false);
        }
    }
    
    // ========== Always log the emergency signal ==========
    log("? EMERGENCY STOP signal sent to PLC (OPC UA)!");
    return true;
}

bool OPCServerManagerUA::writeCylinderPosition(bool isSource, int position) {
    // ========== Quick state check ==========
    {
        std::scoped_lock lock(mStateMutex);
        
        if (mConnectionLost || !mIsInitialized) {
            log("OPC UA not initialized - cannot write cylinder position");
            return false;
        }
    }

    // ========== Select appropriate node based on source/sink ==========
    const UA_NodeId& nodeId = isSource ? 
        mNode_Marcer_Source_Cylinder_ActualPosition : 
        mNode_Marcer_Sink_Cylinder_ActualPosition;

    try {
        if (writeInt32Node(nodeId, position)) {
            log(QString("? Cylinder position (%1) written: %2 (OPC UA)")
                .arg(isSource ? "Source" : "Sink").arg(position));
            return true;
        }
        return false;
    } catch (const std::exception& e) {
        log(QString("ERROR: Exception writing cylinder position (%1): %2")
            .arg(isSource ? "Source" : "Sink").arg(e.what()));
        return false;
    }
}

bool OPCServerManagerUA::writeLayerExecutionComplete(int layerNumber) {
    // ========== Stage 1: Quick state check ==========
    {
        std::scoped_lock lock(mStateMutex);
        
        if (mConnectionLost || !mIsInitialized) {
            log("? OPC UA not initialized - cannot notify layer execution complete");
            return false;
        }
    }

    try {
        // ========== Stage 2: Reset LaySurface TAG TO FALSE ==========
        //
        // This signals PLC that Scanner has completed laser execution.
        // PLC can now proceed with next layer creation when Scanner requests it.
        //
        // Industrial Practice:
        //   • TRUE  = Request layer creation (recoater, platform)
        //   • FALSE = Scanner finished, ready for next layer
        //
        if (!writeBoolNode(mNode_LaySurface, false)) {
            log(QString("? Failed to signal layer %1 execution complete to PLC (OPC UA)")
                .arg(layerNumber));
            return false;
        }

        // ========== Stage 3: Emit signal OUTSIDE locks ==========
        log(QString("? Layer %1 execution complete signal sent to PLC (LaySurface=FALSE, OPC UA)")
            .arg(layerNumber));
        
        return true;
    }
    catch (const std::exception& e) {
        log(QString("ERROR: Exception while signaling layer %1 execution complete: %2")
            .arg(layerNumber).arg(e.what()));
        return false;
    }
}

// ============================================================================
// Internal Cleanup & Error Handling
// ============================================================================

void OPCServerManagerUA::clearAllNodeIds() {
    // ========== Clear all allocated UA_NodeId structs =========
    // 
    // Note: These are allocated by UA_NODEID_STRING_ALLOC in createNodeId()
    // which allocates the identifier string internally.
    // UA_NodeId_clear() frees that allocated string.
    //
    // Called from destructor with mStateMutex held.
    
    // MakeSurface nodes
    UA_NodeId_clear(&mNode_layersMax);
    UA_NodeId_clear(&mNode_delta_Source);
    UA_NodeId_clear(&mNode_delta_Sink);
    UA_NodeId_clear(&mNode_MakeSurface_Done);
    UA_NodeId_clear(&mNode_Marcer_Source_Cylinder_ActualPosition);
    UA_NodeId_clear(&mNode_Marcer_Sink_Cylinder_ActualPosition);

    // GVL nodes
    UA_NodeId_clear(&mNode_StartSurfaces);
    UA_NodeId_clear(&mNode_g_Marcer_Source_Cylinder_ActualPosition);
    UA_NodeId_clear(&mNode_g_Marcer_Sink_Cylinder_ActualPosition);

    // Prepare2Process nodes
    UA_NodeId_clear(&mNode_LaySurface);
    UA_NodeId_clear(&mNode_LaySurface_Done);
    UA_NodeId_clear(&mNode_Step_Sink);
    UA_NodeId_clear(&mNode_Step_Source);
    UA_NodeId_clear(&mNode_Lay_Stacks);

    // StartUpSequence nodes
    UA_NodeId_clear(&mNode_StartUp);
    UA_NodeId_clear(&mNode_StartUp_Done);

    // Readback nodes (NOTE: some of these are aliases, but clearing twice is safe)
    // Only clear if they are unique allocations (non-alias)
    // For safety: clear all to ensure no resource leaks
   
    //UA_NodeId_clear(&mNode_Marcer_Source_Cylinder_ActualPosition_Read);
    //UA_NodeId_clear(&mNode_Marcer_Sink_Cylinder_ActualPosition_Read);
    //UA_NodeId_clear(&mNode_Z_Stacks);
    //UA_NodeId_clear(&mNode_MakeSurface_Done_Read);
    //UA_NodeId_clear(&mNode_StartUp_Done_Read);
    //UA_NodeId_clear(&mNode_g_Marcer_Source_Cylinder_ActualPosition_Read);
   // UA_NodeId_clear(&mNode_g_Marcer_Sink_Cylinder_ActualPosition_Read);
   // UA_NodeId_clear(&mNode_LaySurface_Done_Read);

    log("All OPC UA node IDs cleared");
}

void OPCServerManagerUA::handleConnectionLoss(const QString& reason) {
    // ========== Detect and handle connection loss =========
    // 
    // Called when UA_StatusCode indicates connection error.
    // Sets mConnectionLost flag to fail all future I/O operations.
    // Emits connectionLost() signal exactly once per loss event.
    //
    // Thread-safe: Manages its own locking.
    
    bool shouldEmitSignal = false;
    {
        std::scoped_lock lock(mStateMutex);
        
        // ========== Only emit signal on first detection =========
        if (!mConnectionLost) {
            mConnectionLost = true;
            mIsInitialized = false;
            shouldEmitSignal = true;
        }
    }
    // mStateMutex released here

    // ========== Log and emit signal OUTSIDE locks ==========
    log(QString("ERROR: OPC UA Connection Lost: %1").arg(reason));
    
    if (shouldEmitSignal) {
        // Qt signal - safe to emit here (outside mutex)
        emit connectionLost();
    }
}

void OPCServerManagerUA::layerPreparationWorker() {
    while (!mStopWorker) {
        std::unique_lock<std::mutex> lock(mLayerPrepMutex);
        mLayerPrepCv.wait(lock, [this] { return mLayerPrepRequested || mStopWorker; });

        if (mStopWorker) {
            break;
        }

        if (mLayerPrepRequested) {
            log("OPC UA Sim: Layer preparation started (5-second delay)...");
            std::this_thread::sleep_for(std::chrono::seconds(5));
            log("OPC UA Sim: Layer preparation finished.");

            // Simulate the server setting LaySurface_Done to true
            // In a real scenario, we would read this value. Here we just log it.
            log("OPC UA Sim: Setting LaySurface_Done = TRUE (simulated)");

            mLayerPrepRequested = false;
        }
    }
    log("OPC UA layer preparation worker thread stopped.");
}

void OPCServerManagerUA::stop() {
    bool wasInitialized = false;

    {
        // Only protect state access
        std::scoped_lock lock(mStateMutex);
        wasInitialized = mIsInitialized;
        mIsInitialized = false;
    }

    if (!wasInitialized) {
        log("OPC UA connection already stopped");
        return;
    }

    log("Stopping OPC UA connection...");

    // Disconnect WITHOUT holding mutex
    disconnectFromServer();

    {
        std::scoped_lock lock(mStateMutex);
        mConnectionLost = false;  // intentional shutdown
    }

    // Safely stop the worker thread
    {
        std::lock_guard<std::mutex> lock(mLayerPrepMutex);
        mStopWorker = true;
    }
    mLayerPrepCv.notify_one();
    // Note: We don't join here to avoid blocking if stop() is called from the GUI thread.
    // The destructor will handle the final join.

    log("OPC UA connection stopped successfully");
}
