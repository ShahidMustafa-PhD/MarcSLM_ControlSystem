# QUICK FIX REFERENCE - RTC5 Scanner Command Failure

## THE ERROR
```
Scanner command failed at index 0 in layer 1, Command type: 0
```

## THE CAUSE
Missing `restart_list()` call after warmup - list pointers not reset to zero

## THE FIX (3 Changes)

### 1. After Warmup (configureTimings)
```cpp
// After warmup completes, ADD:
stop_execution();
reset_error(-1);
restart_list();        // ? CRITICAL: Resets list pointers to 0
set_start_list(1);
mStartFlags = 1;
```

### 2. Between Layers (stopScanning)
```cpp
// After layer execution, ADD:
disable_laser();
stop_execution();
reset_error(-1);
restart_list();        // ? CRITICAL: Resets list pointers to 0
set_start_list(1);
mStartFlags = 1;
```

### 3. Parameter Application (applySegmentParameters)
```cpp
// CHANGE:
write_da_x(channel, power);       // ? OLD: Immediate mode

// TO:
write_da_x_list(channel, power);  // ? NEW: List mode
```

## WHY restart_list() IS CRITICAL

It resets RTC5 DSP list pointers:
- Input pointer: 8 ? 0
- Output pointer: 8 ? 0
- List buffer: cleared
- State machine: reset to EMPTY

Without it: Commands queue at WRONG positions ? Execution reads garbage data ? Command fails

## BUILD STATUS
? Build successful

## EXPECTED RESULT
All 5 test layers execute without errors

## FILES CHANGED
- `scanner_lib/Scanner.cpp` (3 methods)

## VERIFICATION
Check log for:
```
List re-initialized: input pointer at 0
List reset complete: input=0, position=0, busy=0
```
