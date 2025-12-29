#include "streamingmarcreader.h"
#include <filesystem>

namespace marc {

static bool checkMagic(const char magic[4]) {
    return magic[0] == 'M' && magic[1] == 'A' && magic[2] == 'R' && magic[3] == 'C';
}

static bool openIfstream(std::ifstream& is, const std::filesystem::path& p) {
#if defined(_WIN32)
    is.open(p.wstring(), std::ios::binary);
#else
    is.open(p.string(), std::ios::binary);
#endif
    return static_cast<bool>(is);
}

// ============================================================================
// Constructor / Destructor
// ============================================================================

StreamingMarcReader::StreamingMarcReader(const std::wstring& path) {
    openFile(path);
    readHeader();
}

StreamingMarcReader::StreamingMarcReader(const std::string& path) {
    openFile(std::filesystem::path(path).wstring());
    readHeader();
}

StreamingMarcReader::~StreamingMarcReader() {
    if (m_ifstream.is_open()) {
        m_ifstream.close();
    }
}

// ============================================================================
// File I/O
// ============================================================================

void StreamingMarcReader::openFile(const std::wstring& path) {
    if (!openIfstream(m_ifstream, std::filesystem::path(path))) {
        throw std::runtime_error("Failed to open MARC file");
    }
}

void StreamingMarcReader::readHeader() {
    try {
        readBytes(&m_header, sizeof(MarcHeader));
        if (!checkMagic(m_header.magic)) {
            throw std::runtime_error("Invalid MARC magic number");
        }
    } catch (const std::exception& e) {
        throw std::runtime_error(std::string("Failed to read MARC header: ") + e.what());
    }
}

// ============================================================================
// Layer Reading (Streaming)
// ============================================================================

Layer StreamingMarcReader::readNextLayer() {
    if (!hasNextLayer()) {
        throw std::runtime_error("No more layers to read");
    }
    ++m_currentLayerIndex;
    return readLayer();
}

Layer StreamingMarcReader::readLayer() {
    Layer L{};
    try {
        readPod(L.layerNumber);
        readPod(L.layerHeight);
        L.layerThickness = 0.0f; // not serialized

        // Hatches
        uint32_t hatchCount = 0;
        readPod(hatchCount);
        L.hatches.reserve(hatchCount);
        for (uint32_t i = 0; i < hatchCount; ++i) {
            L.hatches.emplace_back(readHatch());
        }

        // Polylines
        uint32_t polylineCount = 0;
        readPod(polylineCount);
        L.polylines.reserve(polylineCount);
        for (uint32_t i = 0; i < polylineCount; ++i) {
            L.polylines.emplace_back(readPolyline());
        }

        // Polygons
        uint32_t polygonCount = 0;
        readPod(polygonCount);
        L.polygons.reserve(polygonCount);
        for (uint32_t i = 0; i < polygonCount; ++i) {
            L.polygons.emplace_back(readPolygon());
        }

        // No circles
        L.support_circles.clear();

        return L;
    } catch (const std::exception& e) {
        throw std::runtime_error(
            std::string("Failed to read layer ") + std::to_string(m_currentLayerIndex) + 
            std::string(": ") + e.what()
        );
    }
}

// ============================================================================
// Geometry Reading
// ============================================================================

GeometryTag StreamingMarcReader::readGeometryTag() {
    GeometryTag tag{};
    readPod(tag.type);
    readPod(tag.category);
    readPod(tag.pointCount);
    return tag;
}

Hatch StreamingMarcReader::readHatch() {
    Hatch h{};
    h.tag = readGeometryTag();
    uint32_t vertices = h.tag.pointCount;
    uint32_t lineCount = vertices / 2;
    h.lines.resize(lineCount);
    for (uint32_t i = 0; i < lineCount; ++i) {
        readPod(h.lines[i].a);
        readPod(h.lines[i].b);
    }
    if (vertices % 2 == 1) {
        Point dummy{};
        readPod(dummy);
    }
    return h;
}

Polyline StreamingMarcReader::readPolyline() {
    Polyline p{};
    p.tag = readGeometryTag();
    p.points.resize(p.tag.pointCount);
    for (uint32_t i = 0; i < p.tag.pointCount; ++i) {
        readPod(p.points[i]);
    }
    return p;
}

Polygon StreamingMarcReader::readPolygon() {
    Polygon p{};
    p.tag = readGeometryTag();
    p.points.resize(p.tag.pointCount);
    for (uint32_t i = 0; i < p.tag.pointCount; ++i) {
        readPod(p.points[i]);
    }
    return p;
}

Circle StreamingMarcReader::readCircle() {
    Circle c{};
    c.tag = readGeometryTag();
    readPod(c.center);
    readPod(c.radius);
    return c;
}

} // namespace marc
