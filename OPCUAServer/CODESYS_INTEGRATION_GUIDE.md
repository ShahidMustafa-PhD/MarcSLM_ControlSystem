# CoDeSys PLC Integration Guide

## Overview

This document describes how to integrate the MarcSLM Control System with **CoDeSys PLCs** using **Modbus TCP** communication. The implementation is **industrial-grade**, designed for **24/7 operation** in production SLM machines.

---

## Table of Contents

1. [Prerequisites](#prerequisites)
2. [Hardware Setup](#hardware-setup)
3. [PLC Configuration (CoDeSys)](#plc-configuration-codesys)
4. [Software Installation](#software-installation)
5. [OPC UA Server Integration](#opc-ua-server-integration)
6. [Testing and Validation](#testing-and-validation)
7. [Troubleshooting](#troubleshooting)
8. [Safety Considerations](#safety-considerations)

---

## Prerequisites

### Hardware Requirements

- **PLC**: CoDeSys-compatible PLC (e.g., Wago PFC200, Beckhoff CX, CODESYS Control Win)
- **Network**: Ethernet connection between PC and PLC (direct or via switch)
- **Stepper Motors**: Connected to PLC digital outputs (Step/Direction)
- **Sensors**: Limit switches, emergency stop, powder level sensors

### Software Requirements

- **Windows 10/11** (x64)
- **Visual Studio 2019+** with C++ Desktop Development
- **CMake 3.16+**
- **vcpkg** (for dependency management)
- **CoDeSys Development System** (for PLC programming)

### Network Requirements

- **IP Address**: Static IP for PLC (e.g., `192.168.1.10`)
- **Subnet**: Same subnet as PC (e.g., `255.255.255.0`)
- **Firewall**: Allow Modbus TCP port `502`

---

## Hardware Setup

### 1. Network Configuration

**Configure PLC IP Address:**

```
PLC IP:      192.168.1.10
Subnet Mask: 255.255.255.0
Gateway:     192.168.1.1 (if needed)
```

**Configure PC Network Adapter:**

```
PC IP:       192.168.1.100
Subnet Mask: 255.255.255.0
Gateway:     192.168.1.1 (optional)
```

**Test Connectivity:**

```bash
ping 192.168.1.10
```

### 2. I/O Wiring

**Digital Outputs (Stepper Motor Control):**

- `DO0`: Source Cylinder Step
- `DO1`: Source Cylinder Direction
- `DO2`: Source Cylinder Enable
- `DO3`: Sink Cylinder Step
- `DO4`: Sink Cylinder Direction
- `DO5`: Sink Cylinder Enable
- `DO6`: Recoater Motor Enable
- `DO7`: Powder Dispenser Valve

**Digital Inputs (Sensors):**

- `DI0`: Source Cylinder Home Switch
- `DI1`: Source Cylinder Top Limit
- `DI2`: Sink Cylinder Home Switch
- `DI3`: Sink Cylinder Top Limit
- `DI4`: Emergency Stop (NC - Normally Closed)
- `DI5`: Recoater Home Position
- `DI6`: Powder Level Low
- `DI7`: Door Interlock

**Analog Inputs (Optional):**

- `AI0`: Source Cylinder Position Encoder (0-10V)
- `AI1`: Sink Cylinder Position Encoder (0-10V)

---

## PLC Configuration (CoDeSys)

### 1. Create New Project

1. Open **CoDeSys Development System**
2. Create new project: **File ? New Project**
3. Select your PLC type (e.g., Wago PFC200)
4. Select template: **Standard Project**

### 2. Configure Modbus TCP Server

**Device Tree:**

```
Device (PLC)
??? Ethernet
?   ??? Modbus TCP Slave
?       ??? Address: 1 (Unit ID)
?       ??? Port: 502
??? Application
    ??? PLC_PRG (ST - Structured Text)
```

**Add Modbus TCP Slave:**

1. Right-click **Ethernet** ? **Add Device**
2. Select **Modbus ? Modbus TCP Slave**
3. Configure:
   - **Unit ID**: `1`
   - **Port**: `502`
   - **Connection Timeout**: `1000 ms`

### 3. Define Global Variables

**File: `GVL_Modbus` (Global Variable List)**

```iecst
(* ==================================================================== *)
(* Modbus Input Registers (Read by OPC UA Server) - Address 30001+    *)
(* ==================================================================== *)
VAR_GLOBAL
    (* Position Feedback - INT32 = 2 registers each *)
    MB_IN_SourcePosition_Hi AT %MW0 : WORD;  (* 30001 *)
    MB_IN_SourcePosition_Lo AT %MW1 : WORD;  (* 30002 *)
    MB_IN_SinkPosition_Hi   AT %MW2 : WORD;  (* 30003 *)
    MB_IN_SinkPosition_Lo   AT %MW3 : WORD;  (* 30004 *)
    
    (* Status Flags - BOOL = 1 register each *)
    MB_IN_MovementComplete  AT %MW4 : WORD;  (* 30005 *)
    MB_IN_PowderFillDone    AT %MW5 : WORD;  (* 30006 *)
    MB_IN_LayerPrepDone     AT %MW6 : WORD;  (* 30007 *)
    MB_IN_StartupDone       AT %MW7 : WORD;  (* 30008 *)
    MB_IN_EmergencyStop     AT %MW8 : WORD;  (* 30009 *)
    
    (* Diagnostics *)
    MB_IN_PlcHeartbeat      AT %MW9  : WORD;  (* 30010 *)
    MB_IN_PlcErrorCode      AT %MW10 : WORD;  (* 30011 *)
    MB_IN_SourceLimitSwitch AT %MW11 : WORD;  (* 30012 *)
    MB_IN_SinkLimitSwitch   AT %MW12 : WORD;  (* 30013 *)
END_VAR

(* ==================================================================== *)
(* Modbus Holding Registers (Written by OPC UA Server) - Address 40001+ *)
(* ==================================================================== *)
VAR_GLOBAL
    (* Command Flags *)
    MB_HOLD_StartPowderFill AT %MW100 : WORD;  (* 40001 *)
    MB_HOLD_StartLayerPrep  AT %MW101 : WORD;  (* 40002 *)
    MB_HOLD_StartStartup    AT %MW102 : WORD;  (* 40003 *)
    MB_HOLD_EmergencyStop   AT %MW103 : WORD;  (* 40004 *)
    
    (* Motion Parameters - INT32 = 2 registers each *)
    MB_HOLD_DeltaSource_Hi  AT %MW104 : WORD;  (* 40005 *)
    MB_HOLD_DeltaSource_Lo  AT %MW105 : WORD;  (* 40006 *)
    MB_HOLD_DeltaSink_Hi    AT %MW106 : WORD;  (* 40007 *)
    MB_HOLD_DeltaSink_Lo    AT %MW107 : WORD;  (* 40008 *)
    MB_HOLD_StepSource_Hi   AT %MW108 : WORD;  (* 40009 *)
    MB_HOLD_StepSource_Lo   AT %MW109 : WORD;  (* 40010 *)
    MB_HOLD_StepSink_Hi     AT %MW110 : WORD;  (* 40011 *)
    MB_HOLD_StepSink_Lo     AT %MW111 : WORD;  (* 40012 *)
    MB_HOLD_ZStacks_Hi      AT %MW112 : WORD;  (* 40013 *)
    MB_HOLD_ZStacks_Lo      AT %MW113 : WORD;  (* 40014 *)
    
    (* Control *)
    MB_HOLD_ResetCommands   AT %MW114 : WORD;  (* 40015 *)
    MB_HOLD_ClientHeartbeat AT %MW115 : WORD;  (* 40016 *)
END_VAR

(* ==================================================================== *)
(* Internal Process Variables (Not exposed via Modbus)                *)
(* ==================================================================== *)
VAR_GLOBAL
    (* Actual positions in microns *)
    SourcePosition : DINT := 0;
    SinkPosition   : DINT := 0;
    
    (* Target positions *)
    SourceTarget : DINT := 0;
    SinkTarget   : DINT := 0;
    
    (* State machine *)
    CurrentState : INT := 0;  (* 0=Idle, 1=Startup, 2=PowderFill, 3=LayerPrep *)
    
    (* Safety *)
    EmergencyStopActive : BOOL := FALSE;
    SourceLimitHit : BOOL := FALSE;
    SinkLimitHit : BOOL := FALSE;
    
    (* Diagnostics *)
    PlcHeartbeat : WORD := 0;
    ErrorCode : WORD := 0;
END_VAR
```

### 4. Implement PLC Logic

**File: `PLC_PRG` (Main Program)**

```iecst
PROGRAM PLC_PRG
VAR
    (* State machine *)
    State : INT := 0;
    
    (* Edge detection for commands *)
    StartPowderFill_Edge : R_TRIG;
    StartLayerPrep_Edge  : R_TRIG;
    StartStartup_Edge    : R_TRIG;
    
    (* Motion control *)
    StepSource_FB : FB_StepperMotor;
    StepSink_FB   : FB_StepperMotor;
    
    (* Timers *)
    MovementTimer : TON;
    
    (* Temporary variables *)
    DeltaSource_INT32 : DINT;
    DeltaSink_INT32 : DINT;
    StepSource_INT32 : DINT;
    StepSink_INT32 : DINT;
    ZStacks_INT32 : DINT;
END_VAR

(* ======================================================================= *)
(* SAFETY: Emergency Stop Check (Highest Priority)                         *)
(* ======================================================================= *)
EmergencyStopActive := NOT %IX4.0 OR (MB_HOLD_EmergencyStop <> 0);

IF EmergencyStopActive THEN
    (* Immediately disable all motors *)
    %QX0.2 := FALSE;  (* Source enable *)
    %QX0.5 := FALSE;  (* Sink enable *)
    %QX0.6 := FALSE;  (* Recoater enable *)
    
    (* Reset state machine *)
    State := 0;
    MB_IN_EmergencyStop := 1;
    ErrorCode := 1;
    
    (* Clear all commands *)
    MB_IN_MovementComplete := 0;
    MB_IN_PowderFillDone := 0;
    MB_IN_LayerPrepDone := 0;
    MB_IN_StartupDone := 0;
    
    RETURN;  (* Exit immediately *)
ELSE
    MB_IN_EmergencyStop := 0;
END_IF

(* ======================================================================= *)
(* Heartbeat & Watchdog                                                    *)
(* ======================================================================= *)
PlcHeartbeat := PlcHeartbeat + 1;
MB_IN_PlcHeartbeat := PlcHeartbeat;

(* Check client heartbeat (optional watchdog) *)
(* If MB_HOLD_ClientHeartbeat stops incrementing, trigger safe state *)

(* ======================================================================= *)
(* Read Limit Switches                                                     *)
(* ======================================================================= *)
SourceLimitHit := %IX0.0 OR %IX0.1;  (* Home or top limit *)
SinkLimitHit   := %IX0.2 OR %IX0.3;
MB_IN_SourceLimitSwitch := BOOL_TO_WORD(SourceLimitHit);
MB_IN_SinkLimitSwitch := BOOL_TO_WORD(SinkLimitHit);

(* ======================================================================= *)
(* Update Position Feedback (Modbus Input Registers)                      *)
(* ======================================================================= *)
MB_IN_SourcePosition_Hi := DINT_TO_WORD(SHR(SourcePosition, 16));
MB_IN_SourcePosition_Lo := DINT_TO_WORD(SourcePosition AND 16#FFFF);
MB_IN_SinkPosition_Hi := DINT_TO_WORD(SHR(SinkPosition, 16));
MB_IN_SinkPosition_Lo := DINT_TO_WORD(SinkPosition AND 16#FFFF);

(* ======================================================================= *)
(* Edge Detection for Commands                                             *)
(* ======================================================================= *)
StartPowderFill_Edge(CLK := (MB_HOLD_StartPowderFill <> 0));
StartLayerPrep_Edge(CLK := (MB_HOLD_StartLayerPrep <> 0));
StartStartup_Edge(CLK := (MB_HOLD_StartStartup <> 0));

(* ======================================================================= *)
(* State Machine                                                           *)
(* ======================================================================= *)
CASE State OF
    
    (* ------------------------------------------------------------------- *)
    0: (* IDLE - Waiting for commands *)
    (* ------------------------------------------------------------------- *)
        MB_IN_MovementComplete := 1;
        
        (* Check for startup command *)
        IF StartStartup_Edge.Q THEN
            State := 1;
            MB_IN_StartupDone := 0;
            ErrorCode := 0;
        END_IF
        
        (* Check for powder fill command *)
        IF StartPowderFill_Edge.Q THEN
            (* Parse INT32 parameters *)
            DeltaSource_INT32 := WORD_TO_DINT(SHL(MB_HOLD_DeltaSource_Hi, 16)) OR 
                                 WORD_TO_DINT(MB_HOLD_DeltaSource_Lo);
            DeltaSink_INT32 := WORD_TO_DINT(SHL(MB_HOLD_DeltaSink_Hi, 16)) OR 
                               WORD_TO_DINT(MB_HOLD_DeltaSink_Lo);
            ZStacks_INT32 := WORD_TO_DINT(SHL(MB_HOLD_ZStacks_Hi, 16)) OR 
                             WORD_TO_DINT(MB_HOLD_ZStacks_Lo);
            
            (* Calculate target positions *)
            SourceTarget := SourcePosition + (DeltaSource_INT32 * ZStacks_INT32);
            SinkTarget := SinkPosition + (DeltaSink_INT32 * ZStacks_INT32);
            
            State := 2;
            MB_IN_PowderFillDone := 0;
            MB_IN_MovementComplete := 0;
        END_IF
        
        (* Check for layer prep command *)
        IF StartLayerPrep_Edge.Q THEN
            StepSource_INT32 := WORD_TO_DINT(SHL(MB_HOLD_StepSource_Hi, 16)) OR 
                                WORD_TO_DINT(MB_HOLD_StepSource_Lo);
            StepSink_INT32 := WORD_TO_DINT(SHL(MB_HOLD_StepSink_Hi, 16)) OR 
                              WORD_TO_DINT(MB_HOLD_StepSink_Lo);
            
            SourceTarget := SourcePosition + StepSource_INT32;
            SinkTarget := SinkPosition + StepSink_INT32;
            
            State := 3;
            MB_IN_LayerPrepDone := 0;
            MB_IN_MovementComplete := 0;
        END_IF
    
    (* ------------------------------------------------------------------- *)
    1: (* STARTUP SEQUENCE *)
    (* ------------------------------------------------------------------- *)
        (* Home source cylinder *)
        IF NOT %IX0.0 THEN  (* Not at home *)
            StepSource_FB(
                Enable := TRUE,
                Direction := FALSE,  (* Move down *)
                Speed := 1000,
                Position => SourcePosition
            );
        END_IF
        
        (* Home sink cylinder *)
        IF NOT %IX0.2 THEN
            StepSink_FB(
                Enable := TRUE,
                Direction := FALSE,
                Speed := 1000,
                Position => SinkPosition
            );
        END_IF
        
        (* Check if both homed *)
        IF %IX0.0 AND %IX0.2 THEN
            SourcePosition := 0;  (* Reset position counters *)
            SinkPosition := 0;
            MB_IN_StartupDone := 1;
            State := 0;
        END_IF
    
    (* ------------------------------------------------------------------- *)
    2: (* POWDER FILL *)
    (* ------------------------------------------------------------------- *)
        (* Move cylinders to create powder bed *)
        StepSource_FB(
            Enable := TRUE,
            Direction := (SourceTarget > SourcePosition),
            Speed := 2000,
            Target := SourceTarget,
            Position => SourcePosition
        );
        
        StepSink_FB(
            Enable := TRUE,
            Direction := (SinkTarget > SinkPosition),
            Speed := 2000,
            Target := SinkTarget,
            Position => SinkPosition
        );
        
        (* Check if movement complete *)
        IF (ABS(SourcePosition - SourceTarget) < 10) AND 
           (ABS(SinkPosition - SinkTarget) < 10) THEN
            MB_IN_PowderFillDone := 1;
            MB_IN_MovementComplete := 1;
            State := 0;
        END_IF
    
    (* ------------------------------------------------------------------- *)
    3: (* LAYER PREPARATION *)
    (* ------------------------------------------------------------------- *)
        (* Move cylinders one layer *)
        StepSource_FB(
            Enable := TRUE,
            Direction := (SourceTarget > SourcePosition),
            Speed := 1500,
            Target := SourceTarget,
            Position => SourcePosition
        );
        
        StepSink_FB(
            Enable := TRUE,
            Direction := (SinkTarget > SinkPosition),
            Speed := 1500,
            Target := SinkTarget,
            Position => SinkPosition
        );
        
        (* Check if cylinders in position *)
        IF (ABS(SourcePosition - SourceTarget) < 10) AND 
           (ABS(SinkPosition - SinkTarget) < 10) THEN
            
            (* Activate recoater *)
            %QX0.6 := TRUE;  (* Recoater enable *)
            
            (* Wait for recoater to complete *)
            MovementTimer(IN := TRUE, PT := T#5S);
            
            IF MovementTimer.Q THEN
                %QX0.6 := FALSE;  (* Disable recoater *)
                MovementTimer(IN := FALSE);
                MB_IN_LayerPrepDone := 1;
                MB_IN_MovementComplete := 1;
                State := 0;
            END_IF
        END_IF
    
END_CASE

(* ======================================================================= *)
(* Update Error Code                                                       *)
(* ======================================================================= *)
MB_IN_PlcErrorCode := ErrorCode;

(* ======================================================================= *)
(* Command Reset Handling                                                  *)
(* ======================================================================= *)
IF MB_HOLD_ResetCommands <> 0 THEN
    MB_HOLD_StartPowderFill := 0;
    MB_HOLD_StartLayerPrep := 0;
    MB_HOLD_StartStartup := 0;
    MB_HOLD_ResetCommands := 0;
END_IF
```

### 5. Configure Modbus Mapping

**In CoDeSys Device Configuration:**

1. Select **Modbus TCP Slave**
2. Right-click ? **Add Channel**
3. Select **Input Registers** (30001-30999)
4. Map to `%MW0` - `%MW12` (13 registers)
5. Right-click ? **Add Channel**
6. Select **Holding Registers** (40001-49999)
7. Map to `%MW100` - `%MW115` (16 registers)

### 6. Build and Download

1. **Build** ? **Build Project** (F11)
2. **Online** ? **Login** (Alt+F8)
3. **Online** ? **Run** (F5)
4. Verify PLC is in **RUN** mode

---

## Software Installation

### 1. Install vcpkg (if not already installed)

```bash
cd C:\
git clone https://github.com/microsoft/vcpkg
cd vcpkg
.\bootstrap-vcpkg.bat
```

### 2. Install libmodbus

```bash
cd C:\vcpkg
.\vcpkg install libmodbus:x64-windows
.\vcpkg integrate install
```

### 3. Build OPC UA Server

```bash
cd C:\Active_Projects\MarcSLM_ControlSystems
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --target OPCUAServer --config Release
```

---

## OPC UA Server Integration

### Example: Integrating CoDeSys Interface into OPC UA Server

**File: `OPCUAServer/main.cpp`**

```cpp
#include "slm_opcua_server.h"
#include "codesys_plc_interface.h"
#include <iostream>
#include <thread>
#include <atomic>
#include <csignal>

std::atomic<bool> g_running{true};

void signalHandler(int signal) {
    std::cout << "\nShutdown signal received (" << signal << ")\n";
    g_running = false;
}

int main() {
    // Install signal handlers
    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);
    
    std::cout << "========================================\n";
    std::cout << "  SLM OPC UA Server with CoDeSys PLC\n";
    std::cout << "========================================\n\n";
    
    // ========================================================================
    // Configure CoDeSys PLC
    // ========================================================================
    
    slm_opcua::PlcConfig plcConfig;
    plcConfig.plcIpAddress = "192.168.1.10";
    plcConfig.plcPort = 502;
    plcConfig.modbusUnitId = 1;
    plcConfig.connectionTimeoutMs = 1000;
    plcConfig.responseTimeoutMs = 500;
    plcConfig.watchdogTimeoutMs = 5000;
    plcConfig.enableWatchdog = true;
    
    // Safety limits (microns)
    plcConfig.maxSourcePosition = 200000;  // 200mm
    plcConfig.maxSinkPosition = 150000;    // 150mm
    plcConfig.maxStepSize = 1000;          // 1mm
    
    // Logging callback
    plcConfig.logCallback = [](const std::string& msg) {
        std::cout << msg << std::endl;
    };
    
    // Create PLC interface
    auto plcInterface = std::make_unique<slm_opcua::CodesysPlcInterface>(plcConfig);
    
    // ========================================================================
    // Connect to PLC
    // ========================================================================
    
    std::cout << "Connecting to CoDeSys PLC...\n";
    if (!plcInterface->connect()) {
        std::cerr << "ERROR: Failed to connect to PLC!\n";
        std::cerr << "  Check:\n";
        std::cerr << "    - PLC is powered on and in RUN mode\n";
        std::cerr << "    - IP address: " << plcConfig.plcIpAddress << "\n";
        std::cerr << "    - Network cable connected\n";
        std::cerr << "    - Firewall allows port 502\n";
        return 1;
    }
    std::cout << "Successfully connected to PLC!\n\n";
    
    // ========================================================================
    // Create OPC UA Server
    // ========================================================================
    
    auto opcServer = std::make_unique<slm_opcua::SlmOpcuaServer>(4840);
    
    if (!opcServer->start()) {
        std::cerr << "ERROR: Failed to start OPC UA server!\n";
        return 1;
    }
    std::cout << "OPC UA Server started on port 4840\n";
    std::cout << "  Endpoint: opc.tcp://localhost:4840\n\n";
    
    // ========================================================================
    // Main Loop: Update OPC UA from PLC
    // ========================================================================
    
    std::thread watchdogThread([&]() {
        while (g_running) {
            plcInterface->updateWatchdog();
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }
    });
    
    std::cout << "Server running. Press Ctrl+C to stop.\n\n";
    
    while (g_running) {
        // Read PLC status
        slm_opcua::PlcStatus plcStatus;
        if (plcInterface->readStatus(plcStatus)) {
            // Update OPC UA server variables
            opcServer->updateSourcePosition(plcStatus.sourcePositionActual);
            opcServer->updateSinkPosition(plcStatus.sinkPositionActual);
            opcServer->updatePowderFillStatus(plcStatus.powderFillDone);
            opcServer->updateLayerPrepStatus(plcStatus.layerPrepDone);
            opcServer->updateStartupStatus(plcStatus.startupDone);
            opcServer->updateEmergencyStop(plcStatus.emergencyStopActive);
            
            // Check watchdog
            if (plcInterface->isWatchdogExpired()) {
                std::cerr << "WARNING: PLC watchdog expired!\n";
                opcServer->updateCommunicationError(true);
            } else {
                opcServer->updateCommunicationError(false);
            }
        } else {
            std::cerr << "ERROR: Failed to read PLC status\n";
            opcServer->updateCommunicationError(true);
            
            // Attempt reconnection
            if (!plcInterface->isConnected()) {
                std::cout << "Attempting to reconnect to PLC...\n";
                plcInterface->reconnect();
            }
        }
        
        // Process OPC UA requests
        opcServer->iterate(100);
        
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    
    // ========================================================================
    // Shutdown
    // ========================================================================
    
    std::cout << "\nShutting down...\n";
    
    watchdogThread.join();
    
    opcServer->stop();
    plcInterface->disconnect();
    
    std::cout << "Server stopped successfully.\n";
    
    return 0;
}
```

---

## Testing and Validation

### 1. Test Modbus Communication (Before Integration)

Use **Modbus Poll** (Windows) to test PLC communication:

1. Download: https://www.modbustools.com/modbus_poll.html
2. Configure connection:
   - **Connection**: Modbus TCP/IP
   - **IP Address**: `192.168.1.10`
   - **Port**: `502`
   - **Modbus ID**: `1`
3. Read input registers `30001-30013`
4. Write holding register `40016` (heartbeat) - verify PLC reads it

### 2. Test Emergency Stop

```cpp
// In your test program
plcInterface->triggerEmergencyStop();

// Verify:
// - PLC immediately stops all motors
// - OPC UA server shows emergency stop active
// - All motion commands are rejected
```

### 3. Test Position Limits

```cpp
// Attempt to exceed limits (should be rejected)
bool result = plcInterface->startLayerPreparation(
    1000000,  // 1 meter - exceeds limit!
    -50
);

// Verify result == false and error logged
```

### 4. Test Watchdog

```cpp
// Simulate communication loss
// 1. Disconnect PLC network cable
// 2. Wait 5 seconds (watchdog timeout)
// 3. Verify isWatchdogExpired() returns true
// 4. Reconnect cable
// 5. Verify automatic reconnection
```

---

## Troubleshooting

### Connection Failed

**Problem**: `Failed to connect to PLC`

**Solutions**:
1. Verify PLC IP: `ping 192.168.1.10`
2. Check PLC is in RUN mode (CoDeSys online view)
3. Verify firewall allows port 502
4. Check Modbus TCP Slave is enabled in PLC configuration

### Communication Timeout

**Problem**: `Watchdog expired` or `Failed to read PLC status`

**Solutions**:
1. Check network latency: `ping -t 192.168.1.10`
2. Increase timeouts in `PlcConfig`
3. Reduce OPC UA update frequency (increase sleep time)
4. Check PLC CPU load (CoDeSys task monitoring)

### Position Not Updating

**Problem**: Position registers always return 0

**Solutions**:
1. Verify Modbus mapping in CoDeSys (Device ? Modbus TCP Slave ? Channels)
2. Check `%MW0` - `%MW12` are mapped to Input Registers 30001-30013
3. Verify PLC program is writing to `MB_IN_SourcePosition_Hi/Lo`
4. Use Modbus Poll to directly read registers

### Commands Not Executed

**Problem**: PLC doesn't respond to commands

**Solutions**:
1. Verify Holding Registers `40001+` are mapped to `%MW100+`
2. Check edge detection logic in PLC (R_TRIG function blocks)
3. Verify no E-Stop active
4. Check PLC state machine is in correct state

---

## Safety Considerations

### Critical Safety Features

1. **Emergency Stop Priority**
   - Hardware E-Stop has highest priority (NC circuit)
   - Software E-Stop via Modbus is backup
   - All motors immediately disabled on E-Stop

2. **Watchdog Monitoring**
   - Bidirectional heartbeat (OPC ? PLC)
   - 5-second timeout triggers safe state
   - Automatic reconnection attempts

3. **Position Limits**
   - Soft limits validated in OPC UA server
   - Hard limits enforced by PLC limit switches
   - Commands exceeding limits are rejected

4. **Fail-Safe Defaults**
   - Communication loss ? Assume unsafe state
   - Timeout ? Stop all motion
   - Invalid data ? Reject command

5. **Equipment Protection**
   - Maximum step size enforced (prevents overtravel)
   - Collision detection via limit switches
   - Motor current monitoring (if available)

### Safety Checklist

- [ ] Emergency stop circuit tested and functional
- [ ] Limit switches installed and tested
- [ ] Watchdog timeout configured (? 5 seconds)
- [ ] Position limits configured correctly
- [ ] Interlock logic implemented (door, powder level)
- [ ] Operator training completed
- [ ] Emergency procedures documented

---

## Production Deployment

### Pre-Deployment Checklist

- [ ] All safety features tested
- [ ] PLC program validated in simulation
- [ ] Communication tested for 24+ hours
- [ ] Backup PLC program saved
- [ ] OPC UA server logs configured
- [ ] Monitoring and alerting set up
- [ ] Maintenance procedures documented

### Monitoring

**Key Metrics to Monitor:**

- PLC heartbeat (should increment continuously)
- Communication success rate (> 99.9%)
- Watchdog timeout events (should be 0)
- Position deviation (actual vs. target)
- Emergency stop activations
- Error codes from PLC

**Logging Configuration:**

```cpp
plcConfig.logCallback = [](const std::string& msg) {
    // Log to file with timestamp
    std::ofstream logFile("plc_communication.log", std::ios::app);
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    logFile << std::put_time(std::localtime(&time), "%Y-%m-%d %H:%M:%S") 
            << " - " << msg << std::endl;
};
```

---

## Support

For technical support:

- **GitHub Issues**: https://github.com/ShahidMustafa-PhD/MarcSLM_ControlSystem/issues
- **Documentation**: See `docs/` folder
- **CoDeSys Forum**: https://forum.codesys.com/

---

## License

This integration guide is part of the MarcSLM Control System.

**Industrial Use**: This software is designed for industrial machinery control. Ensure compliance with:
- **IEC 61508** (Functional Safety)
- **IEC 61131** (PLC Programming)
- **CE Marking** (if applicable)
- **Local safety regulations**

---

**Last Updated**: 2024
**Version**: 1.0.0
