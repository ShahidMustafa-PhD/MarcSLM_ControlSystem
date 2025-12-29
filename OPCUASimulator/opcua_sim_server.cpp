#include "opcua_sim_server.h"
#include <open62541/plugin/log_stdout.h>
#include <chrono>
#include <iostream>
#include <atomic>

OpcUaSimServer::OpcUaSimServer() {}
OpcUaSimServer::~OpcUaSimServer() { stop(); }

void OpcUaSimServer::configure(const std::string& endpoint, const std::string& nsUri, UA_UInt16 nsIndexDefault) {
    m_endpoint = endpoint;
    m_nsUri = nsUri;
    m_nsIndex = nsIndexDefault; // default 2
    std::cout << "[CONFIG] Endpoint: " << endpoint << std::endl;
    std::cout << "[CONFIG] Namespace URI: " << nsUri << std::endl;
    std::cout << "[CONFIG] Namespace Index: " << nsIndexDefault << std::endl;
}

bool OpcUaSimServer::start() {
    std::cout << "[START] Creating server..." << std::endl;
    
    m_server = UA_Server_new();
    if (!m_server) {
        std::cerr << "[ERROR] Failed to create OPC UA server" << std::endl;
        return false;
    }
    
    // Logging setup removed for compatibility
    
    std::cout << "[START] Server created successfully" << std::endl;
    
    // Setup namespace and variables
    setupNamespace();
    addVariables();
    
    // Start the server
    UA_StatusCode status = UA_Server_run_startup(m_server);
    if (status != UA_STATUSCODE_GOOD) {
        std::cerr << "[ERROR] Failed to start OPC UA server: " << UA_StatusCode_name(status) << std::endl;
        UA_Server_delete(m_server);
        m_server = nullptr;
        return false;
    }
    
    std::cout << "[START] Server started and listening on " << m_endpoint << std::endl;
    return true;
}

void OpcUaSimServer::stop() {
    if (m_server) {
        std::cout << "[STOP] Shutting down server..." << std::endl;
        UA_Server_run_shutdown(m_server);
        UA_Server_delete(m_server);
        m_server = nullptr;
        std::cout << "[STOP] Server stopped" << std::endl;
    }
}

void OpcUaSimServer::iterate() {
    if (!m_server) return;
    UA_Server_run_iterate(m_server, false);
    applyBehavior();
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
}

void OpcUaSimServer::run() {
    if (!m_server) {
        std::cerr << "[ERROR] Server not started - call start() first" << std::endl;
        return;
    }
    
    std::cout << "[RUN] Server loop starting..." << std::endl;
    std::atomic<bool> running(true);
    
    try {
        while (running) {
            UA_Server_run_iterate(m_server, false);
            applyBehavior();
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    } catch (const std::exception& e) {
        std::cerr << "[EXCEPTION] Server loop: " << e.what() << std::endl;
    }
    
    std::cout << "[RUN] Server loop exited" << std::endl;
}

void OpcUaSimServer::setupNamespace() {
    std::cout << "[NAMESPACE] Registering namespace..." << std::endl;
    
    // Register namespace - open62541 always returns index >= 1 (0 is reserved for OPC UA standard)
    // The server may assign a different index than requested if it conflicts
    UA_UInt16 idx = UA_Server_addNamespace(m_server, m_nsUri.c_str());
    
    std::cout << "[NAMESPACE] Namespace URI: " << m_nsUri << std::endl;
    std::cout << "[NAMESPACE] Assigned index: " << idx << std::endl;
    
    // Update m_nsIndex to the actual assigned index
    m_nsIndex = idx;
    
    if (m_nsIndex != 2) {
        std::cout << "[WARNING] Expected namespace index 2, got " << m_nsIndex << std::endl;
        std::cout << "[WARNING] Client must use OPC_UA_NAMESPACE_INDEX=" << m_nsIndex << " environment variable" << std::endl;
    }
}

static UA_NodeId makeStringNodeId(UA_UInt16 ns, const char* s) {
    return UA_NODEID_STRING_ALLOC(ns, s);
}

void OpcUaSimServer::addVariables() {
    std::cout << "[VARIABLES] Adding variables to namespace index: " << m_nsIndex << std::endl;
    std::cout << "[VARIABLES] Total variables to add: 19" << std::endl;
    
    UA_NodeId parent = UA_NODEID_NUMERIC(0, UA_NS0ID_OBJECTSFOLDER);
    UA_NodeId type = UA_NODEID_NUMERIC(0, UA_NS0ID_BASEDATAVARIABLETYPE);
    UA_NodeId ref = UA_NODEID_NUMERIC(0, UA_NS0ID_ORGANIZES);

    int addedCount = 0;
    auto addVar = [&](UA_NodeId& outId, const char* idStr, const UA_DataType* dt, const void* initValue, UA_Boolean writable) {
        UA_VariableAttributes attr = UA_VariableAttributes_default;
        attr.displayName = UA_LOCALIZEDTEXT_ALLOC("en-US", idStr);
        attr.description = UA_LOCALIZEDTEXT_ALLOC("en-US", idStr);
        attr.accessLevel = UA_ACCESSLEVELMASK_READ | (writable ? UA_ACCESSLEVELMASK_WRITE : 0);
        attr.userAccessLevel = attr.accessLevel;
        UA_Variant_setScalarCopy(&attr.value, initValue, dt);
        outId = makeStringNodeId(m_nsIndex, idStr);
        UA_NodeId newNodeId;
        UA_StatusCode st = UA_Server_addVariableNode(m_server, outId, parent, ref, UA_QUALIFIEDNAME(m_nsIndex, const_cast<char*>(idStr)), type, attr, nullptr, &newNodeId);
        UA_Variant_clear(&attr.value);
        UA_LocalizedText_clear(&attr.displayName);
        UA_LocalizedText_clear(&attr.description);
        if (st != UA_STATUSCODE_GOOD) {
            std::cerr << "[ERROR] Failed to add '" << idStr << "' - " << UA_StatusCode_name(st) << std::endl;
        } else {
            addedCount++;
            std::cout << "[OK] Added variable: " << idStr << std::endl;
        }
    };

    // Initialize defaults
    PlcState init = m_state;

    // MakeSurface
    addVar(nid_Z_Stacks, "CECC.MaTe_DLMS.MakeSurface.Z_Stacks", &UA_TYPES[UA_TYPES_INT32], &init.Z_Stacks, UA_TRUE);
    addVar(nid_Delta_Source, "CECC.MaTe_DLMS.MakeSurface.Delta_Source", &UA_TYPES[UA_TYPES_INT32], &init.Delta_Source, UA_TRUE);
    addVar(nid_Delta_Sink, "CECC.MaTe_DLMS.MakeSurface.Delta_Sink", &UA_TYPES[UA_TYPES_INT32], &init.Delta_Sink, UA_TRUE);
    addVar(nid_MakeSurface_Done, "CECC.MaTe_DLMS.MakeSurface.MakeSurface_Done", &UA_TYPES[UA_TYPES_BOOLEAN], &init.MakeSurface_Done, UA_TRUE);
    addVar(nid_Marcer_Source_Cylinder_ActualPosition, "CECC.MaTe_DLMS.MakeSurface.Marcer_Source_Cylinder_ActualPosition", &UA_TYPES[UA_TYPES_INT32], &init.Marcer_Source_Cylinder_ActualPosition, UA_TRUE);
    addVar(nid_Marcer_Sink_Cylinder_ActualPosition, "CECC.MaTe_DLMS.MakeSurface.Marcer_Sink_Cylinder_ActualPosition", &UA_TYPES[UA_TYPES_INT32], &init.Marcer_Sink_Cylinder_ActualPosition, UA_TRUE);

    // GVL
    addVar(nid_StartSurfaces, "CECC.MaTe_DLMS.GVL.StartSurfaces", &UA_TYPES[UA_TYPES_BOOLEAN], &init.StartSurfaces, UA_TRUE);
    addVar(nid_g_Marcer_Source_Cylinder_ActualPosition, "CECC.MaTe_DLMS.GVL.g_Marcer_Source_Cylinder_ActualPosition", &UA_TYPES[UA_TYPES_INT32], &init.g_Marcer_Source_Cylinder_ActualPosition, UA_TRUE);
    addVar(nid_g_Marcer_Sink_Cylinder_ActualPosition, "CECC.MaTe_DLMS.GVL.g_Marcer_Sink_Cylinder_ActualPosition", &UA_TYPES[UA_TYPES_INT32], &init.g_Marcer_Sink_Cylinder_ActualPosition, UA_TRUE);

    // Prepare2Process
    addVar(nid_LaySurface, "CECC.MaTe_DLMS.Prepare2Process.LaySurface", &UA_TYPES[UA_TYPES_BOOLEAN], &init.LaySurface, UA_TRUE);
    addVar(nid_LaySurface_Done, "CECC.MaTe_DLMS.Prepare2Process.LaySurface_Done", &UA_TYPES[UA_TYPES_BOOLEAN], &init.LaySurface_Done, UA_TRUE);
    addVar(nid_Step_Sink, "CECC.MaTe_DLMS.Prepare2Process.Step_Sink", &UA_TYPES[UA_TYPES_INT32], &init.Step_Sink, UA_TRUE);
    addVar(nid_Step_Source, "CECC.MaTe_DLMS.Prepare2Process.Step_Source", &UA_TYPES[UA_TYPES_INT32], &init.Step_Source, UA_TRUE);
    addVar(nid_Lay_Stacks, "CECC.MaTe_DLMS.Prepare2Process.Lay_Stacks", &UA_TYPES[UA_TYPES_INT32], &init.Lay_Stacks, UA_TRUE);

    // StartUpSequence
    addVar(nid_StartUp, "CECC.MaTe_DLMS.StartUpSequence.StartUp", &UA_TYPES[UA_TYPES_BOOLEAN], &init.StartUp, UA_TRUE);
    addVar(nid_StartUp_Done, "CECC.MaTe_DLMS.StartUpSequence.StartUp_Done", &UA_TYPES[UA_TYPES_BOOLEAN], &init.StartUp_Done, UA_TRUE);
    
    std::cout << "[VARIABLES] ========================================" << std::endl;
    std::cout << "[VARIABLES] Successfully added " << addedCount << "/19 variables" << std::endl;
    std::cout << "[VARIABLES] Namespace: " << m_nsUri << std::endl;
    std::cout << "[VARIABLES] Index: " << m_nsIndex << std::endl;
    std::cout << "[VARIABLES] Ready for client connections" << std::endl;
    std::cout << "[VARIABLES] ========================================" << std::endl;
}

void OpcUaSimServer::writeVar(const UA_NodeId& nid, const void* src, const UA_DataType* dt) {
    if (!m_server) return;
    UA_Variant v; UA_Variant_init(&v);
    UA_Variant_setScalarCopy(&v, src, dt);
    UA_Server_writeValue(m_server, nid, v);
    UA_Variant_clear(&v);
}

void OpcUaSimServer::readVar(const UA_NodeId& nid, void* dst, const UA_DataType* dt) {
    if (!m_server) return;
    UA_Variant v; UA_Variant_init(&v);
    UA_Server_readValue(m_server, nid, &v);
    if (UA_Variant_hasScalarType(&v, dt) && v.data) {
        std::memcpy(dst, v.data, dt->memSize);
    }
    UA_Variant_clear(&v);
}

void OpcUaSimServer::applyBehavior() {
    std::lock_guard<std::mutex> lock(m_mutex);

    // Read all writable values from the server to update local state
    readVar(nid_StartUp, &m_state.StartUp, &UA_TYPES[UA_TYPES_BOOLEAN]);
    readVar(nid_StartSurfaces, &m_state.StartSurfaces, &UA_TYPES[UA_TYPES_BOOLEAN]);
    readVar(nid_LaySurface, &m_state.LaySurface, &UA_TYPES[UA_TYPES_BOOLEAN]);
    readVar(nid_Z_Stacks, &m_state.Z_Stacks, &UA_TYPES[UA_TYPES_INT32]);
    readVar(nid_Delta_Source, &m_state.Delta_Source, &UA_TYPES[UA_TYPES_INT32]);
    readVar(nid_Delta_Sink, &m_state.Delta_Sink, &UA_TYPES[UA_TYPES_INT32]);
    readVar(nid_Step_Source, &m_state.Step_Source, &UA_TYPES[UA_TYPES_INT32]);
    readVar(nid_Step_Sink, &m_state.Step_Sink, &UA_TYPES[UA_TYPES_INT32]);
    readVar(nid_Lay_Stacks, &m_state.Lay_Stacks, &UA_TYPES[UA_TYPES_INT32]);


    // 1) Startup Sequence
    if (m_state.StartUp && !m_state.StartUp_Done) {
        std::cout << "[SIM] Startup sequence initiated by client." << std::endl;
        std::this_thread::sleep_for(std::chrono::seconds(2)); // Simulate work
        m_state.StartUp_Done = UA_TRUE;
        writeVar(nid_StartUp_Done, &m_state.StartUp_Done, &UA_TYPES[UA_TYPES_BOOLEAN]);
        std::cout << "[SIM] Startup sequence complete. StartUp_Done -> TRUE" << std::endl;
    }

    // 2) MakeSurface (Powder Fill) when StartSurfaces is TRUE
    if (m_state.StartSurfaces) {
        if (!m_state.MakeSurface_Done) {
            std::cout << "[SIM] Powder fill sequence initiated by client." << std::endl;
            // Simulate moving cylinders based on Z_Stacks
            for (int i = 0; i < m_state.Z_Stacks; ++i) {
                m_state.Marcer_Source_Cylinder_ActualPosition += m_state.Delta_Source;
                m_state.Marcer_Sink_Cylinder_ActualPosition += m_state.Delta_Sink;
                std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Simulate movement
            }
            m_state.MakeSurface_Done = UA_TRUE;
            writeVar(nid_MakeSurface_Done, &m_state.MakeSurface_Done, &UA_TYPES[UA_TYPES_BOOLEAN]);
            std::cout << "[SIM] Powder fill complete. MakeSurface_Done -> TRUE" << std::endl;
        }
    }
    else {
        // Client may reset this
        m_state.MakeSurface_Done = UA_FALSE;
        writeVar(nid_MakeSurface_Done, &m_state.MakeSurface_Done, &UA_TYPES[UA_TYPES_BOOLEAN]);
    }


    // 3) Layer Creation Handshake (The core logic)
    // STEP 1 & 2: Client requests layer preparation by setting LaySurface=TRUE
    if (m_state.LaySurface && !m_state.PreparingLayer) {
        std::cout << "[SIM] Layer preparation requested (LaySurface=TRUE)." << std::endl;
        m_state.PreparingLayer = UA_TRUE;
        m_state.LaySurface_Done = UA_FALSE;
        writeVar(nid_LaySurface_Done, &m_state.LaySurface_Done, &UA_TYPES[UA_TYPES_BOOLEAN]);
        std::cout << "[SIM] Simulating recoater/platform movement..." << std::endl;

        // Simulate the work of preparing the layer (e.g., recoater movement)
        std::this_thread::sleep_for(std::chrono::seconds(2)); // Simulate work

        // Update cylinder positions based on Step_Source and Step_Sink
        m_state.Marcer_Source_Cylinder_ActualPosition += m_state.Step_Source;
        m_state.Marcer_Sink_Cylinder_ActualPosition += m_state.Step_Sink;

        // STEP 3: Server signals layer is ready by setting LaySurface_Done=TRUE
        m_state.LaySurface_Done = UA_TRUE;
        writeVar(nid_LaySurface_Done, &m_state.LaySurface_Done, &UA_TYPES[UA_TYPES_BOOLEAN]);
        std::cout << "[SIM] Layer prepared. LaySurface_Done -> TRUE" << std::endl;
    }
    // STEP 5 & 6: Client signals execution is complete by setting LaySurface=FALSE
    else if (!m_state.LaySurface && m_state.PreparingLayer) {
        std::cout << "[SIM] Client signaled layer execution complete (LaySurface=FALSE)." << std::endl;
        m_state.PreparingLayer = UA_FALSE;
        m_state.LaySurface_Done = UA_FALSE;
        writeVar(nid_LaySurface_Done, &m_state.LaySurface_Done, &UA_TYPES[UA_TYPES_BOOLEAN]);
        std::cout << "[SIM] Resetting for next layer cycle. LaySurface_Done -> FALSE" << std::endl;
    }

    // Update mirrored global variables
    m_state.g_Marcer_Source_Cylinder_ActualPosition = m_state.Marcer_Source_Cylinder_ActualPosition;
    m_state.g_Marcer_Sink_Cylinder_ActualPosition = m_state.Marcer_Sink_Cylinder_ActualPosition;
    writeVar(nid_g_Marcer_Source_Cylinder_ActualPosition, &m_state.g_Marcer_Source_Cylinder_ActualPosition, &UA_TYPES[UA_TYPES_INT32]);
    writeVar(nid_g_Marcer_Sink_Cylinder_ActualPosition, &m_state.g_Marcer_Sink_Cylinder_ActualPosition, &UA_TYPES[UA_TYPES_INT32]);
    writeVar(nid_Marcer_Source_Cylinder_ActualPosition, &m_state.Marcer_Source_Cylinder_ActualPosition, &UA_TYPES[UA_TYPES_INT32]);
    writeVar(nid_Marcer_Sink_Cylinder_ActualPosition, &m_state.Marcer_Sink_Cylinder_ActualPosition, &UA_TYPES[UA_TYPES_INT32]);
}
