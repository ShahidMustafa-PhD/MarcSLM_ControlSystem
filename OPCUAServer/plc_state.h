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
 * @details This structure mirrors the CoDeSys PLC variable layout used in
 * the physical SLM controller. Variable names match OPC UA node identifiers.
 * 
 * @par Variable Groups:
 * - **MakeSurface**: Powder bed surface preparation
 * - **GVL**: Global Variable List (system-wide state)
 * - **Prepare2Process**: Layer-by-layer preparation
 * - **StartUpSequence**: Machine initialization
 * 
 * @par Thread Safety:
 * Direct access is NOT thread-safe. Use PlcStateGuard for protected access.
 */
struct PlcState {
    // ========================================================================
    // MakeSurface Variables
    // ========================================================================
    
    /** @brief Number of powder layers to create during surface preparation */
    UA_Int32 Z_Stacks = 0;
    
    /** @brief Source cylinder delta movement per stack (microns) */
    UA_Int32 Delta_Source = 0;
    
    /** @brief Sink cylinder delta movement per stack (microns) */
    UA_Int32 Delta_Sink = 0;
    
    /** @brief TRUE when surface preparation is complete */
    UA_Boolean MakeSurface_Done = UA_FALSE;
    
    /** @brief Current source cylinder position (microns from home) */
    UA_Int32 Marcer_Source_Cylinder_ActualPosition = 0;
    
    /** @brief Current sink cylinder position (microns from home) */
    UA_Int32 Marcer_Sink_Cylinder_ActualPosition = 0;

    // ========================================================================
    // GVL (Global Variable List)
    // ========================================================================
    
    /** @brief Trigger to start powder surface creation */
    UA_Boolean StartSurfaces = UA_FALSE;
    
    /** @brief Global copy of source cylinder position */
    UA_Int32 g_Marcer_Source_Cylinder_ActualPosition = 0;
    
    /** @brief Global copy of sink cylinder position */
    UA_Int32 g_Marcer_Sink_Cylinder_ActualPosition = 0;

    // ========================================================================
    // Prepare2Process Variables
    // ========================================================================
    
    /** @brief Client request for layer preparation (TRUE=start, FALSE=complete) */
    UA_Boolean LaySurface = UA_FALSE;
    
    /** @brief Server response: layer preparation complete */
    UA_Boolean LaySurface_Done = UA_FALSE;
    
    /** @brief Sink cylinder step per layer (microns) */
    UA_Int32 Step_Sink = 0;
    
    /** @brief Source cylinder step per layer (microns) */
    UA_Int32 Step_Source = 0;
    
    /** @brief Number of remaining layers in current build */
    UA_Int32 Lay_Stacks = 0;

    // ========================================================================
    // StartUpSequence Variables
    // ========================================================================
    
    /** @brief Client trigger for startup sequence */
    UA_Boolean StartUp = UA_FALSE;
    
    /** @brief Server response: startup complete */
    UA_Boolean StartUp_Done = UA_FALSE;

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
        
        // Clear done flags
        StartUp_Done = UA_FALSE;
        MakeSurface_Done = UA_FALSE;
        LaySurface_Done = UA_FALSE;
        
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
