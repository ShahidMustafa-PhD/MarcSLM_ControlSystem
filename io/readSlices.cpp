#include "readSlices.h"

#include <cstring>
#include <fstream>

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

bool readSlices::open(const std::string& path) {
    return open(std::filesystem::path(path).wstring());
}

bool readSlices::open(const std::wstring& path) {
    m_path = std::filesystem::path(path);
    m_layers.clear();

    std::ifstream is;
    if (!openIfstream(is, m_path)) return false;

    try {
        // Read header exactly as written
        readBytes(is, &m_header, sizeof(MarcHeader));
        if (!checkMagic(m_header.magic)) return false;

        // Read layers sequentially
        for (uint32_t i = 0; i < m_header.totalLayers; ++i) {
            Layer layer = readLayer(is);
            m_layers.emplace_back(std::move(layer));
        }
    } catch (...) {
        return false;
    }

    return true;
}

bool readSlices::isMarcFile(const std::string& path, std::string* error) {
    return isMarcFile(std::filesystem::path(path).wstring(), error);
}

bool readSlices::isMarcFile(const std::wstring& path, std::string* error) {
    std::ifstream is;
    if (!openIfstream(is, std::filesystem::path(path))) {
        if (error) *error = "Cannot open file";
        return false;
    }
    MarcHeader hdr{};
    is.read(reinterpret_cast<char*>(&hdr), sizeof(MarcHeader));
    if (!is) {
        if (error) *error = "File too small";
        return false;
    }
    if (!checkMagic(hdr.magic)) {
        if (error) *error = "Invalid magic";
        return false;
    }
    return true;
}

GeometryTag readSlices::readGeometryTag(std::ifstream& is) {
    GeometryTag tag{};
    readPod(is, tag.type);
    readPod(is, tag.category);
    readPod(is, tag.pointCount);
    return tag;
}

Hatch readSlices::readHatch(std::ifstream& is) {
    Hatch h{};
    h.tag = readGeometryTag(is);
    // Number of vertices encoded in tag.pointCount; 2 vertices per line
    uint32_t vertices = h.tag.pointCount;
    uint32_t lineCount = vertices / 2; // if odd, last point ignored
    h.lines.resize(lineCount);
    for (uint32_t i = 0; i < lineCount; ++i) {
        readPod(is, h.lines[i].a);
        readPod(is, h.lines[i].b);
    }
    // If vertices was odd, skip one dangling point
    if (vertices % 2 == 1) {
        Point dummy{};
        readPod(is, dummy);
    }
    return h;
}

Polyline readSlices::readPolyline(std::ifstream& is) {
    Polyline p{};
    p.tag = readGeometryTag(is);
    p.points.resize(p.tag.pointCount);
    for (uint32_t i = 0; i < p.tag.pointCount; ++i) {
        readPod(is, p.points[i]);
    }
    return p;
}

Polygon readSlices::readPolygon(std::ifstream& is) {
    Polygon p{};
    p.tag = readGeometryTag(is);
    p.points.resize(p.tag.pointCount);
    for (uint32_t i = 0; i < p.tag.pointCount; ++i) {
        readPod(is, p.points[i]);
    }
    return p;
}

Circle readSlices::readCircle(std::ifstream& is) {
    Circle c{};
    c.tag = readGeometryTag(is);
    readPod(is, c.center);
    readPod(is, c.radius);
    return c;
}

Layer readSlices::readLayer(std::ifstream& is) {
    Layer L{};
    // Writer serializes: layerNumber, layerHeight, hatchCount, [hatches], polylineCount, [polylines], polygonCount, [polygons]
    readPod(is, L.layerNumber);
    readPod(is, L.layerHeight);
    L.layerThickness = 0.0f; // not serialized in this version

    // Hatches
    uint32_t hatchCount = 0;
    readPod(is, hatchCount);
    L.hatches.reserve(hatchCount);
    for (uint32_t i = 0; i < hatchCount; ++i) {
        L.hatches.emplace_back(readHatch(is));
    }

    // Polylines
    uint32_t polylineCount = 0;
    readPod(is, polylineCount);
    L.polylines.reserve(polylineCount);
    for (uint32_t i = 0; i < polylineCount; ++i) {
        L.polylines.emplace_back(readPolyline(is));
    }

    // Polygons
    uint32_t polygonCount = 0;
    readPod(is, polygonCount);
    L.polygons.reserve(polygonCount);
    for (uint32_t i = 0; i < polygonCount; ++i) {
        L.polygons.emplace_back(readPolygon(is));
    }

    // No circles serialized by writer
    L.support_circles.clear();

    return L;
}

} // namespace marc
