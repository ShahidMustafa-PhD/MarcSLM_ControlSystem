# PRODUCTION VERIFICATION CHECKLIST

## Pre-Deployment Validation

### Code Review Checklist
- [ ] All `try-catch` blocks present in thread entry points
  - opcThreadFunc() has outer try-catch (line ~276)
  - OPCWorker::initialize() has try-catch (line ~48)
  - OPCWorker::shutdown() has try-catch (line ~147)
  - startWorkers() has try-catch (line ~180)
  - stopWorkers() has try-catch (line ~220)

- [ ] No unregistered Qt meta-types in signals
  - All signal parameters are: bool, QString, int, or void
  - No size_t, uint32_t, or custom structs in signals
  - All cross-thread signals use Qt::QueuedConnection

- [ ] UA_NodeId cleanup verified
  - clearAllNodeIds() called in destructor (line ~62 opcserverua.cpp)
  - UA_NodeId_clear() called for each allocated node (lines 661-702)
  - No ua_types.c functions called directly (wrapped via safe interfaces)

- [ ] UA_Client lifecycle verified
  - UA_ClientDeleter custom deleter implemented (opcserverua.h line 38-44)
  - mClient is std::unique_ptr<UA_Client, UA_ClientDeleter>
  - disconnectFromServer() calls mClient.reset()

- [ ] Thread state validation verified
  - startWorkers() checks mOPCThread.joinable() before creation (line 187)
  - stopWorkers() checks mOPCThread.joinable() before join (line 246)
  - Thread creation wrapped in try-catch (line 195)

- [ ] Exception safety verified
  - All UA_Variant variables initialized/cleared in same scope
  - All resource allocations guarded with try-catch
  - RAII principles followed for UA_Client and UA_NodeId

### Build Verification
```bash
# Step 1: Clean build
cmake --clean
cmake --build . --config Release

# Expected: No compilation errors
# Expected: No linking errors (UA_Client* functions resolved)
```

- [ ] Build succeeds with zero errors
- [ ] Build succeeds with zero warnings (in modified files)
- [ ] All symbols resolve (open62541 lib linked)

### Runtime Verification (Startup)

**Test Case 1: Normal Startup**
```
Expected Log Output:
  SLMWorkerManager created in thread QThread(...)
  SLMWorkerManager::startWorkers() - Starting OPC UA worker thread
  SLMWorkerManager::startWorkers() - Creating std::thread
  SLMWorkerManager::startWorkers() - OPC UA worker thread spawned successfully
  opcThreadFunc() - Thread function started
  opcThreadFunc() - Worker thread ID: "12345" (some numeric thread ID)
  opcThreadFunc() - Connecting worker signals
  opcThreadFunc() - Signals connected successfully
  opcThreadFunc() - Calling localWorker.initialize()
  OPCWorker::initialize() - Starting OPC UA initialization in thread "12345"
  OPCWorker::initialize() - Creating OPCServerManagerUA instance
  OPCWorker::initialize() - OPC UA manager created successfully
  OPCWorker::initialize() - Attempting to connect to OPC UA server
  ========== OPC UA Initialization Starting ==========
  Connecting to OPC UA Server...
  OPC UA URL (default): opc.tcp://localhost:4840
  Configured namespace index: 2
  Initiating connection phase...
  Attempting to connect to server...
  Connecting to: opc.tcp://localhost:4840
  [If simulator running]: ? Connected to OPC UA server: ...
  Setting up OPC UA node IDs...
  ? Successfully created OPC UA node IDs (namespace: 2)
  OPC UA Server initialized successfully
  ========== OPC UA Initialization COMPLETE ==========
  OPCWorker::initialize() - OPC UA server initialized successfully
  OPCWorker::initialize() - Connection established, ready for operations
  opcThreadFunc() - Initialization completed, checking manager pointer
  opcThreadFunc() - Storing OPC UA manager pointer atomically
  opcThreadFunc() - Entering wait loop
  SLMWorkerManager::onOPCInitialized() - Received initialization result: true
  SLMWorkerManager::onOPCInitialized() - OPC UA initialized successfully
  [Signal emitted]: systemReady()
```

Check:
- [ ] Thread ID printed (confirms thread was created)
- [ ] No "Unhandled exception" in opcThreadFunc
- [ ] No "Cannot queue arguments" error
- [ ] systemReady() signal emitted (GUI responds)
- [ ] Consumer threads start successfully after this

**Test Case 2: OPC UA Server Not Running**
```
Expected Log Output:
  opcThreadFunc() - Thread function started
  OPCWorker::initialize() - Starting OPC UA initialization in thread "12345"
  ...
  Attempting to connect to server...
  Connecting to: opc.tcp://localhost:4840
  ERROR: Connection failed with status: BadTimeout
  Hint: Connection timeout. Server may be slow to respond.
  ERROR: Failed to connect to OPC UA server
  ========== OPC UA Initialization FAILED ==========
  OPCWorker::initialize() - OPC UA initialization failed (returned false)
  opcThreadFunc() - Initialization completed, checking manager pointer
  [No "Storing OPC UA manager pointer" message]
  opcThreadFunc() - Entering wait loop
  SLMWorkerManager::onOPCInitialized() - Received initialization result: false
  SLMWorkerManager::onOPCInitialized() - OPC UA initialization failed
  [Signal emitted]: systemError("OPC UA initialization failed")
```

Check:
- [ ] No crash (no c0000374 error)
- [ ] No unhandled exception
- [ ] Graceful error handling via signal
- [ ] Application can retry startup

### Runtime Verification (Shutdown)

**Test Case 3: Normal Shutdown**
```
Expected Log Output:
  [Shutdown initiates...]
  ScanStreamingManager::stopProcess() - Initiating graceful shutdown
  [Producer/Consumer log messages...]
  Producer thread finished
  Consumer thread finished
  ProcessController::shutdownOPCWorker()
  SLMWorkerManager::stopWorkers() - Initiating graceful shutdown
  SLMWorkerManager::stopWorkers() - Shutdown signal sent
  SLMWorkerManager::stopWorkers() - Waiting for OPC UA thread to join
  opcThreadFunc() - Shutdown signal received, cleaning up
  OPCWorker::shutdown() - Shutting down OPC UA in thread "12345"
  OPCWorker::shutdown() - Calling OPC UA manager destructor
  OPC UA client disconnected
  All OPC UA node IDs cleared
  [No mention of "double-free", "invalid pointer", or c0000374]
  OPCWorker::shutdown() - Shutdown complete
  opcThreadFunc() - Thread function exiting normally
  SLMWorkerManager::stopWorkers() - OPC UA thread joined successfully
  SLMWorkerManager::stopWorkers() - Shutdown complete
  [Application exits cleanly]
```

Check:
- [ ] "Shutdown signal received" printed (thread woke from condition_variable)
- [ ] "All OPC UA node IDs cleared" printed (no heap corruption risk)
- [ ] "Thread function exiting normally" printed (no exception)
- [ ] "Thread joined successfully" printed (join completed)
- [ ] No c0000374 error
- [ ] No heap corruption warnings

**Test Case 4: Multiple Start/Stop Cycles**
```
Procedure:
  1. Start application ? see systemReady() signal
  2. Stop application ? see normal shutdown
  3. Restart without killing process ? see startup again
  4. Verify no "previous thread still active" errors
  5. Verify no memory leaks

Expected:
  First startup: Full init sequence
  Shutdown: Normal cleanup
  Second startup: Full init sequence (from scratch)
  [No errors about thread state]
  [No growing memory footprint]
```

Check:
- [ ] Second startup succeeds
- [ ] No thread state errors
- [ ] Logs show clean state between cycles
- [ ] Memory stable (task manager / Process Explorer)

### Stress Verification

**Test Case 5: OPC UA Connection Loss During Operation**
```
Procedure:
  1. Start application, initialize OPC UA
  2. Kill OPC UA simulator/server
  3. Try to write OPC tag from GUI
  4. Observe error handling

Expected:
  OPCController::writeStartUp() ? writeBoolNode()
  [In writeBoolNode, UA_Client_writeValueAttribute returns BADCONNECTIONCLOSED]
  handleConnectionLoss() sets mConnectionLost = true
  [Signal emitted]: connectionLost()
  [Subsequent writes fail immediately without hanging]
  GUI displays error, user can retry
```

Check:
- [ ] No hang (write times out immediately)
- [ ] Error logged with reason
- [ ] connectionLost() signal emitted
- [ ] Subsequent operations fail fast (don't retry indefinitely)

**Test Case 6: Shutdown During OPC UA Operation**
```
Procedure:
  1. Start application
  2. Immediately call stopWorkers() (while init in progress)
  3. Observe for race conditions

Expected:
  opcThreadFunc() sees mOPCRunning.load() == false
  Exits wait loop (if past initialization)
  Calls shutdown() successfully
  No partial initialization artifacts
  No memory leaks
```

Check:
- [ ] No crash
- [ ] Logs show orderly shutdown
- [ ] No hanging threads

### Memory Verification

**Test Case 7: Heap Integrity After Shutdown**
```
Tools:
  - Windows Task Manager (Commit Size)
  - Visual Studio Debug Heap
  - Dr. Memory (external tool)

Procedure:
  1. Note baseline memory
  2. Startup OPC UA
  3. Perform operations (read/write tags)
  4. Shutdown OPC UA
  5. Check memory returned to baseline

Expected:
  No memory growth after shutdown
  No heap corruption errors in debug output
```

Check:
- [ ] Memory freed (commit size returns to baseline)
- [ ] No heap corruption warnings
- [ ] No resource leaks (handles, threads, mutexes)

---

## Post-Deployment Monitoring

### Application Logs (Daily)
- [ ] No "Unhandled exception" messages
- [ ] No "c0000374" error codes
- [ ] No "Cannot queue arguments of type" messages
- [ ] No "double-free" or "invalid pointer" warnings
- [ ] Thread startup/shutdown logged cleanly

### System Logs (Weekly)
- [ ] No application crashes in Event Viewer
- [ ] No heap corruption events
- [ ] No thread-related exceptions

### Hardware Integration Test (First Run)
- [ ] Startup sequence completes
- [ ] OPC UA reads PLC parameters correctly
- [ ] Scanner initializes and executes layers
- [ ] Shutdown sequence is orderly
- [ ] No hardware left in bad state

---

## Troubleshooting Guide

If you see these errors after deployment, here's what to check:

### Error: "Unhandled exception in opcThreadFunc"
**Cause**: Exception in OPCWorker::initialize() or setupNodeIds()
**Action**: Check logs for specific exception message, verify OPC UA server is reachable

### Error: "Cannot queue arguments of type 'size_t'"
**Cause**: Old binary or code still has unregistered meta-types
**Action**: Clean build, rebuild all, verify no Qt signal parameter types are custom types

### Error: "Critical error detected c0000374"
**Cause**: UA_NodeId not cleared, or UA_Client deleted twice
**Action**: Verify opcserverua.cpp destructor calls clearAllNodeIds(), check UA_ClientDeleter custom deleter is present

### Error: "OPC UA thread still active" on restart
**Cause**: Previous thread still running, or join() failed
**Action**: Verify stopWorkers() completes before startWorkers() called again, check for hangs

### Error: "Segmentation fault during shutdown"
**Cause**: Accessing cleared OPC manager pointer
**Action**: Verify all OPC operations check mIsInitialized before proceeding, check no dangling pointers

---

## Success Criteria

After applying all fixes, your application should:

? **Start cleanly**
- Thread created successfully
- OPC UA connects and initializes
- systemReady() signal emitted
- Scanner threads start and consume tasks

? **Run stably**
- No crashes during operation
- OPC UA reads/writes execute
- Layer synchronization works
- No memory growth over time

? **Shutdown cleanly**
- All threads join successfully
- No heap corruption
- No orphaned resources
- Process exits with code 0

? **Recover from errors**
- Connection loss handled gracefully
- Retry mechanism works
- Manual restart succeeds
- No stuck threads

If all test cases pass and logs show clean operation, your application is production-ready.

