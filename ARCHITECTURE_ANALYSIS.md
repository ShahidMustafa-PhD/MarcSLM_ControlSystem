# System Architecture and Data Flow Analysis

## Complete System Architecture

```
???????????????????????????????????????????????????????????????????
?                    LAYER 1: User Interface                       ?
?                                                                  ?
?  ????????????????????????????????????????????????????????????  ?
?  ?  MarcSLMControlSystem.exe (Qt GUI Application)           ?  ?
?  ?  - File: launcher/mainwindow.cpp                         ?  ?
?  ?  - Role: User interaction & visualization                ?  ?
?  ?  - Technology: Qt Widgets, C++17                         ?  ?
?  ????????????????????????????????????????????????????????????  ?
?                                                                  ?
?  Components:                                                     ?
?  - ProcessController    ? Orchestrates the build process        ?
?  - OPCController        ? OPC UA client (connects to server)    ?
?  - ScannerController    ? RTC5 laser scanner interface          ?
?  - ScanStreamingManager ? Layer-by-layer execution              ?
?                                                                  ?
????????????????????????????????????????????????????????????????????
                         ?
                         ? Protocol: OPC UA (open62541)
                         ? Connection: opc.tcp://localhost:4840
                         ? Namespace: urn:CODESYS:MaTe_DLMS (index 2)
                         ?
                         ?
???????????????????????????????????????????????????????????????????
?              LAYER 2: OPC UA Server (Middleware)                 ?
?                                                                  ?
?  ????????????????????????????????????????????????????????????  ?
?  ?  OPCUAServer.exe (Protocol Bridge)                       ?  ?
?  ?  - File: OPCUAServer/slm_opcua_server.cpp               ?  ?
?  ?  - Role: Translate OPC UA ? PLC protocols               ?  ?
?  ?  - Technology: open62541, C++17                          ?  ?
?  ????????????????????????????????????????????????????????????  ?
?                                                                  ?
?  Current Mode: SIMULATION (simulatePlc = true)                  ?
?                                                                  ?
?  ????????????????????????????????????????????????????????????  ?
?  ?  OPC UA Variables (Namespace 2)                          ?  ?
?  ????????????????????????????????????????????????????????????  ?
?  ?  CECC.MaTe_DLMS.StartUpSequence.StartUp         [R/W]   ?  ?
?  ?  CECC.MaTe_DLMS.StartUpSequence.StartUp_Done    [R/W]   ?  ?
?  ?  CECC.MaTe_DLMS.MakeSurface.Z_Stacks            [R/W]   ?  ?
?  ?  CECC.MaTe_DLMS.MakeSurface.Delta_Source        [R/W]   ?  ?
?  ?  CECC.MaTe_DLMS.MakeSurface.Delta_Sink          [R/W]   ?  ?
?  ?  CECC.MaTe_DLMS.MakeSurface.MakeSurface_Done    [R/W]   ?  ?
?  ?  CECC.MaTe_DLMS.GVL.StartSurfaces               [R/W]   ?  ?
?  ?  CECC.MaTe_DLMS.Prepare2Process.LaySurface      [R/W]   ?  ?
?  ?  CECC.MaTe_DLMS.Prepare2Process.LaySurface_Done [R/W]   ?  ?
?  ?  CECC.MaTe_DLMS.Prepare2Process.Step_Source     [R/W]   ?  ?
?  ?  CECC.MaTe_DLMS.Prepare2Process.Step_Sink       [R/W]   ?  ?
?  ?  CECC.MaTe_DLMS.Prepare2Process.Lay_Stacks      [R/W]   ?  ?
?  ?  ...MakeSurface.Marcer_Source_..._ActualPos     [R/W]   ?  ?
?  ?  ...MakeSurface.Marcer_Sink_..._ActualPosition  [R/W]   ?  ?
?  ?  ...GVL.g_Marcer_Source_..._ActualPosition      [R/W]   ?  ?
?  ?  ...GVL.g_Marcer_Sink_..._ActualPosition        [R/W]   ?  ?
?  ????????????????????????????????????????????????????????????  ?
?                                                                  ?
????????????????????????????????????????????????????????????????????
                         ?
                         ? Protocol: ? NOT IMPLEMENTED
                         ? Expected: Modbus TCP / EtherCAT / Profinet
                         ? Status: MISSING PLC COMMUNICATION LAYER
                         ?
                         ?
???????????????????????????????????????????????????????????????????
?              LAYER 3: PLC Controllers (Not Connected)            ?
?                                                                  ?
?  Expected Hardware:                                              ?
?  - Beckhoff TwinCAT PLC (ADS/EtherCAT)                          ?
?  - CoDeSys PLC (Modbus TCP/RTU)                                 ?
?  - Siemens S7 PLC (Profinet/S7 protocol)                        ?
?                                                                  ?
?  Expected Function:                                              ?
?  - Read position commands from OPC UA server                     ?
?  - Send step pulses to stepper motor drivers                     ?
?  - Read encoder feedback                                         ?
?  - Monitor safety interlocks                                     ?
?  - Implement emergency stop logic                                ?
?                                                                  ?
?  Current Status: ? NOT CONNECTED - NO INTERFACE CODE EXISTS     ?
?                                                                  ?
????????????????????????????????????????????????????????????????????
                         ?
                         ? Protocol: Stepper pulses / Servo commands
                         ? Interface: PLC digital outputs
                         ?
                         ?
???????????????????????????????????????????????????????????????????
?              LAYER 4: Motor Drivers (Not Controlled)             ?
?                                                                  ?
?  Physical Hardware:                                              ?
?  - Source Cylinder Stepper Motor Driver                          ?
?  - Sink Cylinder Stepper Motor Driver                            ?
?  - Absolute position encoders                                    ?
?  - Mechanical limit switches                                     ?
?  - Emergency stop circuit                                        ?
?                                                                  ?
?  Current Status: ? NO COMMANDS RECEIVED - MOTORS IDLE           ?
?                                                                  ?
???????????????????????????????????????????????????????????????????
```

## Data Flow: Powder Fill Command

### Simulation Mode (Current)

```
[GUI] User clicks "Start Powder Fill"
  ?
[OPCController::writePowderFillParameters()]
  ?? writes: Z_Stacks = 100
  ?? writes: Delta_Source = 50
  ?? writes: Delta_Sink = 50
  ?? writes: StartSurfaces = TRUE
  
  ? OPC UA Protocol (localhost:4840)
  
[OPCUAServer] receives write request
  ?
[SlmOpcUaServer::syncVariablesToState()]
  ?? reads Z_Stacks, Delta_Source, Delta_Sink, StartSurfaces from OPC UA
  
  ?
[SlmOpcUaServer::applyPlcBehavior()]
  ?? SIMULATION MODE: if (m_config.simulatePlc)
      ?? logs: "[SIM] Powder fill initiated (Z_Stacks=100)"
      ?? Marcer_Source_ActualPosition += Delta_Source * Z_Stacks
      ?? Marcer_Sink_ActualPosition += Delta_Sink * Z_Stacks
      ?? MakeSurface_Done = TRUE
      ?? logs: "[SIM] Powder fill complete -> MakeSurface_Done = TRUE"
  
  ?
[SlmOpcUaServer::syncStateToVariables()]
  ?? writes updated positions and status back to OPC UA variables
  
  ? OPC UA Protocol (localhost:4840)
  
[OPCController] polls and reads:
  ?? Marcer_Source_Cylinder_ActualPosition (updated)
  ?? Marcer_Sink_Cylinder_ActualPosition (updated)
  ?? MakeSurface_Done = TRUE
  
  ?
[GUI] updates display:
  ?? Source position: 5000 ?m (fake value)
  ?? Sink position: 5000 ?m (fake value)
  ?? Status: "Powder fill complete"
  
? REAL HARDWARE: No movement (just internal simulation)
```

### Production Mode (Required Implementation)

```
[GUI] User clicks "Start Powder Fill"
  ?
[OPCController::writePowderFillParameters()]
  ?? Same as simulation...
  
  ? OPC UA Protocol
  
[OPCUAServer] receives write request
  ?
[SlmOpcUaServer::applyPlcBehavior()]
  ?? PRODUCTION MODE: if (!m_config.simulatePlc)
      ?? logs: "[PRODUCTION] Powder fill requested"
      ?
      ?? TODO: Connect to PLC via Modbus TCP
      ?   ?? plc->connect("192.168.1.10", 502)
      ?
      ?? TODO: Write position commands to PLC
      ?   ?? plc->writeRegister(REG_DELTA_SOURCE, 50)
      ?   ?? plc->writeRegister(REG_DELTA_SINK, 50)
      ?   ?? plc->writeRegister(REG_Z_STACKS, 100)
      ?   ?? plc->writeRegister(REG_START_POWDER_FILL, 1)
      ?
      ?? TODO: Wait for PLC to complete movement
      ?   ?? while (!plc->readRegister(REG_MOVEMENT_COMPLETE))
      ?
      ?? TODO: Read actual positions from encoders
      ?   ?? actualSource = plc->readRegister(REG_SOURCE_ENCODER)
      ?   ?? actualSink = plc->readRegister(REG_SINK_ENCODER)
      ?   ?? update state with real values
      ?
      ?? logs: "[PRODUCTION] Movement complete"
  
  ? Modbus TCP / EtherCAT (NOT IMPLEMENTED)
  
[PLC Controller]
  ?? Reads position commands from registers
  ?? Calculates step count: steps = (Delta * Z_Stacks) / step_resolution
  ?? Sends step pulses to motor drivers
  ?? Monitors encoder feedback
  ?? Checks safety limits
  ?? Sets movement complete flag
  
  ? Step/Direction signals
  
[Stepper Motor Drivers]
  ?? Source cylinder: moves UP by Delta_Source * Z_Stacks
  ?? Sink cylinder: moves DOWN by Delta_Sink * Z_Stacks
  ?? Real-time position feedback via encoders
  
  ?
[OPCUAServer] reads encoder values from PLC
  ?? Updates OPC UA variables with REAL positions
  
  ? OPC UA Protocol
  
[GUI] displays REAL cylinder positions
```

## Code Locations

### GUI Side (Works Correctly)

```cpp
// File: controllers/opccontroller.cpp
bool OPCController::writePowderFillParameters(int layers, int deltaSource, int deltaSink)
{
    // Client writes to OPC UA variables
    mOPCServer->writePowderFillParameters(layers, deltaSource, deltaSink);
}

// File: opcserver/opcserverua.cpp
bool OPCServerManagerUA::writePowderFillParameters(int layers, int deltaSource, int deltaSink)
{
    // Writes to Z_Stacks, Delta_Source, Delta_Sink, StartSurfaces
    writeInt32Node(m_Node_layersMax, layers);
    writeInt32Node(m_Node_delta_Source, deltaSource);
    writeInt32Node(m_Node_delta_Sink, deltaSink);
    writeBoolNode(m_Node_StartSurfaces, true);
}
```

### Server Side (Missing PLC Interface)

```cpp
// File: OPCUAServer/slm_opcua_server.cpp (line 480)
void SlmOpcUaServer::applyPlcBehavior()
{
    auto guard = m_stateContainer.lock();
    PlcState& state = guard.state();
    
    if (!m_config.simulatePlc) {
        // ================================================================
        // PRODUCTION MODE - NEEDS IMPLEMENTATION
        // ================================================================
        //
        // TODO: Add PLC communication here!
        //
        // Example for Modbus TCP:
        //   m_plc->writeRegister(REG_DELTA_SOURCE, state.Delta_Source);
        //   m_plc->writeRegister(REG_START_FILL, 1);
        //   
        //   // Wait for completion
        //   while (!m_plc->readRegister(REG_FILL_COMPLETE)) {
        //       std::this_thread::sleep_for(std::chrono::milliseconds(100));
        //   }
        //   
        //   // Read actual positions
        //   state.Marcer_Source_Cylinder_ActualPosition = 
        //       m_plc->readRegister(REG_SOURCE_POSITION);
        //
        // ================================================================
        
        log("[PRODUCTION] WARNING: No PLC interface - using stub!");
        
        // Current stub: Just acknowledge commands without hardware control
        if (state.StartSurfaces && !state.MakeSurface_Done) {
            log("[PRODUCTION] Powder fill requested (NO REAL HW CONTROL)");
            state.MakeSurface_Done = UA_TRUE;
        }
    } else {
        // ================================================================
        // SIMULATION MODE - CURRENTLY ACTIVE
        // ================================================================
        
        if (state.StartSurfaces && !state.MakeSurface_Done) {
            log("[SIM] Powder fill initiated (Z_Stacks=" + 
                std::to_string(state.Z_Stacks) + ")");
            
            // Simulate cylinder movements
            for (int32_t i = 0; i < state.Z_Stacks; ++i) {
                state.Marcer_Source_Cylinder_ActualPosition += state.Delta_Source;
                state.Marcer_Sink_Cylinder_ActualPosition += state.Delta_Sink;
            }
            
            state.MakeSurface_Done = UA_TRUE;
            log("[SIM] Powder fill complete -> MakeSurface_Done = TRUE");
        }
    }
}
```

## Handshake Protocol

### Layer-by-Layer Handshake (Bidirectional)

```
?????????????                              ???????????????
?  Scanner  ?                              ?  OPC Server ?
?  (Client) ?                              ?  (Server)   ?
?????????????                              ???????????????
      ?                                           ?
      ? 1. Request layer preparation              ?
      ???????????????????????????????????????????>?
      ?   Write: LaySurface = TRUE                ?
      ?   Write: Step_Source = 50                 ?
      ?   Write: Step_Sink = 50                   ?
      ?                                           ?
      ?                                2. Prepare ?
      ?                     <???????????????????? ?
      ?                     Server moves cylinders?
      ?                     (simulation or real)  ?
      ?                                           ?
      ?                       3. Layer ready       ?
      ? <??????????????????????????????????????????
      ?   Read: LaySurface_Done = TRUE            ?
      ?                                           ?
      ? 4. Execute laser scanning                 ?
      ?   (RTC5 scanner draws layer)              ?
      ?                                           ?
      ? 5. Signal completion                      ?
      ???????????????????????????????????????????>?
      ?   Write: LaySurface = FALSE               ?
      ?                                           ?
      ?                         6. Acknowledge    ?
      ?                     <???????????????????? ?
      ?                     Server resets state   ?
      ?   Read: LaySurface_Done = FALSE           ?
      ?                                           ?
      ? 7. Ready for next layer                   ?
      ?   (loop back to step 1)                   ?
      ?                                           ?
```

## Configuration Files

### Current Configuration

```cpp
// File: OPCUAServer/main.cpp (line 108)
slm_opcua::ServerConfig config;
config.endpointUrl = "opc.tcp://0.0.0.0:4840";
config.namespaceUri = "urn:CODESYS:MaTe_DLMS";
config.namespaceIndex = 2;
config.pollingIntervalMs = 10;
config.enableFailSafe = true;
config.watchdogTimeoutMs = 5000;
config.simulatePlc = true;  // ? THIS IS THE KEY SETTING
config.simLayerPrepTimeMs = 100;
```

### Production Configuration (Required)

```json
// File: OPCUAServer/plc_config.json (TO BE CREATED)
{
    "mode": "production",
    "plc": {
        "type": "modbus_tcp",
        "ip": "192.168.1.10",
        "port": 502,
        "unit_id": 1,
        "connection_timeout_ms": 1000,
        "read_timeout_ms": 500
    },
    "registers": {
        "control": {
            "start_powder_fill": 1000,
            "start_layer_prep": 1001,
            "emergency_stop": 1002
        },
        "parameters": {
            "delta_source": 1100,
            "delta_sink": 1101,
            "step_source": 1102,
            "step_sink": 1103,
            "z_stacks": 1104
        },
        "status": {
            "movement_complete": 2000,
            "powder_fill_done": 2001,
            "layer_prep_done": 2002,
            "emergency_stop_active": 2003
        },
        "feedback": {
            "source_position_encoder": 3000,
            "sink_position_encoder": 3001,
            "source_position_target": 3002,
            "sink_position_target": 3003
        }
    },
    "safety": {
        "max_source_position_um": 200000,
        "max_sink_position_um": 150000,
        "min_position_um": 0,
        "max_step_size_um": 100,
        "watchdog_timeout_ms": 5000,
        "enable_soft_limits": true,
        "enable_hard_limits": true
    },
    "motion": {
        "steps_per_micron": 8.0,
        "max_velocity_um_per_sec": 10000,
        "acceleration_um_per_sec2": 50000,
        "enable_backlash_compensation": true,
        "backlash_um": 2
    }
}
```

## Summary

### What's Working ?
- **Architecture is sound** - Proper separation of concerns
- **OPC UA communication** - Client-server protocol working perfectly
- **Scanner integration** - RTC5 laser control functional
- **Simulation mode** - Complete PLC emulation for testing
- **Safety systems** - Fail-safe controller ready

### What's Missing ?
- **PLC communication layer** - No Modbus/EtherCAT code
- **Motor control interface** - No actual position commands sent
- **Encoder feedback** - No real position readings
- **Hardware safety checks** - No physical limit monitoring
- **Production configuration** - No PLC settings file

### Why It's This Way
The system was designed as a **modular architecture** where:
- **GUI** handles user interaction
- **OPC UA Server** handles protocol bridging
- **PLCs** handle physical control

The **bridge layer** (OPC UA server ? PLC communication) was never completed.

### How to Fix
See: `PRODUCTION_MODE_INTEGRATION_GUIDE.md`

---

**Last Updated:** 2024-02-03  
**Architecture Status:** ? Sound design, ? Incomplete implementation  
**Production Readiness:** Requires PLC interface layer
