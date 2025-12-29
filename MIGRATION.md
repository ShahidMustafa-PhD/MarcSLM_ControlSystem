# OPC DA to OPC UA Migration Guide

## Overview

This project has been migrated from OPC DA (32-bit, COM-based) to OPC UA (64-bit, open62541-based).

## Changes Made

### 1. **New OPC UA Manager Class**
   - Created `opcserver_lib/opcserverua.h` and `opcserver_lib/opcserverua.cpp`
   - Implements `OPCServerManagerUA` class using open62541 library
   - Maintains identical public API to `OPCServerManager` for transparent integration

### 2. **Dependencies**
   - Added `vcpkg.json` manifest file
   - Dependency: `open62541` (version >=1.3.0)
   - Dependency: `nlohmann_json` (existing)

### 3. **CMake Configuration**
   - Removed all OPC DA library dependencies (`lib` folder)
   - Removed COM stubs (`opcda_i.c`, `opccomn_i.c`, `opcEnum_i.c`)
   - Removed OLE32, OLEAUT32, UUID linkage (no longer needed)
   - Added `find_package(open62541 CONFIG REQUIRED)`
   - Added `target_link_libraries(MarcControl PRIVATE open62541::open62541)`

### 4. **Controller Updates**
   - Updated `controllers/opccontroller.h` to use `OPCServerManagerUA`
   - Updated `controllers/opccontroller.cpp` to instantiate UA manager
   - Updated `controllers/slm_worker_manager.h` to use UA manager
   - Updated `controllers/slm_worker_manager.cpp` to use UA manager

### 5. **UI Updates**
   - Updated `launcher/mainwindow.h` to use `OPCServerManagerUA::OPCData`
   - Updated `launcher/mainwindow.cpp` signal handlers for UA types

## Building the Project

### Prerequisites

1. **Install vcpkg**
   ```powershell
   git clone https://github.com/Microsoft/vcpkg.git C:\vcpkg
   cd C:\vcpkg
   .\bootstrap-vcpkg.bat
   ```

2. **Install dependencies via vcpkg**
   ```powershell
   cd C:\Projects\VolMarc_64_bit
   C:\vcpkg\vcpkg install --triplet x64-windows
   ```
   This will install:
   - `open62541:x64-windows`
   - `nlohmann_json:x64-windows`

3. **Configure CMake**
   ```powershell
   cmake -S . -B build -G "Ninja" `
     -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
     -DCMAKE_PREFIX_PATH="C:/vcpkg/installed/x64-windows;C:/Qt/6.10.1/msvc2022_64" `
     -DCMAKE_BUILD_TYPE=Release
   ```

4. **Build**
   ```powershell
   cmake --build build --config Release
   ```

## OPC UA Server Configuration

### Environment Variables

You can configure the OPC UA connection via environment variables:

- `OPC_UA_URL`: OPC UA server endpoint URL
  - Default: `opc.tcp://localhost:4840`
  - Example: `opc.tcp://192.168.1.100:4840`

- `OPC_UA_NAMESPACE_INDEX`: Namespace index for CoDeSys variables
  - Default: `2`
  - Example: `3`

### Example Configuration

```powershell
# Windows PowerShell
$env:OPC_UA_URL = "opc.tcp://localhost:4840"
$env:OPC_UA_NAMESPACE_INDEX = "2"
.\install-release\MarcSLM_Launcher.exe
```

## Node ID Mapping

### OPC DA (Old) to OPC UA (New)

OPC DA tags were accessed via hierarchical string paths:
```
CECC.MaTe_DLMS.StartUpSequence.StartUp
```

OPC UA nodes use namespace index + identifier:
```
ns=2;s=CECC.MaTe_DLMS.StartUpSequence.StartUp
```

The `OPCServerManagerUA` class automatically constructs node IDs using:
- Namespace index (default: 2)
- String identifier (same as OPC DA tag)

## Functional Equivalence

### Read/Write Operations

All OPC operations maintain identical signatures:

```cpp
// OPC DA (Old)
bool writeStartUp(bool value);
bool writePowderFillParameters(int layers, int deltaSource, int deltaSink);
bool writeLayerParameters(int layers, int deltaSource, int deltaSink);
bool writeBottomLayerParameters(int layers, int deltaSource, int deltaSink);
bool writeEmergencyStop();
bool writeCylinderPosition(bool isSource, int position);
bool writeLayerExecutionComplete(int layerNumber);
bool readData(OPCData& data);

// OPC UA (New) - IDENTICAL SIGNATURES
bool writeStartUp(bool value);
bool writePowderFillParameters(int layers, int deltaSource, int deltaSink);
bool writeLayerParameters(int layers, int deltaSource, int deltaSink);
bool writeBottomLayerParameters(int layers, int deltaSource, int deltaSink);
bool writeEmergencyStop();
bool writeCylinderPosition(bool isSource, int position);
bool writeLayerExecutionComplete(int layerNumber);
bool readData(OPCData& data);
```

### Data Structures

The `OPCData` structure is identical:

```cpp
struct OPCData {
    int sourceCylPosition = 0;
    int sinkCylPosition = 0;
    int g_sourceCylPosition = 0;
    int g_sinkCylPosition = 0;
    int stacksLeft = 0;
    int ready2Powder = 0;
    int startUpDone = 0;
    int powderSurfaceDone = 0;
};
```

## Industrial SLM Workflow

The bidirectional layer synchronization workflow remains unchanged:

1. **Scanner requests layer creation**
   - Calls `writeLayerParameters()`
   - OPC UA writes: `Lay_Stacks`, `Step_Source`, `Step_Sink`, `LaySurface=TRUE`

2. **PLC executes layer creation**
   - Recoater, platform movement
   - When complete: Sets `LaySurface_Done=TRUE`

3. **ProcessController detects completion**
   - Polling detects `LaySurface_Done`
   - Calls `ScanStreamingManager::notifyPLCPrepared()`
   - Wakes consumer thread

4. **Consumer thread executes laser scanning**
   - Reads `RTCCommandBlock` from queue
   - Executes jump/mark commands on Scanner
   - Turns laser OFF

5. **Scanner signals completion**
   - Calls `writeLayerExecutionComplete(layerNumber)`
   - OPC UA writes: `LaySurface=FALSE`
   - Signals PLC that scanner is done

6. **Loop repeats for next layer**

## Testing

### Verify OPC UA Connection

1. Ensure OPC UA server is running (CoDeSys Runtime or equivalent)
2. Verify server is accessible: `opc.tcp://localhost:4840`
3. Launch application:
   ```powershell
   .\install-release\MarcSLM_Launcher.exe
   ```
4. Click "Initialize OPC" button
5. Check system log for:
   ```
   Connecting to OPC UA Server...
   OPC UA URL defaulting to opc.tcp://localhost:4840
   OPC UA namespace index defaulting to 2
   ? Connected to OPC UA server: opc.tcp://localhost:4840
   ? Successfully created OPC UA node IDs (namespace: 2)
   OPC UA Server initialized successfully
   ? OPC UA Server initialized successfully
   ```

### Test Layer Synchronization

1. Initialize both OPC and Scanner
2. Load a MARC file and JSON config
3. Start production SLM process
4. Observe bidirectional handshake in log:
   ```
   ? Layer parameters sent: 1 layers (OPC UA)
   ? Layer 1 execution complete signal sent to PLC (LaySurface=FALSE, OPC UA)
   ```

## Troubleshooting

### "Failed to connect to OPC UA server"

**Cause**: OPC UA server not running or wrong URL

**Solution**:
1. Verify server is running
2. Check server URL: `$env:OPC_UA_URL = "opc.tcp://192.168.1.100:4840"`
3. Test with UaExpert or similar OPC UA client

### "Node type mismatch"

**Cause**: Incorrect namespace index or variable types

**Solution**:
1. Verify namespace index: `$env:OPC_UA_NAMESPACE_INDEX = "3"`
2. Use UaExpert to browse server namespace
3. Verify variable types match (Int32, Boolean)

### "CMake could not find open62541"

**Cause**: vcpkg dependencies not installed

**Solution**:
```powershell
cd C:\Projects\VolMarc_64_bit
C:\vcpkg\vcpkg install open62541:x64-windows nlohmann_json:x64-windows
cmake -S . -B build -G "Ninja" `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

## Migration Benefits

1. **64-bit Native**: No longer constrained to 32-bit COM architecture
2. **Platform Independent**: open62541 is cross-platform (Windows, Linux, embedded)
3. **Modern Protocol**: OPC UA is the industry standard (IEC 62541)
4. **Better Security**: OPC UA supports encryption, authentication, and authorization
5. **Simplified Deployment**: No COM registration or registry dependencies
6. **Static Linking**: Entire OPC UA client statically linked (no runtime DLL issues)

## Files Changed

### Created
- `vcpkg.json` - Package manifest
- `opcserver_lib/opcserverua.h` - OPC UA manager header
- `opcserver_lib/opcserverua.cpp` - OPC UA manager implementation
- `MIGRATION.md` - This file

### Modified
- `CMakeLists.txt` - Build system updates
- `controllers/opccontroller.h` - Use UA manager
- `controllers/opccontroller.cpp` - Use UA manager
- `controllers/slm_worker_manager.h` - Use UA manager
- `controllers/slm_worker_manager.cpp` - Use UA manager
- `launcher/mainwindow.h` - Use UA data types
- `launcher/mainwindow.cpp` - Use UA data types

### Removed (from build)
- All files in `lib/` folder (OPC DA wrappers)
- `lib/opcda_i.c`, `lib/opccomn_i.c`, `lib/opcEnum_i.c` (COM stubs)
- `opcserver_lib/OPCGuids.cpp` (COM GUIDs)
- `opcserver_lib/opcserver.cpp` (OPC DA manager)
- `opcserver_lib/opcserver.h` (OPC DA manager header)

**Note**: Old OPC DA files remain in repository for reference but are excluded from build.

## Support

For issues or questions:
1. Check system log in application (detailed OPC UA diagnostics)
2. Verify OPC UA server configuration (endpoint, namespace, security)
3. Test connection with UaExpert or similar tool
4. Review open62541 documentation: https://open62541.org/
