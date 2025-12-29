#pragma once
#include <open62541/server.h>
#include <open62541/server_config_default.h>
#include <thread>
#include <mutex>
#include <atomic>
#include <string>

struct PlcState {
    // MakeSurface
    UA_Int32 Z_Stacks = 0;
    UA_Int32 Delta_Source = 0;
    UA_Int32 Delta_Sink = 0;
    UA_Boolean MakeSurface_Done = UA_FALSE;
    UA_Int32 Marcer_Source_Cylinder_ActualPosition = 0;
    UA_Int32 Marcer_Sink_Cylinder_ActualPosition = 0;

    // GVL
    UA_Boolean StartSurfaces = UA_FALSE;
    UA_Int32 g_Marcer_Source_Cylinder_ActualPosition = 0;
    UA_Int32 g_Marcer_Sink_Cylinder_ActualPosition = 0;

    // Prepare2Process
    UA_Boolean LaySurface = UA_FALSE;
    UA_Boolean LaySurface_Done = UA_FALSE;
    UA_Int32 Step_Sink = 0;
    UA_Int32 Step_Source = 0;
    UA_Int32 Lay_Stacks = 0;

    // StartUpSequence
    UA_Boolean StartUp = UA_FALSE;
    UA_Boolean StartUp_Done = UA_FALSE;

    // Internal simulator state
    UA_Boolean PreparingLayer = UA_FALSE;
};

class OpcUaSimServer {
public:
    OpcUaSimServer();
    ~OpcUaSimServer();

    void configure(const std::string& endpoint, const std::string& nsUri, UA_UInt16 nsIndexDefault);
    bool start();
    void iterate();
    void run();  // Main server loop - blocks until stop() is called
    void stop();

private:
    void setupNamespace();
    void addVariables();
    void applyBehavior();

    // open62541 server
    UA_Server* m_server = nullptr;
    UA_UInt16 m_nsIndex = 2;
    std::string m_nsUri;
    std::string m_endpoint;

    PlcState m_state;
    std::mutex m_mutex;

    // NodeIds
    UA_NodeId nid_Z_Stacks;
    UA_NodeId nid_Delta_Source;
    UA_NodeId nid_Delta_Sink;
    UA_NodeId nid_MakeSurface_Done;
    UA_NodeId nid_Marcer_Source_Cylinder_ActualPosition;
    UA_NodeId nid_Marcer_Sink_Cylinder_ActualPosition;

    UA_NodeId nid_StartSurfaces;
    UA_NodeId nid_g_Marcer_Source_Cylinder_ActualPosition;
    UA_NodeId nid_g_Marcer_Sink_Cylinder_ActualPosition;

    UA_NodeId nid_LaySurface;
    UA_NodeId nid_LaySurface_Done;
    UA_NodeId nid_Step_Sink;
    UA_NodeId nid_Step_Source;
    UA_NodeId nid_Lay_Stacks;

    UA_NodeId nid_StartUp;
    UA_NodeId nid_StartUp_Done;

    // helper
    void writeVar(const UA_NodeId& nid, const void* src, const UA_DataType* dt);
    void readVar(const UA_NodeId& nid, void* dst, const UA_DataType* dt);
};
