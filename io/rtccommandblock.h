#pragma once

#include <cstdint>
#include <vector>
#include <memory>

namespace marc {

// Forward declaration
class BuildStyle;

// ============================================================================
// RTCCommandBlock - Scanner command batch with laser parameters
// ============================================================================
/**
 * @brief RTCCommandBlock - One layer's worth of RTC5 commands + laser parameters
 * 
 * Flow:
 *   1. Producer reads MARC layer geometry (hatches, polylines, polygons)
 *   2. For each geometry segment, resolve BuildStyle from config.json
 *   3. Convert geometry points to RTC5 coordinates (mm ? bits)
 *   4. Add jump/mark commands with embedded laser parameters
 *   5. Consumer executes commands on RTC5 device, setting power/speed per segment
 * 
 * Laser parameter switching:
 *   - Before each segment with different BuildStyle, apply setLaserPower() and setLaserSpeed()
 *   - RTC5 caches speed until next change (deterministic parameter flow)
 *   - Power applied via analog output or direct register write
 */
struct RTCCommandBlock {
    // Layer metadata
    uint32_t layerNumber = 0;
    float layerHeight = 0.0f;
    float layerThickness = 0.0f;

    // Statistics
    size_t hatchCount = 0;
    size_t polylineCount = 0;
    size_t polygonCount = 0;

    // RTC5 scan commands (already converted to bits)
    struct Command {
        enum Type { Jump, Mark, SetPower, SetSpeed, SetFocus, Delay } type;
        long x = 0, y = 0;                      // For Jump/Mark (RTC5 bits)
        double paramValue = 0.0;                // For SetPower/Speed/Focus (physical units)
        uint32_t delayMs = 0;                   // For Delay (milliseconds)
    };

    std::vector<Command> commands;

    // ========== NEW: Laser parameters per command segment ==========
    /**
     * @brief ParameterSegment - Group of commands using the same laser parameters
     * 
     * REASON: Same layer may have multiple geometry types (hatches, contours, etc.)
     * Each type uses different laserPower, laserSpeed from config.json
     * 
     * FLOW:
     *   - Producer groups commands by BuildStyle ID
     *   - At segment boundary, inserts SetPower / SetSpeed commands
     *   - Consumer reads parameter commands and applies to RTC5 before executing marks
     */
    struct ParameterSegment {
        size_t startCmd = 0;                    // First command index in this segment
        size_t endCmd = 0;                      // Last command index (inclusive)

        uint32_t buildStyleId = 0;              // BuildStyle ID from config.json
        double laserPower = 0.0;              // Watts or mW
        double laserSpeed = 250.0;             // mm/s (mark speed)
        double jumpSpeed = 1500.0;              // mm/s (jump speed)
        uint32_t laserMode = 0;                 // 0=normal, 1=pulse, 2=point
        double laserFocus = 0.1;                // Focus offset (mm)
    };

    std::vector<ParameterSegment> parameterSegments;

    // Helper to find segment for a given command index
    const ParameterSegment* getSegmentFor(size_t cmdIndex) const;

    // Helper to add a segment
    void addParameterSegment(uint32_t buildStyleId,
                             double laserPower, double laserSpeed, double jumpSpeed,
                             uint32_t laserMode, double laserFocus);
};

} // namespace marc
