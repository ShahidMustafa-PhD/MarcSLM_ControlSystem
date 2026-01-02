#ifndef OPCSERVERMANAGERUA_H
#define OPCSERVERMANAGERUA_H

#include <QObject>
#include <QString>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <thread>
#include <condition_variable>
#include <open62541/client.h>
#include <open62541/client_config_default.h>
#include <open62541/client_highlevel.h>

// ============================================================================
// RAII Wrapper for UA_Client Lifecycle
// ============================================================================
/**
 * @brief Custom deleter for UA_Client to ensure deterministic cleanup
 * 
 * Ensures:
 * - UA_Client_disconnect() called before deletion
 * - UA_Client_delete() called exactly once
 * - No resource leaks on abnormal termination
 */
struct UA_ClientDeleter {
    void operator()(UA_Client* client) const noexcept {
        if (client) {
            UA_Client_disconnect(client);
            UA_Client_delete(client);
        }
    }
};

// Type alias for safe UA_Client ownership
using UA_ClientPtr = std::unique_ptr<UA_Client, UA_ClientDeleter>;

// ============================================================================
// RAII Wrapper for UA_NodeId Cleanup
// ============================================================================
/**
 * @brief RAII wrapper for allocated UA_NodeId
 * 
 * Ensures UA_NodeId_clear() is called on scope exit.
 * Used by createNodeId() to manage allocated node IDs.
 */
struct UA_NodeIdRAII {
    UA_NodeId nodeId;
    
    UA_NodeIdRAII() : nodeId(UA_NODEID_NULL) {}
    
    explicit UA_NodeIdRAII(const UA_NodeId& id) : nodeId(id) {}
    
    ~UA_NodeIdRAII() {
        UA_NodeId_clear(&nodeId);
    }
    
    // Prevent copies; move is allowed
    UA_NodeIdRAII(const UA_NodeIdRAII&) = delete;
    UA_NodeIdRAII& operator=(const UA_NodeIdRAII&) = delete;
    
    UA_NodeIdRAII(UA_NodeIdRAII&& other) noexcept : nodeId(other.nodeId) {
        other.nodeId = UA_NODEID_NULL;
    }
    
    UA_NodeIdRAII& operator=(UA_NodeIdRAII&& other) noexcept {
        UA_NodeId_clear(&nodeId);
        nodeId = other.nodeId;
        other.nodeId = UA_NODEID_NULL;
        return *this;
    }
    
    const UA_NodeId& get() const { return nodeId; }
};

// ============================================================================
// OPCServerManagerUA - OPC UA Client Manager using open62541
// ============================================================================
/**
 * @brief OPC UA client manager wrapping open62541 library
 * 
 * This class provides the same interface as OPCServerManager (OPC DA)
 * but uses OPC UA protocol via open62541 library for 64-bit compatibility.
 * 
 * Architecture:
 * - Replaces OPC DA (COM-based, 32-bit only) with OPC UA (platform-independent)
 * - Uses open62541 static library linked via vcpkg
 * - Maintains identical public API for transparent integration
 * - Supports same industrial SLM workflow as OPC DA version
 * 
 * Thread Safety Model:
 * - All public methods are thread-safe for concurrent access
 * - Can be safely called from OPC worker thread, GUI thread, or async callbacks
 * - Uses std::mutex with minimal lock scopes to avoid contention
 * - Client state (connected/disconnected) is synchronized via mutex
 * - No deadlock risk: single mutex, no nested locking
 * 
 * Lifetime & RAII:
 * - UA_Client is owned by std::unique_ptr with custom deleter
 * - Destructor guarantees: disconnect before delete
 * - All UA_NodeId allocations are stored as stack values (no cleanup needed)
 * - UA_Variant objects are initialized/cleared in stack scope (RAII-style)
 * - Safe for move semantics, deleted copy constructor/assignment
 * 
 * Error Handling:
 * - All UA_StatusCode values are checked and logged
 * - Partial initialization is rolled back on failure
 * - Methods fail safely without throwing exceptions
 * - Connection loss detected and propagated via signals
 * 
 * Node ID Mapping:
 * - OPC DA tags (e.g., "CECC.MaTe_DLMS.StartUpSequence.StartUp")
 * - Maps to OPC UA node IDs (namespace index + identifier string)
 * - Node IDs configured via environment variables or defaults
 * - Default namespace index: 2 (CoDeSys convention)
 * - Default server URL: opc.tcp://localhost:4840 (CoDeSys default)
 */
class OPCServerManagerUA : public QObject {
    Q_OBJECT

public:
    // ========== Public Constants ==========
    static constexpr uint16_t DEFAULT_NAMESPACE_INDEX = 2;
    static constexpr const char* DEFAULT_SERVER_URL = "opc.tcp://localhost:4840";
    static constexpr uint32_t CONNECTION_TIMEOUT_MS = 5000;
    static constexpr uint32_t OPERATION_SLEEP_MS = 100;

    explicit OPCServerManagerUA(QObject* parent = nullptr);
    ~OPCServerManagerUA();

    // Prevent copy semantics (owning unique_ptr)
    OPCServerManagerUA(const OPCServerManagerUA&) = delete;
    OPCServerManagerUA& operator=(const OPCServerManagerUA&) = delete;

    // Initialization
    bool initialize();
    bool isInitialized() const;
    void stop();  // NEW: Safely shut down OPC UA connection

    // OPC Operations (identical interface to OPCServerManager)
    bool writeStartUp(bool value);
    bool writePowderFillParameters(int layers, int deltaSource, int deltaSink);
    bool writeLayerParameters(int layers, int deltaSource, int deltaSink);
    bool writeBottomLayerParameters(int layers, int deltaSource, int deltaSink);
    bool writeEmergencyStop();
    bool writeCylinderPosition(bool isSource, int position);
    
    // ========== Bidirectional Layer Synchronization (OPC UA) ==========
    //
    // writeLayerExecutionComplete():
    //   Notifies PLC that laser scanning for current layer is complete.
    //   OPC UA version: writes Boolean FALSE to LaySurface node
    //
    // Industrial Standard:
    //   Same bidirectional handshake as OPC DA:
    //   1. OPC UA server creates layer (recoater, platform) ? signals "layer ready"
    //   2. Scanner executes layer (laser scan) ? signals "layer done"
    //   3. OPC UA server proceeds with next layer creation
    //
    bool writeLayerExecutionComplete(int layerNumber);
    
    // Data reading structure (identical to OPC DA version)
    struct OPCData {
        int sourceCylPosition = 0;
        int sinkCylPosition = 0;
        int g_sourceCylPosition = 0;
        int g_sinkCylPosition = 0;
        int stacksLeft = 0;
        int ready2Powder = 0;
        int startUpDone = 0;
        int powderSurfaceDone = 0;
    };

    bool readData(OPCData& data);

    // Logging callback
    void setLogCallback(std::function<void(const QString&)> callback);

signals:
    void logMessage(const QString& message);
    void connectionLost();
    void dataUpdated(const OPCData& data);

private:
    // ========== OPC UA Connection Management ==========
    
    /**
     * Establishes connection to OPC UA server.
     * Assumes mClient is already created.
     * On failure, mClient is deleted and set to nullptr.
     * Thread-safe: called only from initialize() under lock.
     */
    bool connectToServer();
    
    /**
     * Disconnects and cleans up OPC UA client.
     * Safe to call multiple times.
     * Thread-safe: uses mutex guard.
     */
    void disconnectFromServer();
    
    // ========== Node ID Management ==========
    
    /**
     * Creates a UA_NodeId from a CoDeSys tag string.
     * Format: "CECC.MaTe_DLMS.StartUpSequence.StartUp"
     * Note: Returned UA_NodeId is stack-allocated, no cleanup required.
     */
    UA_NodeId createNodeId(const QString& nodeIdString);
    
    /**
     * Initializes all node ID references after successful server connection.
     * Called only from initialize() under lock.
     */
    void setupNodeIds();
    
    // ========== Read/Write Helpers (Thread-Safe) ==========
    
    /**
     * Reads a 32-bit integer from OPC UA node.
     * Thread-safe: called with mutex locked.
     * Returns false if read fails or type mismatch.
     */
    bool readInt32Node(const UA_NodeId& nodeId, int& value);
    
    /**
     * Reads a boolean from OPC UA node.
     * Thread-safe: called with mutex locked.
     * Returns false if read fails or type mismatch.
     */
    bool readBoolNode(const UA_NodeId& nodeId, bool& value);
    
    /**
     * Writes a 32-bit integer to OPC UA node.
     * Thread-safe: called with mutex locked.
     * Manages UA_Variant lifecycle (init/clear).
     */
    bool writeInt32Node(const UA_NodeId& nodeId, int value);
    
    /**
     * Writes a boolean to OPC UA node.
     * Thread-safe: called with mutex locked.
     * Manages UA_Variant lifecycle (init/clear).
     */
    bool writeBoolNode(const UA_NodeId& nodeId, bool value);
    
    // ========== Logging Helper ==========
    
    void log(const QString& message);

    // ========== Layer Preparation Simulation ==========
    void layerPreparationWorker();
    
    // ========== Synchronization & State Management ==========
    
    /**
     * Mutex protecting state transitions:
     * - mClient lifecycle (create/delete)
     * - mIsInitialized flag
     * - mConnectionLost flag
     * 
     * Lock scope: MINIMAL - released BEFORE all UA_Client_* calls
     * This prevents deadlocks and allows open62541 to handle I/O safely.
     */
    mutable std::mutex mStateMutex;
    
    /**
     * Separate mutex exclusively protecting open62541 calls.
     * 
     * Rationale:
     * - open62541 clients are NOT thread-safe
     * - Must serialize ALL access to UA_Client_* methods
     * - Independent from mStateMutex to allow state checks without blocking I/O
     * 
     * Lock scope: ONLY during UA_Client_* calls
     * ALWAYS released before Qt signals (logMessage, connectionLost, dataUpdated)
     */
    mutable std::mutex mUaCallMutex;

    // NEW: Mutex and CV for layer preparation simulation
    std::mutex mLayerPrepMutex;
    std::condition_variable mLayerPrepCv;
    std::thread mLayerPrepThread;
    std::atomic<bool> mStopWorker{false};
    std::atomic<bool> mLayerPrepRequested{false};
    
    /**
     * Flag indicating connection loss was detected.
     * Once set, all read/write operations fail immediately.
     * Protected by mStateMutex.
     */
    bool mConnectionLost;
    
    // ========== OPC UA Client Ownership ==========
    
    /**
     * OPC UA client handle, owned by std::unique_ptr.
     * Custom deleter ensures:
     * - UA_Client_disconnect() before deletion
     * - UA_Client_delete() called exactly once
     * - Safe cleanup even on abnormal termination
     * 
     * Protected by mStateMutex.
     * Accessed (read) under mUaCallMutex for all UA_Client_* operations.
     */
    UA_ClientPtr mClient;
    
    // ========== Initialization State ==========
    
    /**
     * True if server is connected and node IDs are initialized.
     * Set to false on disconnection or connection loss.
     * Protected by mStateMutex.
     */
    bool mIsInitialized;
    
    // ========== Connection Configuration ==========
    
    QString mServerUrl;
    QString mNamespaceUri;
    uint16_t mNamespaceIndex;
    
    // ========== Node IDs for all OPC tags ==========
    // Note: All UA_NodeIds are now created with UA_NODEID_STRING_ALLOC
    // and properly cleaned up in destructor via clearAllNodeIds()
    
    // MakeSurface nodes
    UA_NodeId mNode_layersMax;
    UA_NodeId mNode_delta_Source;
    UA_NodeId mNode_delta_Sink;
    UA_NodeId mNode_MakeSurface_Done;
    UA_NodeId mNode_Marcer_Source_Cylinder_ActualPosition;
    UA_NodeId mNode_Marcer_Sink_Cylinder_ActualPosition;
    
    // GVL nodes
    UA_NodeId mNode_StartSurfaces;
    UA_NodeId mNode_g_Marcer_Source_Cylinder_ActualPosition;
    UA_NodeId mNode_g_Marcer_Sink_Cylinder_ActualPosition;
    
    // Prepare2Process nodes
    UA_NodeId mNode_LaySurface;
    UA_NodeId mNode_LaySurface_Done;
    UA_NodeId mNode_Step_Sink;
    UA_NodeId mNode_Step_Source;
    UA_NodeId mNode_Lay_Stacks;
    
    // StartUpSequence nodes
    UA_NodeId mNode_StartUp;
    UA_NodeId mNode_StartUp_Done;
    
    // Readback nodes (for async reading)
    UA_NodeId mNode_Marcer_Source_Cylinder_ActualPosition_Read;
    UA_NodeId mNode_Marcer_Sink_Cylinder_ActualPosition_Read;
    UA_NodeId mNode_Z_Stacks;
    UA_NodeId mNode_MakeSurface_Done_Read;
    UA_NodeId mNode_StartUp_Done_Read;
    UA_NodeId mNode_g_Marcer_Source_Cylinder_ActualPosition_Read;
    UA_NodeId mNode_g_Marcer_Sink_Cylinder_ActualPosition_Read;
    UA_NodeId mNode_LaySurface_Done_Read;
    
    // ========== Internal Cleanup Helper ==========
    
    /**
     * Clears all allocated UA_NodeId structs on shutdown.
     * Called from destructor to prevent resource leaks.
     */
    void clearAllNodeIds();
    
    /**
     * Detects and handles connection loss.
     * Called when UA_StatusCode indicates connection error.
     * Resets client state and emits connectionLost() signal once.
     * Thread-safe: manages its own locking.
     */
    void handleConnectionLoss(const QString& reason);
    
    // ========== Logging Callback ==========
    
    std::function<void(const QString&)> mLogCallback;
};

#endif // OPCSERVERMANAGERUA_H
