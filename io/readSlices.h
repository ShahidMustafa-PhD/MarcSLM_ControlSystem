#ifndef MARC_READSLICES_H
#define MARC_READSLICES_H

#include <cstdint>
#include <string>
#include <vector>
#include <fstream>
#include <stdexcept>
#include <ctime>
#include <filesystem>

namespace marc {

// -------------------------------
// Header
// -------------------------------
struct MarcHeader {
    char magic[4];                 // "MARC"
    uint32_t version;              // Version number
    uint32_t totalLayers;          // Total number of layers
    uint64_t indexTableOffset;     // File offset where index table starts
    uint64_t timestamp;            // File creation timestamp
    char printerId[32];            // Optional printer identification
    char reserved[100];            // Reserved for future use
};

// -------------------------------
// Geometry Structures
// -------------------------------
struct Point {
    float x;
    float y;
};

struct Line {
    Point a;
    Point b;
};

struct GeometryTag {
    uint32_t type;       // Subtype (0–15)
    uint32_t category;   // 1=hatch, 2=polyline, 3=polygon, 4=point (circle)
    uint32_t pointCount; // Total number of points
};

struct Circle {
    GeometryTag tag;  // Geometry metadata
    Point center;
    float radius;
};

struct Hatch {
    GeometryTag tag;             // Geometry metadata
    std::vector<Line> lines;     // Vector of line segments
};

struct Polyline {
    GeometryTag tag;             // Geometry metadata
    std::vector<Point> points;   // Vector of connected points
};

struct Polygon {
    GeometryTag tag;             // Geometry metadata
    std::vector<Point> points;   // Vector of polygon points
};

// -------------------------------
// Layer Data Block
// -------------------------------
struct Layer {
    uint32_t layerNumber;                 // Unique layer number
    float layerHeight;                    // Accumulated height or Z
    float layerThickness;                 // Layer thickness
    std::vector<Hatch> hatches;           // Collection of hatches
    std::vector<Polyline> polylines;      // Collection of polylines
    std::vector<Polygon> polygons;        // Collection of polygons
    std::vector<Circle> support_circles;  // Collection of support circles
};

// Reader class for .marc files
class readSlices {
public:
    readSlices() = default;

    // Open and parse the file fully (UTF-8 or wide paths supported)
    bool open(const std::string& path);
    bool open(const std::wstring& path);

    // Accessors
    const MarcHeader& header() const { return m_header; }
    const std::vector<Layer>& layers() const { return m_layers; }

    // Utility
    static bool isMarcFile(const std::string& path, std::string* error = nullptr);
    static bool isMarcFile(const std::wstring& path, std::string* error = nullptr);

private:
    // Low-level read helpers (throw on failure)
    template <typename T>
    static void readPod(std::ifstream& is, T& out) {
        is.read(reinterpret_cast<char*>(&out), sizeof(T));
        if (!is) throw std::runtime_error("Unexpected EOF while reading POD");
    }

    static void readBytes(std::ifstream& is, void* dst, std::size_t len) {
        is.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(len));
        if (!is) throw std::runtime_error("Unexpected EOF while reading bytes");
    }

    static GeometryTag readGeometryTag(std::ifstream& is);

    Hatch readHatch(std::ifstream& is);
    Polyline readPolyline(std::ifstream& is);
    Polygon readPolygon(std::ifstream& is);
    Circle readCircle(std::ifstream& is);
    Layer readLayer(std::ifstream& is);

    std::filesystem::path m_path;
    MarcHeader m_header{};
    std::vector<Layer> m_layers;
};

} // namespace marc

#endif // MARC_READSLICES_H
