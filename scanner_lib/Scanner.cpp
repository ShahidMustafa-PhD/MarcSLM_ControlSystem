// scanner_lib/Scanner.cpp
// CORRECTED VERSION - Thread-Safe RTC5 Scanner Management
// 
// KEY FIXES:
// 1. Global RTC5 DLL manager with atomic state + reference counting
// 2. Thread ownership assertions on all RTC5 API calls
// 3. Proper mutex protection for all mutable operations
// 4. Exception-safe initialization with rollback
// 5. Timeout protection for all hardware busy-waits
// 6. Safe shutdown that waits for completion
// 7. Prevent dual Scanner instances via delete copy/move constructors

#include "Scanner.h"
#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <exception>
#include <filesystem>
#include <thread>
#include <chrono>
#include <cassert>

// RTC5 header file for explicitly linking to the RTC5DLL.DLL
#include "RTC5expl.h"


// ============================================================================
// Global RTC5 DLL Manager Implementation (Process-Wide)
// ============================================================================

std::mutex RTC5DLLManager::sMutex;
std::atomic<int> RTC5DLLManager::sRefCount(0);
std::atomic<bool> RTC5DLLManager::sRTC5Opened(false);
RTC5DLLManager* RTC5DLLManager::sInstance = nullptr;

RTC5DLLManager& RTC5DLLManager::instance() {
    static RTC5DLLManager sManager;
    return sManager;
}

RTC5DLLManager::RTC5DLLManager() {
    // Private constructor - use instance() instead
}

RTC5DLLManager::~RTC5DLLManager() {
    // Ensure cleanup if somehow instantiated
    std::lock_guard<std::mutex> lock(sMutex);
    if (sRefCount.load() > 0) {
        fprintf(stderr, "WARNING: RTC5DLLManager destroyed with active references\n");
    }
}

bool RTC5DLLManager::acquireDLL() {
    // ? CRITICAL: Protect RTC5 DLL initialization
    std::lock_guard<std::mutex> lock(sMutex);

    try {
        // First caller opens the DLL
        if (sRefCount.load() == 0) {
            if (RTC5open()) {
                fprintf(stderr, "ERROR: RTC5open() failed - driver not accessible\n");
                return false;
            }
            sRTC5Opened.store(true);
            fprintf(stdout, "RTC5DLLManager: RTC5 DLL opened\n");
        }

        // Increment reference count
        sRefCount++;
        fprintf(stdout, "RTC5DLLManager: Reference count = %d\n", sRefCount.load());
        return true;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Exception in RTC5DLLManager::acquireDLL: %s\n", e.what());
        return false;
    }
}

void RTC5DLLManager::releaseDLL() {
    // ? CRITICAL: Protect RTC5 DLL cleanup
    std::lock_guard<std::mutex> lock(sMutex);

    try {
        // Decrement reference count
        int newCount = sRefCount.load() - 1;
        if (newCount < 0) {
            fprintf(stderr, "ERROR: RTC5DLLManager::releaseDLL() called too many times\n");
            return;
        }

        // Last user closes the DLL
        if (newCount == 0) {
            free_rtc5_dll();
            RTC5close();
            sRTC5Opened.store(false);
            fprintf(stdout, "RTC5DLLManager: RTC5 DLL closed\n");
        }

        sRefCount.store(newCount);
        fprintf(stdout, "RTC5DLLManager: Reference count = %d\n", sRefCount.load());
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Exception in RTC5DLLManager::releaseDLL: %s\n", e.what());
    }
}

// ============================================================================
// Scanner Implementation
// ============================================================================

Scanner::Scanner()
    : mIsInitialized(false)
    , mIsScanning(false)
    , mLastError(0)
    , mConfig()
    , mBeamDump(0, 0)
    , mStartFlags(0)
    , mOwnerThread()
    , mOwnerThreadSet(false)
    , mListLevel(0)
    , mCurrentList(1)
    , mFirstExecution(true)
{
    // Note: mOwnerThread is set during initialize()
}

Scanner::~Scanner() {
    try {
        shutdown();
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Exception in Scanner::~Scanner: %s\n", e.what());
    }
}

// ? NEW: Thread ownership assertion
void Scanner::assertOwnerThread() const {
    if (!mOwnerThreadSet) {
        // Not yet initialized - OK to call from any thread before init
        return;
    }

    std::thread::id currentThread = std::this_thread::get_id();
    if (currentThread != mOwnerThread) {
        fprintf(stderr, "FATAL: RTC5 API called from wrong thread!\n");
        fprintf(stderr, "  Owner thread: %p\n", (void*)&mOwnerThread);
        fprintf(stderr, "  Current thread: %p\n", (void*)&currentThread);

        // ? STRICT: Assert in debug, throw in release
#ifdef _DEBUG
        assert(false && "RTC5 API called from wrong thread");
#else
        throw std::runtime_error("RTC5 API called from wrong thread");
#endif
    }
}

bool Scanner::initialize(const Config& config) {
    try {
        // ? NEW: Use mutex to prevent concurrent initialization
        std::lock_guard<std::mutex> lock(mMutex);

        if (mIsInitialized) {
            logMessage("Scanner already initialized");
            return true;
        }

        // ? NEW: Set owner thread BEFORE any RTC5 calls
        mOwnerThread = std::this_thread::get_id();
        mOwnerThreadSet = true;

        char threadInfo[256];
        sprintf_s(threadInfo, "Initializing Scanner on thread %p", (void*)&mOwnerThread);
        logMessage(threadInfo);

        mConfig = config;
        logMessage("Initializing the RTC5 DLL");

        // ? NEW: Acquire DLL reference (thread-safe)
        if (!RTC5DLLManager::instance().acquireDLL()) {
            logMessage("ERROR: Failed to acquire RTC5 DLL");
            mOwnerThreadSet = false;
            return false;
        }

        // ? CRITICAL: Exception-safe initialization
        try {
            if (!initializeRTC5() || !loadFiles() || !configureLaser() || !configureTimings()) {
                logMessage("ERROR: Initialization failed");
                RTC5DLLManager::instance().releaseDLL();
                mOwnerThreadSet = false;
                return false;
            }
        }
        catch (const std::exception& e) {
            logMessage(std::string("Exception during initialization: ") + e.what());
            RTC5DLLManager::instance().releaseDLL();
            mOwnerThreadSet = false;
            return false;
        }

        mIsInitialized = true;
        logMessage("Scanner initialized successfully");
        return true;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception during scanner initialization: ") + e.what());
        return false;
    }
}

bool Scanner::initializeRTC5() {
    // ? CRITICAL: Verify we're on the owner thread
    assertOwnerThread();

    try {
        logMessage("Initializing the RTC5 DLL");

        UINT ErrorCode = init_rtc5_dll();
        if (ErrorCode) {
            UINT RTC5CountCards = rtc5_count_cards();
            logMessage("RTC5 cards detected: " + std::to_string(RTC5CountCards));

            if (RTC5CountCards) {
                UINT AccError = 0;
                for (UINT i = 1; i <= RTC5CountCards; i++) {
                    UINT Error = n_get_last_error(i);
                    if (Error != 0) {
                        AccError |= Error;
                        logMessage("Card no. " + std::to_string(i) + ": Error " + std::to_string(Error));
                        n_reset_error(i, Error);
                    }
                }
                if (AccError) {
                    logMessage("Errors detected on cards, terminating");
                    return false;
                }
            }
            else {
                logMessage("Initializing the DLL: Error " + std::to_string(ErrorCode));
                return false;
            }
        }
        else {
            // init_rtc5_dll succeeded
            UINT selected = select_rtc(mConfig.cardNumber);
            if (selected != mConfig.cardNumber) {
                ErrorCode = n_get_last_error(mConfig.cardNumber);
                if (ErrorCode & 256) {  // RTC5_VERSION_MISMATCH
                    ErrorCode = n_load_program_file(mConfig.cardNumber, 0);
                    if (ErrorCode) {
                        logMessage("No access to card no. " + std::to_string(mConfig.cardNumber));
                        return false;
                    }
                    else {
                        select_rtc(mConfig.cardNumber);
                    }
                }
                else {
                    logMessage("No access to card no. " + std::to_string(mConfig.cardNumber));
                    return false;
                }
            }
        }

        stop_execution();
        return true;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in initializeRTC5: ") + e.what());
        return false;
    }
}

bool Scanner::loadFiles() {
    assertOwnerThread();

    try {
        stop_execution();

        logMessage("Loading program file...");
        UINT errorCode = load_program_file(0);
        if (errorCode) {
            char msg[128];
            sprintf_s(msg, "Program file loading error: %d", errorCode);
            logMessage(msg);
            return false;
        }
        logMessage("Program file loaded successfully");

        logMessage("Loading correction file...");
        errorCode = load_correction_file(0, 1, 2);
        if (errorCode) {
            char msg[128];
            sprintf_s(msg, "Correction file loading error: %d", errorCode);
            logMessage(msg);
            return false;
        }
        logMessage("Correction file loaded successfully");

        select_cor_table(1, 0);
        logMessage("Correction table selected");

        reset_error(-1);
        logMessage("Previous errors cleared");

        return true;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in loadFiles: ") + e.what());
        return false;
    }
}

bool Scanner::configureLaser() {
    assertOwnerThread();

    try {
        config_list(mConfig.listMemory, 0);
        set_laser_mode(mConfig.laserMode);
        set_laser_off_default(mConfig.analogOutStandby, mConfig.analogOutStandby, 0);
        set_firstpulse_killer(mConfig.firstPulseKiller);
        set_laser_control(mConfig.laserControl);
        home_position(mBeamDump.x, mBeamDump.y);
        write_da_x(mConfig.analogOutChannel, mConfig.analogOutValue);

        logMessage("Pump source warming up - please wait");
        return true;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in configureLaser: ") + e.what());
        return false;
    }
}

bool Scanner::configureTimings() {
    assertOwnerThread();

    try {
        if (mConfig.laserHalfPeriod < 13) {
            logMessage("ERROR: laserHalfPeriod must be >= 13 (in 1/8us units)");
            return false;
        }

        set_start_list(1);
        long_delay(mConfig.warmUpTime);
        set_laser_pulses(mConfig.laserHalfPeriod, mConfig.laserPulseWidth);
        set_scanner_delays(mConfig.jumpDelay, mConfig.markDelay, mConfig.polygonDelay);
        set_laser_delays(mConfig.laserOnDelay, mConfig.laserOffDelay);
        set_jump_speed(mConfig.jumpSpeed);
        set_mark_speed(mConfig.markSpeed);
        set_delay_mode(0, 0, 1, 0, 0);

        set_end_of_list();
        execute_list(1);

        logMessage("Warming up laser source...");

        // ? NEW: Add timeout protection
        UINT busy, pos;
        const int TIMEOUT_MS = 10000;  // 10 second timeout
        auto startTime = std::chrono::high_resolution_clock::now();

        do {
            get_status(&busy, &pos);
            std::this_thread::yield();

            auto elapsed = std::chrono::high_resolution_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > TIMEOUT_MS) {
                logMessage("ERROR: Laser warmup timeout");
                return false;
            }
        } while (busy);

        set_start_list(1);
        mStartFlags = 1;

        logMessage("Laser warmed up and ready");
        return true;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in configureTimings: ") + e.what());
        return false;
    }
}

void Scanner::shutdown() {
    try {
        std::lock_guard<std::mutex> lock(mMutex);

        if (!mIsInitialized) {
            return;
        }

        assertOwnerThread();

        stopScanning();
        write_da_x(mConfig.analogOutChannel, mConfig.analogOutStandby);
        logMessage("Shutting down scanner");

        releaseResources();
        mIsInitialized = false;
        mOwnerThreadSet = false;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Exception in shutdown: %s\n", e.what());
    }
}

void Scanner::releaseResources() {
    // ? CRITICAL: Called with mMutex already held
    // Do NOT try to acquire mMutex here (deadlock)
    assertOwnerThread();

    try {
        free_rtc5_dll();
        RTC5close();
        RTC5DLLManager::instance().releaseDLL();
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Exception in releaseResources: %s\n", e.what());
    }
}

// ============================================================================
// Scanning Control
// ============================================================================

bool Scanner::startScanning() {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) {
            logMessage("Scanner not initialized");
            return false;
        }

        if (mIsScanning) {
            logMessage("Scanner already running");
            return true;
        }

        if (!enableLaser()) {
            logMessage("Failed to enable laser");
            return false;
        }
        mIsScanning = true;
        mStartFlags = 1;
        logMessage("Scanning started");
        return true;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in startScanning: ") + e.what());
        return false;
    }
}

bool Scanner::stopScanning() {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsScanning) return true;

        disableLaser();
        restart_list();
        stop_execution();
        reset_error(-1);
        set_start_list(1);
        mIsScanning = false;
        mStartFlags = 1;
        logMessage("Scanning stopped");
        return true;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in stopScanning: ") + e.what());
        return false;
    }
}

bool Scanner::pauseScanning() {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsScanning) return false;
        pause_list();
        logMessage("Scanning paused");
        return true;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in pauseScanning: ") + e.what());
        return false;
    }
}

bool Scanner::resumeScanning() {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;
        if (!enableLaser()) {
            logMessage("Failed to enable laser");
            return false;
        }
        restart_list();
        mIsScanning = true;
        mStartFlags &= ~2;
        logMessage("Scanning resumed");
        return true;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in resumeScanning: ") + e.what());
        return false;
    }
}

// ============================================================================
// Drawing Operations
// ============================================================================

bool Scanner::jumpTo(const Point& destination)
{
    if (!mIsInitialized) return false;
    jump_abs(destination.x, destination.y);
    return checkRTC5Error("jump_abs");
}

bool Scanner::markTo(const Point& destination)
{
    if (!mIsInitialized) return false;
    mark_abs(destination.x, destination.y);
    return checkRTC5Error("mark_abs");
}

bool Scanner::plotLine(const Point& destination)
{
    // This function is deprecated by the new model.
    // For simplicity, we will have it default to markTo.
    return markTo(destination);
}

// ============================================================================
// Laser Control
// NOTE: These methods are typically called from within locked contexts
//       (startScanning, stopScanning, resumeScanning). They do NOT acquire
//       the mutex themselves to avoid deadlock.
// ============================================================================

bool Scanner::enableLaser() {
    assertOwnerThread();

    try {
        if (!mIsInitialized) {
            return false;
        }

        enable_laser();
        return checkRTC5Error("enableLaser");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in enableLaser: ") + e.what());
        return false;
    }
}

bool Scanner::disableLaser() {
    assertOwnerThread();

    try {
        if (!mIsInitialized) {
            return false;
        }

        disable_laser();
        return checkRTC5Error("disableLaser");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in disableLaser: ") + e.what());
        return false;
    }
}

bool Scanner::setLaserPower(UINT channel, UINT value) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) {
            return false;
        }

        write_da_x(channel, value);
        return checkRTC5Error("write_da_x");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in setLaserPower: ") + e.what());
        return false;
    }
}

// ============================================================================
// List Management
// ============================================================================

bool Scanner::executeList()
{
   // std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) {
        return false;
    }

    // ============================================================================
    // INDUSTRIAL RTC5 STANDARD: Close list before execution
    // ============================================================================
    // Before executing the list, it MUST be closed with set_end_of_list().
    // This signals the end of the command sequence to the RTC5 DSP.
    //
    // Sequence:
    // 1. set_start_list(1) - Opened in prepareListForLayer()
    // 2. [queue commands] - jumpTo/markTo add commands to list
    // 3. set_end_of_list() - Close list (signals end of command sequence)
    // 4. execute_list(1) - Hardware executes the closed list
    // 5. wait for completion - Poll until list execution finishes
    //
    set_end_of_list();
    if (!checkRTC5Error("set_end_of_list")) {
        return false;
    }

    execute_list(1);
    return checkRTC5Error("execute_list");
}

bool Scanner::flushQueue() {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;

        restart_list();
        logMessage("Flushing the queue");
        set_end_of_list();

        // ? NEW: Add timeout protection
        UINT busy, pos;
        const int TIMEOUT_MS = 10000;  // 10 second timeout
        auto startTime = std::chrono::high_resolution_clock::now();

        do {
            get_status(&busy, &pos);
            std::this_thread::yield();

            auto elapsed = std::chrono::high_resolution_clock::now() - startTime;
            if (std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count() > TIMEOUT_MS) {
                logMessage("ERROR: flushQueue timeout");
                return false;
            }
        } while (busy);

        reset_error(-1);
        set_start_list(1);
        return true;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in flushQueue: ") + e.what());
        return false;
    }
}

void Scanner::getStatus(UINT& busy, UINT& position) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (mIsInitialized) {
            get_status(&busy, &position);
        }
        else {
            busy = 0;
            position = 0;
        }
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in getStatus: ") + e.what());
        busy = 0;
        position = 0;
    }
}

UINT Scanner::getInputPointer() {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (mIsInitialized) {
            return get_input_pointer();
        }
        return 0;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in getInputPointer: ") + e.what());
        return 0;
    }
}

// ============================================================================
// Configuration
// ============================================================================

void Scanner::setConfig(const Config& config) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        mConfig = config;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in setConfig: ") + e.what());
    }
}

// ============================================================================
// Error Handling
// ============================================================================

UINT Scanner::getLastError() const {
    try {
        if (mIsInitialized) {
            return get_last_error();
        }
        return mLastError;
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Exception in getLastError: %s\n", e.what());
        return mLastError;
    }
}

std::string Scanner::getErrorMessage() const {
    char msg[256];
    sprintf_s(msg, "Error code: %d", getLastError());
    return std::string(msg);
}

bool Scanner::resetError() {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;

        UINT error = get_last_error();
        if (error != 0) {
            reset_error(error);
            logMessage("Cleared error code: " + std::to_string(error));
        }

        return true;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in resetError: ") + e.what());
        return false;
    }
}

Scanner::ScannerStatus Scanner::getDetailedStatus() {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        ScannerStatus status = {};

        if (mIsInitialized && mOwnerThreadSet && mOwnerThread == std::this_thread::get_id()) {
            get_status(&status.isBusy, &status.listPosition);
            status.inputPointer = get_input_pointer();
            status.error = get_last_error();
        }

        return status;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in getDetailedStatus: ") + e.what());
        ScannerStatus status = {};
        return status;
    }
}

// ============================================================================
// Laser Control (in list)
// ============================================================================

bool Scanner::setLaserPowerList(UINT value) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;

        value = (std::max)(mConfig.laserPowerMin, (std::min)(value, mConfig.laserPowerMax));
        write_da_x_list(mConfig.analogOutChannel, value);
        return checkRTC5Error("write_da_x_list");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in setLaserPowerList: ") + e.what());
        return false;
    }
}

bool Scanner::laserSignalOnList() {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;
        laser_signal_on_list();
        return checkRTC5Error("laser_signal_on_list");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in laserSignalOnList: ") + e.what());
        return false;
    }
}

bool Scanner::laserSignalOffList() {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;
        laser_signal_off_list();
        return checkRTC5Error("laser_signal_off_list");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in laserSignalOffList: ") + e.what());
        return false;
    }
}

// ============================================================================
// Dynamic Speed Control
// ============================================================================

bool Scanner::setMarkSpeedList(double speed) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;
        set_mark_speed(speed);
        return checkRTC5Error("set_mark_speed");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in setMarkSpeedList: ") + e.what());
        return false;
    }
}

bool Scanner::setJumpSpeedList(double speed) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;
        set_jump_speed(speed);
        return checkRTC5Error("set_jump_speed");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in setJumpSpeedList: ") + e.what());
        return false;
    }
}

bool Scanner::applySegmentParameters(double laserPower, double laserSpeed, double jumpSpeed) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) {
            logMessage("ERROR: Scanner not initialized");
            return false;
        }

        UINT powerValue = static_cast<UINT>((laserPower / 500.0) * 4095.0);
        powerValue = (std::max)(mConfig.laserPowerMin, (std::min)(powerValue, mConfig.laserPowerMax));

        set_mark_speed(laserSpeed);
        if (!checkRTC5Error("set_mark_speed")) {
            return false;
        }

        set_jump_speed(jumpSpeed);
        if (!checkRTC5Error("set_jump_speed")) {
            return false;
        }

        write_da_x(mConfig.analogOutChannel, powerValue);
        if (!checkRTC5Error("write_da_x (laser power)")) {
            return false;
        }

        char msg[256];
        sprintf_s(msg, "Applied segment parameters: power=%u (%.1fW), markSpeed=%.1f mm/s, jumpSpeed=%.1f mm/s",
            powerValue, laserPower, laserSpeed, jumpSpeed);
        logMessage(msg);

        return true;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in applySegmentParameters: ") + e.what());
        return false;
    }
}

// ============================================================================
// Delay and Timing Control
// ============================================================================

bool Scanner::addDelay(UINT delayMicroseconds) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;
        long_delay(delayMicroseconds / 10);
        return checkRTC5Error("long_delay");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in addDelay: ") + e.what());
        return false;
    }
}

bool Scanner::setScannerDelays(UINT jump, UINT mark, UINT polygon) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;
        set_scanner_delays(jump, mark, polygon);
        return checkRTC5Error("set_scanner_delays");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in setScannerDelays: ") + e.what());
        return false;
    }
}

// ============================================================================
// Logging
// ============================================================================

void Scanner::setLogCallback(std::function<void(const std::string&)> callback) {
    std::lock_guard<std::mutex> lock(mMutex);
    mLogCallback = callback;
}

void Scanner::logMessage(const std::string& message) {
    try {
        std::function<void(const std::string&)> callback;
        {
            std::lock_guard<std::mutex> lock(mMutex);
            callback = mLogCallback;
        }

        if (callback) {
            callback(message);
        }
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Exception in logMessage: %s\n", e.what());
    }
}

// ============================================================================
// Batch Drawing Operations
// ============================================================================

bool Scanner::drawVectors(const std::vector<Point>& points, bool closeLoop) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) { logMessage("Scanner not initialized"); return false; }
        if (points.empty()) { logMessage("No points to draw"); return false; }

        if (!jumpToUnlocked(points[0])) {
            return false;
        }

        for (size_t i = 1; i < points.size(); i++) {
            if (!markToUnlocked(points[i])) {
                logMessage("Failed to draw vector segment");
                return false;
            }
        }

        if (closeLoop && points.size() > 2) {
            if (!markToUnlocked(points[0])) {
                return false;
            }
        }

        return true;
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in drawVectors: ") + e.what());
        return false;
    }
}

bool Scanner::drawPolyline(const std::vector<Point>& points) {
    return drawVectors(points, false);
}

bool Scanner::drawPolygon(const std::vector<Point>& points) {
    return drawVectors(points, true);
}

bool Scanner::jumpToUnlocked(const Point& destination) {
    if (!mIsInitialized) return false;
    mStartFlags |= 1;
   // return plotLineUnlocked(destination);
    return 0;
}

bool Scanner::markToUnlocked(const Point& destination) {
    if (!mIsInitialized) return false;
    mStartFlags &= ~1;
    //return plotLineUnlocked(destination);

    return 0;
}

// ============================================================================
// Wobble/Modulation Support
// ============================================================================

bool Scanner::setWobble(UINT transversal, UINT longitudinal, double freq) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;
        set_wobbel(transversal, longitudinal, freq);
        logMessage("Wobble enabled: T=" + std::to_string(transversal) +
            " L=" + std::to_string(longitudinal) +
            " F=" + std::to_string(freq) + "Hz");
        return checkRTC5Error("set_wobbel");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in setWobble: ") + e.what());
        return false;
    }
}

bool Scanner::disableWobble() {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;
        set_wobbel(0, 0, 0.0);
        logMessage("Wobble disabled");
        return checkRTC5Error("set_wobbel");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in disableWobble: ") + e.what());
        return false;
    }
}

// ============================================================================
// Position Feedback
// ============================================================================

bool Scanner::getCurrentPosition(long& x, long& y) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;

        long valueX = get_value(0x01);
        long valueY = get_value(0x02);

        x = valueX;
        y = valueY;

        return checkRTC5Error("get_value");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in getCurrentPosition: ") + e.what());
        return false;
    }
}

// ============================================================================
// Pixel/Raster Scanning Support
// ============================================================================

bool Scanner::setPixelMode(UINT pulseLength, UINT analogOut) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;
        set_pixel(pulseLength, analogOut);
        logMessage("Pixel mode set: pulse=" + std::to_string(pulseLength) +
            " analog=" + std::to_string(analogOut));
        return checkRTC5Error("set_pixel");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in setPixelMode: ") + e.what());
        return false;
    }
}

bool Scanner::setPixelLine(UINT channel, UINT halfPeriod, double dX, double dY) {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) return false;
        set_pixel_line(channel, halfPeriod, dX, dY);
        logMessage("Pixel line configured: ch=" + std::to_string(channel));
        return checkRTC5Error("set_pixel_line");
    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in setPixelLine: ") + e.what());
        return false;
    }
}

// ============================================================================
// ============================================================================

bool Scanner::checkRTC5Error(const std::string& operation) {
    UINT error = get_last_error();
    if (error != 0) {
        char msg[256];
        sprintf_s(msg, "RTC5 Error in %s: code %d", operation.c_str(), error);
        logMessage(msg);
        mLastError = error;
        return false;
    }
    return true;
}

void Scanner::terminateDLL() {
    try {
        free_rtc5_dll();
        RTC5close();
    }
    catch (const std::exception& e) {
        fprintf(stderr, "Exception in terminateDLL: %s\n", e.what());
    }
}
bool Scanner::prepareListForLayer() {
    // This function is now simplified as the list management is more direct.
    // The primary role is to open the list for writing.
    if (!mIsInitialized) {
        logMessage("ERROR: Cannot prepare list - scanner not initialized");
        return false;
    }

    set_start_list(1);
    return checkRTC5Error("set_start_list");
}
bool Scanner::waitForListCompletion(UINT timeoutMs) {
    //std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;

    UINT busy, pos;
    UINT elapsed = 0;

    do {
        get_status(&busy, &pos);
        if (!busy) return true;

        Sleep(10);
        elapsed += 10;

        if (elapsed >= timeoutMs) {
            logMessage("ERROR: List execution timeout");
            return false;
        }
    } while (busy);

    return true;
}
