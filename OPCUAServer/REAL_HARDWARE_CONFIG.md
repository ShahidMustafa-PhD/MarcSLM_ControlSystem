# OPC UA Server - Real Hardware Configuration Summary

## ? Configuration Complete

Your OPC UA Server is now configured to connect to **real hardware** (CoDeSys PLC) on your machine.

---

## How It Works

### 1. OPC UA Server Layer (localhost:4840)

**Purpose**: Expose PLC variables via OPC UA protocol

**Configuration**:
- **Endpoint**: `opc.tcp://localhost:4840`
- **Namespace**: `ns=2` (auto-assigned by open62541)
- **Node IDs**: `ns=2;s=|var|CECC-D.Application.MakeSurface.Z_Stacks`
- **Thread-Safe**: Yes ?
- **Memory-Safe**: Yes ?
- **Type-Safe**: Yes ?

### 2. PLC Communication Layer (localhost:502)

**Purpose**: Connect to actual CoDeSys PLC via Modbus TCP

**Configuration**:
- **PLC IP**: `localhost` (127.0.0.1) - PLC on same machine
- **Protocol**: Modbus TCP
- **Port**: `502` (standard Modbus TCP port)
- **Unit ID**: `1` (default CoDeSys slave ID)
- **Timeout**: 1000ms connection, 500ms response
- **Auto-Reconnect**: Every 2 seconds if connection lost

### 3. Client Application

**Purpose**: Control the SLM machine via OPC UA

**Configuration**:
- **Server URL**: `opc.tcp://localhost:4840`
- **Namespace**: `ns=2` (matches server)
- **Node IDs**: `|var|CECC-D.Application.MakeSurface.Z_Stacks`

---

## What Happens When You Start the Server

### Startup Sequence

```
[1] Server creates UA_Server instance
     ?
[2] Registers namespace "urn:CODESYS:CECC-D" ? Gets index 2
     ?
[3] Adds 17 OPC UA variables at ns=2;s=|var|...
     ?
[4] Starts OPC UA server on localhost:4840
     ?
[5] Attempts Modbus TCP connection to localhost:502
     ?
[6] ? SUCCESS: Connected to CoDeSys PLC
     OR
     ? DEGRADED: PLC not accessible, server continues anyway
     ?
[7] Enters main loop: Poll PLC every 10ms
```

### Main Loop (Every 10ms)

```
???????????????????????????????????????
?  OPC UA Server Iteration            ?
???????????????????????????????????????
? 1. Process OPC UA client messages   ?
? 2. Read OPC UA variables ? State    ?
? 3. Read PLC status via Modbus TCP   ?  ? Real hardware communication
? 4. Update State from PLC            ?
? 5. Apply PLC behavior logic         ?
? 6. Write State ? PLC via Modbus     ?  ? Real hardware control
? 7. Update OPC UA variables          ?
? 8. Feed watchdog                    ?
? 9. Check safety conditions          ?
???????????????????????????????????????
       ? Repeat every 10ms
```

---

## Real Hardware Communication

### What the Server Does

**When client writes to OPC UA**:
```
Client writes Z_Stacks=10 to ns=2;s=|var|...|Z_Stacks
  ?
Server receives write via OPC UA
  ?
Server updates internal state: state.Z_Stacks = 10
  ?
Server writes to PLC via Modbus TCP:
  - Register 40013-40014 ? 10 (INT32)
  ?
PLC receives command and executes
  ?
PLC updates status registers
  ?
Server reads PLC status via Modbus TCP
  ?
Server updates OPC UA variables
  ?
Client reads updated values
```

### Modbus Communication Example

**Write Command** (Start Powder Fill):
```
Modbus Write Request:
  Function: 0x10 (Write Multiple Registers)
  Slave ID: 1
  Start Reg: 40005 (HOLD_DELTA_SOURCE)
  Values: [100, 0, 50, 0, 10, 0, 1]
           ^^^^^^^  ^^^^^  ^^^^^  ^
           Delta    Delta  Z_Stk  Start
           Source   Sink          Flag
  
Response: OK (0x10)
```

**Read Status** (Get Positions):
```
Modbus Read Request:
  Function: 0x04 (Read Input Registers)
  Slave ID: 1
  Start Reg: 30001
  Count: 4 registers
  
Response: [0x00, 0x64, 0x00, 0x32]
           ^^^^^^^^^  ^^^^^^^^^
           Source=100 Sink=50
```

---

## PLC Requirements on Your Machine

### CoDeSys Configuration Required

1. **Modbus TCP Server Device**:
```
Device Tree:
??? PLC Logic (Application)
    ??? Modbus TCP Server
        ??? Port: 502
        ??? Unit ID: 1
        ??? Enable: YES
        ??? Network Interface: 127.0.0.1 (localhost)
```

2. **Variable Mapping to Modbus Registers**:
```
CoDeSys Symbol Configuration:
??? CECC-D.Application
    ??? MakeSurface
    ?   ??? Z_Stacks ? Holding Register 40013
    ?   ??? Delta_Source ? Holding Register 40005
    ?   ??? Delta_Sink ? Holding Register 40007
    ?   ??? Marcer_Source_Cylinder_ActualPosition ? Input Register 30001
    ?   ??? Marcer_Sink_Cylinder_ActualPosition ? Input Register 30003
    ??? StartUpSequence
        ??? StartUp ? Holding Register 40003
```

3. **OPC UA Server on PLC** (Optional - for direct access):
```
Device Tree:
??? OPC UA Server
    ??? Port: 4840
    ??? Enable: YES
    ??? Endpoint: opc.tcp://0.0.0.0:4840
    ??? Symbol Configuration: Export All
```

---

## Verification Commands

### Check PLC is Running

```bash
# Windows
ping localhost
ping 127.0.0.1

# Check Modbus port is open
telnet localhost 502
# OR
Test-NetConnection -ComputerName localhost -Port 502
```

### Check OPC UA Server is Running

```bash
# Check port 4840 is listening
netstat -an | findstr :4840

# Should show:
TCP    0.0.0.0:4840           0.0.0.0:0              LISTENING
# OR
TCP    127.0.0.1:4840         0.0.0.0:0              LISTENING
```

### Check Both Services

```bash
# Full check
netstat -an | findstr "502 4840"

# Expected output:
TCP    127.0.0.1:502          0.0.0.0:0              LISTENING  ? PLC Modbus
TCP    127.0.0.1:4840         0.0.0.0:0              LISTENING  ? OPC UA Server
```

---

## Expected Behavior

### Success Case (PLC Connected)

**Server Logs**:
```
? [START] CoDeSys PLC CONNECTED SUCCESSFULLY
? [START] Initial PLC Status:
? [START]   Source Position: 0 µm
? [START]   Sink Position: 0 µm
? [START]   Emergency Stop: OK
? [START]   PLC Heartbeat: 1234
? [START] OPC UA Server STARTED
? [START] PLC Connection: ACTIVE
```

**Client Logs**:
```
? Connected to OPC UA server: opc.tcp://localhost:4840
? Setting up node IDs...
? Successfully created OPC UA node IDs (namespace: 2)
? Powder fill parameters sent to PLC (OPC UA)
```

### Degraded Case (PLC Not Connected)

**Server Logs**:
```
? [ERROR] FAILED TO CONNECT TO CoDeSys PLC!
?? [ERROR] The server will continue in DEGRADED MODE.
? [START] OPC UA Server STARTED
?? [START] PLC Connection: DEGRADED
```

**Client Logs**:
```
? Connected to OPC UA server: opc.tcp://localhost:4840
? Setting up node IDs...
? Successfully created OPC UA node IDs (namespace: 2)
? Powder fill parameters sent to PLC (OPC UA)
?? (Hardware will not move - no PLC connection)
```

---

## Key Differences: Simulation vs Production

| Aspect | Simulation Mode | Production Mode |
|--------|----------------|-----------------|
| **PLC Connection** | No connection | Connects to localhost:502 |
| **Hardware Control** | Emulated in software | Real Modbus TCP commands |
| **Position Updates** | Calculated values | Read from PLC encoders |
| **Status Flags** | Instant (software) | Real timing from PLC |
| **Safety** | Software limits only | Hardware + software limits |
| **Risk** | Zero risk | Controls real hardware |
| **Use Case** | Testing, development | Production operation |

---

## Command Reference

### Start Production Server

```bash
cd OPCUAServer/build/Release

# Connect to real PLC (default mode now)
./OPCUAServer.exe --production

# If you need simulation (testing without hardware)
./OPCUAServer.exe --simulate
```

### Environment Variable Overrides

```bash
# Override PLC IP if not localhost
set PLC_IP_ADDRESS=192.168.1.10

# Force simulation mode
set OPC_UA_SIMULATE=1

# Override OPC UA endpoint
set OPC_UA_ENDPOINT=opc.tcp://0.0.0.0:4840
```

---

## Architecture Summary

```
Your Application (Client)
         ? OPC UA (ns=2)
         ? opc.tcp://localhost:4840
         ?
OPC UA Server (This Application)
         ? Modbus TCP
         ? localhost:502
         ?
CoDeSys PLC (On Same Machine)
         ? Digital I/O
         ?
Physical Hardware (Motors, Cylinders, Laser)
```

---

## Final Checklist

- [x] **Server configured** for production mode (default)
- [x] **PLC IP** set to `localhost` (same machine)
- [x] **Namespace index** set to `2` (matches client)
- [x] **Node IDs** use `|var|` prefix
- [x] **Data types** corrected (INT16/INT32/BOOL)
- [x] **Thread safety** implemented
- [x] **Memory safety** via RAII
- [x] **Type safety** via strong typing
- [x] **Error handling** comprehensive
- [x] **Documentation** complete
- [x] **Build** successful ?

---

## Next Action

**Run the server now:**

```bash
cd OPCUAServer/build/Release
./OPCUAServer.exe --production
```

**Look for this in the logs:**
```
[START] CoDeSys PLC CONNECTED SUCCESSFULLY  ? This confirms hardware connection!
[START] OPC UA Server STARTED
[START] PLC Connection: ACTIVE
```

If you see this, **your server is successfully connected to real hardware!** ??

---

**End of Real Hardware Configuration Summary**
