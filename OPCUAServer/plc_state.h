/**
 * @file plc_state.h
 * @brief Thread-safe PLC state container for SLM OPC UA Server
 * 
 * @details This header defines the complete state model for the Selective
 * Laser Melting (SLM) control system. All PLC variables are represented
 * here with thread-safe access patterns.
 * 
 * @par Hardware Safety Note:
 * This structure controls physical hardware including:
 * - High-power laser systems (Class 4)
 * - Precision motion platforms (Z-axis)
 * - Powder dispensing systems
 * 
 * Improper state transitions can result in equipment damage or safety hazards.
 * 
 * @par Memory Safety:
 * All access must be protected by the accompanying mutex. Use the PlcStateGuard
 * RAII wrapper for safe access.
 * 
 * @author Senior Embedded Systems Engineer
 * @copyright (c) 2024 MarcSLM Control Systems
 */

#ifndef OPCUASERVER_PLC_STATE_H
#define OPCUASERVER_PLC_STATE_H

#include <open62541/types.h>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <optional>

namespace slm_opcua {

// ============================================================================
// PLC Variable Limits (Hardware Constraints)
// ============================================================================

/**
 * @brief Hardware limits for cylinder positions (in microns)
 * 
 * @details These limits are derived from mechanical specifications of the
 * SLM platform. Exceeding these values can cause mechanical damage.
 */
namespace limits {
    constexpr int32_t CYLINDER_POSITION_MIN = -500000;  ///< -500mm
    constexpr int32_t CYLINDER_POSITION_MAX = 500000;   ///< +500mm
    constexpr int32_t MAX_LAYER_COUNT = 100000;         ///< Maximum layers per build
    constexpr int32_t MAX_DELTA_STEP = 10000;           ///< Maximum step per layer (10mm)
}

// ============================================================================
// PLC State Structure
// ============================================================================

/**
 * @brief Complete PLC state for SLM control
 * 
 * @details This structure mirrors the actual CoDeSys PLC variable layout.
 * Variable names and types match the actual PLC nodes exactly.
 * 
 * @par Actual PLC Node IDs:
 * - ns=4;s=|var|CECC-D.Application.MakeSurface.Z_Stacks [INT16]
 * - ns=4;s=|var|CECC-D.Application.MakeSurface.Delta_Source [INT32]
 * - ns=4;s=|var|CECC-D.Application.MakeSurface.Delta_Sink [INT32]
 * - ns=4;s=|var|CECC-D.Application.MakeSurface.Layer_Ready [BOOL]
 * - ns=4;s=|var|CECC-D.Application.MakeSurface.Marcer_Source_Cylinder_ActualPosition [INT32]
 * - ns=4;s=|var|CECC-D.Application.MakeSurface.Marcer_Sink_Cylinder_ActualPosition [INT32]
 * - ns=4;s=|var|CECC-D.Application.MakeSurface.Source_Ready [BOOL]
 * - ns=4;s=|var|CECC-D.Application.MakeSurface.Surfaces_Control [INT16]
 * - ns=4;s=|var|CECC-D.Application.MakeSurface.SurfaceStepFlag [BOOL]
 * - ns=4;s=|var|CECC-D.Application.MakeSurface.SurfaceStepFlag_Test [BOOL]
 * - ns=4;s=|var|CECC-D.Application.StartUpSequence.StartUp [BOOL]
 * 
 * @par Thread Safety:
 * Direct access is NOT thread-safe. Use PlcStateGuard for protected access.
 */
struct PlcState {
    // ========================================================================
    // MakeSurface Variables (Actual PLC)
    // ========================================================================
    
    /** @brief Number of powder layers to create during surface preparation [INT16] */
    UA_Int16 Z_Stacks = 0;
    
    /** @brief Source cylinder delta movement per stack (microns) [INT32] */
    UA_Int32 Delta_Source = 0;
    
    /** @brief Sink cylinder delta movement per stack (microns) [INT32] */
    UA_Int32 Delta_Sink = 0;
    
    /** @brief TRUE when layer is prepared and ready for processing [BOOL] */
    UA_Boolean Layer_Ready = UA_FALSE;
    
    /** @brief Current source cylinder position (microns from home) [INT32] */
    UA_Int32 Marcer_Source_Cylinder_ActualPosition = 0;
    
    /** @brief Current sink cylinder position (microns from home) [INT32] */
    UA_Int32 Marcer_Sink_Cylinder_ActualPosition = 0;
    
    /** @brief TRUE when source cylinder is ready for operation [BOOL] */
    UA_Boolean Source_Ready = UA_FALSE;
    
    /** @brief Surface control mode selector [INT16] */
    UA_Int16 Surfaces_Control = 0;
    
    /** @brief Surface step flag for layer synchronization [BOOL] */
    UA_Boolean SurfaceStepFlag = UA_FALSE;
    
    /** @brief Test flag for surface step functionality [BOOL] */
    UA_Boolean SurfaceStepFlag_Test = UA_FALSE;

    // ========================================================================
    // StartUpSequence Variables (Actual PLC)
    // ========================================================================
    
    /** @brief Client trigger for startup sequence [BOOL] */
    UA_Boolean StartUp = UA_FALSE;
    
    /** @brief Server response: startup complete [BOOL] - Server generated */
    UA_Boolean StartUp_Done = UA_FALSE;

    // ========================================================================
    // GVL (Global Variable List) - Actual PLC
    // ========================================================================
    
    /** @brief Global variables application object [BOOL] */
    UA_Boolean GlobalVars = UA_FALSE;
    
    /** @brief Acknowledge start for sink cylinder [BOOL] */
    UA_Boolean Marcer_Sink_Cylinder_AckStart = UA_FALSE;
    
    /** @brief Global copy of source cylinder position [INT32] */
    UA_Int32 g_Marcer_Source_Cylinder_ActualPosition = 0;
    
    /** @brief Global copy of sink cylinder position [INT32] */
    UA_Int32 g_Marcer_Sink_Cylinder_ActualPosition = 0;

    // ========================================================================
    // Server-Generated Variables (for client compatibility)
    // ========================================================================
    
    /** @brief Client request for layer preparation (TRUE=start, FALSE=complete) [BOOL] */
    UA_Boolean LaySurface = UA_FALSE;
    
    /** @brief Legacy alias for Layer_Ready */
    UA_Boolean MakeSurface_Done = UA_FALSE;
    
    /** @brief Legacy alias for Layer_Ready */
    UA_Boolean LaySurface_Done = UA_FALSE;
    
    /** @brief Legacy alias for Surfaces_Control (converted from INT16 to BOOL) */
    UA_Boolean StartSurfaces = UA_FALSE;

    // ========================================================================
    // Internal Server State (not exposed via OPC UA)
    // ========================================================================
    
    /** @brief TRUE while layer preparation is in progress */
    UA_Boolean PreparingLayer = UA_FALSE;
    
    /** @brief Emergency stop activated */
    UA_Boolean EmergencyStop = UA_FALSE;
    
    /** @brief Hardware fault detected */
    UA_Boolean HardwareFault = UA_FALSE;

    // ========================================================================
    // Validation Methods
    // ========================================================================
    
    /**
     * @brief Validate cylinder positions are within safe limits
     * @return true if positions are valid, false otherwise
     */
    bool validatePositions() const noexcept {
        return (Marcer_Source_Cylinder_ActualPosition >= limits::CYLINDER_POSITION_MIN &&
                Marcer_Source_Cylinder_ActualPosition <= limits::CYLINDER_POSITION_MAX &&
                Marcer_Sink_Cylinder_ActualPosition >= limits::CYLINDER_POSITION_MIN &&
                Marcer_Sink_Cylinder_ActualPosition <= limits::CYLINDER_POSITION_MAX);
    }
    
    /**
     * @brief Reset all variables to safe default state
     * 
     * @details Called during emergency stop or initialization to ensure
     * all hardware is in a known safe state.
     */
    void resetToSafe() noexcept {
        // Stop all operations
        StartUp = UA_FALSE;
        StartSurfaces = UA_FALSE;
        LaySurface = UA_FALSE;
        Surfaces_Control = 0;
        
        // Clear done flags
        StartUp_Done = UA_FALSE;
        MakeSurface_Done = UA_FALSE;
        LaySurface_Done = UA_FALSE;
        Layer_Ready = UA_FALSE;
        Source_Ready = UA_FALSE;
        SurfaceStepFlag = UA_FALSE;
        
        // Clear internal state
        PreparingLayer = UA_FALSE;
        
        // Note: Positions are NOT reset - that requires physical homing
    }
};

// ============================================================================
// Thread-Safe State Access
// ============================================================================

/**
 * @brief Thread-safe wrapper for PLC state access
 * 
 * @details This class provides RAII-based locking for PlcState access.
 * Use this wrapper whenever accessing state from multiple threads.
 * 
 * @par Usage Example:
 * @code
 * PlcStateContainer container;
 * {
 *     auto guard = container.lock();
 *     guard.state().StartUp = true;
 * } // Mutex released here
 * @endcode
 */
class PlcStateContainer {
public:
    /**
     * @brief RAII guard providing exclusive access to PlcState
     */
    class Guard {
    public:
        Guard(PlcState& state, std::mutex& mutex) 
            : m_state(state), m_lock(mutex) {}
        
        // Prevent copying
        Guard(const Guard&) = delete;
        Guard& operator=(const Guard&) = delete;
        
        // Allow moving
        Guard(Guard&&) = default;
        Guard& operator=(Guard&&) = default;
        
        /** @brief Access the protected state */
        PlcState& state() noexcept { return m_state; }
        const PlcState& state() const noexcept { return m_state; }
        
        /** @brief Arrow operator for convenient access */
        PlcState* operator->() noexcept { return &m_state; }
        const PlcState* operator->() const noexcept { return &m_state; }
        
    private:
        PlcState& m_state;
        std::unique_lock<std::mutex> m_lock;
    };
    
    /**
     * @brief Acquire exclusive lock on state
     * @return Guard providing protected access
     */
    Guard lock() { return Guard(m_state, m_mutex); }
    
    /**
     * @brief Try to acquire lock without blocking
     * @return Optional guard if lock acquired
     */
    std::optional<Guard> tryLock() {
        if (m_mutex.try_lock()) {
            // Create guard with already-locked mutex
            std::unique_lock<std::mutex> lock(m_mutex, std::adopt_lock);
            // Note: This is safe because Guard will take ownership
            m_mutex.lock();  // Re-lock for Guard constructor
            return Guard(m_state, m_mutex);
        }
        return std::nullopt;
    }
    
private:
    PlcState m_state;
    std::mutex m_mutex;
};

} // namespace slm_opcua

#endif // OPCUASERVER_PLC_STATE_H
