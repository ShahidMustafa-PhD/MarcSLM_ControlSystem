/**
 * @file codesys_plc_interface.cpp
 * @brief Implementation of industrial-grade CoDeSys PLC Interface
 * 
 * @details This implementation uses libmodbus for Modbus TCP communication.
 * All operations are thread-safe and include comprehensive error handling.
 * 
 * @par Dependencies:
 * - libmodbus (https://libmodbus.org/)
 * 
 * @par Installation (Windows):
 * @code
 * vcpkg install libmodbus:x64-windows
 * @endcode
 * 
 * @author Senior Industrial Automation Engineer
 * @copyright (c) 2024 MarcSLM Control Systems
 */

#include "codesys_plc_interface.h"

#ifdef HAS_PLC_INTERFACE
#include <modbus/modbus.h>
#else
#warning "libmodbus not found - PLC interface will be non-functional"
// Stub implementation when libmodbus is not available
typedef struct modbus_t modbus_t;
#endif

#include <cstring>
#include <sstream>
#include <iomanip>
#include <algorithm>

namespace slm_opcua {

// ============================================================================
// Constructor / Destructor
// ============================================================================

CodesysPlcInterface::CodesysPlcInterface(const PlcConfig& config)
    : m_config(config)
    , m_modbusCtx(nullptr)
{
    m_lastCommunication = std::chrono::steady_clock::now();
    m_lastWatchdogUpdate = std::chrono::steady_clock::now();
    
    log("CoDeSys PLC Interface initialized");
    log("Target PLC: " + m_config.plcIpAddress + ":" + std::to_string(m_config.plcPort));
}

CodesysPlcInterface::~CodesysPlcInterface()
{
    disconnect();
    log("CoDeSys PLC Interface destroyed");
}

// ============================================================================
// Connection Management
// ============================================================================

bool CodesysPlcInterface::connect()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (m_connected) {
        log("Already connected to PLC");
        return true;
    }
    
    log("Connecting to CoDeSys PLC at " + m_config.plcIpAddress + 
        ":" + std::to_string(m_config.plcPort));
    
    // Create Modbus TCP context
    if (!createModbusContext()) {
        return false;
    }
    
    // Set timeouts
    if (!setModbusTimeouts()) {
        destroyModbusContext();
        return false;
    }
    
    // Establish connection
    modbus_t* ctx = static_cast<modbus_t*>(m_modbusCtx);
    if (modbus_connect(ctx) == -1) {
        logError("Failed to connect to PLC", errno);
        destroyModbusContext();
        return false;
    }
    
    m_connected = true;
    m_reconnectAttempts = 0;
    m_lastCommunication = std::chrono::steady_clock::now();
    
    log("Successfully connected to CoDeSys PLC");
    
    // Read initial status
    PlcStatus status;
    if (readStatus(status)) {
        log("Initial PLC status read successfully");
        log("Source position: " + std::to_string(status.sourcePositionActual) + " ?m");
        log("Sink position: " + std::to_string(status.sinkPositionActual) + " ?m");
        
        if (status.emergencyStopActive) {
            log("WARNING: PLC is in EMERGENCY STOP state!");
        }
    }
    
    return true;
}

void CodesysPlcInterface::disconnect()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (!m_connected) {
        return;
    }
    
    log("Disconnecting from CoDeSys PLC");
    
    // Send final heartbeat = 0 to signal clean disconnect
    writeHoldingRegister(ModbusRegisters::HOLD_CLIENT_HEARTBEAT, 0);
    
    destroyModbusContext();
    m_connected = false;
    
    log("Disconnected from CoDeSys PLC");
}

bool CodesysPlcInterface::isConnected() const
{
    return m_connected.load();
}

bool CodesysPlcInterface::reconnect()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    log("Attempting to reconnect to PLC...");
    
    // Disconnect first
    if (m_connected) {
        disconnect();
    }
    
    // Try to reconnect
    uint32_t attempt = 0;
    while (attempt < m_config.maxReconnectAttempts) {
        attempt++;
        m_reconnectAttempts = attempt;
        
        log("Reconnect attempt " + std::to_string(attempt) + "/" + 
            std::to_string(m_config.maxReconnectAttempts));
        
        if (connect()) {
            log("Reconnection successful");
            return true;
        }
        
        // Wait before retry
        std::this_thread::sleep_for(
            std::chrono::milliseconds(m_config.reconnectIntervalMs));
    }
    
    log("ERROR: Reconnection failed after " + std::to_string(attempt) + " attempts");
    return false;
}

// ============================================================================
// Command Interface
// ============================================================================

bool CodesysPlcInterface::startStartupSequence()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (!m_connected) {
        log("ERROR: Cannot start startup sequence - not connected to PLC");
        return false;
    }
    
    log("Starting machine startup sequence");
    
    // Reset any previous commands first
    if (!resetCommands()) {
        log("WARNING: Failed to reset commands before startup");
    }
    
    // Set startup command flag
    if (!writeHoldingRegister(ModbusRegisters::HOLD_START_STARTUP, 1)) {
        log("ERROR: Failed to write startup command");
        return false;
    }
    
    log("Startup sequence command sent successfully");
    return true;
}

bool CodesysPlcInterface::startPowderFill(int32_t zStacks, int32_t deltaSource, int32_t deltaSink)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (!m_connected) {
        log("ERROR: Cannot start powder fill - not connected to PLC");
        return false;
    }
    
    log("Starting powder fill: zStacks=" + std::to_string(zStacks) +
        ", deltaSource=" + std::to_string(deltaSource) + 
        ", deltaSink=" + std::to_string(deltaSink));
    
    // Safety validation
    int32_t projectedSourcePos = m_cachedStatus.sourcePositionActual + (zStacks * deltaSource);
    int32_t projectedSinkPos = m_cachedStatus.sinkPositionActual + (zStacks * deltaSink);
    
    if (!validatePositionLimits(projectedSourcePos, projectedSinkPos)) {
        log("ERROR: Powder fill would exceed position limits");
        log("  Projected source: " + std::to_string(projectedSourcePos) + " ?m");
        log("  Projected sink: " + std::to_string(projectedSinkPos) + " ?m");
        return false;
    }
    
    // Reset previous commands
    if (!resetCommands()) {
        log("WARNING: Failed to reset commands before powder fill");
    }
    
    // Write parameters
    if (!writeInt32Holding(ModbusRegisters::HOLD_Z_STACKS, zStacks)) {
        log("ERROR: Failed to write Z stacks");
        return false;
    }
    
    if (!writeInt32Holding(ModbusRegisters::HOLD_DELTA_SOURCE, deltaSource)) {
        log("ERROR: Failed to write delta source");
        return false;
    }
    
    if (!writeInt32Holding(ModbusRegisters::HOLD_DELTA_SINK, deltaSink)) {
        log("ERROR: Failed to write delta sink");
        return false;
    }
    
    // Start powder fill
    if (!writeHoldingRegister(ModbusRegisters::HOLD_START_POWDER_FILL, 1)) {
        log("ERROR: Failed to write powder fill command");
        return false;
    }
    
    log("Powder fill command sent successfully");
    return true;
}

bool CodesysPlcInterface::startLayerPreparation(int32_t stepSource, int32_t stepSink)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (!m_connected) {
        log("ERROR: Cannot start layer preparation - not connected to PLC");
        return false;
    }
    
    log("Starting layer preparation: stepSource=" + std::to_string(stepSource) +
        ", stepSink=" + std::to_string(stepSink));
    
    // Safety validation: step sizes
    if (!isStepSizeSafe(stepSource) || !isStepSizeSafe(std::abs(stepSink))) {
        log("ERROR: Step size exceeds safe limit (" + 
            std::to_string(m_config.maxStepSize) + " ?m)");
        return false;
    }
    
    // Safety validation: projected positions
    int32_t projectedSourcePos = m_cachedStatus.sourcePositionActual + stepSource;
    int32_t projectedSinkPos = m_cachedStatus.sinkPositionActual + stepSink;
    
    if (!validatePositionLimits(projectedSourcePos, projectedSinkPos)) {
        log("ERROR: Layer preparation would exceed position limits");
        log("  Projected source: " + std::to_string(projectedSourcePos) + " ?m");
        log("  Projected sink: " + std::to_string(projectedSinkPos) + " ?m");
        return false;
    }
    
    // Reset previous commands
    if (!resetCommands()) {
        log("WARNING: Failed to reset commands before layer preparation");
    }
    
    // Write step parameters
    if (!writeInt32Holding(ModbusRegisters::HOLD_STEP_SOURCE, stepSource)) {
        log("ERROR: Failed to write step source");
        return false;
    }
    
    if (!writeInt32Holding(ModbusRegisters::HOLD_STEP_SINK, stepSink)) {
        log("ERROR: Failed to write step sink");
        return false;
    }
    
    // Start layer preparation
    if (!writeHoldingRegister(ModbusRegisters::HOLD_START_LAYER_PREP, 1)) {
        log("ERROR: Failed to write layer preparation command");
        return false;
    }
    
    log("Layer preparation command sent successfully");
    return true;
}

bool CodesysPlcInterface::triggerEmergencyStop()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    log("!!! EMERGENCY STOP TRIGGERED !!!");
    
    if (!m_connected) {
        log("ERROR: Cannot send emergency stop - not connected to PLC");
        log("  PLC should have independent E-Stop circuit!");
        return false;
    }
    
    // Emergency stop is CRITICAL - single focused write
    bool success = writeHoldingRegister(ModbusRegisters::HOLD_EMERGENCY_STOP, 1);
    
    if (success) {
        log("Emergency stop signal sent to PLC");
    } else {
        log("CRITICAL ERROR: Failed to send emergency stop to PLC!");
    }
    
    return success;
}

bool CodesysPlcInterface::resetCommands()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (!m_connected) {
        return false;
    }
    
    // Clear all command flags
    uint16_t clearCommands[16] = {0};
    
    bool success = writeHoldingRegisters(
        ModbusRegisters::HOLD_START_POWDER_FILL, 
        4,  // Clear first 4 command flags
        clearCommands);
    
    if (success) {
        log("Command flags reset successfully");
    }
    
    return success;
}

// ============================================================================
// Status Reading
// ============================================================================

bool CodesysPlcInterface::readStatus(PlcStatus& status)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (!m_connected) {
        return false;
    }
    
    // Read all input registers in one transaction (efficient)
    uint16_t inputRegs[16] = {0};
    const uint16_t numInputRegs = 13;  // 0-12
    
    if (!readInputRegisters(ModbusRegisters::INPUT_SOURCE_POSITION, 
                            numInputRegs, 
                            inputRegs)) {
        status.connected = false;
        status.communicationHealthy = false;
        return false;
    }
    
    // Update connection state
    status.connected = true;
    status.communicationHealthy = true;
    status.reconnectAttempts = m_reconnectAttempts.load();
    
    // Parse positions (INT32)
    status.sourcePositionActual = 
        (static_cast<int32_t>(inputRegs[0]) << 16) | inputRegs[1];
    status.sinkPositionActual = 
        (static_cast<int32_t>(inputRegs[2]) << 16) | inputRegs[3];
    
    // Parse status flags (BOOL)
    status.movementComplete = (inputRegs[4] != 0);
    status.powderFillDone = (inputRegs[5] != 0);
    status.layerPrepDone = (inputRegs[6] != 0);
    status.startupDone = (inputRegs[7] != 0);
    status.emergencyStopActive = (inputRegs[8] != 0);
    
    // Parse diagnostics
    status.plcHeartbeat = inputRegs[9];
    status.plcErrorCode = inputRegs[10];
    status.sourceLimitSwitch = (inputRegs[11] != 0);
    status.sinkLimitSwitch = (inputRegs[12] != 0);
    
    // Update timestamps
    status.lastSuccessfulRead = std::chrono::steady_clock::now();
    status.totalReads++;
    
    // Cache status
    {
        std::lock_guard<std::mutex> statusLock(m_statusMutex);
        m_cachedStatus = status;
    }
    
    m_lastCommunication = std::chrono::steady_clock::now();
    
    // Check for errors
    if (status.plcErrorCode != 0) {
        log("WARNING: PLC error code: " + std::to_string(status.plcErrorCode));
    }
    
    if (status.emergencyStopActive) {
        log("WARNING: PLC emergency stop is active");
    }
    
    return true;
}

bool CodesysPlcInterface::readPositions(int32_t& sourcePos, int32_t& sinkPos)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    if (!m_connected) {
        return false;
    }
    
    // Read just the position registers (fast)
    if (!readInt32Input(ModbusRegisters::INPUT_SOURCE_POSITION, sourcePos)) {
        return false;
    }
    
    if (!readInt32Input(ModbusRegisters::INPUT_SINK_POSITION, sinkPos)) {
        return false;
    }
    
    m_lastCommunication = std::chrono::steady_clock::now();
    
    return true;
}

PlcStatus CodesysPlcInterface::getCachedStatus() const
{
    std::lock_guard<std::mutex> lock(m_statusMutex);
    return m_cachedStatus;
}

// ============================================================================
// Watchdog and Health Monitoring
// ============================================================================

void CodesysPlcInterface::updateWatchdog()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    std::lock_guard<std::mutex> watchdogLock(m_watchdogMutex);
    
    if (!m_connected || !m_config.enableWatchdog) {
        return;
    }
    
    // Increment client heartbeat
    m_clientHeartbeat++;
    
    // Write heartbeat to PLC
    if (writeHoldingRegister(ModbusRegisters::HOLD_CLIENT_HEARTBEAT, m_clientHeartbeat)) {
        m_lastWatchdogUpdate = std::chrono::steady_clock::now();
    } else {
        log("WARNING: Failed to update watchdog heartbeat");
    }
    
    // Read PLC heartbeat to verify it's incrementing
    PlcStatus status;
    if (readStatus(status)) {
        if (status.plcHeartbeat == m_lastPlcHeartbeat) {
            log("WARNING: PLC heartbeat not incrementing - PLC may be frozen");
        }
        m_lastPlcHeartbeat = status.plcHeartbeat;
    }
}

bool CodesysPlcInterface::isWatchdogExpired() const
{
    if (!m_config.enableWatchdog) {
        return false;
    }
    
    std::lock_guard<std::mutex> lock(m_watchdogMutex);
    
    auto now = std::chrono::steady_clock::now();
    auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastCommunication).count();
    
    return elapsed > m_config.watchdogTimeoutMs;
}

uint64_t CodesysPlcInterface::getTimeSinceLastCommunication() const
{
    std::lock_guard<std::mutex> lock(m_watchdogMutex);
    
    auto now = std::chrono::steady_clock::now();
    return std::chrono::duration_cast<std::chrono::milliseconds>(
        now - m_lastCommunication).count();
}

// ============================================================================
// Safety Validation
// ============================================================================

bool CodesysPlcInterface::isPositionSafe(int32_t position, bool isSource) const
{
    if (!m_config.enableSoftLimits) {
        return true;
    }
    
    int32_t maxPos = isSource ? m_config.maxSourcePosition : m_config.maxSinkPosition;
    
    if (position < m_config.minPosition || position > maxPos) {
        return false;
    }
    
    return true;
}

bool CodesysPlcInterface::isStepSizeSafe(int32_t stepSize) const
{
    return std::abs(stepSize) <= m_config.maxStepSize;
}

// ============================================================================
// Configuration
// ============================================================================

void CodesysPlcInterface::updateConfig(const PlcConfig& config)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    
    bool needsReconnect = 
        (config.plcIpAddress != m_config.plcIpAddress) ||
        (config.plcPort != m_config.plcPort) ||
        (config.modbusUnitId != m_config.modbusUnitId);
    
    m_config = config;
    
    if (needsReconnect && m_connected) {
        log("Configuration changed - reconnecting to PLC");
        reconnect();
    }
}

PlcConfig CodesysPlcInterface::getConfig() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_config;
}

// ============================================================================
// Internal Implementation - Modbus Communication
// ============================================================================

bool CodesysPlcInterface::readInputRegisters(uint16_t startAddr, uint16_t count, uint16_t* dest)
{
    if (!m_modbusCtx) {
        return false;
    }
    
    modbus_t* ctx = static_cast<modbus_t*>(m_modbusCtx);
    
    for (uint32_t retry = 0; retry < m_config.maxReadRetries; retry++) {
        int rc = modbus_read_input_registers(ctx, startAddr, count, dest);
        
        if (rc == count) {
            {
                std::lock_guard<std::mutex> statusLock(m_statusMutex);
                m_cachedStatus.totalReads++;
            }
            return true;
        }
        
        if (retry < m_config.maxReadRetries - 1) {
            log("Modbus read retry " + std::to_string(retry + 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    logError("Failed to read input registers", errno);
    
    {
        std::lock_guard<std::mutex> statusLock(m_statusMutex);
        m_cachedStatus.failedReads++;
        m_cachedStatus.communicationHealthy = false;
    }
    
    // Consider reconnection on persistent failure
    if (m_config.maxReconnectAttempts > 0) {
        log("Communication failure - will attempt reconnection");
        m_connected = false;
    }
    
    return false;
}

bool CodesysPlcInterface::readHoldingRegisters(uint16_t startAddr, uint16_t count, uint16_t* dest)
{
    if (!m_modbusCtx) {
        return false;
    }
    
    modbus_t* ctx = static_cast<modbus_t*>(m_modbusCtx);
    
    for (uint32_t retry = 0; retry < m_config.maxReadRetries; retry++) {
        int rc = modbus_read_registers(ctx, startAddr, count, dest);
        
        if (rc == count) {
            {
                std::lock_guard<std::mutex> statusLock(m_statusMutex);
                m_cachedStatus.totalReads++;
            }
            return true;
        }
        
        if (retry < m_config.maxReadRetries - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    logError("Failed to read holding registers", errno);
    
    {
        std::lock_guard<std::mutex> statusLock(m_statusMutex);
        m_cachedStatus.failedReads++;
        m_cachedStatus.communicationHealthy = false;
    }
    
    return false;
}

bool CodesysPlcInterface::writeHoldingRegister(uint16_t addr, uint16_t value)
{
    if (!m_modbusCtx) {
        return false;
    }
    
    modbus_t* ctx = static_cast<modbus_t*>(m_modbusCtx);
    
    for (uint32_t retry = 0; retry < m_config.maxWriteRetries; retry++) {
        int rc = modbus_write_register(ctx, addr, value);
        
        if (rc == 1) {
            {
                std::lock_guard<std::mutex> statusLock(m_statusMutex);
                m_cachedStatus.totalWrites++;
                m_cachedStatus.lastSuccessfulWrite = std::chrono::steady_clock::now();
            }
            m_lastCommunication = std::chrono::steady_clock::now();
            return true;
        }
        
        if (retry < m_config.maxWriteRetries - 1) {
            log("Modbus write retry " + std::to_string(retry + 1));
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    logError("Failed to write holding register", errno);
    
    {
        std::lock_guard<std::mutex> statusLock(m_statusMutex);
        m_cachedStatus.failedWrites++;
        m_cachedStatus.communicationHealthy = false;
    }
    
    return false;
}

bool CodesysPlcInterface::writeHoldingRegisters(uint16_t startAddr, uint16_t count, const uint16_t* values)
{
    if (!m_modbusCtx) {
        return false;
    }
    
    modbus_t* ctx = static_cast<modbus_t*>(m_modbusCtx);
    
    for (uint32_t retry = 0; retry < m_config.maxWriteRetries; retry++) {
        int rc = modbus_write_registers(ctx, startAddr, count, values);
        
        if (rc == count) {
            {
                std::lock_guard<std::mutex> statusLock(m_statusMutex);
                m_cachedStatus.totalWrites++;
                m_cachedStatus.lastSuccessfulWrite = std::chrono::steady_clock::now();
            }
            m_lastCommunication = std::chrono::steady_clock::now();
            return true;
        }
        
        if (retry < m_config.maxWriteRetries - 1) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
        }
    }
    
    logError("Failed to write holding registers", errno);
    
    {
        std::lock_guard<std::mutex> statusLock(m_statusMutex);
        m_cachedStatus.failedWrites++;
        m_cachedStatus.communicationHealthy = false;
    }
    
    return false;
}

// ============================================================================
// INT32 Helpers (CoDeSys/Modbus Convention)
// ============================================================================

bool CodesysPlcInterface::readInt32Input(uint16_t addr, int32_t& value)
{
    uint16_t regs[2] = {0};
    
    if (!readInputRegisters(addr, 2, regs)) {
        return false;
    }
    
    // CoDeSys stores INT32 as big-endian across 2 registers
    value = (static_cast<int32_t>(regs[0]) << 16) | regs[1];
    
    return true;
}

bool CodesysPlcInterface::writeInt32Holding(uint16_t addr, int32_t value)
{
    uint16_t regs[2];
    
    // Split INT32 into 2 registers (big-endian)
    regs[0] = static_cast<uint16_t>((value >> 16) & 0xFFFF);
    regs[1] = static_cast<uint16_t>(value & 0xFFFF);
    
    return writeHoldingRegisters(addr, 2, regs);
}

// ============================================================================
// Internal Implementation - Connection Management
// ============================================================================

bool CodesysPlcInterface::createModbusContext()
{
    // Create Modbus TCP context
    modbus_t* ctx = modbus_new_tcp(
        m_config.plcIpAddress.c_str(), 
        m_config.plcPort);
    
    if (ctx == nullptr) {
        logError("Failed to create Modbus context", errno);
        return false;
    }
    
    // Set slave ID (unit identifier)
    if (modbus_set_slave(ctx, m_config.modbusUnitId) == -1) {
        logError("Failed to set Modbus slave ID", errno);
        modbus_free(ctx);
        return false;
    }
    
    m_modbusCtx = ctx;
    return true;
}

void CodesysPlcInterface::destroyModbusContext()
{
    if (m_modbusCtx) {
        modbus_t* ctx = static_cast<modbus_t*>(m_modbusCtx);
        
        if (m_connected) {
            modbus_close(ctx);
        }
        
        modbus_free(ctx);
        m_modbusCtx = nullptr;
    }
}

bool CodesysPlcInterface::setModbusTimeouts()
{
    if (!m_modbusCtx) {
        return false;
    }
    
    modbus_t* ctx = static_cast<modbus_t*>(m_modbusCtx);
    
    // Set response timeout
    uint32_t sec = m_config.responseTimeoutMs / 1000;
    uint32_t usec = (m_config.responseTimeoutMs % 1000) * 1000;
    
    if (modbus_set_response_timeout(ctx, sec, usec) == -1) {
        logError("Failed to set response timeout", errno);
        return false;
    }
    
    // Set byte timeout (inter-byte delay)
    if (modbus_set_byte_timeout(ctx, 0, 100000) == -1) {  // 100ms
        logError("Failed to set byte timeout", errno);
        return false;
    }
    
    return true;
}

// ============================================================================
// Validation Helpers
// ============================================================================

bool CodesysPlcInterface::validatePositionLimits(int32_t sourcePos, int32_t sinkPos)
{
    if (!m_config.enableSoftLimits) {
        return true;
    }
    
    if (!isPositionSafe(sourcePos, true)) {
        log("ERROR: Source position " + std::to_string(sourcePos) + 
            " ?m exceeds limits [" + std::to_string(m_config.minPosition) + 
            ", " + std::to_string(m_config.maxSourcePosition) + "]");
        return false;
    }
    
    if (!isPositionSafe(sinkPos, false)) {
        log("ERROR: Sink position " + std::to_string(sinkPos) + 
            " ?m exceeds limits [" + std::to_string(m_config.minPosition) + 
            ", " + std::to_string(m_config.maxSinkPosition) + "]");
        return false;
    }
    
    return true;
}

// ============================================================================
// Logging
// ============================================================================

void CodesysPlcInterface::log(const std::string& message)
{
    if (m_config.logCallback) {
        m_config.logCallback("[CoDeSys PLC] " + message);
    }
}

void CodesysPlcInterface::logError(const std::string& message, int modbusErrno)
{
    std::ostringstream oss;
    oss << "[CoDeSys PLC] ERROR: " << message;
    
    if (modbusErrno != 0) {
        oss << " - " << modbus_strerror(modbusErrno);
    }
    
    if (m_config.logCallback) {
        m_config.logCallback(oss.str());
    }
}

} // namespace slm_opcua
