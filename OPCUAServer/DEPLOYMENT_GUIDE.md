# OPC UA Server Deployment Guide

## Overview

This industrial-quality OPC UA server implements the interface between the MarcSLM Control System and CoDeSys PLC. The server is built using the open62541 library and follows industrial safety standards.

---

## Architecture

```
???????????????????????
?  MarcSLM Client     ?  (opcserverua.cpp)
?  (OPC UA Client)    ?
???????????????????????
           ? OPC UA Protocol (ns=4)
           ? opc.tcp://localhost:4840
           ?
???????????????????????
?  OPC UA Server      ?  (slm_opcua_server.cpp)
?  (This Application) ?
???????????????????????
           ? Modbus TCP or Direct I/O
           ?
???????????????????????
?  CoDeSys PLC        ?
?  192.168.1.10:502   ?
???????????????????????
```

---

## Actual PLC Node IDs

The server exposes variables matching the actual CoDeSys PLC:

### MakeSurface Variables
```
ns=4;s=|var|CECC-D.Application.MakeSurface.Z_Stacks                              [INT16]
ns=4;s=|var|CECC-D.Application.MakeSurface.Delta_Source                          [INT32]
ns=4;s=|var|CECC-D.Application.MakeSurface.Delta_Sink                            [INT32]
ns=4;s=|var|CECC-D.Application.MakeSurface.Layer_Ready                           [BOOL]
ns=4;s=|var|CECC-D.Application.MakeSurface.Marcer_Source_Cylinder_ActualPosition [INT32]
ns=4;s=|var|CECC-D.Application.MakeSurface.Marcer_Sink_Cylinder_ActualPosition   [INT32]
ns=4;s=|var|CECC-D.Application.MakeSurface.Source_Ready                          [BOOL]
ns=4;s=|var|CECC-D.Application.MakeSurface.Surfaces_Control                      [INT16]
ns=4;s=|var|CECC-D.Application.MakeSurface.SurfaceStepFlag                       [BOOL]
ns=4;s=|var|CECC-D.Application.MakeSurface.SurfaceStepFlag_Test                  [BOOL]
```

### StartUpSequence Variables
```
ns=4;s=|var|CECC-D.Application.StartUpSequence.StartUp                           [BOOL]
```

### GVL (Global Variable List)
```
ns=4;s=|appo|CECC-D.Application.GlobalVars                                       [BOOL]
ns=4;s=|var|CECC-D.Application.GVL.Marcer_Sink_Cylinder.AckStart                 [BOOL]
ns=4;s=|var|CECC-D.Application.GVL.g_Marcer_Source_Cylinder_ActualPosition       [INT32]
ns=4;s=|var|CECC-D.Application.GVL.g_Marcer_Sink_Cylinder_ActualPosition         [INT32]
```

---

## Build Instructions

### Prerequisites

1. **CMake** >= 3.16
2. **vcpkg** package manager
3. **open62541** library (installed via vcpkg)
4. **C++17** compatible compiler

### Install Dependencies

```bash
# Install vcpkg (if not already installed)
git clone https://github.com/Microsoft/vcpkg.git
cd vcpkg
./bootstrap-vcpkg.bat  # Windows
./bootstrap-vcpkg.sh   # Linux

# Install open62541
./vcpkg install open62541:x64-windows  # Windows
./vcpkg install open62541:x64-linux    # Linux
```

### Build the Server

```bash
cd OPCUAServer
mkdir build
cd build

# Configure
cmake .. -DCMAKE_TOOLCHAIN_FILE=path/to/vcpkg/scripts/buildsystems/vcpkg.cmake

# Build
cmake --build . --config Release

# The executable will be in build/Release/OPCUAServer.exe (Windows)
```

---

## Running the Server

### Simulation Mode (Default - No Hardware)

```bash
OPCUAServer.exe --simulate
```

This mode:
- Does NOT connect to physical PLC
- Simulates PLC behavior internally
- Safe for testing without hardware
- Listens on `opc.tcp://localhost:4840`

### Production Mode (Real Hardware)

```bash
OPCUAServer.exe --production
```

This mode:
- Connects to actual CoDeSys PLC at `192.168.1.10:502`
- Controls real hardware
- Requires physical safety systems operational
- **?? WARNING**: Improper use can damage equipment

### Command Line Options

```
OPCUAServer [options]

Options:
  --help, -h          Show help message
  --endpoint URL      OPC UA endpoint (default: opc.tcp://localhost:4840)
  --namespace URI     Namespace URI (default: urn:CODESYS:CECC-D)
  --simulate          Enable PLC simulation mode (default)
  --production        Disable PLC simulation (real hardware)
  --no-failsafe       Disable fail-safe controller (testing only)
  --verbose           Enable verbose logging
```

### Environment Variables

```bash
# Windows
set OPC_UA_ENDPOINT=opc.tcp://localhost:4840
set OPC_UA_NAMESPACE_URI=urn:CODESYS:CECC-D
set OPC_UA_SIMULATE=1

# Linux
export OPC_UA_ENDPOINT=opc.tcp://localhost:4840
export OPC_UA_NAMESPACE_URI=urn:CODESYS:CECC-D
export OPC_UA_SIMULATE=1
```

---

## Connecting the Client

### Client Configuration

In your `OPCServerManagerUA` client code, ensure:

```cpp
// opcserverua.h
static constexpr uint16_t DEFAULT_NAMESPACE_INDEX = 4;  // MUST be 4
static constexpr const char* DEFAULT_SERVER_URL = "opc.tcp://localhost:4840";
```

### Client Node ID Format

The client must use the exact node ID format:

```cpp
// CORRECT - With |var| prefix
mNode_layersMax = createNodeId("|var|CECC-D.Application.MakeSurface.Z_Stacks");

// WRONG - Without prefix
mNode_layersMax = createNodeId("CECC-D.Application.MakeSurface.Z_Stacks");
```

### Data Type Mapping

| PLC Variable | OPC UA Type | C++ Type | Client Read/Write |
|--------------|-------------|----------|-------------------|
| Z_Stacks | INT16 | `int16_t` | INT32 (auto-convert) |
| Delta_Source | INT32 | `int32_t` | INT32 |
| Delta_Sink | INT32 | `int32_t` | INT32 |
| Layer_Ready | BOOL | `bool` | BOOLEAN |
| StartUp | BOOL | `bool` | BOOLEAN |
| Surfaces_Control | INT16 | `int16_t` | INT32 (auto-convert) |

---

## Verification

### Using UaExpert

1. **Download UaExpert**: https://www.unified-automation.com/products/development-tools/uaexpert.html
2. **Connect** to `opc.tcp://localhost:4840`
3. **Browse** the address space
4. **Verify** namespace index is 4
5. **Check** all node IDs start with `|var|CECC-D.Application...`

### Expected Address Space

```
Objects
??? ns=4;s=|var|CECC-D.Application.MakeSurface.Z_Stacks
??? ns=4;s=|var|CECC-D.Application.MakeSurface.Delta_Source
??? ns=4;s=|var|CECC-D.Application.MakeSurface.Delta_Sink
??? ... (all other variables)
```

---

## Troubleshooting

### Problem: Client gets `BadNodeIdUnknown`

**Cause**: Namespace index mismatch or incorrect node ID format

**Solution**:
1. Verify client uses namespace index **4** (not 2)
2. Verify node IDs use `|var|` prefix
3. Check server logs for actual namespace index assigned

### Problem: Server fails to start

**Cause**: Port 4840 already in use

**Solution**:
```bash
# Check if port is in use
netstat -an | findstr :4840

# Kill existing process or use different port
OPCUAServer.exe --endpoint opc.tcp://localhost:4841
```

### Problem: Production mode can't connect to PLC

**Cause**: PLC not accessible or wrong IP address

**Solution**:
1. Verify PLC is powered on and in RUN mode
2. Ping the PLC: `ping 192.168.1.10`
3. Check network cable connection
4. Verify firewall allows Modbus TCP port 502
5. Update PLC IP in `slm_opcua_server.cpp` if needed

---

## Safety Features

### Fail-Safe Controller

- **Watchdog Timer**: 5-second timeout
- **Emergency Stop**: Immediate hardware halt
- **Position Limits**: ±500mm software limits
- **Type Safety**: Strict data type validation

### Emergency Stop Trigger

The server automatically triggers emergency stop if:
- PLC watchdog timeout (5 seconds no response)
- Hardware position limits exceeded
- PLC emergency stop button pressed
- Unhandled exception in server code

---

## Code Structure

```
OPCUAServer/
??? main.cpp                      # Entry point, CLI parsing
??? slm_opcua_server.h            # Server class declaration
??? slm_opcua_server.cpp          # Server implementation
??? plc_state.h                   # PLC variable definitions
??? fail_safe_controller.h        # Safety system
??? fail_safe_controller.cpp      # Safety implementation
??? codesys_plc_interface.h       # PLC communication interface
??? codesys_plc_interface.cpp     # Modbus TCP implementation
??? CMakeLists.txt                # Build configuration
```

---

## Performance

- **Polling Rate**: 10ms (100 Hz)
- **Response Time**: < 50ms typical
- **Memory Usage**: ~10 MB
- **CPU Usage**: < 5% on modern systems

---

## Maintenance

### Log Files

The server logs to `stdout`. To capture logs:

```bash
# Windows
OPCUAServer.exe > server_log.txt 2>&1

# Linux
./OPCUAServer > server_log.txt 2>&1
```

### Monitoring

Key log messages to monitor:
- `[START] OPC UA Server STARTED` - Server initialized
- `[PRODUCTION] PLC Connection: ACTIVE` - PLC connected
- `[ERROR]` - Any error messages
- `[WARNING] ?? PLC WATCHDOG TIMEOUT!` - Safety critical

---

## Support

For issues or questions:
1. Check this deployment guide
2. Review server logs
3. Use UaExpert to verify node IDs
4. Contact: Senior Embedded Systems Engineer

---

## License

Copyright (c) 2024 MarcSLM Control Systems
Industrial Control System for Selective Laser Melting

---

**End of Deployment Guide**
