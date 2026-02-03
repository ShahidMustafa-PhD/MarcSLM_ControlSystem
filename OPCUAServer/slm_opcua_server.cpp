/**
 * @file slm_opcua_server.cpp
 * @brief Implementation of Production OPC UA Server for SLM Control
 * 
 * @details This file implements the complete OPC UA server logic for
 * controlling a Selective Laser Melting (SLM) machine. The implementation
 * follows industrial safety standards and uses RAII patterns throughout.
 * 
 * @par OPC UA Information Model:
 * The server exposes variables in namespace index 2 (CoDeSys convention)
 * with the following structure:
 * 
 * @code
 * Objects/
 *   CECC.MaTe_DLMS.MakeSurface.Z_Stacks [Int32, RW]
 *   CECC.MaTe_DLMS.MakeSurface.Delta_Source [Int32, RW]
 *   CECC.MaTe_DLMS.MakeSurface.Delta_Sink [Int32, RW]
 *   CECC.MaTe_DLMS.MakeSurface.MakeSurface_Done [Boolean, RW]
 *   CECC.MaTe_DLMS.MakeSurface.Marcer_Source_Cylinder_ActualPosition [Int32, RW]
 *   CECC.MaTe_DLMS.MakeSurface.Marcer_Sink_Cylinder_ActualPosition [Int32, RW]
 *   CECC.MaTe_DLMS.GVL.StartSurfaces [Boolean, RW]
 *   CECC.MaTe_DLMS.GVL.g_Marcer_Source_Cylinder_ActualPosition [Int32, RW]
 *   CECC.MaTe_DLMS.GVL.g_Marcer_Sink_Cylinder_ActualPosition [Int32, RW]
 *   CECC.MaTe_DLMS.Prepare2Process.LaySurface [Boolean, RW]
 *   CECC.MaTe_DLMS.Prepare2Process.LaySurface_Done [Boolean, RW]
 *   CECC.MaTe_DLMS.Prepare2Process.Step_Sink [Int32, RW]
 *   CECC.MaTe_DLMS.Prepare2Process.Step_Source [Int32, RW]
 *   CECC.MaTe_DLMS.Prepare2Process.Lay_Stacks [Int32, RW]
 *   CECC.MaTe_DLMS.StartUpSequence.StartUp [Boolean, RW]
 *   CECC.MaTe_DLMS.StartUpSequence.StartUp_Done [Boolean, RW]
 * @endcode
 * 
 * @author Senior Embedded Systems Engineer
 * @copyright (c) 2024 MarcSLM Control Systems
 */

#include "slm_opcua_server.h"
#include "codesys_plc_interface.h"
#include <open62541/plugin/log_stdout.h>

#include <iostream>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <chrono>

namespace slm_opcua {

// ============================================================================
// Helper: Create string-based NodeId
// ============================================================================

static UA_NodeId makeStringNodeId(uint16_t ns, const char* identifier)
{
    return UA_NODEID_STRING_ALLOC(ns, identifier);
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

SlmOpcUaServer::SlmOpcUaServer(const ServerConfig& config)
    : m_config(config)
{
    // Initialize all node IDs to null
    std::memset(&m_nodes, 0, sizeof(m_nodes));
    
    // Create fail-safe controller
    FailSafeController::Config fsConfig;
    fsConfig.watchdogTimeoutMs = m_config.watchdogTimeoutMs;
    fsConfig.autoRecovery = false;
    fsConfig.logCallback = m_config.logCallback;
    
    m_failSafe = std::make_unique<FailSafeController>(fsConfig);
    
    // Create CoDeSys PLC interface if in production mode
    if (!m_config.simulatePlc) {
        log("[SERVER] Initializing CoDeSys PLC interface...");
        
        PlcConfig plcConfig;
        plcConfig.plcIpAddress = "192.168.1.10";  // TODO: Load from config file
        plcConfig.plcPort = 502;
        plcConfig.modbusUnitId = 1;
        plcConfig.connectionTimeoutMs = 1000;
        plcConfig.responseTimeoutMs = 500;
        plcConfig.reconnectIntervalMs = 2000;
        plcConfig.maxReconnectAttempts = 10;
        plcConfig.watchdogTimeoutMs = m_config.watchdogTimeoutMs;
        plcConfig.enableWatchdog = true;
        
        // Safety limits (microns)
        plcConfig.maxSourcePosition = 200000;  // 200mm
        plcConfig.maxSinkPosition = 150000;    // 150mm
        plcConfig.minPosition = 0;
        plcConfig.maxStepSize = 1000;          // 1mm max step
        
        plcConfig.enableSoftLimits = true;
        plcConfig.enableHardLimits = true;
        plcConfig.enableEmergencyStop = true;
        
        // Forward logs to server logger
        plcConfig.logCallback = [this](const std::string& msg) {
            log(msg);
        };
        
        m_plcInterface = std::make_unique<CodesysPlcInterface>(plcConfig);
        
        log("[SERVER] CoDeSys PLC interface created");
    }
    
    log("[SERVER] SlmOpcUaServer constructed");
    log("[SERVER] Endpoint: " + m_config.endpointUrl);
    log("[SERVER] Namespace URI: " + m_config.namespaceUri);
    log("[SERVER] Simulate PLC: " + std::string(m_config.simulatePlc ? "YES" : "NO"));
}

SlmOpcUaServer::~SlmOpcUaServer()
{
    stop();
    
    // Ensure server is cleaned up
    if (m_server) {
        log("[SERVER] Cleaning up OPC UA server in destructor");
        
        // Detach fail-safe first
        m_failSafe->detachServer();
        
        // Clean up node IDs
        clearNodeIds();
        
        // Shutdown and delete server
        UA_Server_run_shutdown(m_server);
        UA_Server_delete(m_server);
        m_server = nullptr;
    }
    
    log("[SERVER] SlmOpcUaServer destroyed");
}

// ============================================================================
// Server Lifecycle
// ============================================================================

bool SlmOpcUaServer::start()
{
    log("[START] Initializing OPC UA server...");
    
    // Create server
    m_server = UA_Server_new();
    if (!m_server) {
        log("[ERROR] Failed to create UA_Server");
        return false;
    }
    
    log("[START] Server instance created");
    
    // Configure server
    UA_ServerConfig* config = UA_Server_getConfig(m_server);
    if (!config) {
        log("[ERROR] Failed to get server config");
        UA_Server_delete(m_server);
        m_server = nullptr;
        return false;
    }
    
    // Set up default config (listens on port 4840 by default)
    UA_StatusCode status = UA_ServerConfig_setDefault(config);
    if (status != UA_STATUSCODE_GOOD) {
        log("[ERROR] Failed to set default config: " + 
            std::string(UA_StatusCode_name(status)));
        UA_Server_delete(m_server);
        m_server = nullptr;
        return false;
    }
    
    log("[START] Server configured with default settings");
    
    // Setup namespace
    if (!setupNamespace()) {
        log("[ERROR] Failed to setup namespace");
        UA_Server_delete(m_server);
        m_server = nullptr;
        return false;
    }
    
    // Add variables
    if (!addVariables()) {
        log("[ERROR] Failed to add variables");
        clearNodeIds();
        UA_Server_delete(m_server);
        m_server = nullptr;
        return false;
    }
    
    // Attach fail-safe controller
    if (m_config.enableFailSafe) {
        m_failSafe->attachServer(m_server, &m_stateContainer);
        log("[START] Fail-safe controller attached");
    }
    
    // Start the server
    status = UA_Server_run_startup(m_server);
    if (status != UA_STATUSCODE_GOOD) {
        log("[ERROR] Server startup failed: " + 
            std::string(UA_StatusCode_name(status)));
        m_failSafe->detachServer();
        clearNodeIds();
        UA_Server_delete(m_server);
        m_server = nullptr;
        return false;
    }
    
    // Connect to PLC if in production mode
    if (!m_config.simulatePlc && m_plcInterface) {
        log("[START] Connecting to CoDeSys PLC...");
        log("[START] This may take a few seconds...");
        
        if (!m_plcInterface->connect()) {
            log("[ERROR] ============================================");
            log("[ERROR] FAILED TO CONNECT TO CoDeSys PLC!");
            log("[ERROR] ============================================");
            log("[ERROR] The server will continue in DEGRADED MODE.");
            log("[ERROR] Check:");
            log("[ERROR]   - PLC is powered on and in RUN mode");
            log("[ERROR]   - PLC IP address: " + m_plcInterface->getConfig().plcIpAddress);
            log("[ERROR]   - Network cable connected");
            log("[ERROR]   - Firewall allows Modbus TCP port 502");
            log("[ERROR] ============================================");
            
            // Don't fail completely - allow server to start
            // Operator can fix PLC connection and it will auto-reconnect
        } else {
            log("[START] ============================================");
            log("[START] CoDeSys PLC CONNECTED SUCCESSFULLY");
            log("[START] ============================================");
            
            // Read initial PLC status
            PlcStatus plcStatus;
            if (m_plcInterface->readStatus(plcStatus)) {
                log("[START] Initial PLC Status:");
                log("[START]   Source Position: " + std::to_string(plcStatus.sourcePositionActual) + " ?m");
                log("[START]   Sink Position: " + std::to_string(plcStatus.sinkPositionActual) + " ?m");
                log("[START]   Emergency Stop: " + std::string(plcStatus.emergencyStopActive ? "ACTIVE" : "OK"));
                log("[START]   PLC Heartbeat: " + std::to_string(plcStatus.plcHeartbeat));
            }
        }
    }
    
    m_running.store(true);
    m_stopRequested.store(false);
    
    log("[START] ============================================");
    log("[START] OPC UA Server STARTED");
    log("[START] Endpoint: " + m_config.endpointUrl);
    log("[START] Namespace Index: " + std::to_string(m_nsIndex));
    log("[START] Mode: " + std::string(m_config.simulatePlc ? "SIMULATION" : "PRODUCTION"));
    if (!m_config.simulatePlc && m_plcInterface && m_plcInterface->isConnected()) {
        log("[START] PLC Connection: ACTIVE");
    }
    log("[START] ============================================");
    
    return true;
}

void SlmOpcUaServer::run()
{
    if (!m_server) {
        log("[ERROR] Server not started - call start() first");
        return;
    }
    
    log("[RUN] Main server loop starting...");
    log("[RUN] Polling interval: " + std::to_string(m_config.pollingIntervalMs) + "ms");
    
    auto lastIteration = std::chrono::steady_clock::now();
    
    while (!m_stopRequested.load()) {
        iterate();
        
        // Calculate time since last iteration
        auto now = std::chrono::steady_clock::now();
        std::chrono::duration<double, std::milli> elapsed = now - lastIteration;
        
        // Sleep for the remaining time in the interval
        int sleepTimeMs = m_config.pollingIntervalMs - static_cast<int>(elapsed.count());
        if (sleepTimeMs > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(sleepTimeMs));
        }
        
        lastIteration = std::chrono::steady_clock::now();
    }
    
    log("[RUN] Stopping server loop...");
}

void SlmOpcUaServer::stop()
{
    if (m_running.load()) {
        log("[STOP] Stopping OPC UA server...");
        
        m_stopRequested.store(true);
        
        // Disconnect from PLC
        if (m_plcInterface) {
            m_plcInterface->disconnect();
        }
        
        m_running.store(false);
        
        log("[STOP] OPC UA server stopped");
    }
}

// ============================================================================
// Node Management
// ============================================================================

bool SlmOpcUaServer::setupNamespace()
{
    log("[NAMESPACE] Setting up namespace: " + m_config.namespaceUri);
    
    // Register namespace with the server
    m_nsIndex = UA_Server_addNamespace(m_server, m_config.namespaceUri.c_str());
    if (m_nsIndex == 0) {
        log("[ERROR] Failed to add namespace: " + m_config.namespaceUri);
        return false;
    }
    
    log("[NAMESPACE] Namespace set up with index: " + std::to_string(m_nsIndex));
    return true;
}

void SlmOpcUaServer::clearNodeIds()
{
    log("[NODE] Clearing node IDs");
    
    // Clear allocated NodeIds from m_allocatedNodeIds vector
    for (UA_NodeId* nodeIdPtr : m_allocatedNodeIds) {
        if (nodeIdPtr && nodeIdPtr->namespaceIndex == m_nsIndex) {
            UA_NodeId_clear(nodeIdPtr);
        }
    }
    m_allocatedNodeIds.clear();
}

bool SlmOpcUaServer::addVariables()
{
    log("[VARIABLES] Adding variables to OPC UA server");
    
    // Initialize default state
    auto guard = m_stateContainer.lock();
    PlcState& state = guard.state();
    
    UA_NodeId parent = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId refType = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);
    UA_NodeId varType = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);
    
    // Helper lambda to add a variable
    auto addVar = [&](UA_NodeId& outNodeId, const char* name, const UA_DataType* dt, 
                      const void* value, bool writable) -> bool {
        UA_VariableAttributes attr = UA_VariableAttributes_default;
        attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", name);
        attr.description = UA_LOCALIZEDTEXT_ALLOC("en-US", name);
        attr.accessLevel = UA_ACCESSLEVELMASK_READ | (writable ? UA_ACCESSLEVELMASK_WRITE : 0);
        attr.userAccessLevel = attr.accessLevel;
        UA_Variant_setScalarCopy(&attr.value, value, dt);
        
        outNodeId = makeStringNodeId(m_nsIndex, name);
        UA_NodeId newNodeId;
        UA_StatusCode st = UA_Server_addVariableNode(
            m_server, outNodeId, parent, refType,
            UA_QUALIFIEDNAME(m_nsIndex, const_cast<char*>(name)),
            varType, attr, nullptr, &newNodeId);
        
        UA_Variant_clear(&attr.value);
        UA_LocalizedText_clear(&attr.displayName);
        UA_LocalizedText_clear(&attr.description);
        
        if (st != UA_STATUSCODE_GOOD) {
            log("[ERROR] Failed to add variable '" + std::string(name) + "': " + 
                std::string(UA_StatusCode_name(st)));
            return false;
        }
        
        // Track for cleanup
        m_allocatedNodeIds.push_back(&outNodeId);
        return true;
    };
    
    // Add all variables
    bool success = true;
    
    // MakeSurface
    success &= addVar(m_nodes.Z_Stacks, "CECC.MaTe_DLMS.MakeSurface.Z_Stacks", 
                      &UA_TYPES[UA_TYPES_INT32], &state.Z_Stacks, true);
    success &= addVar(m_nodes.Delta_Source, "CECC.MaTe_DLMS.MakeSurface.Delta_Source",
                      &UA_TYPES[UA_TYPES_INT32], &state.Delta_Source, true);
    success &= addVar(m_nodes.Delta_Sink, "CECC.MaTe_DLMS.MakeSurface.Delta_Sink",
                      &UA_TYPES[UA_TYPES_INT32], &state.Delta_Sink, true);
    success &= addVar(m_nodes.MakeSurface_Done, "CECC.MaTe_DLMS.MakeSurface.MakeSurface_Done",
                      &UA_TYPES[UA_TYPES_BOOLEAN], &state.MakeSurface_Done, true);
    success &= addVar(m_nodes.Marcer_Source_Cylinder_ActualPosition,
                      "CECC.MaTe_DLMS.MakeSurface.Marcer_Source_Cylinder_ActualPosition",
                      &UA_TYPES[UA_TYPES_INT32], &state.Marcer_Source_Cylinder_ActualPosition, true);
    success &= addVar(m_nodes.Marcer_Sink_Cylinder_ActualPosition,
                      "CECC.MaTe_DLMS.MakeSurface.Marcer_Sink_Cylinder_ActualPosition",
                      &UA_TYPES[UA_TYPES_INT32], &state.Marcer_Sink_Cylinder_ActualPosition, true);
    
    // GVL
    success &= addVar(m_nodes.StartSurfaces, "CECC.MaTe_DLMS.GVL.StartSurfaces",
                      &UA_TYPES[UA_TYPES_BOOLEAN], &state.StartSurfaces, true);
    success &= addVar(m_nodes.g_Marcer_Source_Cylinder_ActualPosition,
                      "CECC.MaTe_DLMS.GVL.g_Marcer_Source_Cylinder_ActualPosition",
                      &UA_TYPES[UA_TYPES_INT32], &state.g_Marcer_Source_Cylinder_ActualPosition, true);
    success &= addVar(m_nodes.g_Marcer_Sink_Cylinder_ActualPosition,
                      "CECC.MaTe_DLMS.GVL.g_Marcer_Sink_Cylinder_ActualPosition",
                      &UA_TYPES[UA_TYPES_INT32], &state.g_Marcer_Sink_Cylinder_ActualPosition, true);
    
    // Prepare2Process
    success &= addVar(m_nodes.LaySurface, "CECC.MaTe_DLMS.Prepare2Process.LaySurface",
                      &UA_TYPES[UA_TYPES_BOOLEAN], &state.LaySurface, true);
    success &= addVar(m_nodes.LaySurface_Done, "CECC.MaTe_DLMS.Prepare2Process.LaySurface_Done",
                      &UA_TYPES[UA_TYPES_BOOLEAN], &state.LaySurface_Done, true);
    success &= addVar(m_nodes.Step_Sink, "CECC.MaTe_DLMS.Prepare2Process.Step_Sink",
                      &UA_TYPES[UA_TYPES_INT32], &state.Step_Sink, true);
    success &= addVar(m_nodes.Step_Source, "CECC.MaTe_DLMS.Prepare2Process.Step_Source",
                      &UA_TYPES[UA_TYPES_INT32], &state.Step_Source, true);
    success &= addVar(m_nodes.Lay_Stacks, "CECC.MaTe_DLMS.Prepare2Process.Lay_Stacks",
                      &UA_TYPES[UA_TYPES_INT32], &state.Lay_Stacks, true);
    
    // StartUpSequence
    success &= addVar(m_nodes.StartUp, "CECC.MaTe_DLMS.StartUpSequence.StartUp",
                      &UA_TYPES[UA_TYPES_BOOLEAN], &state.StartUp, true);
    success &= addVar(m_nodes.StartUp_Done, "CECC.MaTe_DLMS.StartUpSequence.StartUp_Done",
                      &UA_TYPES[UA_TYPES_BOOLEAN], &state.StartUp_Done, true);
    
    if (success) {
        log("[VARIABLES] All variables added successfully (17 total)");
    } else {
        log("[ERROR] Some variables failed to add");
    }
    
    return success;
}

void SlmOpcUaServer::iterate()
{
    if (!m_server) {
        return;
    }
    
    std::lock_guard<std::mutex> lock(m_serverMutex);
    
    // Process OPC UA messages
    UA_Server_run_iterate(m_server, false);
    
    // Sync variables to state
    syncVariablesToState();
    
    // Apply PLC behavior (simulation or production)
    applyPlcBehavior();
    
    // Sync state back to variables
    syncStateToVariables();
    
    // Feed watchdog
    if (m_config.enableFailSafe) {
        m_failSafe->feedWatchdog();
    }
}

// ============================================================================
// OPC UA Variable Access
// ============================================================================

void SlmOpcUaServer::writeVar(const UA_NodeId& nodeId, const void* value, const UA_DataType* type)
{
    if (!m_server) {
        return;
    }
    
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Variant_setScalarCopy(&variant, value, type);
    UA_Server_writeValue(m_server, nodeId, variant);
    UA_Variant_clear(&variant);
}

void SlmOpcUaServer::readVar(const UA_NodeId& nodeId, void* value, const UA_DataType* type)
{
    if (!m_server) {
        return;
    }
    
    UA_Variant variant;
    UA_Variant_init(&variant);
    UA_Server_readValue(m_server, nodeId, &variant);
    
    if (UA_Variant_hasScalarType(&variant, type) && variant.data) {
        std::memcpy(value, variant.data, type->memSize);
    }
    
    UA_Variant_clear(&variant);
}

void SlmOpcUaServer::syncStateToVariables()
{
    auto guard = m_stateContainer.lock();
    const PlcState& state = guard.state();
    
    // Write all state values to OPC UA variables
    writeVar(m_nodes.Z_Stacks, &state.Z_Stacks, &UA_TYPES[UA_TYPES_INT32]);
    writeVar(m_nodes.Delta_Source, &state.Delta_Source, &UA_TYPES[UA_TYPES_INT32]);
    writeVar(m_nodes.Delta_Sink, &state.Delta_Sink, &UA_TYPES[UA_TYPES_INT32]);
    writeVar(m_nodes.MakeSurface_Done, &state.MakeSurface_Done, &UA_TYPES[UA_TYPES_BOOLEAN]);
    writeVar(m_nodes.Marcer_Source_Cylinder_ActualPosition, 
             &state.Marcer_Source_Cylinder_ActualPosition, &UA_TYPES[UA_TYPES_INT32]);
    writeVar(m_nodes.Marcer_Sink_Cylinder_ActualPosition,
             &state.Marcer_Sink_Cylinder_ActualPosition, &UA_TYPES[UA_TYPES_INT32]);
    
    writeVar(m_nodes.StartSurfaces, &state.StartSurfaces, &UA_TYPES[UA_TYPES_BOOLEAN]);
    writeVar(m_nodes.g_Marcer_Source_Cylinder_ActualPosition,
             &state.g_Marcer_Source_Cylinder_ActualPosition, &UA_TYPES[UA_TYPES_INT32]);
    writeVar(m_nodes.g_Marcer_Sink_Cylinder_ActualPosition,
             &state.g_Marcer_Sink_Cylinder_ActualPosition, &UA_TYPES[UA_TYPES_INT32]);
    
    writeVar(m_nodes.LaySurface, &state.LaySurface, &UA_TYPES[UA_TYPES_BOOLEAN]);
    writeVar(m_nodes.LaySurface_Done, &state.LaySurface_Done, &UA_TYPES[UA_TYPES_BOOLEAN]);
    writeVar(m_nodes.Step_Sink, &state.Step_Sink, &UA_TYPES[UA_TYPES_INT32]);
    writeVar(m_nodes.Step_Source, &state.Step_Source, &UA_TYPES[UA_TYPES_INT32]);
    writeVar(m_nodes.Lay_Stacks, &state.Lay_Stacks, &UA_TYPES[UA_TYPES_INT32]);
    
    writeVar(m_nodes.StartUp, &state.StartUp, &UA_TYPES[UA_TYPES_BOOLEAN]);
    writeVar(m_nodes.StartUp_Done, &state.StartUp_Done, &UA_TYPES[UA_TYPES_BOOLEAN]);
}

void SlmOpcUaServer::syncVariablesToState()
{
    auto guard = m_stateContainer.lock();
    PlcState& state = guard.state();
    
    // Read all values from OPC UA variables to state
    readVar(m_nodes.StartUp, &state.StartUp, &UA_TYPES[UA_TYPES_BOOLEAN]);
    readVar(m_nodes.StartSurfaces, &state.StartSurfaces, &UA_TYPES[UA_TYPES_BOOLEAN]);
    readVar(m_nodes.LaySurface, &state.LaySurface, &UA_TYPES[UA_TYPES_BOOLEAN]);
    readVar(m_nodes.Z_Stacks, &state.Z_Stacks, &UA_TYPES[UA_TYPES_INT32]);
    readVar(m_nodes.Delta_Source, &state.Delta_Source, &UA_TYPES[UA_TYPES_INT32]);
    readVar(m_nodes.Delta_Sink, &state.Delta_Sink, &UA_TYPES[UA_TYPES_INT32]);
    readVar(m_nodes.Step_Source, &state.Step_Source, &UA_TYPES[UA_TYPES_INT32]);
    readVar(m_nodes.Step_Sink, &state.Step_Sink, &UA_TYPES[UA_TYPES_INT32]);
    readVar(m_nodes.Lay_Stacks, &state.Lay_Stacks, &UA_TYPES[UA_TYPES_INT32]);
}

// ============================================================================
// PLC Behavior (Simulation or Production)
// ============================================================================

void SlmOpcUaServer::applyPlcBehavior()
{
    auto guard = m_stateContainer.lock();
    PlcState& state = guard.state();
    
    // ========================================================================
    // PRODUCTION MODE: Real CoDeSys PLC Interface
    // ========================================================================
    
    if (!m_config.simulatePlc && m_plcInterface) {
        // Check PLC connection
        if (!m_plcInterface->isConnected()) {
            static auto lastReconnectAttempt = std::chrono::steady_clock::now();
            auto now = std::chrono::steady_clock::now();
            auto elapsed = std::chrono::duration_cast<std::chrono::seconds>(
                now - lastReconnectAttempt).count();
            
            if (elapsed >= 5) {
                log("[PRODUCTION] Attempting PLC reconnection...");
                m_plcInterface->reconnect();
                lastReconnectAttempt = now;
            }
            return;
        }
        
        // Read PLC status
        PlcStatus plcStatus;
        if (m_plcInterface->readStatus(plcStatus)) {
            state.Marcer_Source_Cylinder_ActualPosition = plcStatus.sourcePositionActual;
            state.Marcer_Sink_Cylinder_ActualPosition = plcStatus.sinkPositionActual;
            state.MakeSurface_Done = plcStatus.powderFillDone;
            state.LaySurface_Done = plcStatus.layerPrepDone;
            state.StartUp_Done = plcStatus.startupDone;
            
            state.g_Marcer_Source_Cylinder_ActualPosition = plcStatus.sourcePositionActual;
            state.g_Marcer_Sink_Cylinder_ActualPosition = plcStatus.sinkPositionActual;
            
            if (plcStatus.emergencyStopActive && m_config.enableFailSafe) {
                m_failSafe->triggerEmergencyStop("PLC emergency stop active");
                state.resetToSafe();
                return;
            }
        }
        
        // Process commands
        if (state.StartUp && !state.StartUp_Done) {
            log("[PRODUCTION] Startup sequence requested");
            if (m_plcInterface->startStartupSequence()) {
                log("[PRODUCTION] ? Startup command sent");
            }
        }
        
        if (state.StartSurfaces && !state.MakeSurface_Done) {
            log("[PRODUCTION] Powder fill requested");
            if (m_plcInterface->startPowderFill(state.Z_Stacks, state.Delta_Source, state.Delta_Sink)) {
                log("[PRODUCTION] ? Powder fill command sent");
            }
        }
        
        if (state.LaySurface && !state.PreparingLayer) {
            log("[PRODUCTION] Layer preparation requested");
            state.PreparingLayer = UA_TRUE;
            if (m_plcInterface->startLayerPreparation(state.Step_Source, state.Step_Sink)) {
                log("[PRODUCTION] ? Layer prep command sent");
            }
        }
        else if (!state.LaySurface && state.PreparingLayer) {
            log("[PRODUCTION] Layer execution complete");
            state.PreparingLayer = UA_FALSE;
            state.LaySurface_Done = UA_FALSE;
            m_plcInterface->resetCommands();
        }
        
        // Update watchdog
        if (m_plcInterface->isWatchdogExpired()) {
            log("[PRODUCTION] ?? PLC WATCHDOG TIMEOUT!");
            if (m_config.enableFailSafe) {
                m_failSafe->triggerEmergencyStop("PLC watchdog timeout");
                state.resetToSafe();
            }
        }
        
        return;
    }
    
    // ========================================================================
    // SIMULATION MODE: Internal PLC Emulation
    // ========================================================================
    
    // Startup sequence
    if (state.StartUp && !state.StartUp_Done) {
        log("[SIM] Startup sequence initiated");
        state.StartUp_Done = UA_TRUE;
        log("[SIM] Startup complete");
    }
    
    // Powder fill
    if (state.StartSurfaces) {
        if (!state.MakeSurface_Done) {
            log("[SIM] Powder fill initiated");
            for (int i = 0; i < state.Z_Stacks; ++i) {
                state.Marcer_Source_Cylinder_ActualPosition += state.Delta_Source;
                state.Marcer_Sink_Cylinder_ActualPosition += state.Delta_Sink;
            }
            state.MakeSurface_Done = UA_TRUE;
            log("[SIM] Powder fill complete");
        }
    } else {
        state.MakeSurface_Done = UA_FALSE;
    }
    
    // Layer preparation
    if (state.LaySurface && !state.PreparingLayer) {
        log("[SIM] Layer preparation requested");
        state.PreparingLayer = UA_TRUE;
        state.LaySurface_Done = UA_FALSE;
        
        state.Marcer_Source_Cylinder_ActualPosition += state.Step_Source;
        state.Marcer_Sink_Cylinder_ActualPosition += state.Step_Sink;
        
        state.LaySurface_Done = UA_TRUE;
        log("[SIM] Layer prepared");
    }
    else if (!state.LaySurface && state.PreparingLayer) {
        log("[SIM] Layer execution complete");
        state.PreparingLayer = UA_FALSE;
        state.LaySurface_Done = UA_FALSE;
        log("[SIM] Ready for next layer");
    }
    
    // Mirror positions
    state.g_Marcer_Source_Cylinder_ActualPosition = state.Marcer_Source_Cylinder_ActualPosition;
    state.g_Marcer_Sink_Cylinder_ActualPosition = state.Marcer_Sink_Cylinder_ActualPosition;
}

// ============================================================================
// Logging
// ============================================================================

void SlmOpcUaServer::log(const std::string& msg)
{
    auto now = std::chrono::system_clock::now();
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&now_c), "%F %T")
        << " [OPC_SERVER] " << msg;
    
    std::cout << oss.str() << std::endl;
    
    if (m_config.logCallback) {
        m_config.logCallback(oss.str());
    }
}

} // namespace slm_opcua
