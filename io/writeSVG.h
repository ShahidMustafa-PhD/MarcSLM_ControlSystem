#ifndef MARC_WRITESVG_H
#define MARC_WRITESVG_H

#include <string>
#include <vector>
#include <fstream>
#include <filesystem>
#include "readSlices.h" // For Layer, Hatch, Polyline, Polygon, Circle and Point

namespace marc {

class writeSVG {
public:
    // Configure output canvas and scaling (physical units in mm)
    struct Options {
        float mmWidth = 200.0f;    // canvas width in millimeters
        float mmHeight = 200.0f;   // canvas height in millimeters
        float scale = 0.2f;        // pixels per mm (base)
        float zoom = 1.0f;         // zoom multiplier
        float offsetX = 0.0f;      // world offset in mm
        float offsetY = 0.0f;      // world offset in mm
        bool invertY = true;       // SVG Y grows down; invert for Cartesian
    };

    explicit writeSVG(Options opt = Options{}) : m_opt(opt) {}

    // Writes a single layer to an SVG file path
    bool writeLayer(const Layer& layer, const std::string& filePath) const;

    // Writes all layers into a directory `svgOutput`, creating it if needed
    bool writeAll(const std::vector<Layer>& layers, const std::string& outDir = "svgOutput") const;

private:
    static std::string svgHeader(int w, int h);
    static std::string svgFooter();

    // Helpers to emit geometry
    void emitHatch(std::ofstream& os, const Hatch& h) const;
    void emitPolyline(std::ofstream& os, const Polyline& p) const;
    void emitPolygon(std::ofstream& os, const Polygon& p) const;
    void emitCircle(std::ofstream& os, const Circle& c) const;

    // Coordinate transform helpers (use direct computations to avoid dependency issues)
    inline double baseScale() const { return static_cast<double>(m_opt.scale); }
    inline double zoomFactor() const { return static_cast<double>(m_opt.zoom); }
    inline double scalePxPerMm() const { return baseScale() * zoomFactor(); }
    inline double canvasWidthPx() const { return static_cast<double>(m_opt.mmWidth) * scalePxPerMm(); }
    inline double canvasHeightPx() const { return static_cast<double>(m_opt.mmHeight) * scalePxPerMm(); }
    inline double tx(double x) const { return (static_cast<double>(x) + static_cast<double>(m_opt.offsetX)) * scalePxPerMm(); }
    inline double ty(double y) const {
        double mapped = (static_cast<double>(y) + static_cast<double>(m_opt.offsetY)) * scalePxPerMm();
        return m_opt.invertY ? (canvasHeightPx() - mapped) : mapped;
    }

    Options m_opt;
};

} // namespace marc

#endif // MARC_WRITESVG_H
