# OPC UA Production Server for SLM Control

## Overview

This directory contains a production-grade OPC UA server for controlling a Selective Laser Melting (SLM) machine. It implements the same OPC UA interface as expected by the `OPCServerManagerUA` client class, ensuring full compatibility.

## Features

- **Industrial Safety**: Fail-safe controller ensures hardware is driven to safe state on errors
- **RAII Memory Management**: All resources are properly managed with smart pointers
- **Thread-Safe State Access**: Mutex-protected access to PLC state
- **Watchdog Monitoring**: Detects and handles communication/processing failures
- **Position Validation**: Prevents out-of-bounds movements that could damage hardware
- **Simulation Mode**: Test without real hardware using built-in PLC emulation

## Files

| File | Description |
|------|-------------|
| `main.cpp` | Entry point with command-line parsing and signal handling |
| `slm_opcua_server.h/cpp` | Main OPC UA server class |
| `plc_state.h` | Thread-safe PLC state container |
| `fail_safe_controller.h/cpp` | Hardware fail-safe logic |
| `CMakeLists.txt` | Build configuration |

## Building

The server is built as part of the main project:

```bash
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --target OPCUAServer
```

The executable is placed in the `install/` directory.

## Usage

### Basic Usage (Simulation Mode)

```bash
./OPCUAServer --simulate
```

### Production Mode (Real Hardware)

```bash
./OPCUAServer --production --endpoint opc.tcp://192.168.1.10:4840
```

### Command-Line Options

| Option | Description |
|--------|-------------|
| `--help, -h` | Show help message |
| `--endpoint URL` | OPC UA endpoint (default: `opc.tcp://0.0.0.0:4840`) |
| `--namespace URI` | Namespace URI (default: `urn:CODESYS:MaTe_DLMS`) |
| `--simulate` | Enable PLC simulation (default) |
| `--production` | Disable PLC simulation (real hardware) |
| `--no-failsafe` | Disable fail-safe controller (testing only!) |
| `--verbose` | Enable verbose logging |

### Environment Variables

| Variable | Description |
|----------|-------------|
| `OPC_UA_ENDPOINT` | Override default endpoint URL |
| `OPC_UA_NAMESPACE_URI` | Override namespace URI |
| `OPC_UA_SIMULATE` | Set to `"0"` for production mode |

## OPC UA Information Model

The server exposes the following variables in namespace index 2:

### MakeSurface (Powder Surface Preparation)

| Node ID | Type | Access | Description |
|---------|------|--------|-------------|
| `CECC.MaTe_DLMS.MakeSurface.Z_Stacks` | Int32 | RW | Number of powder layers |
| `CECC.MaTe_DLMS.MakeSurface.Delta_Source` | Int32 | RW | Source step per stack (?m) |
| `CECC.MaTe_DLMS.MakeSurface.Delta_Sink` | Int32 | RW | Sink step per stack (?m) |
| `CECC.MaTe_DLMS.MakeSurface.MakeSurface_Done` | Boolean | RW | Surface preparation complete |
| `CECC.MaTe_DLMS.MakeSurface.Marcer_Source_Cylinder_ActualPosition` | Int32 | RW | Source position (?m) |
| `CECC.MaTe_DLMS.MakeSurface.Marcer_Sink_Cylinder_ActualPosition` | Int32 | RW | Sink position (?m) |

### GVL (Global Variables)

| Node ID | Type | Access | Description |
|---------|------|--------|-------------|
| `CECC.MaTe_DLMS.GVL.StartSurfaces` | Boolean | RW | Trigger surface creation |
| `CECC.MaTe_DLMS.GVL.g_Marcer_Source_Cylinder_ActualPosition` | Int32 | RW | Global source position |
| `CECC.MaTe_DLMS.GVL.g_Marcer_Sink_Cylinder_ActualPosition` | Int32 | RW | Global sink position |

### Prepare2Process (Layer Preparation)

| Node ID | Type | Access | Description |
|---------|------|--------|-------------|
| `CECC.MaTe_DLMS.Prepare2Process.LaySurface` | Boolean | RW | Request layer preparation |
| `CECC.MaTe_DLMS.Prepare2Process.LaySurface_Done` | Boolean | RW | Layer preparation complete |
| `CECC.MaTe_DLMS.Prepare2Process.Step_Sink` | Int32 | RW | Sink step per layer (?m) |
| `CECC.MaTe_DLMS.Prepare2Process.Step_Source` | Int32 | RW | Source step per layer (?m) |
| `CECC.MaTe_DLMS.Prepare2Process.Lay_Stacks` | Int32 | RW | Remaining layers |

### StartUpSequence

| Node ID | Type | Access | Description |
|---------|------|--------|-------------|
| `CECC.MaTe_DLMS.StartUpSequence.StartUp` | Boolean | RW | Trigger startup |
| `CECC.MaTe_DLMS.StartUpSequence.StartUp_Done` | Boolean | RW | Startup complete |

## Layer Preparation Handshake

The server implements a bidirectional handshake for layer-by-layer processing:

```
1. Client: LaySurface = TRUE     (Request layer preparation)
2. Server: [Moves cylinders]
3. Server: LaySurface_Done = TRUE (Layer ready for scanning)
4. Client: [Performs laser scanning]
5. Client: LaySurface = FALSE    (Scanning complete)
6. Server: LaySurface_Done = FALSE (Ready for next layer)
7. [Repeat from step 1]
```

## Safety Features

### Fail-Safe Controller

The fail-safe controller monitors system health and triggers safe shutdown when:

- **Watchdog Timeout**: No heartbeat for 5+ seconds
- **Position Out of Bounds**: Cylinder positions exceed ±500mm
- **State Validation Failure**: Invalid state transitions detected
- **Software Exception**: Unhandled exceptions caught

### Safe State

When triggered, the fail-safe controller:

1. Sets `LaserOff = TRUE` (interlock active)
2. Sets `MotionStop = TRUE` (halt all axes)
3. Clears all `Done` flags
4. Logs the incident with timestamp

### Recovery

After an emergency stop, use `--production` mode to restart:

```bash
./OPCUAServer --production
```

The server will perform pre-flight safety checks before allowing operation.

## Comparison: Simulator vs Production Server

| Feature | OPCUASimulator | OPCUAServer |
|---------|----------------|-------------|
| Purpose | Testing/Development | Production Deployment |
| PLC Behavior | Always simulated | Optional simulation |
| Fail-Safe | None | Full implementation |
| Position Validation | None | ±500mm limits |
| Watchdog | None | 5-second timeout |
| Memory Safety | Basic | RAII throughout |
| Signal Handling | None | SIGINT/SIGTERM |

## License

© 2024 MarcSLM Control Systems. All rights reserved.
