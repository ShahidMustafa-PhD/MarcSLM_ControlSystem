# Production Mode Integration Guide

## Problem Summary

Your SLM control system has **three layers**:

```
???????????????????????????????????????????????
?  LAYER 1: GUI Application                   ?
?  File: MarcSLMControlSystem.exe             ?
?  Role: User interface (OPC UA Client)       ?
???????????????????????????????????????????????
                   ? OPC UA Protocol
                   ? (localhost:4840)
                   ?
???????????????????????????????????????????????
?  LAYER 2: OPC UA Server (Middleware)        ?
?  File: OPCUAServer.exe                      ?
?  Role: Protocol bridge                      ?
?  STATUS: ?? RUNNING IN SIMULATION MODE      ?
???????????????????????????????????????????????
                   ? Modbus/EtherCAT/ProfiNet
                   ? (NOT IMPLEMENTED)
                   ?
???????????????????????????????????????????????
?  LAYER 3: Physical PLCs                     ?
?  Type: Beckhoff/CoDeSys/Siemens             ?
?  Role: Motor control & safety               ?
?  STATUS: ? NOT CONNECTED                    ?
???????????????????????????????????????????????
                   ? Motor signals
                   ?
???????????????????????????????????????????????
?  LAYER 4: Stepper Motors                    ?
?  Components: Source & Sink cylinders        ?
?  STATUS: ? NO COMMANDS RECEIVED             ?
???????????????????????????????????????????????
```

## Current Situation

Your OPC UA Server (`OPCUAServer.exe`) is **running in simulation mode** by default:

```cpp
// From OPCUAServer/main.cpp line 132
config.simulatePlc = true;  // Default to simulation for safety
```

This means:
- ? The GUI can connect and send commands
- ? The server responds with text logs (`[SIM] ...`)
- ? **NO REAL HARDWARE IS CONTROLLED**
- ? The source/sink cylinders don't move
- ? Position updates are fake (just internal variables)

## Why This Happened

The production OPC UA server was designed as a **protocol bridge** to sit between:
1. Your high-level control software (GUI)
2. Industrial PLCs that actually drive the motors

However, **the PLC communication layer was never implemented**. The server only has:
- **Simulation mode** - Emulates PLC responses internally
- **Production mode stub** - Just logs warnings (no actual hardware control)

## Solution Options

### Option 1: Quick Fix - Use Simulation Mode (Current State)

**Use Case:** Testing, development, demonstration

**Command:**
```cmd
cd C:\Active_Projects\MarcSLM_ControlSystems\install
OPCUAServer.exe --simulate
```

**Result:**
- GUI works normally
- Server emulates PLC behavior internally
- NO REAL HARDWARE CONTROL
- Safe for testing

### Option 2: Enable Production Mode (Requires PLC Integration)

**Use Case:** Production operation with real hardware

**Step 1: Switch to Production Mode**

```cmd
cd C:\Active_Projects\MarcSLM_ControlSystems\install
OPCUAServer.exe --production
```

Or set environment variable:
```cmd
set OPC_UA_SIMULATE=0
OPCUAServer.exe
```

**Step 2: Implement PLC Communication**

You need to add code in `OPCUAServer/slm_opcua_server.cpp` function `applyPlcBehavior()` to:

1. **Connect to your PLCs** via industrial protocol:
   - **Modbus TCP** (most common for CoDeSys)
   - **EtherCAT** (high-performance motion control)
   - **Profinet** (Siemens)
   - **ADS/TwinCAT** (Beckhoff)

2. **Write position commands** to motor controllers:
   ```cpp
   // Example for Modbus TCP
   modbusWrite(PLC_IP, REGISTER_STEP_SOURCE, state.Step_Source);
   modbusWrite(PLC_IP, REGISTER_STEP_SINK, state.Step_Sink);
   ```

3. **Read actual positions** from encoders:
   ```cpp
   state.Marcer_Source_Cylinder_ActualPosition = modbusRead(PLC_IP, REGISTER_SOURCE_POS);
   state.Marcer_Sink_Cylinder_ActualPosition = modbusRead(PLC_IP, REGISTER_SINK_POS);
   ```

4. **Monitor safety limits** and interlocks:
   ```cpp
   if (position > MAX_SAFE_POSITION) {
       m_failSafe->triggerEmergencyStop("Position out of bounds");
   }
   ```

## Detailed PLC Integration Guide

### For Modbus TCP (CoDeSys/Generic PLCs)

**1. Install Modbus Library**
```sh
vcpkg install libmodbus
```

**2. Add to CMakeLists.txt**
```cmake
find_package(libmodbus CONFIG REQUIRED)
target_link_libraries(OPCUAServer PRIVATE modbus)
```

**3. Implement in slm_opcua_server.cpp**
```cpp
#include <modbus/modbus.h>

class ModbusPlcInterface {
public:
    bool connect(const std::string& ip, int port = 502) {
        m_ctx = modbus_new_tcp(ip.c_str(), port);
        if (!m_ctx) return false;
        return modbus_connect(m_ctx) == 0;
    }
    
    bool writeRegister(int address, uint16_t value) {
        return modbus_write_register(m_ctx, address, value) == 1;
    }
    
    uint16_t readRegister(int address) {
        uint16_t value;
        modbus_read_registers(m_ctx, address, 1, &value);
        return value;
    }
    
private:
    modbus_t* m_ctx = nullptr;
};
```

**4. Integrate into Server**
```cpp
// In applyPlcBehavior() production mode:
if (state.LaySurface && !state.PreparingLayer) {
    // Send position commands to PLC
    m_plc->writeRegister(REG_STEP_SOURCE, state.Step_Source);
    m_plc->writeRegister(REG_STEP_SINK, state.Step_Sink);
    m_plc->writeRegister(REG_START_MOVE, 1);
    
    state.PreparingLayer = UA_TRUE;
}

// Poll for movement complete
if (state.PreparingLayer) {
    uint16_t moveComplete = m_plc->readRegister(REG_MOVE_COMPLETE);
    if (moveComplete) {
        // Read actual positions
        state.Marcer_Source_Cylinder_ActualPosition = 
            m_plc->readRegister(REG_SOURCE_POS);
        state.Marcer_Sink_Cylinder_ActualPosition = 
            m_plc->readRegister(REG_SINK_POS);
        
        state.LaySurface_Done = UA_TRUE;
        log("[PRODUCTION] Layer preparation complete");
    }
}
```

### For EtherCAT (High-Performance Motion)

**1. Install SOEM Library**
```sh
git clone https://github.com/OpenEtherCATsociety/SOEM.git
```

**2. Configure EtherCAT Network**
```cpp
// Initialize EtherCAT master
ec_init("eth0");  // Your network interface

// Configure servo drives
ec_config_map(&IOmap);
ec_configdc();

// Start cyclic operation
ec_send_processdata();
ec_receive_processdata(EC_TIMEOUTRET);
```

**3. Send Position Commands**
```cpp
// Write target position to EtherCAT slave
int32_t targetPos = state.Step_Source;
memcpy(&IOmap[SERVO_SOURCE_OUTPUT], &targetPos, sizeof(int32_t));
ec_send_processdata();

// Read actual position
int32_t actualPos;
memcpy(&actualPos, &IOmap[SERVO_SOURCE_INPUT], sizeof(int32_t));
state.Marcer_Source_Cylinder_ActualPosition = actualPos;
```

## Configuration

### PLC Network Settings

Create a configuration file: `OPCUAServer/plc_config.json`

```json
{
    "plc_type": "modbus_tcp",
    "plc_ip": "192.168.1.10",
    "plc_port": 502,
    "registers": {
        "step_source": 1000,
        "step_sink": 1001,
        "start_move": 1002,
        "move_complete": 1003,
        "source_position": 2000,
        "sink_position": 2001,
        "emergency_stop": 3000
    },
    "safety": {
        "max_source_position": 200000,
        "max_sink_position": 150000,
        "min_position": 0,
        "watchdog_timeout_ms": 5000
    }
}
```

## Testing Procedure

### Phase 1: Simulation Mode Testing
```cmd
# Test GUI with simulator (safe, no hardware)
OPCUAServer.exe --simulate
MarcSLMControlSystem.exe
```

**Expected:** GUI works, logs show `[SIM]`, no real movement

### Phase 2: Production Mode Without PLC
```cmd
# Test production mode logic (no PLC connected)
OPCUAServer.exe --production
```

**Expected:** Server logs warnings about missing PLC interface

### Phase 3: Production Mode With PLC
```cmd
# Test with real PLC (DANGER: REAL HARDWARE WILL MOVE)
OPCUAServer.exe --production --endpoint opc.tcp://0.0.0.0:4840
```

**Safety Checklist:**
- [ ] Emergency stop button functional
- [ ] Safety interlocks tested
- [ ] Movement limits configured
- [ ] Fail-safe controller enabled
- [ ] All personnel clear of machine
- [ ] Test with reduced speeds first

### Phase 4: Full System Integration
```cmd
# Production operation
set OPC_UA_SIMULATE=0
OPCUAServer.exe
MarcSLMControlSystem.exe
```

## Current Status and Next Steps

### ? What's Working Now
- OPC UA server starts successfully
- GUI connects to server
- Commands are sent and logged
- Namespace and variables are correct
- Simulation mode works perfectly

### ? What's Missing
1. **PLC communication layer** - No Modbus/EtherCAT/Profinet code
2. **Motor controller interface** - No actual position commands sent
3. **Encoder feedback** - No real position readings
4. **Safety interlocks** - No hardware limit checks
5. **Production configuration** - No PLC IP/registers defined

### ?? Immediate Actions Required

**FOR TESTING (Safe):**
```cmd
# Continue using simulation mode
OPCUAServer.exe --simulate
```

**FOR PRODUCTION (Requires Implementation):**
1. Identify your PLC hardware (brand/model)
2. Get PLC programming manual and Modbus register map
3. Implement PLC communication layer
4. Configure safety limits
5. Test with machine in manual mode
6. Gradually enable automated control

## Support and Documentation

### Related Files
- `OPCUAServer/slm_opcua_server.cpp` - Server implementation (line 480: `applyPlcBehavior()`)
- `OPCUAServer/main.cpp` - Entry point and command-line parsing
- `OPCUAServer/plc_state.h` - State structure definition
- `OPCUAServer/fail_safe_controller.h` - Safety controller

### Useful Resources
- **Modbus Protocol:** https://www.modbus.org/
- **EtherCAT:** https://www.ethercat.org/
- **CoDeSys OPC UA:** https://help.codesys.com/
- **libmodbus Documentation:** https://libmodbus.org/
- **SOEM (EtherCAT):** https://github.com/OpenEtherCATsociety/SOEM

### Getting Help
When asking for help, provide:
1. PLC brand and model number
2. Communication protocol used (Modbus/EtherCAT/other)
3. Network configuration (IP addresses, registers)
4. Error messages from server logs
5. Whether machine moves in manual PLC mode

## Conclusion

Your system is **architecturally correct** but **incomplete**. The OPC UA server is a well-designed middleware component, but it needs the **PLC communication layer** to control real hardware.

**Current recommendation:** Continue using **simulation mode** until the PLC interface is implemented and thoroughly tested.

---

**Last Updated:** 2024-02-03  
**Status:** Production PLC interface required for hardware control  
**Risk Level:** ?? High (will not move real hardware without implementation)
