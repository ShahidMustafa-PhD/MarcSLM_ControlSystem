#include "buildstyle.h"
#include <sstream>
#include <fstream>
#include <stdexcept>
#include <nlohmann/json.hpp>

namespace marc {

using json = nlohmann::json;

// ============================================================================
// BuildStyle Implementation
// ============================================================================

bool BuildStyle::isValid() const {
    // ========== VALIDATION RULES ==========
    //
    // REQUIRED:
    // - id > 0 (valid BuildStyle ID)
    // - name not empty (human-readable identifier)
    // - laserSpeed > 0.0 (mark speed must be positive)
    //
    // OPTIONAL:
    // - laserPower >= 0.0 (allows 0.0 for test mode / external control)
    //   * 0.0 can mean: laser OFF (pilot marking)
    //   * 0.0 can mean: power controlled by external DA converter
    //   * 0.0 can mean: power set via RTC5 write_da_1/write_da_2 commands
    //
    return id > 0 && !name.empty() && laserPower >= 0.0 && laserSpeed > 0.0;
}

std::string BuildStyle::validationError() const {
    // ========== DETAILED VALIDATION ERROR REPORTING ==========
    // Returns specific reason why BuildStyle is invalid
    
    if (id == 0) {
        return "id must be > 0 (got " + std::to_string(id) + ")";
    }
    if (name.empty()) {
        return "name cannot be empty";
    }
    if (laserPower < 0.0) {
        return "laserPower must be >= 0.0 (got " + std::to_string(laserPower) + ")";
    }
    if (laserSpeed <= 0.0) {
        return "laserSpeed must be > 0.0 (got " + std::to_string(laserSpeed) + ")";
    }
    return "valid";
}

std::string BuildStyle::debugString() const {
    std::ostringstream ss;
    ss << "BuildStyle{id=" << id
       << ", name=" << name
       << ", laserPower=" << laserPower
       << ", laserSpeed=" << laserSpeed
       << ", jumpSpeed=" << jumpSpeed
       << ", mode=" << laserMode << "}";
    return ss.str();
}

// ============================================================================
// BuildStyleLibrary Implementation
// ============================================================================

bool BuildStyleLibrary::loadFromJson(const std::string& jsonPath) {
    try {
        std::ifstream file(jsonPath);
        if (!file.is_open()) {
            throw std::runtime_error("Cannot open JSON file: " + jsonPath);
        }

        std::string content((std::istreambuf_iterator<char>(file)),
                            std::istreambuf_iterator<char>());
        file.close();

        return parseJsonArray(content);
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("BuildStyleLibrary::loadFromJson failed: ") + e.what());
    }
}

bool BuildStyleLibrary::parseJsonArray(const std::string& jsonContent) {
    try {
        auto doc = json::parse(jsonContent);

        // Expect root object with "buildStyles" array
        if (!doc.contains("buildStyles")) {
            throw std::runtime_error("No 'buildStyles' array in JSON");
        }

        auto buildStyles = doc["buildStyles"];
        if (!buildStyles.is_array()) {
            throw std::runtime_error("'buildStyles' is not an array");
        }

        mStyles.clear();

        for (const auto& styleObj : buildStyles) {
            BuildStyle style;

            // Required fields
            if (!styleObj.contains("id")) {
                throw std::runtime_error("buildStyle missing 'id' field");
            }
            style.id = styleObj["id"].get<uint32_t>();

            if (!styleObj.contains("name")) {
                throw std::runtime_error("buildStyle missing 'name' field");
            }
            style.name = styleObj["name"].get<std::string>();

            // Optional description
            if (styleObj.contains("description")) {
                style.description = styleObj["description"].get<std::string>();
            }

            // Laser config (with defaults)
            if (styleObj.contains("laserId")) {
                style.laserId = styleObj["laserId"].get<uint32_t>();
            }
            if (styleObj.contains("laserMode")) {
                style.laserMode = styleObj["laserMode"].get<uint32_t>();
            }
            if (styleObj.contains("laserPower")) {
                style.laserPower = styleObj["laserPower"].get<double>();
            }
            if (styleObj.contains("laserFocus")) {
                style.laserFocus = styleObj["laserFocus"].get<double>();
            }

            // Speeds
            if (styleObj.contains("laserSpeed")) {
                style.laserSpeed = styleObj["laserSpeed"].get<double>();
            }
            if (styleObj.contains("jumpSpeed")) {
                style.jumpSpeed = styleObj["jumpSpeed"].get<double>();
            }

            // Geometry and point parameters
            if (styleObj.contains("hatchSpacing")) {
                style.hatchSpacing = styleObj["hatchSpacing"].get<double>();
            }
            if (styleObj.contains("layerThickness")) {
                style.layerThickness = styleObj["layerThickness"].get<double>();
            }
            if (styleObj.contains("pointDistance")) {
                style.pointDistance = styleObj["pointDistance"].get<double>();
            }
            if (styleObj.contains("pointDelay")) {
                style.pointDelay = styleObj["pointDelay"].get<uint32_t>();
            }
            if (styleObj.contains("pointExposureTime")) {
                style.pointExposureTime = styleObj["pointExposureTime"].get<uint32_t>();
            }

            // Timing
            if (styleObj.contains("jumpDelay")) {
                style.jumpDelay = styleObj["jumpDelay"].get<double>();
            }

            // Validate and insert
            if (!style.isValid()) {
                std::string reason = style.validationError();
                throw std::runtime_error(
                    "Invalid buildStyle (id=" + std::to_string(style.id) + 
                    ", name='" + style.name + "'): " + reason);
            }

            mStyles[style.id] = style;
        }

        return true;
    } catch (const json::exception& e) {
        throw std::runtime_error(std::string("JSON parse error: ") + e.what());
    }
}

BuildStyle* BuildStyleLibrary::getStyle(uint32_t geometryTypeId) {
    auto it = mStyles.find(geometryTypeId);
    if (it != mStyles.end()) {
        return &it->second;
    }
    return nullptr;
}

const BuildStyle* BuildStyleLibrary::getStyle(uint32_t geometryTypeId) const {
    auto it = mStyles.find(geometryTypeId);
    if (it != mStyles.end()) {
        return &it->second;
    }
    return nullptr;
}

BuildStyle* BuildStyleLibrary::getStyleById(uint32_t buildStyleId) {
    for (auto& pair : mStyles) {
        if (pair.second.id == buildStyleId) {
            return &pair.second;
        }
    }
    return nullptr;
}

const BuildStyle* BuildStyleLibrary::getStyleById(uint32_t buildStyleId) const {
    for (const auto& pair : mStyles) {
        if (pair.second.id == buildStyleId) {
            return &pair.second;
        }
    }
    return nullptr;
}

std::string BuildStyleLibrary::debugString() const {
    std::ostringstream ss;
    ss << "BuildStyleLibrary{count=" << mStyles.size() << ", styles=[";
    bool first = true;
    for (const auto& pair : mStyles) {
        if (!first) ss << ", ";
        ss << pair.second.debugString();
        first = false;
    }
    ss << "]}";
    return ss.str();
}

} // namespace marc
