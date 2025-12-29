# CRASH ROOT CAUSE ANALYSIS - Test SLM Process

## Executive Summary
**Critical Issues Causing ~5s Crash**:

1. **RACE CONDITION: Multiple Scanner Instances**
   - `ScannerController::mScanner` (UI thread) initialized via manual buttons
   - `ScanStreamingManager::consumerThreadFunc()` creates local `Scanner` (consumer thread)
   - Both compete for global `s_rtc5Opened` static flag ? double-init/double-close of native DLL
   - Symptom: Access violation 5s after start when hardware state becomes inconsistent

2. **UNPROTECTED GLOBAL STATE**
   - `static bool s_rtc5Opened` is NOT atomic
   - No mutex around `RTC5open()`, `init_rtc5_dll()`, `free_rtc5_dll()`, `RTC5close()`
   - Thread A calls `RTC5open()` while Thread B calls `RTC5close()` ? **CRASH**

3. **MISSING THREAD OWNERSHIP ASSERTIONS**
   - No checks that RTC5 API calls happen on the owner thread
   - Commented-out `std::lock_guard` calls throughout Scanner class
   - No thread-id tracking; any thread can call any method

4. **UNSAFE SHUTDOWN PATTERN**
   - `releaseResources()` called from multiple contexts without coordination
   - `Scanner::~Scanner()` calls `shutdown()` which calls `releaseResources()`
   - If two Scanner instances exist and both destruct, double-free of DLL

5. **EXCEPTION-UNSAFE INITIALIZATION**
   - `initialize()` can throw exceptions that leave DLL in half-initialized state
   - No rollback mechanism if mid-initialization fails
   - Subsequent operations on partially-initialized hardware crash

6. **INFINITE LOOPS + HARDWARE TIMEOUT**
   - `plotLineUnlocked()` has 5000-retry loop (5s total)
   - If hardware never responds, loop completes but list state may be corrupted
   - Following commands may fail or crash due to inconsistent state

7. **MISSING QUEUE FLUSH PROTECTION**
   - `flushQueue()` uses busy-wait without timeout
   - If hardware stuck, infinite loop blocks consumer thread
   - Test mode enqueues synthetic layers without proper synchronization

## Detailed Failure Modes

### Failure Mode 1: Concurrent DLL Initialization
```
Timeline:
  T=0.0s   Consumer thread: Scanner scanner; scanner.initialize()
           ? initializeRTC5() ? RTC5open() ? s_rtc5Opened=true
  
  T=2.5s   Warmup/loop in configureTimings() - waiting for laser
  
  T=2.7s   UI thread (stale reference): destructor called on old ScannerController::mScanner
           ? Scanner::~Scanner() ? shutdown() ? releaseResources()
           ? free_rtc5_dll() + RTC5close()
           
  T=2.8s   Consumer thread still in `while (busy)` loop in configureTimings()
           ? calls get_status() on closed DLL
           ? **ACCESS VIOLATION**
```

### Failure Mode 2: Incomplete Mutex Protection
```cpp
// Scanner.cpp (current code):
bool Scanner::initialize() {
    // NO LOCK - not protected
    if (!initializeRTC5()) { ... }
    // ...
}

// initializeRTC5() - also NO LOCK
if (!s_rtc5Opened) {  // ?? Race: two threads read false simultaneously
    if (RTC5open()) { ... }  // Both call RTC5open() - CRASH
    s_rtc5Opened = true;  // Second write undefined behavior
}
```

### Failure Mode 3: Thread Ownership Violation
```cpp
// Main thread initializes on UI thread:
ScannerController::initialize() 
  ? Scanner::initialize()  // RTC5 DLL opened on UI thread

// Test process spawns consumer thread:
consumerThreadFunc()
  ? Scanner scanner; scanner.initialize()  // RTC5 DLL opened AGAIN on consumer thread
  ? scanner.executeList()  // ? WRONG THREAD - RTC5 expects same thread as init
```

## Why the Crash Happens at ~5 Seconds

1. `configureTimings()` is the SLOWEST initialization step
2. It includes `long_delay(2000000/10)` = 2 second hardware warmup
3. Then `set_start_list()`, `execute_list()`, and busy-wait loop
4. Total = ~5s for full initialization sequence
5. **Crash occurs at end of initialization loop when hardware doesn't respond as expected** due to DLL state corruption from concurrent access

## Key Evidence

1. **Commented-out mutexes throughout Scanner.cpp**:
   ```cpp
   bool Scanner::jumpTo(...) {
       //std::lock_guard<std::mutex> lock(mMutex);  // ?? COMMENTED OUT!
       if (!mIsInitialized) return false;
   ```

2. **Unprotected global state**:
   ```cpp
   static bool s_rtc5Opened = false;  // ?? NOT atomic, NOT protected
   ```

3. **No thread ownership tracking**:
   ```cpp
   void Scanner::releaseResources() {
      // assert(std::this_thread::get_id() == mOwnerThread);  // ?? NO ASSERTION
       free_rtc5_dll();
       RTC5close();
   }
   ```

4. **Double-free pattern in destructor**:
   ```cpp
   Scanner::~Scanner() {
       shutdown();  // Which calls releaseResources()
   }
   // If two Scanners exist and both are destroyed...CRASH
   ```

## Solutions Provided

1. **Re-enable and fix mutex protection** (was intentionally commented out)
2. **Make `s_rtc5Opened` atomic + protect all init/close with global mutex**
3. **Add thread ownership assertions** throughout RTC5 API calls
4. **Implement reference counting** for DLL lifetime (only close when last user exits)
5. **Add exception handling** to all critical methods
6. **Implement timeout protection** for busy-wait loops
7. **Ensure single Scanner instance** across entire application lifetime
8. **Add meaningful error logging** instead of silent crashes

---

## Files Modified
1. `scanner_lib/Scanner.h` - Add thread tracking, ref counting, thread assertions
2. `scanner_lib/Scanner.cpp` - Complete overhaul with proper synchronization
3. `controllers/scannercontroller.cpp` - Route all access through single instance
4. `controllers/scanstreamingmanager.cpp` - Verify consumer-thread-only access

---

**Status**: Ready for implementation
**Risk Level**: HIGH - DLL concurrency is critical
**Mitigation**: Must enforce single RTC5 owner (consumer thread only)
