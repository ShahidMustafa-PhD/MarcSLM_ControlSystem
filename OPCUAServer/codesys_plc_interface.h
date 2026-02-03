/**
 * @file codesys_plc_interface.h
 * @brief Industrial-grade CoDeSys PLC Interface via Modbus TCP
 * 
 * @details This file implements a robust, production-ready interface to
 * CoDeSys PLCs using the Modbus TCP protocol. The implementation follows
 * industrial safety standards (IEC 61508, IEC 61131) and provides:
 * 
 * - Thread-safe communication with configurable timeouts
 * - Automatic reconnection on connection loss
 * - Watchdog monitoring and heartbeat mechanism
 * - Emergency stop propagation
 * - Position limit validation
 * - Connection health monitoring
 * - Comprehensive error logging
 * 
 * @par Modbus Register Map (CoDeSys Convention):
 * 
 * **Input Registers (Read from PLC):**
 * - 30001-30002: Source cylinder actual position (INT32, ?m)
 * - 30003-30004: Sink cylinder actual position (INT32, ?m)
 * - 30005: Movement complete flag (BOOL)
 * - 30006: Powder fill complete flag (BOOL)
 * - 30007: Layer preparation complete flag (BOOL)
 * - 30008: Startup complete flag (BOOL)
 * - 30009: Emergency stop status (BOOL)
 * - 30010: PLC heartbeat counter (UINT16)
 * - 30011: PLC error code (UINT16)
 * 
 * **Holding Registers (Write to PLC):**
 * - 40001: Start powder fill command (BOOL)
 * - 40002: Start layer preparation command (BOOL)
 * - 40003: Start startup sequence command (BOOL)
 * - 40004: Emergency stop trigger (BOOL)
 * - 40005-40006: Delta source (INT32, ?m)
 * - 40007-40008: Delta sink (INT32, ?m)
 * - 40009-40010: Step source (INT32, ?m)
 * - 40011-40012: Step sink (INT32, ?m)
 * - 40013-40014: Z stacks count (INT32)
 * - 40015: Reset commands (BOOL)
 * - 40016: Client heartbeat counter (UINT16)
 * 
 * @par Thread Safety:
 * All public methods are thread-safe and can be called from multiple threads
 * (OPC UA server thread, main thread, watchdog thread, etc.)
 * 
 * @par Error Handling:
 * - Automatic reconnection on communication failure
 * - Configurable retry attempts and timeouts
 * - Detailed error reporting via callback
 * - Fail-safe defaults on error conditions
 * 
 * @author Senior Industrial Automation Engineer
 * @copyright (c) 2024 MarcSLM Control Systems
 * @version 1.0.0
 */

#pragma once

#include <string>
#include <functional>
#include <memory>
#include <atomic>
#include <mutex>
#include <chrono>
#include <cstdint>

namespace slm_opcua {

// ============================================================================
// PLC Register Map (CoDeSys/Modbus TCP Convention)
// ============================================================================

/**
 * @brief Modbus register addresses for CoDeSys PLC interface
 * 
 * @details CoDeSys uses standard Modbus TCP addressing:
 * - Input Registers: 30001-39999 (read-only from client perspective)
 * - Holding Registers: 40001-49999 (read/write)
 * 
 * Note: Modbus library uses 0-based addressing, so subtract 30001/40001
 */
struct ModbusRegisters {
    // ========================================================================
    // Input Registers (Read from PLC) - Base address 30001
    // ========================================================================
    
    // Position feedback (INT32 = 2 registers each)
    static constexpr uint16_t INPUT_SOURCE_POSITION = 0;      // 30001-30002
    static constexpr uint16_t INPUT_SINK_POSITION = 2;        // 30003-30004
    
    // Status flags (BOOL = 1 register each)
    static constexpr uint16_t INPUT_MOVEMENT_COMPLETE = 4;    // 30005
    static constexpr uint16_t INPUT_POWDER_FILL_DONE = 5;     // 30006
    static constexpr uint16_t INPUT_LAYER_PREP_DONE = 6;      // 30007
    static constexpr uint16_t INPUT_STARTUP_DONE = 7;         // 30008
    static constexpr uint16_t INPUT_EMERGENCY_STOP = 8;       // 30009
    
    // Diagnostics
    static constexpr uint16_t INPUT_PLC_HEARTBEAT = 9;        // 30010
    static constexpr uint16_t INPUT_PLC_ERROR_CODE = 10;      // 30011
    static constexpr uint16_t INPUT_SOURCE_LIMIT_SWITCH = 11; // 30012
    static constexpr uint16_t INPUT_SINK_LIMIT_SWITCH = 12;   // 30013
    
    // ========================================================================
    // Holding Registers (Write to PLC) - Base address 40001
    // ========================================================================
    
    // Command flags (BOOL = 1 register each)
    static constexpr uint16_t HOLD_START_POWDER_FILL = 0;     // 40001
    static constexpr uint16_t HOLD_START_LAYER_PREP = 1;      // 40002
    static constexpr uint16_t HOLD_START_STARTUP = 2;         // 40003
    static constexpr uint16_t HOLD_EMERGENCY_STOP = 3;        // 40004
    
    // Motion parameters (INT32 = 2 registers each)
    static constexpr uint16_t HOLD_DELTA_SOURCE = 4;          // 40005-40006
    static constexpr uint16_t HOLD_DELTA_SINK = 6;            // 40007-40008
    static constexpr uint16_t HOLD_STEP_SOURCE = 8;           // 40009-40010
    static constexpr uint16_t HOLD_STEP_SINK = 10;            // 40011-40012
    static constexpr uint16_t HOLD_Z_STACKS = 12;             // 40013-40014
    
    // Control
    static constexpr uint16_t HOLD_RESET_COMMANDS = 14;       // 40015
    static constexpr uint16_t HOLD_CLIENT_HEARTBEAT = 15;     // 40016
};

// ============================================================================
// PLC Connection Configuration
// ============================================================================

/**
 * @brief Configuration for CoDeSys PLC connection
 */
struct PlcConfig {
    // Network settings
    std::string plcIpAddress = "192.168.1.10";
    uint16_t plcPort = 502;  // Standard Modbus TCP port
    uint8_t modbusUnitId = 1;  // Modbus slave ID (usually 1 for CoDeSys)
    
    // Timeouts (industrial standard: 100-500ms)
    uint32_t connectionTimeoutMs = 1000;
    uint32_t responseTimeoutMs = 500;
    uint32_t reconnectIntervalMs = 2000;
    
    // Retry configuration
    uint32_t maxReconnectAttempts = 10;
    uint32_t maxReadRetries = 3;
    uint32_t maxWriteRetries = 3;
    
    // Watchdog (safety-critical)
    uint32_t watchdogTimeoutMs = 5000;
    bool enableWatchdog = true;
    
    // Safety limits (microns)
    int32_t maxSourcePosition = 200000;  // 200mm
    int32_t maxSinkPosition = 150000;    // 150mm
    int32_t minPosition = 0;
    int32_t maxStepSize = 1000;          // 1mm per step
    
    // Enable/disable safety features
    bool enableSoftLimits = true;
    bool enableHardLimits = true;
    bool enableEmergencyStop = true;
    
    // Logging callback
    std::function<void(const std::string&)> logCallback;
};

// ============================================================================
// PLC Status Information
// ============================================================================

/**
 * @brief Current PLC status and feedback
 */
struct PlcStatus {
    // Connection state
    bool connected = false;
    bool communicationHealthy = false;
    uint32_t reconnectAttempts = 0;
    
    // Position feedback
    int32_t sourcePositionActual = 0;
    int32_t sinkPositionActual = 0;
    
    // Status flags
    bool movementComplete = false;
    bool powderFillDone = false;
    bool layerPrepDone = false;
    bool startupDone = false;
    bool emergencyStopActive = false;
    
    // Limit switches
    bool sourceLimitSwitch = false;
    bool sinkLimitSwitch = false;
    
    // Diagnostics
    uint16_t plcHeartbeat = 0;
    uint16_t plcErrorCode = 0;
    std::chrono::steady_clock::time_point lastSuccessfulRead;
    std::chrono::steady_clock::time_point lastSuccessfulWrite;
    
    // Statistics
    uint64_t totalReads = 0;
    uint64_t totalWrites = 0;
    uint64_t failedReads = 0;
    uint64_t failedWrites = 0;
};

// ============================================================================
// CoDeSys PLC Interface Class
// ============================================================================

/**
 * @brief Industrial-grade interface to CoDeSys PLC via Modbus TCP
 * 
 * This class provides thread-safe, robust communication with CoDeSys PLCs
 * for controlling SLM machine hardware (stepper motors, pneumatics, etc.)
 * 
 * Features:
 * - Automatic connection management with reconnection
 * - Heartbeat monitoring (bidirectional)
 * - Emergency stop propagation
 * - Position limit validation
 * - Thread-safe operation
 * - Comprehensive error handling
 * - Industrial-grade reliability (designed for 24/7 operation)
 * 
 * Safety Philosophy:
 * - Fail-safe defaults: On error, assume unsafe state
 * - Watchdog monitoring: Detect communication loss
 * - Limit validation: Prevent out-of-bounds movements
 * - Emergency stop priority: Always honored immediately
 */
class CodesysPlcInterface {
public:
    /**
     * @brief Constructor
     * @param config PLC connection configuration
     */
    explicit CodesysPlcInterface(const PlcConfig& config);
    
    /**
     * @brief Destructor - ensures clean disconnection
     */
    ~CodesysPlcInterface();
    
    // Prevent copying (owns Modbus connection)
    CodesysPlcInterface(const CodesysPlcInterface&) = delete;
    CodesysPlcInterface& operator=(const CodesysPlcInterface&) = delete;
    
    // ========================================================================
    // Connection Management
    // ========================================================================
    
    /**
     * @brief Connect to CoDeSys PLC
     * @return true if connection successful
     * 
     * Thread-safe: Can be called from any thread
     * Blocking: May take up to connectionTimeoutMs
     */
    bool connect();
    
    /**
     * @brief Disconnect from PLC
     * 
     * Thread-safe: Can be called from any thread
     * Ensures clean shutdown of Modbus connection
     */
    void disconnect();
    
    /**
     * @brief Check if connected to PLC
     * @return true if connection is active and healthy
     * 
     * Thread-safe: Non-blocking
     */
    bool isConnected() const;
    
    /**
     * @brief Attempt to reconnect to PLC
     * @return true if reconnection successful
     * 
     * Thread-safe: Blocking, may take several seconds
     * Automatically called on communication errors if configured
     */
    bool reconnect();
    
    // ========================================================================
    // Command Interface (Write to PLC)
    // ========================================================================
    
    /**
     * @brief Start machine startup sequence
     * @return true if command sent successfully
     * 
     * PLC Action: Initializes machine, homes axes, checks safety interlocks
     */
    bool startStartupSequence();
    
    /**
     * @brief Start powder fill operation
     * @param zStacks Number of layers to fill
     * @param deltaSource Source cylinder increment per layer (?m)
     * @param deltaSink Sink cylinder decrement per layer (?m)
     * @return true if command sent successfully
     * 
     * PLC Action: Moves cylinders to create initial powder bed
     * Safety: Validates position limits before sending
     */
    bool startPowderFill(int32_t zStacks, int32_t deltaSource, int32_t deltaSink);
    
    /**
     * @brief Start layer preparation (single layer)
     * @param stepSource Source cylinder increment (?m)
     * @param stepSink Sink cylinder decrement (?m)
     * @return true if command sent successfully
     * 
     * PLC Action: Moves cylinders, activates recoater
     * Safety: Validates step size and position limits
     */
    bool startLayerPreparation(int32_t stepSource, int32_t stepSink);
    
    /**
     * @brief Trigger emergency stop
     * @return true if command sent successfully
     * 
     * PLC Action: Immediately halts all motion, disables motors
     * Priority: Bypasses all queues, sent immediately
     * Safety: Critical path - must complete in < 100ms
     */
    bool triggerEmergencyStop();
    
    /**
     * @brief Reset command flags after operation complete
     * @return true if reset successful
     * 
     * PLC Action: Clears command registers for next operation
     */
    bool resetCommands();
    
    // ========================================================================
    // Status Reading (Read from PLC)
    // ========================================================================
    
    /**
     * @brief Read current PLC status
     * @param status Output status structure
     * @return true if read successful
     * 
     * Reads all input registers in a single Modbus transaction
     * Thread-safe: Can be called concurrently
     * Performance: Optimized for minimal network traffic
     */
    bool readStatus(PlcStatus& status);
    
    /**
     * @brief Read cylinder positions only (fast)
     * @param sourcePos Source cylinder position (?m)
     * @param sinkPos Sink cylinder position (?m)
     * @return true if read successful
     * 
     * Optimized for high-frequency polling (e.g., GUI updates)
     */
    bool readPositions(int32_t& sourcePos, int32_t& sinkPos);
    
    /**
     * @brief Get cached status (non-blocking)
     * @return Last successfully read status
     * 
     * Returns cached data without network I/O
     * Useful for high-frequency queries
     */
    PlcStatus getCachedStatus() const;
    
    // ========================================================================
    // Watchdog and Health Monitoring
    // ========================================================================
    
    /**
     * @brief Update watchdog and heartbeat
     * 
     * Call this periodically (e.g., every 100ms) to:
     * - Increment client heartbeat counter
     * - Check PLC heartbeat for communication health
     * - Detect watchdog timeout
     * 
     * Thread-safe: Can be called from dedicated watchdog thread
     */
    void updateWatchdog();
    
    /**
     * @brief Check if watchdog has expired
     * @return true if communication timeout detected
     * 
     * Safety: If true, assume PLC communication lost
     * Action: Trigger emergency stop or safe state
     */
    bool isWatchdogExpired() const;
    
    /**
     * @brief Get time since last successful communication
     * @return Milliseconds since last read/write succeeded
     */
    uint64_t getTimeSinceLastCommunication() const;
    
    // ========================================================================
    // Safety Validation
    // ========================================================================
    
    /**
     * @brief Validate position is within safe limits
     * @param position Position to validate (?m)
     * @param isSource true for source cylinder, false for sink
     * @return true if position is safe
     * 
     * Checks against configured soft and hard limits
     */
    bool isPositionSafe(int32_t position, bool isSource) const;
    
    /**
     * @brief Validate step size is safe
     * @param stepSize Step size to validate (?m)
     * @return true if step size is within safe range
     */
    bool isStepSizeSafe(int32_t stepSize) const;
    
    // ========================================================================
    // Configuration
    // ========================================================================
    
    /**
     * @brief Update PLC configuration (runtime)
     * @param config New configuration
     * 
     * Note: Requires reconnection if IP/port changed
     */
    void updateConfig(const PlcConfig& config);
    
    /**
     * @brief Get current configuration
     * @return Current PLC configuration
     */
    PlcConfig getConfig() const;

private:
    // ========================================================================
    // Internal Implementation
    // ========================================================================
    
    // Modbus communication
    bool readInputRegisters(uint16_t startAddr, uint16_t count, uint16_t* dest);
    bool readHoldingRegisters(uint16_t startAddr, uint16_t count, uint16_t* dest);
    bool writeHoldingRegister(uint16_t addr, uint16_t value);
    bool writeHoldingRegisters(uint16_t startAddr, uint16_t count, const uint16_t* values);
    
    // INT32 helpers (CoDeSys uses big-endian INT32 across 2 registers)
    bool readInt32Input(uint16_t addr, int32_t& value);
    bool writeInt32Holding(uint16_t addr, int32_t value);
    
    // Connection management
    bool createModbusContext();
    void destroyModbusContext();
    bool setModbusTimeouts();
    
    // Validation helpers
    bool validatePositionLimits(int32_t sourcePos, int32_t sinkPos);
    
    // Logging
    void log(const std::string& message);
    void logError(const std::string& message, int modbusErrno);
    
    // ========================================================================
    // Member Variables
    // ========================================================================
    
    PlcConfig m_config;
    
    // Modbus context (opaque pointer to libmodbus structure)
    void* m_modbusCtx;  // modbus_t* (using void* to avoid including modbus.h here)
    
    // Thread safety
    mutable std::recursive_mutex m_mutex;  // Recursive for internal calls
    
    // Connection state
    std::atomic<bool> m_connected{false};
    std::atomic<uint32_t> m_reconnectAttempts{0};
    
    // Cached status
    PlcStatus m_cachedStatus;
    mutable std::mutex m_statusMutex;
    
    // Watchdog
    std::chrono::steady_clock::time_point m_lastCommunication;
    std::chrono::steady_clock::time_point m_lastWatchdogUpdate;
    uint16_t m_clientHeartbeat{0};
    uint16_t m_lastPlcHeartbeat{0};
    mutable std::mutex m_watchdogMutex;
};

} // namespace slm_opcua
