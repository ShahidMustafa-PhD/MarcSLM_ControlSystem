#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>

namespace marc {

// ============================================================================
// BuildStyle - Scan parameters from config.json
// ============================================================================
/**
 * @brief BuildStyle - Encapsulates all laser/scan parameters for a geometry type
 * 
 * Loaded from config.json buildStyles array.
 * Maps geometry type to production SLM/DMLS parameters.
 * 
 * Flow: config.json ? BuildStyleLibrary ? RTCCommandBlock ? RTC5 device
 */
struct BuildStyle {
    // Identity
    uint32_t id = 0;                       // buildStyle ID from config
    std::string name;                      // e.g. "CoreContour_Volume"
    std::string description;               // e.g. "core contour on volume"

    // Laser configuration
    uint32_t laserId = 1;                  // Laser module ID
    uint32_t laserMode = 0;                // 0=normal, 1=pulse, 2=point
    double laserPower = 0.0;             // Watts or mW (depends on hardware)
    double laserFocus = 0.1;               // Focus offset (mm)

    // Scan speeds (RTC5: typically 0-3000 mm/s or bits/ms)
    double laserSpeed = 250.0;            // Mark speed (mm/s)
    double jumpSpeed = 1500.0;             // Jump speed (mm/s)

    // Geometry parameters
    double hatchSpacing = 0.1;             // Hatch line spacing (mm)
    double layerThickness = 0.03;          // Layer thickness (mm)

    // Point sequence parameters
    double pointDistance = 0.05;           // Distance between point exposures (mm)
    uint32_t pointDelay = 1;               // Delay before point (ms)
    uint32_t pointExposureTime = 100;      // Point dwell time (ms)

    // Timing
    double jumpDelay = 1.0;                // Delay after jump before mark (ms)

    // Validation
    bool isValid() const;
    std::string debugString() const;
};

// ============================================================================
// BuildStyleLibrary - Runtime lookup table for buildStyles
// ============================================================================
/**
 * @brief BuildStyleLibrary - Maps geometry type ? BuildStyle
 * 
 * USAGE:
 *   BuildStyleLibrary lib;
 *   lib.loadFromJson(configJsonPath);
 *   BuildStyle* style = lib.getStyle(geometryTag.type);
 *   if (style) {
 *       laserPower = style->laserPower;
 *       laserSpeed = style->laserSpeed;
 *   }
 */
class BuildStyleLibrary {
public:
    BuildStyleLibrary() = default;
    ~BuildStyleLibrary() = default;

    // Load from JSON file
    bool loadFromJson(const std::string& jsonPath);

    // Query by geometry type ID
    BuildStyle* getStyle(uint32_t geometryTypeId);
    const BuildStyle* getStyle(uint32_t geometryTypeId) const;

    // Query by buildStyle ID
    BuildStyle* getStyleById(uint32_t buildStyleId);
    const BuildStyle* getStyleById(uint32_t buildStyleId) const;

    // Utility
    size_t count() const { return mStyles.size(); }
    bool isEmpty() const { return mStyles.empty(); }

    // Debug
    std::string debugString() const;

private:
    // Map: geometry type ID ? BuildStyle
    std::unordered_map<uint32_t, BuildStyle> mStyles;

    bool parseJsonArray(const std::string& jsonContent);
};

} // namespace marc
