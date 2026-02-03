# Complete Installation Guide - CoDeSys PLC Integration

## Quick Start

This guide walks you through installing all dependencies and building the production OPC UA server with full CoDeSys PLC support.

---

## Prerequisites

- **Windows 10/11** (x64)
- **Visual Studio 2019+** with C++ Desktop Development
- **Git** for Windows
- **Administrator** access (for vcpkg installation)

---

## Step 1: Install vcpkg (Dependency Manager)

### 1.1 Download and Bootstrap vcpkg

Open **PowerShell as Administrator** and run:

```powershell
# Create directory
cd C:\
New-Item -ItemType Directory -Path "vcpkg" -Force
cd vcpkg

# Clone vcpkg repository
git clone https://github.com/microsoft/vcpkg .

# Bootstrap vcpkg
.\bootstrap-vcpkg.bat

# Integrate with Visual Studio
.\vcpkg integrate install
```

**Expected Output:**
```
Applied user-wide integration for this vcpkg root.

All MSBuild C++ projects can now #include any installed libraries.
```

### 1.2 Set Environment Variable (Optional)

```powershell
[Environment]::SetEnvironmentVariable("VCPKG_ROOT", "C:\vcpkg", "User")
```

---

## Step 2: Install Dependencies

### 2.1 Install open62541 (OPC UA Library)

```powershell
cd C:\vcpkg
.\vcpkg install open62541:x64-windows
```

**Installation Time:** ~3-5 minutes

### 2.2 Install libmodbus (Modbus TCP Library)

```powershell
.\vcpkg install libmodbus:x64-windows
```

**Installation Time:** ~1-2 minutes

### 2.3 Verify Installation

```powershell
.\vcpkg list | Select-String -Pattern "open62541|libmodbus"
```

**Expected Output:**
```
libmodbus:x64-windows                 3.1.10#3
open62541:x64-windows                 1.3.8#1
```

---

## Step 3: Build MarcSLM Control System

### 3.1 Clone Repository (if not already done)

```powershell
cd C:\Active_Projects
git clone https://github.com/ShahidMustafa-PhD/MarcSLM_ControlSystem MarcSLM_ControlSystems
cd MarcSLM_ControlSystems
```

### 3.2 Configure CMake with vcpkg Toolchain

```powershell
# Create build directory
cmake -B build -G "Visual Studio 16 2019" -A x64 `
  -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
  -DCMAKE_BUILD_TYPE=Release
```

**Expected Output:**
```
-- Found open62541 for OPCUAServer
-- Found libmodbus for CoDeSys PLC interface
-- Configuring done
-- Generating done
```

### 3.3 Build the Project

```powershell
cmake --build build --target OPCUAServer --config Release
```

**Build Time:** ~2-5 minutes (first build)

### 3.4 Verify Build Success

```powershell
dir install\OPCUAServer.exe
```

**Expected Output:**
```
    Directory: C:\Active_Projects\MarcSLM_ControlSystems\install

Mode                 LastWriteTime         Length Name
----                 -------------         ------ ----
-a---          2024-02-03   2:45 PM       2856960 OPCUAServer.exe
```

---

## Step 4: Configure Network for PLC Communication

### 4.1 Identify Network Adapter

Open **Control Panel ? Network and Sharing Center ? Change adapter settings**

Find the adapter connected to your PLC (usually Ethernet)

### 4.2 Set Static IP Address

**Right-click adapter ? Properties ? Internet Protocol Version 4 (TCP/IPv4) ? Properties**

```
[?] Use the following IP address:
    
    IP address:      192.168.1.100
    Subnet mask:     255.255.255.0
    Default gateway: (leave blank or 192.168.1.1)
    
[?] Use the following DNS server addresses:
    
    Preferred DNS server: (leave blank or 8.8.8.8)
```

Click **OK** and close all dialogs.

### 4.3 Test PLC Connectivity

```powershell
ping 192.168.1.10
```

**Expected Output (if PLC is connected):**
```
Pinging 192.168.1.10 with 32 bytes of data:
Reply from 192.168.1.10: bytes=32 time<1ms TTL=64
Reply from 192.168.1.10: bytes=32 time<1ms TTL=64
```

---

## Step 5: Configure Firewall

### 5.1 Allow Modbus TCP Port 502

Open **Windows Defender Firewall with Advanced Security**

**Inbound Rules ? New Rule...**

```
Rule Type: Port
Specific local ports: 502
Action: Allow the connection
Profile: All checked
Name: Modbus TCP (PLC Communication)
```

### 5.2 Allow OPC UA Port 4840

Create another rule for OPC UA:

```
Rule Type: Port
Specific local ports: 4840
Action: Allow the connection
Profile: All checked
Name: OPC UA Server
```

---

## Step 6: Test Installation

### 6.1 Run OPC UA Server in Simulation Mode (No PLC Required)

```powershell
cd C:\Active_Projects\MarcSLM_ControlSystems\install
.\OPCUAServer.exe --simulate
```

**Expected Output:**
```
?????????????????????????????????????????????????????????????????????????
?   SLM OPC UA Server                                                    ?
?????????????????????????????????????????????????????????????????????????

[CONFIG] ============================================
[CONFIG] Server Configuration:
[CONFIG]   Endpoint:      opc.tcp://0.0.0.0:4840
[CONFIG]   Mode:          SIMULATION
[CONFIG] ============================================

[START] OPC UA Server STARTED
[START] Endpoint: opc.tcp://0.0.0.0:4840
[START] Mode: SIMULATION

Server running. Press Ctrl+C to stop.
```

Press **Ctrl+C** to stop.

### 6.2 Run in Production Mode (Requires PLC Connection)

```powershell
.\OPCUAServer.exe --production
```

**Expected Output (if PLC connected):**
```
[CONFIG] Mode:          PRODUCTION

[START] Connecting to CoDeSys PLC...
[START] ============================================
[START] CoDeSys PLC CONNECTED SUCCESSFULLY
[START] ============================================
[START] Initial PLC Status:
[START]   Source Position: 0 ?m
[START]   Sink Position: 0 ?m
[START]   Emergency Stop: OK
[START]   PLC Heartbeat: 42

[START] OPC UA Server STARTED
[START] PLC Connection: ACTIVE

Server running. Press Ctrl+C to stop.
```

**Expected Output (if PLC NOT connected):**
```
[ERROR] ============================================
[ERROR] FAILED TO CONNECT TO CoDeSys PLC!
[ERROR] ============================================
[ERROR] Check:
[ERROR]   - PLC is powered on and in RUN mode
[ERROR]   - PLC IP address: 192.168.1.10
[ERROR]   - Network cable connected
[ERROR]   - Firewall allows Modbus TCP port 502
[ERROR] ============================================
```

---

## Step 7: Configure PLC IP Address (if different from 192.168.1.10)

### Option 1: Create Configuration File

Create `OPCUAServer/plc_config.json`:

```json
{
    "plc_ip": "192.168.1.10",
    "plc_port": 502,
    "modbus_unit_id": 1,
    "watchdog_timeout_ms": 5000,
    "max_source_position_um": 200000,
    "max_sink_position_um": 150000
}
```

### Option 2: Use Environment Variable

```powershell
$env:PLC_IP_ADDRESS="192.168.2.50"
.\OPCUAServer.exe --production
```

### Option 3: Modify Source Code

Edit `OPCUAServer/slm_opcua_server.cpp` (line ~98):

```cpp
plcConfig.plcIpAddress = "192.168.2.50";  // Your PLC IP
```

Then rebuild:

```powershell
cmake --build build --target OPCUAServer --config Release
```

---

## Step 8: Test with Main Control Application

### 8.1 Start OPC UA Server

```powershell
cd C:\Active_Projects\MarcSLM_ControlSystems\install
.\OPCUAServer.exe --production
```

Leave this running.

### 8.2 Start Main Control Application

Open **new PowerShell window**:

```powershell
cd C:\Active_Projects\MarcSLM_ControlSystems\install
.\MarcSLMControlSystem.exe
```

### 8.3 Initialize OPC Connection in GUI

1. Click **"Initialize OPC"** button
2. Wait for connection confirmation
3. Click **"Startup"** button
4. Verify OPC UA server console shows:
   ```
   [PRODUCTION] Startup sequence requested
   [PRODUCTION] ? Startup command sent to PLC
   ```

---

## Troubleshooting

### Problem: vcpkg integrate failed

**Error:** `vcpkg integrate install failed with error`

**Solution:**
```powershell
# Run as Administrator
Start-Process powershell -Verb runAs
cd C:\vcpkg
.\vcpkg integrate install
```

### Problem: CMake cannot find vcpkg toolchain

**Error:** `CMAKE_TOOLCHAIN_FILE is not set`

**Solution:**
```powershell
# Use full path to toolchain file
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
```

### Problem: libmodbus not found

**Error:** `Cannot open include file: 'modbus/modbus.h'`

**Solution:**
```powershell
# Reinstall libmodbus
cd C:\vcpkg
.\vcpkg remove libmodbus:x64-windows
.\vcpkg install libmodbus:x64-windows

# Clean build directory
cd C:\Active_Projects\MarcSLM_ControlSystems
Remove-Item -Recurse -Force build
cmake -B build -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake
cmake --build build --target OPCUAServer --config Release
```

### Problem: Cannot connect to PLC

**Error:** `Failed to connect to PLC`

**Checklist:**
1. Verify PLC is powered on
2. Check PLC is in **RUN** mode (CoDeSys online view)
3. Verify network cable connected
4. Test connectivity: `ping 192.168.1.10`
5. Check firewall: `netsh advfirewall firewall show rule name="Modbus TCP"`
6. Verify PLC Modbus TCP slave is configured (port 502, unit ID 1)
7. Use **Modbus Poll** tool to test PLC communication

### Problem: PLC reads fail with timeout

**Error:** `Watchdog expired` or `Failed to read PLC status`

**Solutions:**
1. Increase timeout in config:
   ```cpp
   plcConfig.responseTimeoutMs = 1000;  // Increase to 1 second
   plcConfig.watchdogTimeoutMs = 10000; // Increase to 10 seconds
   ```
2. Reduce polling frequency in `main()`:
   ```cpp
   std::this_thread::sleep_for(std::chrono::milliseconds(100)); // Was 50ms
   ```
3. Check network latency: `ping -t 192.168.1.10`
4. Monitor PLC CPU load in CoDeSys

---

## Verification Checklist

Before production deployment, verify:

- [ ] vcpkg installed and integrated
- [ ] open62541 installed (`vcpkg list | grep open62541`)
- [ ] libmodbus installed (`vcpkg list | grep libmodbus`)
- [ ] OPCUAServer.exe builds without errors
- [ ] Simulation mode works (`--simulate` flag)
- [ ] Network configured (PC: 192.168.1.100, PLC: 192.168.1.10)
- [ ] PLC responds to `ping 192.168.1.10`
- [ ] Firewall allows ports 502 (Modbus) and 4840 (OPC UA)
- [ ] PLC Modbus TCP slave configured and running
- [ ] Production mode connects to PLC (`--production` flag)
- [ ] Main GUI can connect to OPC UA server
- [ ] Commands are sent to PLC and acknowledged

---

## Next Steps

1. **Configure PLC Program**: See `CODESYS_INTEGRATION_GUIDE.md`
2. **Test Safety Features**: Emergency stop, limit switches, watchdog
3. **Calibrate Motion**: Set correct step sizes for micron-level accuracy
4. **Production Deployment**: Install as Windows service, configure logging

---

## Support

- **GitHub Issues**: https://github.com/ShahidMustafa-PhD/MarcSLM_ControlSystem/issues
- **Documentation**: `OPCUAServer/CODESYS_INTEGRATION_GUIDE.md`
- **vcpkg Support**: https://github.com/microsoft/vcpkg/issues

---

**Last Updated**: 2024-02-03  
**Version**: 1.0.0
