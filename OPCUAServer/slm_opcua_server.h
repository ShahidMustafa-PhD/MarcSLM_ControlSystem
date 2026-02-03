/**
 * @file slm_opcua_server.h
 * @brief Production OPC UA Server for SLM Control System
 * 
 * @details This header defines the main OPC UA server class that interfaces
 * with real PLCs and SLM hardware. It replaces the simulator for production
 * deployment while maintaining the same OPC UA interface.
 * 
 * @par Architecture:
 * This server implements the exact same OPC UA information model as expected
 * by the OPCServerManagerUA client class. All node IDs, variable types, and
 * methods are compatible.
 * 
 * @par Node ID Format (CoDeSys Convention):
 * @code
 * CECC.MaTe_DLMS.<FunctionBlock>.<Variable>
 * @endcode
 * 
 * Examples:
 * - CECC.MaTe_DLMS.StartUpSequence.StartUp
 * - CECC.MaTe_DLMS.Prepare2Process.LaySurface
 * - CECC.MaTe_DLMS.MakeSurface.Z_Stacks
 * 
 * @par Safety Features:
 * - Fail-safe controller integration
 * - Emergency stop handling
 * - Position limit validation
 * - Watchdog monitoring
 * 
 * @par Thread Safety:
 * The server runs in a dedicated thread. All external access to state is
 * synchronized via the PlcStateContainer.
 * 
 * @par Memory Safety:
 * - All UA_NodeId allocations are tracked and cleaned up
 * - RAII patterns used throughout
 * - No raw pointer ownership
 * 
 * @author Senior Embedded Systems Engineer
 * @copyright (c) 2024 MarcSLM Control Systems
 */

#ifndef OPCUASERVER_SLM_OPCUA_SERVER_H
#define OPCUASERVER_SLM_OPCUA_SERVER_H

#include "plc_state.h"
#include "fail_safe_controller.h"
#include "codesys_plc_interface.h"

#include <open62541/server.h>
#include <open62541/server_config_default.h>

#include <string>
#include <thread>
#include <mutex>
#include <atomic>
#include <functional>
#include <chrono>
#include <memory>
#include <vector>

namespace slm_opcua {

// ============================================================================
// Configuration
// ============================================================================

/**
 * @brief Server configuration parameters
 */
struct ServerConfig {
    /** @brief OPC UA endpoint URL (default: opc.tcp://0.0.0.0:4840) */
    std::string endpointUrl = "opc.tcp://0.0.0.0:4840";
    
    /** @brief Namespace URI for SLM variables */
    std::string namespaceUri = "urn:CODESYS:MaTe_DLMS";
    
    /** @brief Default namespace index (CoDeSys convention: 2) */
    uint16_t namespaceIndex = 2;
    
    /** @brief Server polling interval (ms) */
    uint32_t pollingIntervalMs = 10;
    
    /** @brief Enable fail-safe controller */
    bool enableFailSafe = true;
    
    /** @brief Watchdog timeout (ms) */
    uint32_t watchdogTimeoutMs = 5000;
    
    /** @brief Simulate PLC behavior (for testing without hardware) */
    bool simulatePlc = true;
    
    /** @brief Simulated layer preparation time (ms) */
    uint32_t simLayerPrepTimeMs = 100;
    
    /** @brief Log callback for server messages */
    std::function<void(const std::string&)> logCallback;
};

// ============================================================================
// OPC UA Server Class
// ============================================================================

/**
 * @brief Production OPC UA Server for SLM Control
 * 
 * @details This class provides a complete OPC UA server implementation for
 * controlling a Selective Laser Melting (SLM) machine. It is designed to:
 * 
 * 1. **Interface with PLCs**: Reads commands and writes status
 * 2. **Control Hardware**: Manages laser, motion, and powder systems
 * 3. **Ensure Safety**: Implements fail-safe logic for all operations
 * 4. **Support Testing**: Can simulate PLC behavior when hardware is offline
 * 
 * @par Usage Example:
 * @code
 * slm_opcua::ServerConfig config;
 * config.endpointUrl = "opc.tcp://0.0.0.0:4840";
 * config.simulatePlc = false;  // Production mode
 * 
 * slm_opcua::SlmOpcUaServer server(config);
 * if (server.start()) {
 *     server.run();  // Blocks until stop() called
 * }
 * @endcode
 */
class SlmOpcUaServer {
public:
    // ========================================================================
    // Construction / Destruction
    // ========================================================================
    
    /**
     * @brief Construct server with configuration
     * @param config Server configuration parameters
     */
    explicit SlmOpcUaServer(const ServerConfig& config = {});
    
    /**
     * @brief Destructor - ensures clean shutdown
     */
    ~SlmOpcUaServer();
    
    // Prevent copying
    SlmOpcUaServer(const SlmOpcUaServer&) = delete;
    SlmOpcUaServer& operator=(const SlmOpcUaServer&) = delete;
    
    // ========================================================================
    // Server Lifecycle
    // ========================================================================
    
    /**
     * @brief Initialize and start the OPC UA server
     * 
     * @return true if server started successfully
     * 
     * @details This method:
     * 1. Creates the UA_Server instance
     * 2. Configures the server endpoint
     * 3. Registers the namespace
     * 4. Adds all OPC UA variables
     * 5. Attaches the fail-safe controller
     * 6. Starts the server event loop
     */
    bool start();
    
    /**
     * @brief Run the main server loop (blocking)
     * 
     * @details Call this from the main thread after start(). The method
     * blocks until stop() is called from another thread.
     * 
     * @par Processing Loop:
     * 1. Process OPC UA messages
     * 2. Apply PLC behavior (if simulating)
     * 3. Update variable values
     * 4. Feed watchdog
     * 5. Check safety conditions
     */
    void run();
    
    /**
     * @brief Process one iteration (non-blocking)
     * 
     * @details Alternative to run() for embedding in custom event loops.
     * Call this periodically (e.g., every 10ms).
     */
    void iterate();
    
    /**
     * @brief Signal the server to stop
     * 
     * @details Can be called from any thread. The run() method will return
     * after completing the current iteration.
     */
    void stop();
    
    /**
     * @brief Check if server is running
     */
    bool isRunning() const noexcept { return m_running.load(); }
    
    // ========================================================================
    // State Access
    // ========================================================================
    
    /**
     * @brief Get thread-safe access to PLC state
     * 
     * @return Reference to state container
     * 
     * @warning Must use container.lock() for safe access
     */
    PlcStateContainer& stateContainer() { return m_stateContainer; }
    
    /**
     * @brief Get fail-safe controller
     */
    FailSafeController& failSafe() { return *m_failSafe; }
    
    // ========================================================================
    // Configuration Access
    // ========================================================================
    
    /**
     * @brief Get actual namespace index assigned by server
     */
    uint16_t getNamespaceIndex() const noexcept { return m_nsIndex; }
    
    /**
     * @brief Get server endpoint URL
     */
    const std::string& getEndpointUrl() const noexcept { return m_config.endpointUrl; }
    
private:
    // ========================================================================
    // Initialization
    // ========================================================================
    
    /**
     * @brief Register namespace with server
     * @return true if successful
     */
    bool setupNamespace();
    
    /**
     * @brief Add all OPC UA variable nodes
     * @return true if all variables added successfully
     */
    bool addVariables();
    
    /**
     * @brief Add a single variable node
     * 
     * @param nodeId Output: assigned node ID
     * @param identifier Node identifier string
     * @param dataType Data type (e.g., UA_TYPES_INT32)
     * @param initialValue Pointer to initial value
     * @param writable true if client can write
     * @return true if variable added successfully
     */
    bool addVariable(UA_NodeId& nodeId,
                     const char* identifier,
                     const UA_DataType* dataType,
                     const void* initialValue,
                     bool writable);
    
    // ========================================================================
    // PLC Behavior Simulation
    // ========================================================================
    
    /**
     * @brief Apply simulated PLC behavior
     * 
     * @details Called each iteration when simulatePlc is enabled.
     * Implements the same state machine as the physical PLC:
     * - Startup sequence handling
     * - Powder fill (MakeSurface) logic
     * - Layer preparation (Prepare2Process) handshake
     */
    void applyPlcBehavior();
    
    // ========================================================================
    // OPC UA Variable Access
    // ========================================================================
    
    /**
     * @brief Write a value to an OPC UA variable
     */
    void writeVar(const UA_NodeId& nodeId, const void* value, const UA_DataType* type);
    
    /**
     * @brief Read a value from an OPC UA variable
     */
    void readVar(const UA_NodeId& nodeId, void* value, const UA_DataType* type);
    
    /**
     * @brief Sync internal state to OPC UA variables
     */
    void syncStateToVariables();
    
    /**
     * @brief Sync OPC UA variables to internal state
     */
    void syncVariablesToState();
    
    // ========================================================================
    // Cleanup
    // ========================================================================
    
    /**
     * @brief Clean up all allocated node IDs
     */
    void clearNodeIds();
    
    // ========================================================================
    // Logging
    // ========================================================================
    
    /**
     * @brief Log a message
     */
    void log(const std::string& message);
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    // ========== Configuration ==========
    ServerConfig m_config;
    
    // ========== OPC UA Server ==========
    UA_Server* m_server;
    uint16_t m_nsIndex;
    std::mutex m_serverMutex;
    
    // ========== State Management ==========
    PlcStateContainer m_stateContainer;
    
    // ========== Safety ==========
    std::unique_ptr<FailSafeController> m_failSafe;
    
    // ========== CoDeSys PLC Interface (Production Mode) ==========
    std::unique_ptr<CodesysPlcInterface> m_plcInterface;
    
    // ========== Node IDs ==========
    struct {
        UA_NodeId Z_Stacks;
        UA_NodeId Delta_Source;
        UA_NodeId Delta_Sink;
        UA_NodeId MakeSurface_Done;
        UA_NodeId Marcer_Source_Cylinder_ActualPosition;
        UA_NodeId Marcer_Sink_Cylinder_ActualPosition;
        
        // GVL
        UA_NodeId StartSurfaces;
        UA_NodeId g_Marcer_Source_Cylinder_ActualPosition;
        UA_NodeId g_Marcer_Sink_Cylinder_ActualPosition;
        
        // Prepare2Process
        UA_NodeId LaySurface;
        UA_NodeId LaySurface_Done;
        UA_NodeId Step_Sink;
        UA_NodeId Step_Source;
        UA_NodeId Lay_Stacks;
        
        // StartUpSequence
        UA_NodeId StartUp;
        UA_NodeId StartUp_Done;
    } m_nodes;
    
    // Track allocated node IDs for cleanup
    std::vector<UA_NodeId*> m_allocatedNodeIds;
    
    // Server lifecycle
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_stopRequested{false};
};

} // namespace slm_opcua

#endif // OPCUASERVER_SLM_OPCUA_SERVER_H
