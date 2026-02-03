# ?? CoDeSys PLC Integration - Deployment Checklist

Use this checklist to ensure complete and safe deployment of the CoDeSys PLC integration.

---

## Phase 1: Software Dependencies ?

### vcpkg Installation

- [ ] **Install vcpkg** (if not already installed)
  ```powershell
  cd C:\
  git clone https://github.com/microsoft/vcpkg
  cd vcpkg
  .\bootstrap-vcpkg.bat
  ```

- [ ] **Integrate with Visual Studio**
  ```powershell
  .\vcpkg integrate install
  ```
  Expected output: `Applied user-wide integration for this vcpkg root.`

- [ ] **Verify vcpkg works**
  ```powershell
  .\vcpkg version
  ```

### libmodbus Installation

- [ ] **Install libmodbus**
  ```powershell
  cd C:\vcpkg
  .\vcpkg install libmodbus:x64-windows
  ```
  Expected time: ~2 minutes

- [ ] **Verify installation**
  ```powershell
  .\vcpkg list | Select-String "libmodbus"
  ```
  Expected output: `libmodbus:x64-windows 3.1.10#3`

### open62541 Installation

- [ ] **Install open62541**
  ```powershell
  .\vcpkg install open62541:x64-windows
  ```
  Expected time: ~3-5 minutes

- [ ] **Verify installation**
  ```powershell
  .\vcpkg list | Select-String "open62541"
  ```
  Expected output: `open62541:x64-windows 1.3.8#1`

---

## Phase 2: Project Build ?

### Clean Build

- [ ] **Delete old build directory** (if exists)
  ```powershell
  cd C:\Active_Projects\MarcSLM_ControlSystems
  Remove-Item -Recurse -Force build -ErrorAction SilentlyContinue
  ```

- [ ] **Configure CMake with vcpkg toolchain**
  ```powershell
  cmake -B build -G "Visual Studio 16 2019" -A x64 `
    -DCMAKE_TOOLCHAIN_FILE=C:/vcpkg/scripts/buildsystems/vcpkg.cmake `
    -DCMAKE_BUILD_TYPE=Release
  ```
  
  Expected output:
  ```
  -- Found open62541 for OPCUAServer
  -- Found libmodbus for CoDeSys PLC interface
  -- Configuring done
  -- Generating done
  ```

- [ ] **Build OPC UA Server**
  ```powershell
  cmake --build build --target OPCUAServer --config Release
  ```
  
  Expected time: ~2-5 minutes (first build)

- [ ] **Verify executable exists**
  ```powershell
  dir install\OPCUAServer.exe
  ```

### Test Simulation Mode

- [ ] **Run server in simulation mode**
  ```powershell
  cd install
  .\OPCUAServer.exe --simulate
  ```
  
  Expected output:
  ```
  [START] OPC UA Server STARTED
  [START] Mode: SIMULATION
  Server running. Press Ctrl+C to stop.
  ```

- [ ] **Stop server** (Ctrl+C)

---

## Phase 3: Network Configuration ?

### Identify Network Adapter

- [ ] **Open Network and Sharing Center**
  - Control Panel ? Network and Sharing Center
  - Change adapter settings

- [ ] **Identify PLC adapter** (usually "Ethernet" or "Local Area Connection")

### Configure Static IP

- [ ] **Right-click adapter ? Properties**

- [ ] **Select "Internet Protocol Version 4 (TCP/IPv4)" ? Properties**

- [ ] **Set static IP address:**
  ```
  [?] Use the following IP address:
  
      IP address:      192.168.1.100
      Subnet mask:     255.255.255.0
      Default gateway: (leave blank)
  
  [?] Use the following DNS server addresses:
  
      Preferred DNS:   (leave blank or 8.8.8.8)
  ```

- [ ] **Click OK and close all dialogs**

### Verify PLC Network

- [ ] **Configure PLC IP** (in CoDeSys or PLC web interface):
  ```
  IP Address:    192.168.1.10
  Subnet Mask:   255.255.255.0
  Gateway:       (leave blank or 192.168.1.1)
  ```

- [ ] **Power cycle PLC** (if IP changed)

- [ ] **Test connectivity:**
  ```powershell
  ping 192.168.1.10
  ```
  
  Expected output:
  ```
  Reply from 192.168.1.10: bytes=32 time<1ms TTL=64
  Reply from 192.168.1.10: bytes=32 time<1ms TTL=64
  Reply from 192.168.1.10: bytes=32 time<1ms TTL=64
  Reply from 192.168.1.10: bytes=32 time<1ms TTL=64
  ```

- [ ] **Continuous ping test** (5 minutes):
  ```powershell
  ping -t 192.168.1.10
  ```
  Verify 0% packet loss, then stop (Ctrl+C)

---

## Phase 4: Firewall Configuration ?

### Allow Modbus TCP

- [ ] **Open Windows Defender Firewall with Advanced Security**

- [ ] **Click "Inbound Rules"**

- [ ] **Click "New Rule..."**

- [ ] **Configure Modbus TCP rule:**
  ```
  Rule Type:             Port
  Protocol:              TCP
  Specific local ports:  502
  Action:                Allow the connection
  Profile:               [?] Domain  [?] Private  [?] Public
  Name:                  Modbus TCP (PLC Communication)
  ```

- [ ] **Verify rule exists:**
  ```powershell
  Get-NetFirewallRule | Where-Object {$_.DisplayName -like "*Modbus*"}
  ```

### Allow OPC UA

- [ ] **Create OPC UA firewall rule:**
  ```
  Rule Type:             Port
  Protocol:              TCP
  Specific local ports:  4840
  Action:                Allow the connection
  Profile:               [?] Domain  [?] Private  [?] Public
  Name:                  OPC UA Server
  ```

- [ ] **Verify rule exists:**
  ```powershell
  Get-NetFirewallRule | Where-Object {$_.DisplayName -like "*OPC*"}
  ```

### Test Ports

- [ ] **Check port 502 is listening** (after starting OPC UA server in production mode):
  ```powershell
  netstat -an | Select-String ":502"
  ```

- [ ] **Check port 4840 is listening** (OPC UA server):
  ```powershell
  netstat -an | Select-String ":4840"
  ```

---

## Phase 5: PLC Programming ?

### CoDeSys Project Setup

- [ ] **Open CoDeSys Development System**

- [ ] **Create new project:**
  - File ? New Project
  - Select your PLC type (e.g., Wago PFC200, Beckhoff CX)
  - Template: Standard Project
  - Language: Structured Text (ST)

### Add Modbus TCP Slave

- [ ] **In Device Tree, right-click Ethernet ? Add Device**

- [ ] **Select "Modbus ? Modbus TCP Slave"**

- [ ] **Configure Modbus TCP Slave:**
  ```
  Unit ID:               1
  Port:                  502
  Connection Timeout:    1000 ms
  Max Connections:       5
  ```

### Add Channel Mappings

- [ ] **Right-click "Modbus TCP Slave" ? Add Channel**

- [ ] **Configure Input Registers channel:**
  ```
  Channel Type:    Input Registers (3xxxx)
  Start Address:   30001
  Count:           13
  Memory Mapping:  %MW0 - %MW12
  ```

- [ ] **Add Holding Registers channel:**
  ```
  Channel Type:    Holding Registers (4xxxx)
  Start Address:   40001
  Count:           16
  Memory Mapping:  %MW100 - %MW115
  ```

### Create Global Variables

- [ ] **Add new Global Variable List: GVL_Modbus**

- [ ] **Copy variable declarations** from `CODESYS_INTEGRATION_GUIDE.md` Section 3

- [ ] **Verify all 29 variables are declared** (13 input + 16 holding)

### Implement PLC Logic

- [ ] **Open PLC_PRG (main program)**

- [ ] **Copy complete program** from `CODESYS_INTEGRATION_GUIDE.md` Section 4

- [ ] **Verify state machine has all 4 states:**
  - [ ] State 0: Idle
  - [ ] State 1: Startup Sequence
  - [ ] State 2: Powder Fill
  - [ ] State 3: Layer Preparation

- [ ] **Verify emergency stop logic** (first lines of program)

### Build and Download

- [ ] **Build project:** Build ? Build (F11)

- [ ] **Verify no errors:**
  ```
  Build: 0 Errors, 0 Warnings
  ```

- [ ] **Connect to PLC:** Online ? Login (Alt+F8)

- [ ] **Download program:** Online ? Download (F6)

- [ ] **Start PLC:** Online ? Run (F5)

- [ ] **Verify PLC status:** "RUN" mode (green indicator)

### Test PLC Modbus

- [ ] **Download Modbus Poll** (https://www.modbustools.com/)

- [ ] **Configure connection:**
  ```
  Connection:      Modbus TCP/IP
  IP Address:      192.168.1.10
  Port:            502
  Modbus ID:       1
  ```

- [ ] **Read input registers 30001-30013:**
  - [ ] Verify values update (heartbeat increments)
  - [ ] Verify positions are 0 or current positions

- [ ] **Write holding register 40016 (client heartbeat):**
  - [ ] Write value: 100
  - [ ] Verify PLC reads it (check in CoDeSys watch window)

---

## Phase 6: Production Test ?

### Start OPC UA Server

- [ ] **Open new PowerShell window**

- [ ] **Navigate to install directory:**
  ```powershell
  cd C:\Active_Projects\MarcSLM_ControlSystems\install
  ```

- [ ] **Start server in production mode:**
  ```powershell
  .\OPCUAServer.exe --production
  ```

- [ ] **Verify PLC connection:**
  ```
  Expected output:
  [START] ============================================
  [START] CoDeSys PLC CONNECTED SUCCESSFULLY
  [START] ============================================
  [START] PLC Connection: ACTIVE
  ```

- [ ] **Leave server running**

### Start Main GUI

- [ ] **Open another PowerShell window**

- [ ] **Start main control application:**
  ```powershell
  cd C:\Active_Projects\MarcSLM_ControlSystems\install
  .\MarcSLMControlSystem.exe
  ```

- [ ] **GUI opens successfully**

### Test OPC Connection

- [ ] **In GUI, click "Initialize OPC" button**

- [ ] **Verify connection successful** (status indicator changes)

- [ ] **Verify OPC UA server logs show connection:**
  ```
  [OPC] Client connected from 127.0.0.1
  ```

### Test Startup Sequence

- [ ] **In GUI, click "Startup" button**

- [ ] **Verify OPC UA server logs:**
  ```
  [PRODUCTION] Startup sequence requested
  [PRODUCTION] ? Startup command sent to PLC
  ```

- [ ] **Verify PLC behavior:**
  - [ ] Cylinders move to home position
  - [ ] Startup_Done flag sets to TRUE
  - [ ] GUI shows "Startup complete"

- [ ] **IMPORTANT: Verify cylinders actually moved!** ? This is the critical test

### Test Powder Fill

- [ ] **In GUI, set parameters:**
  ```
  Z_Stacks:      10
  Delta_Source:  50 ?m
  Delta_Sink:    50 ?m
  ```

- [ ] **Click "Start Powder Fill"**

- [ ] **Verify OPC UA server logs:**
  ```
  [PRODUCTION] Powder fill requested
  [PRODUCTION]   Z_Stacks: 10
  [PRODUCTION]   Delta_Source: 50 ?m
  [PRODUCTION]   Delta_Sink: 50 ?m
  [PRODUCTION] ? Powder fill command sent to PLC
  ```

- [ ] **Verify PLC behavior:**
  - [ ] Source cylinder moves UP (10 × 50?m = 500?m)
  - [ ] Sink cylinder moves DOWN (10 × 50?m = 500?m)
  - [ ] MakeSurface_Done flag sets to TRUE
  - [ ] GUI shows "Powder fill complete"

- [ ] **Verify GUI displays REAL positions:**
  - [ ] Source Position: 500 ?m (or close to it)
  - [ ] Sink Position: 500 ?m (or close to it)

### Test Layer Preparation

- [ ] **In GUI, set parameters:**
  ```
  Step_Source:   50 ?m
  Step_Sink:     -50 ?m
  ```

- [ ] **Click "Prepare Layer"**

- [ ] **Verify OPC UA server logs:**
  ```
  [PRODUCTION] Layer preparation requested
  [PRODUCTION]   Step_Source: 50 ?m
  [PRODUCTION]   Step_Sink: -50 ?m
  [PRODUCTION] ? Layer preparation command sent to PLC
  ```

- [ ] **Verify PLC behavior:**
  - [ ] Source cylinder moves UP 50?m
  - [ ] Sink cylinder moves DOWN 50?m
  - [ ] Recoater activates (if configured)
  - [ ] LaySurface_Done flag sets to TRUE
  - [ ] GUI shows "Layer complete"

---

## Phase 7: Safety Testing ?

### Emergency Stop Test

- [ ] **While cylinders are moving, press hardware E-Stop button**

- [ ] **Verify behavior:**
  - [ ] All motors stop immediately (< 100ms)
  - [ ] PLC MB_IN_EmergencyStop flag = TRUE
  - [ ] OPC UA server detects E-Stop
  - [ ] GUI shows emergency stop status

- [ ] **Release E-Stop button**

- [ ] **Verify system recovers:**
  - [ ] MB_IN_EmergencyStop flag = FALSE
  - [ ] Server resumes normal operation
  - [ ] Can restart operations after reset

### Position Limit Test

- [ ] **In GUI, attempt to exceed position limit:**
  ```
  Z_Stacks:      1000
  Delta_Source:  1000 ?m
  Delta_Sink:    1000 ?m
  ```
  (This would move 1,000,000 ?m = 1 meter, exceeding 200mm limit)

- [ ] **Click "Start Powder Fill"**

- [ ] **Verify rejection:**
  ```
  [PRODUCTION] ? Failed to send powder fill command
  [PRODUCTION]   Possible cause: Safety limits exceeded
  [ERROR] Powder fill would exceed position limits
  ```

- [ ] **Verify cylinders DO NOT move**

- [ ] **Verify emergency stop triggered** (if configured)

### Watchdog Test

- [ ] **While server running, disconnect PLC network cable**

- [ ] **Wait 5 seconds** (watchdog timeout)

- [ ] **Verify server logs:**
  ```
  [PRODUCTION] ?? PLC WATCHDOG TIMEOUT!
  [PRODUCTION]   Time since last communication: 5000 ms
  [ERROR] PLC watchdog expired
  ```

- [ ] **Verify emergency stop triggered** (if configured)

- [ ] **Reconnect network cable**

- [ ] **Verify automatic reconnection:**
  ```
  [PRODUCTION] PLC disconnected - attempting reconnection...
  [PRODUCTION] ? PLC reconnected successfully
  ```

### Communication Loss Test

- [ ] **While server running, stop PLC** (set to STOP mode)

- [ ] **Verify server detects failure:**
  ```
  [PRODUCTION] WARNING: Failed to read PLC status
  [ERROR] Communication failure - will attempt reconnection
  ```

- [ ] **Start PLC** (set to RUN mode)

- [ ] **Verify automatic recovery:**
  ```
  [PRODUCTION] ? PLC reconnected successfully
  ```

---

## Phase 8: Production Deployment ?

### Documentation

- [ ] **Print safety procedures** from `CODESYS_INTEGRATION_GUIDE.md` Section 8

- [ ] **Create operator manual** with:
  - [ ] Startup procedure
  - [ ] Emergency stop procedure
  - [ ] Troubleshooting guide
  - [ ] Contact information

- [ ] **Post safety warnings** on machine

### Training

- [ ] **Train operators on:**
  - [ ] Normal operation (startup, powder fill, layer prep)
  - [ ] Emergency stop procedure
  - [ ] What to do on errors
  - [ ] When to call support

- [ ] **Train maintenance personnel on:**
  - [ ] Network configuration
  - [ ] PLC programming basics
  - [ ] Log file interpretation
  - [ ] Watchdog troubleshooting

### Monitoring

- [ ] **Set up log file rotation**

- [ ] **Configure alerts:**
  - [ ] Watchdog timeout
  - [ ] Emergency stop activations
  - [ ] Communication failures
  - [ ] Position limit violations

- [ ] **Monitor key metrics:**
  - [ ] PLC heartbeat (should increment continuously)
  - [ ] Communication success rate (> 99.9%)
  - [ ] Error codes from PLC
  - [ ] Position accuracy

### Backup

- [ ] **Backup PLC program** (export from CoDeSys)

- [ ] **Backup configuration files**

- [ ] **Backup log files** (last 30 days)

- [ ] **Document PLC IP address and settings**

---

## ? Final Verification

### Critical Success Criteria

- [ ] **Software builds without errors**

- [ ] **Simulation mode works**

- [ ] **Production mode connects to PLC**

- [ ] **GUI connects to OPC UA server**

- [ ] **Commands reach PLC** (verified in Modbus Poll)

- [ ] **Cylinders actually move** (visual confirmation)

- [ ] **Position feedback is accurate** (within 10?m)

- [ ] **Emergency stop works** (< 100ms response)

- [ ] **Safety limits enforced** (commands rejected)

- [ ] **Watchdog detects timeout** (5-second test)

- [ ] **Automatic reconnection works** (network loss test)

### Sign-Off

- [ ] **Software Engineer:** ______________________ Date: ________

- [ ] **PLC Programmer:** ______________________ Date: ________

- [ ] **Safety Officer:** ______________________ Date: ________

- [ ] **Operations Manager:** ______________________ Date: ________

---

## ?? Troubleshooting Reference

### Common Issues

| Problem | Solution | Document Reference |
|---------|----------|-------------------|
| Cannot connect to PLC | Check network, firewall, PLC IP | COMPLETE_INSTALLATION_GUIDE.md Section 7 |
| libmodbus not found | Install via vcpkg | SOLUTION_README.md Step 1 |
| Cylinders not moving | Verify PLC in RUN mode, Modbus mapping | CODESYS_INTEGRATION_GUIDE.md Section 6 |
| Watchdog timeout | Increase timeout, check network latency | COMPLETE_INSTALLATION_GUIDE.md Troubleshooting |
| Build errors | Clean build directory, reinstall deps | This document Phase 2 |

---

## ?? Support Contacts

**Technical Support:**
- GitHub Issues: https://github.com/ShahidMustafa-PhD/MarcSLM_ControlSystem/issues
- Email: (your support email)

**Emergency Contact:**
- Phone: (your emergency phone)
- Available: 24/7 for safety-critical issues

---

**Checklist Version:** 1.0.0  
**Last Updated:** 2024-02-03  
**Status:** Ready for deployment
