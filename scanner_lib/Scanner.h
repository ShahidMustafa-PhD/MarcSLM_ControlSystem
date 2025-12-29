#ifndef SCANNER_H
#define SCANNER_H

#include <windows.h>
#include <functional>
#include <string>
#include <vector>
#include <mutex>
#include <atomic>
#include <thread>
#include <algorithm>

// RTC5 header file for explicitly linking to the RTC5DLL.DLL
#include "RTC5expl.h"

// ============================================================================
// Global RTC5 DLL Lifecycle Management (Process-Wide)
// ============================================================================
// CRITICAL: RTC5 DLL has global state that is NOT thread-safe
// This manager ensures only ONE thread can initialize/close the DLL at a time
// Reference counting ensures DLL stays open while any Scanner uses it
class RTC5DLLManager {
public:
    static RTC5DLLManager& instance();
    
    // ✅ NEW: Thread-safe reference counting for DLL lifetime
    bool acquireDLL();      // Call before RTC5open() - increments ref count
    void releaseDLL();      // Call after RTC5close() - decrements ref count
    
private:
    RTC5DLLManager();
    ~RTC5DLLManager();
    
    static std::mutex sMutex;
    static std::atomic<int> sRefCount;      // ✅ Atomic reference counter
    static std::atomic<bool> sRTC5Opened;   // ✅ Atomic flag
    static RTC5DLLManager* sInstance;
};

// ============================================================================
// Scanner Class - Handles RTC5 Scanner operations for SCANLAB
// ============================================================================
class Scanner {
public:
    // Constructor / Destructor
    Scanner();
    ~Scanner();
    
    // ✅ NEW: Delete copy/move operations to prevent dual ownership
    Scanner(const Scanner&) = delete;
    Scanner& operator=(const Scanner&) = delete;
    Scanner(Scanner&&) = delete;
    Scanner& operator=(Scanner&&) = delete;

    // Configuration constants (can be customized)
    struct Config {
        UINT cardNumber = 1;            // Default card number
        UINT listMemory = 10000;        // Size of list 1 memory
        UINT laserMode = 1;             // YAG 1 mode
        UINT laserControl = 0x18;       // Laser signals LOW active
        UINT startGap = 1000;           // Gap ahead between input_pointer and out_pointer
        UINT loadGap = 100;             // Gap ahead between out_pointer and input_pointer
        UINT pointerCount = 0x3F;       // Pointer mask for checking the gap

        // Analog output
        UINT analogOutChannel = 1;      // AnalogOut Channel 1 used
        UINT analogOutValue = 640;      // Standard Pump Source Value
        UINT analogOutStandby = 0;      // Standby Pump Source Value
        // Enhanced laser control
        UINT laserAnalogMode = 0;      // 0=digital, 1=analog1, 2=analog2
        UINT laserPowerMin = 0;         // Minimum laser power (0-4095)
        UINT laserPowerMax = 4095;      // Maximum laser power (0-4095)
        UINT laserStandbyPower = 0;     // Standby power level
        // Timing parameters
        UINT warmUpTime = 2000000 / 10; // 2 s [10 us]
        UINT laserHalfPeriod = 50 * 8;  // 50 us [1/8 us]
        UINT laserPulseWidth = 5 * 8;   // 5 us [1/8 us]
        UINT firstPulseKiller = 200 * 8;// 200 us [1/8 us]
        long laserOnDelay = 100 * 1;    // 100 us [1 us]
        UINT laserOffDelay = 100 * 1;   // 100 us [1 us]

        // Scanner delays and speeds
        UINT jumpDelay = 250 / 10;      // 250 us [10 us]
        UINT markDelay = 100 / 10;      // 100 us [10 us]
        UINT polygonDelay = 50 / 10;    // 50 us [10 us]
        double markSpeed = 250.0;       // [16 Bits/ms]
        double jumpSpeed = 1000.0;      // [16 Bits/ms]

        // Wobble/modulation parameters (for better surface quality)
        bool enableWobble = false;
        UINT wobbleTransversal = 0;     // Transversal amplitude (microns)
        UINT wobbleLongitudinal = 0;    // Longitudinal amplitude (microns)
        double wobbleFreq = 0.0;        // Wobble frequency (Hz)
    };
    struct ScannerStatus {
        UINT isBusy;           // Changed from bool to UINT for compatibility with get_status
        UINT listPosition;
        UINT inputPointer;
        UINT error;
        long encoderX;
        long encoderY;
    };

    // Point structure
    struct Point {
        long x;
        long y;

        Point() : x(0), y(0) {}
        Point(long xVal, long yVal) : x(xVal), y(yVal) {}

        // ✅ Add validation
        bool isValid() const {
            return (x >= -32767 && x <= 32767 && 
                    y >= -32767 && y <= 32767);
        }
    };

    // Initialization
    // ✅ CRITICAL: Must be called from the thread that will own the Scanner
    bool initialize(const Config& config = Config());
    bool isInitialized() const { return mIsInitialized; }
    void shutdown();

    // Basic operations
    bool startScanning();
    bool stopScanning();
    bool pauseScanning();
    bool resumeScanning();
    bool isScanning() const { return mIsScanning; }

    // Drawing operations
    bool jumpTo(const Point& destination);
    bool markTo(const Point& destination);
    bool plotLine(const Point& destination);
    void setBeamDump(const Point& location);

    // ========== NEW: RTC5 List Buffer Management (Demo3 Pattern) =========
    // These methods follow the proven Demo3.cpp pattern for reliable command queuing
    bool loadListBuffer(UINT listNumber, UINT position);  // Checks if buffer is ready
    UINT getCurrentListLevel() const { return mListLevel; }
    bool isListBufferFull() const { return mListLevel >= (mConfig.listMemory - 1); }
    void resetListLevel() { mListLevel = 0; }

    // Laser control
    bool enableLaser();
    bool disableLaser();
    bool setLaserPower(UINT channel, UINT value);

    // List management
    bool executeList();
    bool flushQueue();
    void getStatus(UINT& busy, UINT& position);
    UINT getInputPointer();

    // Configuration
    void setConfig(const Config& config);
    Config getConfig() const { return mConfig; }

    // Error handling
    UINT getLastError() const;
    std::string getErrorMessage() const;
    bool resetError();
    ScannerStatus getDetailedStatus();

    // Enhanced laser control (added in list)
    bool setLaserPowerList(UINT value);
    bool laserSignalOnList();
    bool laserSignalOffList();

    // Dynamic speed control (per segment during marking)
    bool setMarkSpeedList(double speed);
    bool setJumpSpeedList(double speed);

    // ========== NEW: Per-segment parameter control =========
    bool applySegmentParameters(double laserPower, double laserSpeed, double jumpSpeed);

    // Delay and timing control
    bool addDelay(UINT delayMicroseconds);
    bool setScannerDelays(UINT jump, UINT mark, UINT polygon);

    // List management helpers
    bool waitForListCompletion(UINT timeoutMs = 5000);
    UINT getListSpace();

    // Callbacks for logging
    void setLogCallback(std::function<void(const std::string&)> callback);

    // Drawing helpers
    bool drawVectors(const std::vector<Point>& points, bool closeLoop = false);
    bool drawPolyline(const std::vector<Point>& points);
    bool drawPolygon(const std::vector<Point>& points);

    // Wobble/modulation support
    bool setWobble(UINT transversal, UINT longitudinal, double freq);
    bool disableWobble();

    // Position query (requires special RTC5 configuration)
    bool getCurrentPosition(long& x, long& y);

    // Pixel/raster scanning support
    bool setPixelMode(UINT pulseLength, UINT analogOut);
    bool setPixelLine(UINT channel, UINT halfPeriod, double dX, double dY);
    bool prepareListForLayer();

    // ✅ NEW: Thread ownership verification (for debugging)
    std::thread::id getOwnerThread() const { return mOwnerThread; }

private:
    // Internal helper methods
    bool initializeRTC5();
    bool loadFiles();
    bool configureLaser();
    bool configureTimings();
    void logMessage(const std::string& message);
    int plotLineInternal(const Point& destination, UINT* start);
    void releaseResources();
    bool checkRTC5Error(const std::string& operation);
    void terminateDLL();

    // Unlocked helpers to avoid re-entrant deadlocks
    bool plotLineUnlocked(const Point& destination);
    bool jumpToUnlocked(const Point& destination);
    bool markToUnlocked(const Point& destination);

    // ✅ NEW: Thread ownership assertion macro
    void assertOwnerThread() const;

    // State variables
    bool mIsInitialized;
    bool mIsScanning;
    UINT mLastError;
    Config mConfig;
    Point mBeamDump;
    UINT mStartFlags;
    
    // ✅ NEW: Thread ownership tracking
    std::thread::id mOwnerThread;
    bool mOwnerThreadSet;
    
    // ✅ NEW: RTC5 List Buffer Management (Demo3 Pattern)
    UINT mListLevel;           // Current number of commands in active list
    UINT mCurrentList;         // Current list number (1 or 2 for dual buffering)
    bool mFirstExecution;      // Track first list execution vs auto_change()
    
    // Callback
    std::function<void(const std::string&)> mLogCallback;

    // Constants
    static constexpr double Pi = 3.14159265358979323846;

    // Mutex for thread safety
    mutable std::mutex mMutex;
};

#endif // SCANNER_H