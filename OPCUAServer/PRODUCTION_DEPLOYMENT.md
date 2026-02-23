# Production Mode Deployment Guide

## Overview

This guide explains how to deploy the OPC UA Server in **PRODUCTION MODE** to connect with **real hardware** (CoDeSys PLC) on your machine.

---

## Architecture - Production Mode

```
???????????????????????????????????????????????????????????????
?  MarcSLM Client Application (Your Application)              ?
?  - OPCServerManagerUA class                                  ?
?  - Connects via: opc.tcp://localhost:4840                    ?
?  - Namespace: ns=2                                           ?
???????????????????????????????????????????????????????????????
                            ? OPC UA Protocol
                            ? (Client ? Server)
???????????????????????????????????????????????????????????????
?  OPC UA Server (OPCUAServer/main.cpp)                        ?
?  - Exposes OPC UA variables at ns=2;s=|var|...              ?
?  - Listens on: opc.tcp://localhost:4840                      ?
?  - Thread-safe, industrial-quality                           ?
???????????????????????????????????????????????????????????????
                            ? Modbus TCP
                            ? (Server ? PLC)
???????????????????????????????????????????????????????????????
?  CoDeSys PLC (Physical Hardware)                             ?
?  - IP: localhost (127.0.0.1) OR 192.168.1.10                 ?
?  - Port: 502 (Modbus TCP)                                    ?
?  - Controls: Motors, Cylinders, Recoater, Laser              ?
???????????????????????????????????????????????????????????????
```

---

## Current Configuration

### ? What's Configured Correctly

| Component | Configuration | Status |
|-----------|---------------|--------|
| **Client** | ns=2, localhost:4840 | ? Correct |
| **Server** | ns=2, localhost:4840 | ? Correct |
| **PLC Target** | localhost:502 (Modbus TCP) | ? Will try localhost first |
| **Mode** | Production (--production) | ? Will connect to real PLC |

---

## Deployment Steps

### Step 1: Build the OPC UA Server

```bash
cd OPCUAServer
mkdir build
cd build

# Configure (adjust vcpkg path if needed)
cmake .. -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake -G Ninja

# Build
cmake --build . --config Release

# Output: build/Release/OPCUAServer.exe
```

### Step 2: Start OPC UA Server in Production Mode

```bash
cd OPCUAServer/build/Release

# Start server (will connect to real PLC)
./OPCUAServer.exe --production
```

**Expected Output**:
```
[CONFIG] Mode:          PRODUCTION
[INFO] PRODUCTION MODE - CONNECTING TO REAL HARDWARE
[START] Connecting to CoDeSys PLC...
[START] This may take a few seconds...
[START] CoDeSys PLC CONNECTED SUCCESSFULLY
[START] OPC UA Server STARTED
[START] Listening on: opc.tcp://localhost:4840
```

### Step 3: Start Your Client Application

```bash
cd build/launcher/Release
./MarcSLM_Launcher.exe
```

**Expected Output**:
```
? OPC UA Server connected successfully
? Setting up node IDs...
? Powder fill parameters sent to PLC (OPC UA)
```

---

## PLC Connection Configuration

### If PLC is on the Same Machine (Localhost)

**Current Configuration** (Already Set):
```cpp
// OPCUAServer/slm_opcua_server.cpp
plcConfig.plcIpAddress = "localhost";  // ? This is already configured
plcConfig.plcPort = 502;
```

The server will attempt to connect to:
- `localhost:502` (127.0.0.1:502)

### If PLC is on a Different Machine

Edit `OPCUAServer/slm_opcua_server.cpp`:

```cpp
// Line ~97
plcConfig.plcIpAddress = "192.168.1.10";  // Change to actual PLC IP
plcConfig.plcPort = 502;
```

Or use environment variable:
```bash
set PLC_IP_ADDRESS=192.168.1.10
./OPCUAServer.exe --production
```

---

## CoDeSys PLC Setup Requirements

### 1. Enable OPC UA Server on PLC

In your CoDeSys project:
1. Open **Symbol Configuration**
2. Mark variables for **OPC UA export**
3. Set **OPC UA Server** to **Enabled**
4. Configure **Endpoint**: `opc.tcp://0.0.0.0:4840`
5. **Download** project to PLC

### 2. Enable Modbus TCP Server on PLC

In your CoDeSys project:
1. Add **Modbus TCP Server** device
2. Configure:
   - **Port**: 502
   - **Unit ID**: 1
   - **Enable**: Yes
3. Map variables to Modbus registers (see below)
4. **Download** project to PLC

### 3. Modbus Register Mapping

You need to map PLC variables to Modbus registers:

#### Input Registers (PLC ? Server)
```
%IW0-1  ? Source cylinder position (INT32)
%IW2-3  ? Sink cylinder position (INT32)
%IX8    ? Movement complete (BOOL)
%IX9    ? Powder fill done (BOOL)
%IX10   ? Layer prep done (BOOL)
%IX11   ? Startup done (BOOL)
%IX12   ? Emergency stop (BOOL)
%IW13   ? PLC heartbeat (UINT16)
```

#### Holding Registers (Server ? PLC)
```
%QX0    ? Start powder fill (BOOL)
%QX1    ? Start layer prep (BOOL)
%QX2    ? Start startup (BOOL)
%QX3    ? Emergency stop (BOOL)
%QW4-5  ? Delta source (INT32)
%QW6-7  ? Delta sink (INT32)
%QW8-9  ? Step source (INT32)
%QW10-11 ? Step sink (INT32)
%QW12-13 ? Z stacks (INT32)
%QW15   ? Client heartbeat (UINT16)
```

---

## Verification Checklist

### Pre-Deployment Checks

- [ ] **PLC is powered on** and in **RUN mode**
- [ ] **OPC UA Server enabled** on PLC (port 4840)
- [ ] **Modbus TCP Server enabled** on PLC (port 502)
- [ ] **Variables mapped** to Modbus registers correctly
- [ ] **Network connectivity** verified (ping localhost or 192.168.1.10)
- [ ] **Firewall** allows ports 502 and 4840
- [ ] **Safety systems** operational (E-stop, interlocks)

### Post-Deployment Verification

- [ ] **Server starts** without errors
- [ ] **PLC connection** successful (check logs)
- [ ] **OPC UA endpoint** accessible (test with UaExpert)
- [ ] **Client connects** successfully
- [ ] **Read operations** return valid data
- [ ] **Write operations** succeed
- [ ] **Emergency stop** triggers correctly
- [ ] **Watchdog** monitors communication health

---

## Troubleshooting

### Issue: Server can't connect to PLC

**Symptoms**:
```
[ERROR] FAILED TO CONNECT TO CoDeSys PLC!
[ERROR] The server will continue in DEGRADED MODE.
```

**Solutions**:

1. **Check PLC is accessible**:
```bash
ping localhost
# OR
ping 192.168.1.10
```

2. **Verify Modbus TCP is enabled** on PLC:
   - CoDeSys: Device ? Modbus TCP Server ? Enabled = Yes
   - Port = 502

3. **Check firewall**:
```bash
# Windows
netsh advfirewall firewall add rule name="Modbus TCP" dir=in action=allow protocol=TCP localport=502
```

4. **Verify PLC is in RUN mode**:
   - CoDeSys: Online ? Login ? Start PLC

5. **Test Modbus connection** manually:
```bash
# Use Modbus testing tool (e.g., mbpoll)
mbpoll -a 1 -r 30001 -c 10 -t 4 localhost
```

### Issue: Client gets `BadNodeIdUnknown`

**Cause**: Namespace index or node ID mismatch

**Solution**: Already fixed! Client now uses `ns=2` matching server.

**Verify**:
```cpp
// opcserver/opcserverua.h - Should be:
static constexpr uint16_t DEFAULT_NAMESPACE_INDEX = 2;  // ? Correct
```

### Issue: Type mismatch errors

**Cause**: PLC uses INT16 but client expects INT32

**Solution**: Server auto-converts INT16 ? INT32. Client reads INT16 as INT32 automatically.

---

## Running in Production

### Option 1: Manual Start (Recommended for Testing)

**Terminal 1** (Server):
```bash
cd OPCUAServer/build/Release
./OPCUAServer.exe --production
```

**Terminal 2** (Client):
```bash
cd build/launcher/Release
./MarcSLM_Launcher.exe
```

### Option 2: Windows Service (Recommended for 24/7 Operation)

Create a Windows service that starts the OPC UA Server automatically:

1. Install NSSM (Non-Sucking Service Manager):
```bash
choco install nssm
```

2. Create service:
```bash
nssm install MarcSLM_OPCUAServer "C:\Active_Projects\MarcSLM_ControlSystems\OPCUAServer\build\Release\OPCUAServer.exe"
nssm set MarcSLM_OPCUAServer AppParameters --production
nssm set MarcSLM_OPCUAServer AppDirectory "C:\Active_Projects\MarcSLM_ControlSystems\OPCUAServer\build\Release"
nssm set MarcSLM_OPCUAServer Description "MarcSLM OPC UA Production Server"
nssm set MarcSLM_OPCUAServer Start SERVICE_AUTO_START
```

3. Start service:
```bash
net start MarcSLM_OPCUAServer
```

### Option 3: Batch Script (Quick Start)

Create `start_production.bat`:
```batch
@echo off
echo Starting MarcSLM OPC UA Server (Production Mode)
echo ================================================
echo.
cd /d "%~dp0OPCUAServer\build\Release"
start "MarcSLM_OPCServer" OPCUAServer.exe --production
echo.
echo Server starting...
echo Wait 5 seconds for server initialization...
timeout /t 5 /nobreak
echo.
cd /d "%~dp0build\launcher\Release"
echo Starting MarcSLM Client Application...
start "MarcSLM_Client" MarcSLM_Launcher.exe
echo.
echo Both applications started!
echo Check console windows for status.
pause
```

---

## Safety Considerations

### ?? CRITICAL SAFETY REQUIREMENTS

Before operating in production mode:

1. **Emergency Stop** button must be functional and accessible
2. **Safety interlocks** must be installed and operational
3. **Limit switches** must be calibrated and tested
4. **Laser safety** systems must be active (Class 4 laser hazard)
5. **Powder handling** safety procedures must be followed
6. **Operator training** completed and documented
7. **Risk assessment** performed and documented
8. **Backup and recovery** procedures established

### Fail-Safe Behavior

The server implements multiple fail-safe mechanisms:

1. **Watchdog Timeout** (5 seconds):
   - If no PLC communication for 5 seconds ? Emergency Stop
   - Action: All motion halted, laser disabled

2. **Position Limits**:
   - Source cylinder: 0 to 200mm
   - Sink cylinder: 0 to 150mm
   - Action: Reject out-of-bounds commands

3. **Communication Loss**:
   - If Modbus connection lost ? Degraded Mode
   - Action: OPC UA server continues, but no hardware control

4. **Emergency Stop**:
   - PLC E-stop pressed ? Immediate halt
   - Action: Server detects and propagates to all clients

---

## Performance Monitoring

### Key Metrics to Monitor

| Metric | Target | Alert If |
|--------|--------|----------|
| **PLC Response Time** | < 50ms | > 500ms |
| **OPC UA Response Time** | < 100ms | > 1000ms |
| **Watchdog Timeout** | < 5s | Expires |
| **Failed Reads/Writes** | < 1% | > 5% |
| **Reconnect Attempts** | 0 | > 3 per hour |

### Log Monitoring

Watch for these log messages:

**Normal Operation**:
```
[START] CoDeSys PLC CONNECTED SUCCESSFULLY
[PRODUCTION] ? Startup command sent
[PRODUCTION] ? Powder fill command sent
[PRODUCTION] ? Layer prep command sent
```

**Warnings**:
```
[PRODUCTION] Attempting PLC reconnection...
[WARNING] Namespace index mismatch!
```

**Critical Errors**:
```
[ERROR] FAILED TO CONNECT TO CoDeSys PLC!
[PRODUCTION] ?? PLC WATCHDOG TIMEOUT!
[ERROR] Position limit exceeded!
```

---

## Hardware Connection Matrix

### Scenario 1: PLC on Same Machine (Windows PC)

```
???????????????????????
?   Windows PC        ?
?                     ?
?  ????????????????   ?
?  ? OPC UA Server?   ?
?  ? localhost:   ?   ?
?  ?  4840        ?   ?
?  ????????????????   ?
?         ?           ?
?  ????????????????   ?
?  ? CoDeSys PLC  ?   ?
?  ? localhost:   ?   ?
?  ?  502         ?   ?
?  ????????????????   ?
?         ?           ?
?  ????????????????   ?
?  ?   Hardware   ?   ?
?  ?   I/O Cards  ?   ?
?  ????????????????   ?
???????????????????????
```

**Configuration**:
- PLC IP: `localhost` (already configured ?)
- No network required
- Fastest communication (local loopback)

### Scenario 2: PLC on Separate Machine

```
???????????????????????        ???????????????????????
?   Control PC        ?        ?   PLC Machine       ?
?                     ?        ?                     ?
?  ????????????????   ?        ?  ????????????????   ?
?  ? OPC UA Server?   ?        ?  ? CoDeSys PLC  ?   ?
?  ? localhost:   ?   ?        ?  ? 192.168.1.10 ?   ?
?  ?  4840        ?   ?        ?  ?  :502        ?   ?
?  ????????????????   ?        ?  ????????????????   ?
?         ?           ?        ?         ?           ?
?         ????????????????????????????????           ?
?                     ? LAN    ?                     ?
?                     ?        ?  ????????????????   ?
?                     ?        ?  ?   Hardware   ?   ?
?                     ?        ?  ?   I/O Cards  ?   ?
?                     ?        ?  ????????????????   ?
???????????????????????        ???????????????????????
```

**Configuration**:
- Edit `slm_opcua_server.cpp` line ~97:
```cpp
plcConfig.plcIpAddress = "192.168.1.10";  // Change from "localhost"
```

---

## Testing Production Mode

### Test 1: Verify PLC Connection

**Start server**:
```bash
./OPCUAServer.exe --production
```

**Look for**:
```
[START] Connecting to CoDeSys PLC...
[START] CoDeSys PLC CONNECTED SUCCESSFULLY  ?
[START]   Source Position: 0 µm
[START]   Sink Position: 0 µm
[START]   Emergency Stop: OK
```

**If you see**:
```
[ERROR] FAILED TO CONNECT TO CoDeSys PLC!  ?
```

Then:
1. Check PLC is powered on
2. Check Modbus TCP is enabled in CoDeSys
3. Check PLC IP address is correct
4. Check firewall settings

### Test 2: Verify OPC UA Exposure

Use UaExpert:
1. Connect to `opc.tcp://localhost:4840`
2. Browse to Objects folder
3. **Expected**: See variables at `ns=2;s=|var|CECC-D.Application.MakeSurface.Z_Stacks`
4. Try to **write** a value ? Should update PLC

### Test 3: End-to-End Test

1. **Start server** in production mode
2. **Start client** application
3. **Click "Initialize OPC"** ? Should succeed
4. **Click "Start Powder Fill"** ? Should trigger actual cylinder movement
5. **Check PLC status** ? Variables should update in real-time

---

## Fallback Behavior

### If PLC Connection Fails

The server runs in **DEGRADED MODE**:

```
[ERROR] FAILED TO CONNECT TO CoDeSys PLC!
[ERROR] The server will continue in DEGRADED MODE.
```

**Behavior**:
- ? OPC UA server **still runs**
- ? Client **can connect**
- ? Variables **can be read/written**
- ? Hardware **is not controlled**
- ?? Server will **auto-retry** connection every 5 seconds

**Use Cases**:
- Testing client without hardware
- Development without PLC access
- Graceful degradation during PLC maintenance

---

## Production Checklist

### Before Each Production Run

- [ ] PLC powered on and in RUN mode
- [ ] Modbus TCP server enabled (port 502)
- [ ] OPC UA server enabled (port 4840)
- [ ] E-stop button tested
- [ ] Limit switches calibrated
- [ ] Safety interlocks operational
- [ ] Powder handling area clear
- [ ] Laser safety procedures followed
- [ ] Backup systems functional
- [ ] Log monitoring active

### Daily Operations

1. **Morning Startup**:
   - Start OPC UA Server: `OPCUAServer.exe --production`
   - Verify PLC connection in logs
   - Start client application
   - Test emergency stop

2. **During Operation**:
   - Monitor server logs for errors
   - Watch for watchdog timeouts
   - Check PLC heartbeat counter increments
   - Monitor cylinder positions

3. **Evening Shutdown**:
   - Complete current build
   - Send emergency stop: `Ctrl+C` on server
   - Verify all motion stopped
   - Power down in correct sequence

---

## Emergency Procedures

### Emergency Stop Procedure

1. **Press physical E-stop button** (fastest)
2. OR **Ctrl+C on server** (software stop)
3. OR **Close client application** (graceful stop)

**System Response**:
- All motion stops immediately (< 100ms)
- Laser disabled
- Pneumatics to safe state
- Server logs emergency event
- Client notified of stop

### Communication Loss Recovery

If logs show:
```
[PRODUCTION] Attempting PLC reconnection...
```

**Actions**:
1. Check PLC is still powered
2. Check network cable connected
3. Check PLC in RUN mode
4. Server will auto-reconnect every 5 seconds
5. No manual intervention required

### Server Crash Recovery

If server crashes:
1. Check logs for exception message
2. Verify PLC is in safe state
3. Restart server: `OPCUAServer.exe --production`
4. Restart client application
5. Perform homing procedure if needed

---

## Maintenance

### Weekly Maintenance

- [ ] Review server logs for errors
- [ ] Check PLC connection statistics
- [ ] Verify watchdog never expired
- [ ] Test emergency stop functionality
- [ ] Check position limits enforced

### Monthly Maintenance

- [ ] Full system backup
- [ ] PLC program backup
- [ ] OPC UA server update (if available)
- [ ] Safety system inspection
- [ ] Calibration verification

---

## Support

### Diagnostic Commands

```bash
# Check server is running
netstat -an | findstr :4840

# Check PLC is accessible
ping localhost
telnet localhost 502

# Monitor server logs
tail -f server_log.txt  # Linux
Get-Content server_log.txt -Wait  # PowerShell
```

### Log File Analysis

**Normal operation pattern**:
```
[START] ? [RUN] ? [PRODUCTION] ? [STOP]
```

**Connection issue pattern**:
```
[START] ? [ERROR] FAILED TO CONNECT ? [RUN] DEGRADED MODE
```

**Emergency stop pattern**:
```
[PRODUCTION] ? [FAIL-SAFE] EMERGENCY STOP ? [STOP]
```

---

## Summary

| Component | Configuration | Ready |
|-----------|---------------|-------|
| **OPC UA Server** | Production mode, localhost:4840 | ? |
| **PLC Connection** | localhost:502 (Modbus TCP) | ? |
| **Client** | ns=2, localhost:4840 | ? |
| **Safety Systems** | Watchdog, E-stop, limits | ? |
| **Documentation** | Complete deployment guide | ? |

**Your system is ready for production deployment!** ??

Just run:
```bash
# Terminal 1
cd OPCUAServer/build/Release
./OPCUAServer.exe --production

# Terminal 2
cd build/launcher/Release
./MarcSLM_Launcher.exe
```

The server will automatically attempt to connect to the real PLC hardware on your machine via `localhost:502`.

---

**End of Production Deployment Guide**
