#include "Scanner.h"

//  File
//      Scanner.cpp
//
//  Abstract
//      A class for controlling SCANLAB RTC5 laser scanner
//      Converted from console application to reusable class
//
//  Author
//      Bernhard Schrems, SCANLAB AG
//      adapted for RTC5: Hermann Waibel, SCANLAB AG
//      Refactored to class by: [Your Name]
//
//  Features
//      - explicit linking to the RTC5DLL.DLL
//      - use of the list buffer as a single list like a circular queue 
//        for continuous data transfer
//      - exception handling
//
//  Necessary Sources
//      RTC5expl.h, RTC5expl.c, Scanner.h

// System header files
#include <windows.h>
#include <stdio.h>
#include <math.h>
#include <exception>
#include <filesystem>

// RTC5 header file for explicitly linking to the RTC5DLL.DLL
#include "RTC5expl.h"
#include "Scanner.h"

// ============================================================================
// Constructor / Destructor
// ============================================================================

Scanner::Scanner()
    : mIsInitialized(false)
    , mIsScanning(false)
    , mLastError(0)
    , mConfig()
    , mBeamDump(0, 0)
    , mStartFlags(0)
{
}

Scanner::~Scanner()
{
    shutdown();
}

// ============================================================================
// Static variables for process-global RTC5 state
// ============================================================================

static bool s_rtc5Opened = false;

// ============================================================================
// Initialization
// ============================================================================

bool Scanner::initialize(const Config& config)
{
    try {
        if (mIsInitialized) {
            logMessage("Scanner already initialized");
            return true;
        }

        mConfig = config;

        logMessage("Initializing the RTC5 DLL");

        if (!initializeRTC5()) {
            releaseResources();
            return false;
        }

        if (!loadFiles() ||
            !configureLaser() ||
            !configureTimings()) {
            releaseResources();
            return false;
        }

        mIsInitialized = true;
        logMessage("Scanner initialized successfully");
        return true;
    } catch (const std::exception& e) {
        logMessage(std::string("Exception during scanner initialization: ") + e.what());
        releaseResources();
        return false;
    } catch (...) {
        logMessage("Unknown exception during scanner initialization");
        releaseResources();
        return false;
    }
}

bool Scanner::initializeRTC5()
{
    // ------------------------------------------------------------
    // MUST be the FIRST ScanLab call in the process
    // ------------------------------------------------------------
    if (!s_rtc5Opened) {

        if (RTC5open()) {

            logMessage("RTC5open failed: driver not accessible");

            return false;

        }

        s_rtc5Opened = true;

    }

    logMessage("Initializing the DLL");

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

                    logMessage("Card no. " + std::to_string(i) + ": Error " + std::to_string(Error) + " detected");

                    n_reset_error(i, Error);

                }

            }

            if (AccError) {

                logMessage("Errors detected on cards, terminating");

                terminateDLL();

                return false;

            }

        } else {

            logMessage("Initializing the DLL: Error " + std::to_string(ErrorCode) + " detected");

            terminateDLL();

            return false;

        }

    } else {

        // init_rtc5_dll succeeded

        UINT selected = select_rtc(mConfig.cardNumber);

        if (selected != mConfig.cardNumber) {

            ErrorCode = n_get_last_error(mConfig.cardNumber);

            if (ErrorCode & 256) { // RTC5_VERSION_MISMATCH

                ErrorCode = n_load_program_file(mConfig.cardNumber, 0);

                if (ErrorCode) {

                    logMessage("No access to card no. " + std::to_string(mConfig.cardNumber));

                    terminateDLL();

                    return false;

                } else {

                    // n_load_program_file successful

                    select_rtc(mConfig.cardNumber);

                }

            } else {

                logMessage("No access to card no. " + std::to_string(mConfig.cardNumber));

                terminateDLL();

                return false;

            }

        }

    }

    //set_rtc4_mode();

    //logMessage("RTC5 initialized in RTC4 mode");

    // Initialize the RTC5

    stop_execution();

    // If the card has been used previously by another application

    // a list might still be running. This would prevent load_program_file

    // and load_correction_file from being executed.

    return true;
}



bool Scanner::loadFiles()
{
    stop_execution();
    // If the card has been used previously by another application 
    // a list might still be running. This would prevent load_program_file
    // and load_correction_file from being executed.

    logMessage("Loading program file...");
    UINT errorCode = load_program_file(0);  // Path = current working path
    if (errorCode) {
        char msg[128];
        sprintf_s(msg, "Program file loading error: %d", errorCode);
        logMessage(msg);
        return false;
    }
    logMessage("Program file loaded successfully");

 

    logMessage("Loading correction file...");
    errorCode = load_correction_file(0, 1, 2);  // Initialize like "D2_1to1.ct5"
    if (errorCode) {
        char msg[128];
        sprintf_s(msg, "Correction file loading error: %d", errorCode);
        logMessage(msg);
        return false;
    }
    logMessage("Correction file loaded successfully");

    select_cor_table(1, 0);  // Table #1 at primary connector (default)
    logMessage("Correction table selected");

    // stop_execution might have created a RTC5_TIMEOUT error
    reset_error(-1);  // Clear all previous error codes
    logMessage("Previous errors cleared");

    return true;
}

bool Scanner::configureLaser()
{
    // Configure list memory
    config_list(mConfig.listMemory, 0);
    
    // Set laser mode (YAG, CO2, fiber, etc.)
    set_laser_mode(mConfig.laserMode);
    
    // ✅ NEW: Use set_laser_off_default for proper standby behavior
    set_laser_off_default(
        mConfig.analogOutStandby,  // Analog1 standby
        mConfig.analogOutStandby,  // Analog2 standby
        0                          // Digital out standby
    );
    
    // First pulse killer (removes first unstable pulse)
    set_firstpulse_killer(mConfig.firstPulseKiller);

    // This function must be called at least once to activate laser 
    // signals. Later on enable/disable_laser would be sufficient.
    set_laser_control(mConfig.laserControl);

    // Activate home jump and specify beam dump
    home_position(mBeamDump.x, mBeamDump.y);

    // Turn on the optical pump source
    write_da_x(mConfig.analogOutChannel, mConfig.analogOutValue);
    
    logMessage("Pump source warming up - please wait");
    return true;
}

bool Scanner::configureTimings()
{
    if (mConfig.laserHalfPeriod < 13) {
        logMessage("ERROR: laserHalfPeriod must be >= 13 (in 1/8us units)");
        return false;
    }

    // ✅ IMPROVED: Set timing parameters
    set_start_list(1);

    // Warmup with laser disabled
    long_delay(mConfig.warmUpTime);

    // Set laser pulse parameters
    set_laser_pulses(mConfig.laserHalfPeriod, mConfig.laserPulseWidth);

    // Set all delays
    set_scanner_delays(mConfig.jumpDelay, mConfig.markDelay, mConfig.polygonDelay);
    set_laser_delays(mConfig.laserOnDelay, mConfig.laserOffDelay);

    // Set speeds
    set_jump_speed(mConfig.jumpSpeed);
    set_mark_speed(mConfig.markSpeed);

    // ✅ NEW: Set sky writing for smoother curves (optional)
    // set_sky_writing(0.0, 0);  // Disabled by default

    // ✅ NEW: Set delay mode for optimized performance
    set_delay_mode(
        0,      // VarPoly: variable polygon delay
        0,      // DirectMove3D: direct 3D movement
        1,      // EdgeLevel: edge-based delays
        0,      // MinJumpDelay
        0       // JumpLengthLimit
    );

    set_end_of_list();
    execute_list(1);

    logMessage("Warming up laser source...");

    UINT busy, pos;
    do {
        get_status(&busy, &pos);
        Sleep(1);
    } while (busy);

    // ✅ FIX: Re-initialize list for continuous operation
    set_start_list(1);

    mStartFlags = 1;  // Start with jump mode

    logMessage("Laser warmed up and ready");
    return true;
}

void Scanner::shutdown()
{
    if (!mIsInitialized) {
        return;
    }

    stopScanning();

    // Activate the pump source standby
    write_da_x(mConfig.analogOutChannel, mConfig.analogOutStandby);

    logMessage("Shutting down scanner");

    releaseResources();
    mIsInitialized = false;
}

void Scanner::releaseResources()
{
   // assert(std::this_thread::get_id() == mOwnerThread);
    free_rtc5_dll();
    RTC5close();
}

// ============================================================================
// Scanning Control
// ============================================================================

bool Scanner::startScanning()
{
   // std::lock_guard<std::mutex> lock(mMutex);

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
    mStartFlags = 1;  // Start with jump mode
    logMessage("Scanning started");
    return true;
}

bool Scanner::stopScanning() {
    //std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsScanning) return true;
    disableLaser();
    restart_list();
    stop_execution();
    reset_error(-1);         // clear timeouts
    set_start_list(1);       // ready for next session
    mIsScanning = false;
    mStartFlags = 1;
    logMessage("Scanning stopped");
    return true;
}

bool Scanner::pauseScanning()
{
   // std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsScanning) return false;
    pause_list();
    logMessage("Scanning paused");
    return true;
}

bool Scanner::resumeScanning()
{
    //std::lock_guard<std::mutex> lock(mMutex);
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

// ============================================================================
// Drawing Operations
// ============================================================================

bool Scanner::jumpTo(const Point& destination)
{
    //std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;
    mStartFlags |= 1;
    return plotLineUnlocked(destination);
}

bool Scanner::markTo(const Point& destination)
{
    //std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;
    mStartFlags &= ~1;
    return plotLineUnlocked(destination);
}

bool Scanner::plotLine(const Point& destination)
{
    ///std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) {
        return false;
    }
    return plotLineUnlocked(destination);
}

bool Scanner::plotLineUnlocked(const Point& destination)
{
    // ✅ FIX: Add timeout to prevent infinite loop
    const int MAX_RETRIES = 5000;  // 5 seconds maximum
    int retries = 0;
    
    while (!plotLineInternal(destination, &mStartFlags)) {
        Sleep(1);
        
        if (++retries > MAX_RETRIES) {
            logMessage("ERROR: plotLine timeout - hardware not responding");
            
            // Check for errors
            UINT error = get_last_error();
            if (error != 0) {
                char msg[128];
                sprintf_s(msg, "Hardware error code: %d", error);
                logMessage(msg);
            }
            
            return false;
        }
    }

    return true;
}

int Scanner::plotLineInternal(const Point& destination, UINT* start)
{
    // ✅ Validate coordinates
    if (!destination.isValid()) {
        logMessage("ERROR: Coordinates out of range");
        return 0;
    }
    
    UINT busy, outPos, inPos;

    inPos = get_input_pointer();

    if ((inPos & mConfig.pointerCount) == mConfig.pointerCount) {
        get_status(&busy, &outPos);

        // Busy & 0x0001: list is still executing, may be paused via pause_list
        // Busy & 0x00fe: list has finished, but home_jumping is still active
        // Busy & 0xff00: && (Busy & 0x00ff) = 0: set_wait
        //                && (Busy & 0x00ff) > 0: pause_list

        // List is running and not paused, no home_jumping
        if (busy == 0x0001) {
            // If OutPos comes too close to InPos it would overtake. Let the list wait.
            if (((inPos >= outPos) && (inPos - outPos < mConfig.startGap / 2))
                || ((inPos < outPos) && (inPos + mConfig.listMemory - outPos < mConfig.startGap / 2))
                ) {
                // *start & 4: Set_wait already pending
                // *start & 8: Final flushing requested, the out_pointer MUST 
                //             come very close to the last input_pointer.
                if (!(*start & 4) && !(*start & 8)) {
                    *start |= 4;
                    set_wait(1);
                    inPos = get_input_pointer();
                    char msg[128];
                    sprintf_s(msg, "WARNING: Wait In = %6d Out = %6d", inPos, outPos);
                    logMessage(msg);
                }
            }
        }

        // List not running and not paused, no home_jumping
        if (!busy) {
            if (!(*start & 2)) {  // execute_list_pos enabled
                // If InPos is far enough ahead of OutPos, start the list
                if (((inPos > outPos) && (inPos - outPos > mConfig.startGap))
                    || ((inPos < outPos) && (inPos + mConfig.listMemory - outPos > mConfig.startGap))
                    ) {
                    if ((outPos + 1) == mConfig.listMemory) {
                        execute_list_pos(1, 0);
                    } else {
                        execute_list_pos(1, outPos + 1);
                    }
                    checkRTC5Error("execute_list_pos");
                }
            }
        }

        // List not running and not home_jumping, but paused via set_wait
        if (!(busy & 0x00ff) && (busy & 0xff00)) {
            if (*start & 4) {  // set_wait pending
                // If InPos is far enough ahead of OutPos, release the list
                if (((inPos > outPos) && (inPos - outPos > mConfig.startGap))
                    || ((inPos < outPos) && (inPos + mConfig.listMemory - outPos > mConfig.startGap))
                    ) {
                    release_wait();
                    *start &= ~4;
                    logMessage("Release");
                }
            }
        }
    }

    get_status(&busy, &outPos);

    if (((inPos > outPos) && (mConfig.listMemory - inPos + outPos > mConfig.loadGap))
        || ((inPos < outPos) && (inPos + mConfig.loadGap) < outPos)
        ) {
        if (*start & 1) {
            jump_abs(destination.x, destination.y);
            checkRTC5Error("jump_abs");
            *start &= ~1;
        }
        else {
            mark_abs(destination.x, destination.y);
            checkRTC5Error("mark_abs");
        }
    
        return 1;  // Success
    }
    else {
        // NOTE: If so-called short list commands are used (mark and jump aren't short)
        // more than one list command might be executed within the same 10 us period.
        // Thus OutPos does not always behave like OutPos++.
        // The same holds for list commands like mark_text (not used within this demonstration).
        // They may occupy more than one list positions.

        // *start & 8: at final flushing, the out_pointer MUST come very close to
        // the last input_pointer.

        if (busy && !(*start & 8)
            && (abs((long)inPos - (long)outPos) < (long)mConfig.loadGap / 10)) {
            char msg[128];
            sprintf_s(msg, "WARNING: In = %6d  Out = %6d", inPos, outPos);
            logMessage(msg);
        }

        return 0;  // Repeat plotLine
    }
}

void Scanner::setBeamDump(const Point& location)
{
   // std::lock_guard<std::mutex> lock(mMutex);
    mBeamDump = location;
    if (mIsInitialized) {
        home_position(mBeamDump.x, mBeamDump.y);
    }
}

// ============================================================================
// Laser Control
// NOTE: These methods are typically called from within locked contexts
//       (startScanning, stopScanning, resumeScanning). They do NOT acquire
//       the mutex themselves to avoid deadlock.
// ============================================================================

bool Scanner::enableLaser()
{
    if (!mIsInitialized) {
        return false;
    }

    enable_laser();
    return checkRTC5Error("enableLaser");
}

bool Scanner::disableLaser()
{
    if (!mIsInitialized) {
        return false;
    }

    disable_laser();
    return checkRTC5Error("disableLaser");
}

bool Scanner::setLaserPower(UINT channel, UINT value)
{
    //std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) {
        return false;
    }

    write_da_x(channel, value);
    return checkRTC5Error("write_da_x");
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

    execute_list(1);
    return checkRTC5Error("execute_list");
}

bool Scanner::flushQueue() {
   // std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;
    restart_list();
    logMessage("Flushing the queue");
    set_end_of_list();
    UINT busy, pos;
    do { get_status(&busy, &pos); std::this_thread::yield(); } while (busy);
    reset_error(-1);
    // optionally reopen for further streaming:
    set_start_list(1);
    return true;
}

void Scanner::getStatus(UINT& busy, UINT& position)
{
    //std::lock_guard<std::mutex> lock(mMutex);
    if (mIsInitialized) {
        get_status(&busy, &position);
    }
    else {
        busy = 0;
        position = 0;
    }
}

UINT Scanner::getInputPointer()
{
    //std::lock_guard<std::mutex> lock(mMutex);
    if (mIsInitialized) {
        return get_input_pointer();
    }
    return 0;
}

// ============================================================================
// Configuration
// ============================================================================

void Scanner::setConfig(const Config& config)
{
   // std::lock_guard<std::mutex> lock(mMutex);
    mConfig = config;
}

// ============================================================================
// Error Handling
// ============================================================================

UINT Scanner::getLastError() const
{
    if (mIsInitialized) {
        return get_last_error();
    }
    return mLastError;
}

std::string Scanner::getErrorMessage() const
{
    char msg[256];
    sprintf_s(msg, "Error code: %d", getLastError());
    return std::string(msg);
}

// ============================================================================
// Logging
// ============================================================================

void Scanner::setLogCallback(std::function<void(const std::string&)> callback)
{
    mLogCallback = callback;
}

void Scanner::logMessage(const std::string& message)
{
    // Copy callback to avoid holding lock during callback execution
    std::function<void(const std::string&)> callback;
    {
        std::lock_guard<std::mutex> lock(mMutex);
        callback = mLogCallback;
    }
    
    // Call outside of lock to prevent re-entrancy deadlock
    if (callback) {
        callback(message);
    }
}

// ============================================================================
// Batch Drawing Operations
// ============================================================================

bool Scanner::drawVectors(const std::vector<Point>& points, bool closeLoop)
{
   // std::lock_guard<std::mutex> lock(mMutex);
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

bool Scanner::drawPolyline(const std::vector<Point>& points)
{
    return drawVectors(points, false);
}

bool Scanner::drawPolygon(const std::vector<Point>& points)
{
    return drawVectors(points, true);
}

// Unlocked helpers
bool Scanner::jumpToUnlocked(const Point& destination)
{
    if (!mIsInitialized) return false;
    mStartFlags |= 1;
    return plotLineUnlocked(destination);
}

bool Scanner::markToUnlocked(const Point& destination)
{
    if (!mIsInitialized) return false;
    mStartFlags &= ~1;
    return plotLineUnlocked(destination);
}

// ============================================================================
// Wobble/Modulation Support
// Per-Segment Parameter Control (dynamic laser settings)
// ============================================================================
// CRITICAL: These methods apply parameters to the RTC5 list BEFORE mark execution
// FLOW: convertLayerToBlock() creates ParameterSegment with parameters
//       consumerThreadFunc() calls applySegmentParameters() before marking
//       RTC5 device applies power/speed and executes marks with correct settings

/// Set wobble parameters for modulated marking
bool Scanner::setWobble(UINT transversal, UINT longitudinal, double freq) {
   // std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;

    set_wobbel(transversal, longitudinal, freq);
    logMessage("Wobble enabled: T=" + std::to_string(transversal) + 
               " L=" + std::to_string(longitudinal) + 
               " F=" + std::to_string(freq) + "Hz");
    return checkRTC5Error("set_wobbel");
}

/// Disable wobble
bool Scanner::disableWobble() {
   // std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;

    set_wobbel(0, 0, 0.0);
    logMessage("Wobble disabled");
    return checkRTC5Error("set_wobbel");
}

bool Scanner::applySegmentParameters(double laserPower, double laserSpeed, double jumpSpeed) {
   // std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) {
        logMessage("ERROR: Scanner not initialized");
        return false;
    }

    // Convert physical units to RTC5 units (typically 0-4095 for power, bits/ms for speeds)
    // Power: scale from Watts (e.g., 200W) to 0-4095 range
    // Assumption: laserPower range is 0-300W mapped to 0-4095
    UINT powerValue = static_cast<UINT>((laserPower / 500.0) * 4095.0);
    powerValue = (std::max)(mConfig.laserPowerMin, (std::min)(powerValue, mConfig.laserPowerMax));

    // Set mark speed (mm/s ? bits/ms conversion handled by RTC5)
    set_mark_speed(laserSpeed);
    if (!checkRTC5Error("set_mark_speed")) {
        return false;
    }

    // Set jump speed
    set_jump_speed(jumpSpeed);
    if (!checkRTC5Error("set_jump_speed")) {
        return false;
    }

    // Set laser power via analog output
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

// ============================================================================
// Position Feedback
// ============================================================================

/// Get current galvo position (requires special RTC5 setup)
bool Scanner::getCurrentPosition(long& x, long& y) {
    //std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;

    // Note: This requires special configuration in RTC5
    // You may need to use get_value() with specific signals
    // Signal IDs may vary based on your RTC5 configuration
    long valueX = get_value(0x01);  // Signal for X position
    long valueY = get_value(0x02);  // Signal for Y position

    x = valueX;
    y = valueY;

    return checkRTC5Error("get_value");
}

// ============================================================================
// Pixel/Raster Scanning Support
// ============================================================================

/// Set pixel mode for raster scanning
bool Scanner::setPixelMode(UINT pulseLength, UINT analogOut) {
   // std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;

    set_pixel(pulseLength, analogOut);
    logMessage("Pixel mode set: pulse=" + std::to_string(pulseLength) + 
               " analog=" + std::to_string(analogOut));
    return checkRTC5Error("set_pixel");
}

/// Set pixel line for synchronized marking
bool Scanner::setPixelLine(UINT channel, UINT halfPeriod, double dX, double dY) {
   // std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;

    set_pixel_line(channel, halfPeriod, dX, dY);
    logMessage("Pixel line configured: ch=" + std::to_string(channel));
    return checkRTC5Error("set_pixel_line");
}

// ============================================================================
// Enhanced Laser Control (added in list)
// ============================================================================

/// Set laser power during marking (within list)
bool Scanner::setLaserPowerList(UINT value) {
    //std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;

    // Clamp value to configured range
    value = (std::max)(mConfig.laserPowerMin, (std::min)(value, mConfig.laserPowerMax));

    write_da_x_list(mConfig.analogOutChannel, value);
    return checkRTC5Error("write_da_x_list");
}

/// Enable/disable laser signal in list
bool Scanner::laserSignalOnList() {
   // std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;
    laser_signal_on_list();
    return checkRTC5Error("laser_signal_on_list");
}

bool Scanner::laserSignalOffList() {
   // std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;
    laser_signal_off_list();
    return checkRTC5Error("laser_signal_off_list");
}

// ============================================================================
// Dynamic Speed Control (per segment during marking)
// ============================================================================

/// Change marking speed within list (for different scan strategies)
bool Scanner::setMarkSpeedList(double speed) {
   // std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;

    set_mark_speed(speed);  // This is immediate
    return checkRTC5Error("set_mark_speed");
}

/// Change jump speed within list
bool Scanner::setJumpSpeedList(double speed) {
    //std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;

    set_jump_speed(speed);
    return checkRTC5Error("set_jump_speed");
}

// ============================================================================
// Delay and Timing Control
// ============================================================================

/// Insert delay in list (useful for settling time, laser stabilization)
bool Scanner::addDelay(UINT delayMicroseconds) {
 //  std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;

    long_delay(delayMicroseconds / 10);  // long_delay uses 10µs units
    return checkRTC5Error("long_delay");
}

/// Set scanner settling delays
bool Scanner::setScannerDelays(UINT jump, UINT mark, UINT polygon) {
    //std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;

    set_scanner_delays(jump, mark, polygon);
    return checkRTC5Error("set_scanner_delays");
}

// ============================================================================
// Comprehensive Status and Error Handling
// ============================================================================

/// Get comprehensive scanner status
Scanner::ScannerStatus Scanner::getDetailedStatus() {
   // std::lock_guard<std::mutex> lock(mMutex);
    ScannerStatus status = {};

    if (mIsInitialized) {
        get_status(&status.isBusy, &status.listPosition);
        status.inputPointer = get_input_pointer();
        status.error = get_last_error();

        // If you have encoders:
        // get_encoder(&status.encoderX, &status.encoderY);
    }

    return status;
}

/// Reset and recover from errors
bool Scanner::resetError() {
    //std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return false;

    UINT error = get_last_error();
    if (error != 0) {
        reset_error(error);
        logMessage("Cleared error code: " + std::to_string(error));
    }

    return true;
}

// ============================================================================
// List Management Helpers
// ============================================================================

/// Wait for list completion (blocking)
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

/// Get list buffer space
UINT Scanner::getListSpace() {
    //std::lock_guard<std::mutex> lock(mMutex);
    if (!mIsInitialized) return 0;
    return get_list_space();
}

// ============================================================================
// Error Checking Helper (private)
// ============================================================================

/// Check for RTC5 errors and log them
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
void Scanner::terminateDLL()
{
    

    free_rtc5_dll();
    RTC5close();

}
bool Scanner::prepareListForLayer() {
    try {
        std::lock_guard<std::mutex> lock(mMutex);
        assertOwnerThread();

        if (!mIsInitialized) {
            logMessage("ERROR: Cannot prepare list - scanner not initialized");
            return false;
        }

        // ============================================================================
        // INDUSTRIAL RTC5 STANDARD: Per-Layer List Preparation
        // ============================================================================
        // Before queuing commands for a new layer, the RTC5 list must be:
        // 1. Reset (clear previous commands)
        // 2. Opened for writing (accept new commands)
        //
        // This is CRITICAL because:
        // - After previous layer execution, list is CLOSED (via set_end_of_list)
        // - Trying to queue commands to a CLOSED list fails silently
        // - Result: "Scanner command failed at index 0"
        //
        // RTC5 API Sequence:
        // restart_list() - Resets input/output pointers to 0, clears list buffer
        // set_start_list(1) - Opens list 1 for writing, enables command queuing
        //

        // STEP 1: Reset list (clear previous layer's commands)
        restart_list();
        logMessage("RTC5 list reset complete (pointers cleared)");

        // STEP 2: Open list for command queuing
        set_start_list(1);
        mStartFlags = 1;  // Initialize to jump mode
        logMessage("RTC5 list opened and ready for command queuing");

        // Verify no hardware errors after list operations
        if (!checkRTC5Error("prepareListForLayer")) {
            logMessage("ERROR: RTC5 error detected after list preparation");
            return false;
        }

        return true;

    }
    catch (const std::exception& e) {
        logMessage(std::string("Exception in prepareListForLayer: ") + e.what());
        return false;
    }
    catch (...) {
        logMessage("Unknown exception in prepareListForLayer");
        return false;
    }
}