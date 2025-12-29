# QUICK REFERENCE - Thread Ownership Fix

## THE ERROR
```
Scanner command failed at index 0 in layer 1, Command type: 0
```

## ROOT CAUSE
Two Scanner instances trying to control same RTC5 hardware from different threads:
- Scanner #1: UI thread (`ScannerController::mScanner`)
- Scanner #2: Consumer thread (`consumerThreadFunc()` local)

## THE FIX

### 1. Prevent UI Init During Process
```cpp
// In on_InitScanner_clicked():
if (processRunning) {
    ? ERROR: Cannot init on UI thread
    return;
}
?? WARNING: Diagnostics only
```

### 2. Shutdown UI Scanner Before Test
```cpp
// In onTestSLMProcess_clicked():
if (uiScannerActive) {
    shutdown();
    ? Clean for consumer thread
}
```

### 3. Add Process State Check
```cpp
// In processcontroller.h:
ProcessState getState() const;
```

## RULES

? **ONLY ONE Scanner instance**  
? **Test/Production: Consumer thread ONLY**  
? **Manual diagnostics: UI thread ONLY (with warning)**  
? **Auto-cleanup if conflict**  

## BUILD STATUS
? Build successful

## TESTING
```
Test SLM Process ? All 5 layers execute successfully
```

## FILES CHANGED
- `launcher/mainwindow.cpp` (2 methods)
- `controllers/processcontroller.h` (1 method added)
