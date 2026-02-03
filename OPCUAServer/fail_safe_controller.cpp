/**
 * @file fail_safe_controller.cpp
 * @brief Implementation of hardware fail-safe controller
 * 
 * @details This file implements the safety-critical logic for the SLM
 * control system. All hardware interactions go through safety validation.
 * 
 * @author Senior Embedded Systems Engineer
 * @copyright (c) 2024 MarcSLM Control Systems
 */

#include "fail_safe_controller.h"
#include <iostream>
#include <sstream>
#include <iomanip>
#include <ctime>

namespace slm_opcua {

// ============================================================================
// Constructor / Destructor
// ============================================================================

FailSafeController::FailSafeController(const Config& config)
    : m_config(config)
    , m_lastWatchdogFeed(std::chrono::steady_clock::now())
{
    log("[FAIL-SAFE] Controller initialized");
    log("[FAIL-SAFE] Watchdog timeout: " + std::to_string(m_config.watchdogTimeoutMs) + "ms");
}

FailSafeController::~FailSafeController()
{
    // Ensure safe state on shutdown
    if (m_server && m_stateContainer) {
        log("[FAIL-SAFE] Shutdown - forcing safe state");
        writeSafeState(false);
    }
    log("[FAIL-SAFE] Controller destroyed");
}

// ============================================================================
// Server Integration
// ============================================================================

void FailSafeController::attachServer(UA_Server* server, PlcStateContainer* stateContainer)
{
    std::lock_guard<std::mutex> lock(m_serverMutex);
    m_server = server;
    m_stateContainer = stateContainer;
    log("[FAIL-SAFE] Attached to OPC UA server");
}

void FailSafeController::detachServer()
{
    std::lock_guard<std::mutex> lock(m_serverMutex);
    
    // Write safe state before detaching
    if (m_server && m_stateContainer) {
        writeSafeState(false);
    }
    
    m_server = nullptr;
    m_stateContainer = nullptr;
    log("[FAIL-SAFE] Detached from server");
}

// ============================================================================
// Safety State Management
// ============================================================================

void FailSafeController::triggerSafeStop(const std::string& reason)
{
    SafetyState expected = SafetyState::NORMAL;
    if (m_state.compare_exchange_strong(expected, SafetyState::STOPPED)) {
        log("[SAFE-STOP] Triggered: " + reason);
        
        std::lock_guard<std::mutex> lock(m_serverMutex);
        if (m_server && m_stateContainer) {
            writeSafeState(false);
        }
    } else if (expected == SafetyState::CAUTION) {
        m_state.store(SafetyState::STOPPED);
        log("[SAFE-STOP] Escalated from CAUTION: " + reason);
        
        std::lock_guard<std::mutex> lock(m_serverMutex);
        if (m_server && m_stateContainer) {
            writeSafeState(false);
        }
    }
}

void FailSafeController::triggerEmergencyStop(const std::string& reason)
{
    // Emergency stop always succeeds regardless of current state
    SafetyState prev = m_state.exchange(SafetyState::EMERGENCY);
    
    log("[EMERGENCY] ============================================");
    log("[EMERGENCY] EMERGENCY STOP ACTIVATED");
    log("[EMERGENCY] Reason: " + reason);
    log("[EMERGENCY] Previous state: " + std::string(safetyStateToString(prev)));
    log("[EMERGENCY] ============================================");
    
    std::lock_guard<std::mutex> lock(m_serverMutex);
    if (m_server && m_stateContainer) {
        writeSafeState(true);
    }
    
    // Reset recovery counter
    m_recoveryAttempts = 0;
}

bool FailSafeController::resetFromEmergency()
{
    SafetyState expected = SafetyState::EMERGENCY;
    if (!m_state.compare_exchange_strong(expected, SafetyState::STOPPED)) {
        log("[FAIL-SAFE] Reset failed: not in EMERGENCY state");
        return false;
    }
    
    log("[FAIL-SAFE] Attempting reset from EMERGENCY");
    
    // Perform safety checks
    bool checksPass = true;
    
    {
        std::lock_guard<std::mutex> lock(m_serverMutex);
        if (m_stateContainer) {
            auto guard = m_stateContainer->lock();
            
            // Check 1: Validate positions
            if (!guard->validatePositions()) {
                log("[FAIL-SAFE] Safety check FAILED: positions out of bounds");
                checksPass = false;
            }
            
            // Check 2: Verify no hardware faults
            if (guard->HardwareFault) {
                log("[FAIL-SAFE] Safety check FAILED: hardware fault active");
                checksPass = false;
            }
        }
    }
    
    // Check 3: Watchdog must be fed recently
    if (isWatchdogExpired()) {
        log("[FAIL-SAFE] Safety check FAILED: watchdog expired");
        checksPass = false;
    }
    
    if (checksPass) {
        m_state.store(SafetyState::NORMAL);
        m_recoveryAttempts = 0;
        log("[FAIL-SAFE] Reset successful - state: NORMAL");
        return true;
    } else {
        m_state.store(SafetyState::EMERGENCY);
        m_recoveryAttempts++;
        log("[FAIL-SAFE] Reset failed - remaining in EMERGENCY (attempt " + 
            std::to_string(m_recoveryAttempts) + ")");
        return false;
    }
}

bool FailSafeController::resetFromSafeStop()
{
    SafetyState expected = SafetyState::STOPPED;
    if (m_state.compare_exchange_strong(expected, SafetyState::NORMAL)) {
        log("[FAIL-SAFE] Reset from STOPPED - state: NORMAL");
        return true;
    }
    
    log("[FAIL-SAFE] Reset failed: not in STOPPED state (current: " + 
        std::string(safetyStateToString(expected)) + ")");
    return false;
}

// ============================================================================
// Watchdog Management
// ============================================================================

void FailSafeController::feedWatchdog()
{
    m_lastWatchdogFeed = std::chrono::steady_clock::now();
}

bool FailSafeController::isWatchdogExpired() const
{
    if (!m_watchdogEnabled.load()) {
        return false;
    }
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastWatchdogFeed).count();
    
    return elapsed > m_config.watchdogTimeoutMs;
}

// ============================================================================
// Value Validation
// ============================================================================

bool FailSafeController::validatePosition(int32_t position, const std::string& variableName)
{
    if (position < limits::CYLINDER_POSITION_MIN || 
        position > limits::CYLINDER_POSITION_MAX) {
        
        std::ostringstream oss;
        oss << "[VALIDATION] Position out of bounds: " << variableName 
            << " = " << position 
            << " (valid: " << limits::CYLINDER_POSITION_MIN 
            << " to " << limits::CYLINDER_POSITION_MAX << ")";
        log(oss.str());
        
        return false;
    }
    return true;
}

bool FailSafeController::validateStateTransition(const PlcState& from, const PlcState& to)
{
    // Rule 1: Cannot start surfaces without startup complete
    if (to.StartSurfaces && !from.StartUp_Done && !to.StartUp_Done) {
        log("[VALIDATION] Invalid transition: StartSurfaces without StartUp_Done");
        return false;
    }
    
    // Rule 2: Cannot request layer while previous layer preparation is in progress
    if (to.LaySurface && from.PreparingLayer) {
        log("[VALIDATION] Invalid transition: LaySurface while PreparingLayer");
        return false;
    }
    
    // Rule 3: Validate position changes
    if (!validatePosition(to.Marcer_Source_Cylinder_ActualPosition, "Source")) {
        return false;
    }
    if (!validatePosition(to.Marcer_Sink_Cylinder_ActualPosition, "Sink")) {
        return false;
    }
    
    return true;
}

// ============================================================================
// Internal Methods
// ============================================================================

void FailSafeController::writeSafeState(bool emergency)
{
    if (!m_stateContainer) {
        return;
    }
    
    auto guard = m_stateContainer->lock();
    
    // Reset all operation flags
    guard->StartUp = UA_FALSE;
    guard->StartSurfaces = UA_FALSE;
    guard->LaySurface = UA_FALSE;
    
    // Clear done flags
    guard->StartUp_Done = UA_FALSE;
    guard->MakeSurface_Done = UA_FALSE;
    guard->LaySurface_Done = UA_FALSE;
    
    // Clear internal state
    guard->PreparingLayer = UA_FALSE;
    
    if (emergency) {
        guard->EmergencyStop = UA_TRUE;
    }
    
    log("[FAIL-SAFE] Safe state written to PLC variables");
}

void FailSafeController::log(const std::string& message) const
{
    // Get timestamp
    auto now = std::chrono::system_clock::now();
    auto time = std::chrono::system_clock::to_time_t(now);
    auto ms = std::chrono::duration_cast<std::chrono::milliseconds>(
        now.time_since_epoch()) % 1000;
    
    std::ostringstream oss;
    oss << std::put_time(std::localtime(&time), "%H:%M:%S")
        << "." << std::setfill('0') << std::setw(3) << ms.count()
        << " " << message;
    
    std::string fullMessage = oss.str();
    
    // Use callback if provided
    if (m_config.logCallback) {
        m_config.logCallback(fullMessage);
    }
    
    // Also output to console for production diagnostics
    std::cout << fullMessage << std::endl;
}

} // namespace slm_opcua
