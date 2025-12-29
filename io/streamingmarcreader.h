#pragma once

#include "readSlices.h"
#include <fstream>
#include <memory>
#include <stdexcept>

namespace marc {

/**
 * @brief StreamingMarcReader - reads .marc file layer-by-layer without loading entire file into memory
 * 
 * DESIGN:
 * - Maintains an open file handle throughout the process
 * - Reads layers sequentially on demand (one at a time)
 * - Does NOT preload all layers into a vector
 * - Thread-safe: meant to be called from a single producer thread
 * 
 * USAGE:
 *   StreamingMarcReader reader(marcFilePath);
 *   while (reader.hasNextLayer()) {
 *       Layer layer = reader.readNextLayer();
 *       // process layer
 *   }
 */
class StreamingMarcReader {
public:
    explicit StreamingMarcReader(const std::wstring& path);
    explicit StreamingMarcReader(const std::string& path);
    ~StreamingMarcReader();

    // non-copyable
    StreamingMarcReader(const StreamingMarcReader&) = delete;
    StreamingMarcReader& operator=(const StreamingMarcReader&) = delete;

    // Accessors
    const MarcHeader& header() const { return m_header; }
    bool hasNextLayer() const { return m_currentLayerIndex < m_header.totalLayers; }
    uint32_t totalLayers() const { return m_header.totalLayers; }
    uint32_t currentLayerIndex() const { return m_currentLayerIndex; }

    // Read next layer from file (sequential only)
    Layer readNextLayer();

private:
    void openFile(const std::wstring& path);
    void readHeader();

    std::ifstream m_ifstream;
    MarcHeader m_header{};
    uint32_t m_currentLayerIndex{0};

    // Low-level read helpers
    template <typename T>
    void readPod(T& out) {
        m_ifstream.read(reinterpret_cast<char*>(&out), sizeof(T));
        if (!m_ifstream) {
            throw std::runtime_error("Unexpected EOF while reading POD");
        }
    }

    void readBytes(void* dst, std::size_t len) {
        m_ifstream.read(reinterpret_cast<char*>(dst), static_cast<std::streamsize>(len));
        if (!m_ifstream) {
            throw std::runtime_error("Unexpected EOF while reading bytes");
        }
    }

    // Layer reading helpers
    GeometryTag readGeometryTag();
    Hatch readHatch();
    Polyline readPolyline();
    Polygon readPolygon();
    Circle readCircle();
    Layer readLayer();
};

} // namespace marc
