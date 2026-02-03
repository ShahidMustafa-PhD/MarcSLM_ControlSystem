/**
 * @file fail_safe_controller.h
 * @brief Hardware fail-safe controller for SLM OPC UA Server
 * 
 * @details This module implements critical safety logic for the Selective
 * Laser Melting (SLM) control system. When errors are detected, it ensures
 * hardware is driven to a safe state.
 * 
 * @par Hardware Safety Features:
 * - Emergency stop detection and response
 * - Laser interlock control
 * - Motion system safe parking
 * - Watchdog timeout handling
 * 
 * @par Industrial Standards Compliance:
 * - IEC 61131-3 (PLC Programming)
 * - IEC 62443 (Industrial Cybersecurity)
 * - ISO 13849 (Machine Safety)
 * 
 * @author Senior Embedded Systems Engineer
 * @copyright (c) 2024 MarcSLM Control Systems
 */

#ifndef OPCUASERVER_FAIL_SAFE_CONTROLLER_H
#define OPCUASERVER_FAIL_SAFE_CONTROLLER_H

#include "plc_state.h"
#include <open62541/server.h>
#include <string>
#include <functional>
#include <chrono>
#include <atomic>

namespace slm_opcua {

// ============================================================================
// Safety State Definitions
// ============================================================================

/**
 * @brief Safety states for the SLM control system
 * 
 * @details Follows a hierarchical safety model:
 * - NORMAL: Full operation allowed
 * - CAUTION: Reduced operation, monitoring active
 * - STOPPED: Motion halted, laser off, awaiting reset
 * - EMERGENCY: All outputs forced to safe state
 */
enum class SafetyState {
    NORMAL,     ///< Normal operation
    CAUTION,    ///< Reduced operation mode
    STOPPED,    ///< Safe stop (recoverable)
    EMERGENCY   ///< Emergency stop (requires manual reset)
};

/**
 * @brief Converts SafetyState to string for logging
 */
inline const char* safetyStateToString(SafetyState state) noexcept {
    switch (state) {
        case SafetyState::NORMAL:    return "NORMAL";
        case SafetyState::CAUTION:   return "CAUTION";
        case SafetyState::STOPPED:   return "STOPPED";
        case SafetyState::EMERGENCY: return "EMERGENCY";
        default:                     return "UNKNOWN";
    }
}

// ============================================================================
// Fail-Safe Controller
// ============================================================================

/**
 * @brief Hardware fail-safe controller
 * 
 * @details Monitors system health and triggers safe shutdown when critical
 * errors are detected. Integrates with the OPC UA server to write safe
 * values to hardware variables.
 * 
 * @par Architecture:
 * This controller operates on a "fail-safe" principle:
 * - Loss of communication = safe state
 * - Software exception = safe state
 * - Watchdog timeout = safe state
 * - Out-of-bounds values = safe state
 * 
 * @par Thread Safety:
 * All public methods are thread-safe. The controller uses atomic operations
 * for state flags and mutex protection for server access.
 */
class FailSafeController {
public:
    /**
     * @brief Configuration for fail-safe behavior
     */
    struct Config {
        /** @brief Time before watchdog triggers (ms) */
        uint32_t watchdogTimeoutMs = 5000;
        
        /** @brief Enable automatic recovery attempts */
        bool autoRecovery = false;
        
        /** @brief Maximum automatic recovery attempts */
        uint32_t maxRecoveryAttempts = 3;
        
        /** @brief Log callback for safety events */
        std::function<void(const std::string&)> logCallback;
    };
    
    /**
     * @brief Construct fail-safe controller
     * @param config Configuration parameters
     */
    explicit FailSafeController(const Config& config = {});
    
    /**
     * @brief Destructor ensures safe state on shutdown
     */
    ~FailSafeController();
    
    // Prevent copying
    FailSafeController(const FailSafeController&) = delete;
    FailSafeController& operator=(const FailSafeController&) = delete;
    
    // ========================================================================
    // Server Integration
    // ========================================================================
    
    /**
     * @brief Attach to OPC UA server for hardware control
     * 
     * @param server Pointer to running UA_Server
     * @param stateContainer Thread-safe state container
     * 
     * @warning Server must remain valid for lifetime of controller
     */
    void attachServer(UA_Server* server, PlcStateContainer* stateContainer);
    
    /**
     * @brief Detach from server (e.g., before server shutdown)
     */
    void detachServer();
    
    // ========================================================================
    // Safety State Management
    // ========================================================================
    
    /**
     * @brief Get current safety state
     */
    SafetyState getState() const noexcept { return m_state.load(); }
    
    /**
     * @brief Check if system is in safe operating state
     */
    bool isSafeToOperate() const noexcept { 
        return m_state.load() == SafetyState::NORMAL; 
    }
    
    /**
     * @brief Trigger safe stop
     * 
     * @param reason Human-readable reason for the stop
     * 
     * @details Stops all motion and disables laser, but allows recovery
     * after the issue is resolved.
     */
    void triggerSafeStop(const std::string& reason);
    
    /**
     * @brief Trigger emergency stop
     * 
     * @param reason Human-readable reason for emergency
     * 
     * @details Forces all outputs to safe state. Requires manual reset
     * via resetFromEmergency() to recover.
     * 
     * @par Hardware Effects:
     * - Laser OFF (interlock active)
     * - Motion STOPPED
     * - Powder feed STOPPED
     * - All "Done" flags cleared
     */
    void triggerEmergencyStop(const std::string& reason);
    
    /**
     * @brief Attempt to reset from emergency state
     * 
     * @return true if reset successful, false if safety checks fail
     * 
     * @details Performs pre-flight safety checks before allowing operation:
     * - Verify all interlocks clear
     * - Confirm positions within limits
     * - Check communication health
     */
    bool resetFromEmergency();
    
    /**
     * @brief Reset from safe stop state
     * 
     * @return true if reset successful
     */
    bool resetFromSafeStop();
    
    // ========================================================================
    // Watchdog Management
    // ========================================================================
    
    /**
     * @brief Feed the watchdog timer
     * 
     * @details Call this periodically from the main processing loop.
     * If not called within watchdogTimeoutMs, triggers safe stop.
     */
    void feedWatchdog();
    
    /**
     * @brief Check if watchdog has timed out
     * @return true if watchdog timed out
     */
    bool isWatchdogExpired() const;
    
    // ========================================================================
    // Value Validation
    // ========================================================================
    
    /**
     * @brief Validate a position value before writing
     * 
     * @param position Value to validate
     * @param variableName Name for error logging
     * @return true if valid, false if out of bounds
     */
    bool validatePosition(int32_t position, const std::string& variableName);
    
    /**
     * @brief Validate state transition
     * 
     * @param from Current state
     * @param to Requested next state
     * @return true if transition is valid
     */
    bool validateStateTransition(const PlcState& from, const PlcState& to);
    
private:
    // ========================================================================
    // Internal Methods
    // ========================================================================
    
    /**
     * @brief Write safe values to all hardware variables
     * @param emergency true if emergency (force all interlocks)
     */
    void writeSafeState(bool emergency);
    
    /**
     * @brief Log a safety-related message
     */
    void log(const std::string& message) const;
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    Config m_config;
    std::atomic<SafetyState> m_state{SafetyState::NORMAL};
    
    // Server integration
    UA_Server* m_server = nullptr;
    PlcStateContainer* m_stateContainer = nullptr;
    mutable std::mutex m_serverMutex;
    
    // Watchdog
    std::chrono::steady_clock::time_point m_lastWatchdogFeed;
    std::atomic<bool> m_watchdogEnabled{true};
    
    // Recovery tracking
    uint32_t m_recoveryAttempts = 0;
};

} // namespace slm_opcua

#endif // OPCUASERVER_FAIL_SAFE_CONTROLLER_H
