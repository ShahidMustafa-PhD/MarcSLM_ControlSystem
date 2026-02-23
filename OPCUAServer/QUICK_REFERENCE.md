# OPC UA Server Quick Reference

## Quick Start

```bash
# Build
cd OPCUAServer/build
cmake .. -DCMAKE_TOOLCHAIN_FILE=path/to/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build . --config Release

# Run (simulation)
./Release/OPCUAServer.exe --simulate

# Run (production)
./Release/OPCUAServer.exe --production
```

---

## Critical Configuration

| Parameter | Value | Why |
|-----------|-------|-----|
| Endpoint | `opc.tcp://localhost:4840` | Matches client |
| Namespace Index | `4` | Actual PLC namespace |
| Namespace URI | `urn:CODESYS:CECC-D` | CoDeSys convention |
| Node ID Prefix | `\|var\|` | CoDeSys export format |

---

## Actual PLC Variables

### MakeSurface
```
ns=4;s=|var|CECC-D.Application.MakeSurface.Z_Stacks                  [INT16]
ns=4;s=|var|CECC-D.Application.MakeSurface.Delta_Source              [INT32]
ns=4;s=|var|CECC-D.Application.MakeSurface.Delta_Sink                [INT32]
ns=4;s=|var|CECC-D.Application.MakeSurface.Layer_Ready               [BOOL]
ns=4;s=|var|CECC-D.Application.MakeSurface.Source_Ready              [BOOL]
ns=4;s=|var|CECC-D.Application.MakeSurface.Surfaces_Control          [INT16]
ns=4;s=|var|CECC-D.Application.MakeSurface.SurfaceStepFlag           [BOOL]
```

### StartUpSequence
```
ns=4;s=|var|CECC-D.Application.StartUpSequence.StartUp               [BOOL]
```

---

## Data Type Mapping

| OPC UA Type | C++ Type | PLC Type |
|-------------|----------|----------|
| INT16 | `UA_Int16` / `int16_t` | INT |
| INT32 | `UA_Int32` / `int32_t` | DINT |
| BOOL | `UA_Boolean` / `bool` | BOOL |

---

## Common Tasks

### Add New Variable

1. **Update `plc_state.h`**:
```cpp
struct PlcState {
    UA_Boolean MyNewVariable = UA_FALSE;
};
```

2. **Update `slm_opcua_server.h`**:
```cpp
struct {
    UA_NodeId MyNewVariable;
} m_nodes;
```

3. **Update `slm_opcua_server.cpp` in `addVariables()`**:
```cpp
success &= addVar(m_nodes.MyNewVariable, 
                  "|var|CECC-D.Application.MyGroup.MyNewVariable",
                  &UA_TYPES[UA_TYPES_BOOLEAN], 
                  &state.MyNewVariable, 
                  true);
```

4. **Update `syncStateToVariables()`**:
```cpp
writeVar(m_nodes.MyNewVariable, &state.MyNewVariable, &UA_TYPES[UA_TYPES_BOOLEAN]);
```

5. **Update `syncVariablesToState()`** (if writable):
```cpp
readVar(m_nodes.MyNewVariable, &state.MyNewVariable, &UA_TYPES[UA_TYPES_BOOLEAN]);
```

---

## Debugging

### Enable Verbose Logging
```bash
OPCUAServer.exe --verbose
```

### Check Namespace Index
Look for this log line:
```
[NAMESPACE] Namespace registered with index: 4
```

### Verify Node IDs with UaExpert
1. Connect to `opc.tcp://localhost:4840`
2. Expand Objects folder
3. Check namespace index is **4**
4. Check node IDs start with `|var|CECC-D.Application...`

### Common Errors

**Error**: `BadNodeIdUnknown`
**Cause**: Client using wrong namespace index or node ID format
**Fix**: Ensure client uses namespace **4** and `|var|` prefix

**Error**: `Type mismatch`
**Cause**: Client expects INT32 but PLC has INT16
**Fix**: Update client to use correct type (or server to auto-convert)

**Error**: `Connection timeout`
**Cause**: Server not running or wrong endpoint
**Fix**: Check server is running on `localhost:4840`

---

## Safety Critical

### Watchdog Timeout
```cpp
config.watchdogTimeoutMs = 5000;  // 5 seconds
```
**Action**: Emergency stop if no updates for 5 seconds

### Position Limits
```cpp
constexpr int32_t CYLINDER_POSITION_MIN = -500000;  // -500mm
constexpr int32_t CYLINDER_POSITION_MAX =  500000;  // +500mm
```
**Action**: Reject writes outside limits

### Emergency Stop
Triggered automatically if:
- PLC watchdog timeout
- Position limits exceeded
- PLC emergency button pressed
- Unhandled exception

---

## Performance Targets

| Metric | Target | Actual |
|--------|--------|--------|
| Polling Rate | 100 Hz | 100 Hz (10ms) |
| Response Time | < 50ms | < 50ms typical |
| Memory Usage | < 20 MB | ~10 MB |
| CPU Usage | < 10% | < 5% |

---

## File Structure

```
OPCUAServer/
??? main.cpp                          # CLI, entry point
??? slm_opcua_server.h/.cpp           # Server core
??? plc_state.h                       # Variable definitions
??? fail_safe_controller.h/.cpp       # Safety system
??? codesys_plc_interface.h/.cpp      # PLC communication
??? DEPLOYMENT_GUIDE.md               # Full deployment guide
??? ENHANCEMENT_SUMMARY.md            # Change summary
??? QUICK_REFERENCE.md                # This file
```

---

## Support Contacts

- **Documentation**: See `DEPLOYMENT_GUIDE.md`
- **Architecture**: See `ENHANCEMENT_SUMMARY.md`
- **Code Issues**: Check server logs first
- **PLC Issues**: Verify PLC connection with `ping 192.168.1.10`

---

**End of Quick Reference**
