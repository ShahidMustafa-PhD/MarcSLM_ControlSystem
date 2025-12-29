#include "writeSVG.h"
#include <sstream>
#include <iomanip>
#include <cmath>

namespace marc {

std::string writeSVG::svgHeader(int w, int h) {
    std::ostringstream ss;
    ss << "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n";
    ss << "<svg xmlns=\"http://www.w3.org/2000/svg\" version=\"1.1\" width=\"" << w << "\" height=\"" << h << "\">\n";
    ss << "  <g fill=\"none\" stroke-linecap=\"round\" stroke-linejoin=\"round\">\n";
    return ss.str();
}

std::string writeSVG::svgFooter() {
    return std::string("  </g>\n</svg>\n");
}

bool writeSVG::writeLayer(const Layer& layer, const std::string& filePath) const {
    std::ofstream os(filePath, std::ios::out | std::ios::trunc);
    if (!os) return false;

    // Derived values from options
    double scale = this->scalePxPerMm();
    double canvasW_px = this->canvasWidthPx();
    double canvasH_px = this->canvasHeightPx();
    int canvasW = static_cast<int>(std::lround(canvasW_px));
    int canvasH = static_cast<int>(std::lround(canvasH_px));

    os << svgHeader(canvasW, canvasH);

    // Metadata text
    os << "  <text x=\"10\" y=\"20\" font-size=\"24\" fill=\"#555\">";
    os << "Layer " << layer.layerNumber << " Z=" << std::fixed << std::setprecision(3) << layer.layerHeight << " mm";
    os << "</text>\n";

    // Compute bounding box in world units
    bool hasGeom = false;
    double minX = 0, minY = 0, maxX = 0, maxY = 0;
    auto includePoint = [&](double x, double y){
        if (!hasGeom) { minX = maxX = x; minY = maxY = y; hasGeom = true; }
        else { if (x < minX) minX = x; if (x > maxX) maxX = x; if (y < minY) minY = y; if (y > maxY) maxY = y; }
    };

    for (const auto& h : layer.hatches) {
        for (const auto& ln : h.lines) {
            includePoint(ln.a.x, ln.a.y);
            includePoint(ln.b.x, ln.b.y);
        }
    }
    for (const auto& p : layer.polylines) {
        for (const auto& pt : p.points) includePoint(pt.x, pt.y);
    }
    for (const auto& p : layer.polygons) {
        for (const auto& pt : p.points) includePoint(pt.x, pt.y);
    }
    for (const auto& c : layer.support_circles) {
        includePoint(c.center.x - c.radius, c.center.y - c.radius);
        includePoint(c.center.x + c.radius, c.center.y + c.radius);
    }

    // Compute translation (pixels) to center geometry on canvas
    double dx = 0.0, dy = 0.0;
    if (hasGeom) {
        double centerX = (minX + maxX) / 2.0;
        double centerY = (minY + maxY) / 2.0;
        double centerPx = this->tx(centerX);
        double centerPy = this->ty(centerY);
        dx = (this->canvasWidthPx() / 2.0) - centerPx;
        dy = (this->canvasHeightPx() / 2.0) - centerPy;
    }

    // Emit transformed group
    {
        std::ostringstream tr; tr << std::fixed << std::setprecision(3) << dx << "," << dy;
        os << "  <g transform=\"translate(" << tr.str() << ")\">\n";

        // Hatches
        os << "    <g stroke=\"#2E7D32\" stroke-width=\"0.4\">\n";
        for (const auto& h : layer.hatches) {
            for (const auto& ln : h.lines) {
                os << "      <line x1=\"" << std::fixed << std::setprecision(3) << this->tx(ln.a.x)
                   << "\" y1=\"" << this->ty(ln.a.y)
                   << "\" x2=\"" << this->tx(ln.b.x)
                   << "\" y2=\"" << this->ty(ln.b.y) << "\"/>\n";
            }
        }
        os << "    </g>\n";

        // Polylines 
        os << "    <g stroke=\"#1976D2\" stroke-width=\"0.4\">\n";
        for (const auto& p : layer.polylines) {
            if (p.points.empty()) continue;
            os << "      <polyline points=\"";
            for (std::size_t i = 0; i < p.points.size(); ++i) {
                const auto& pt = p.points[i];
                os << std::fixed << std::setprecision(3) << this->tx(pt.x) << "," << this->ty(pt.y);
                if (i + 1 < p.points.size()) os << " ";
            }
            os << "\" fill=\"none\"/>\n";
        }
        os << "    </g>\n";

        // Polygons
        os << "    <g stroke=\"#C62828\" stroke-width=\"0.4\" fill=\"none\">\n";
        for (const auto& p : layer.polygons) {
            if (p.points.empty()) continue;
            os << "      <polygon points=\"";
            for (std::size_t i = 0; i < p.points.size(); ++i) {
                const auto& pt = p.points[i];
                os << std::fixed << std::setprecision(3) << this->tx(pt.x) << "," << this->ty(pt.y);
                if (i + 1 < p.points.size()) os << " ";
            }
            os << "\"/>\n";
        }
        os << "    </g>\n";

        // Circles
        os << "    <g stroke=\"#EF6C00\" stroke-width=\"0.4\" fill=\"none\">\n";
        for (const auto& c : layer.support_circles) {
            os << "      <circle cx=\"" << std::fixed << std::setprecision(3) << this->tx(c.center.x)
               << "\" cy=\"" << this->ty(c.center.y)
               << "\" r=\"" << (static_cast<double>(c.radius) * this->scalePxPerMm()) << "\"/>\n";
        }
        os << "    </g>\n";

        os << "  </g>\n"; // close transform group
    }

    // Draw center guide lines (100 mm each)
    double lengthPx = 100.0 * this->scalePxPerMm();
    double halfLen = lengthPx / 2.0;
    double cx = this->canvasWidthPx() / 2.0;
    double cy = this->canvasHeightPx() / 2.0;

    os << "  <g stroke=\"#000000\" stroke-width=\"0.4\">\n";
    os << "    <line x1=\"" << std::fixed << std::setprecision(3) << (cx - halfLen)
       << "\" y1=\"" << cy
       << "\" x2=\"" << (cx + halfLen)
       << "\" y2=\"" << cy << "\"/>\n";
    os << "  </g>\n";

    os << "  <g stroke=\"#000000\" stroke-width=\"0.4\">\n";
    os << "    <line x1=\"" << cx
       << "\" y1=\"" << (cy - halfLen)
       << "\" x2=\"" << cx
       << "\" y2=\"" << (cy + halfLen) << "\"/>\n";
    os << "  </g>\n";

    // Add red reference circle of 200mm diameter (100mm radius) at canvas center
    os << "  <circle cx=\"" << std::fixed << std::setprecision(3) << cx
       << "\" cy=\"" << cy
       << "\" r=\"" << (100.0 * this->scalePxPerMm())
       << "\" stroke=\"red\" stroke-width=\"1\" fill=\"none\"/>\n";

    os << svgFooter();
    return true;
}

bool writeSVG::writeAll(const std::vector<Layer>& layers, const std::string& outDir) const {
    std::error_code ec;
    std::filesystem::create_directories(outDir, ec);
    if (ec) return false;

    for (const auto& layer : layers) {
        std::ostringstream name;
        name << outDir << "/layer_" << std::setw(6) << std::setfill('0') << layer.layerNumber << ".svg";
        if (!writeLayer(layer, name.str())) return false;
    }
    return true;
}

// Legacy emit helpers
void writeSVG::emitHatch(std::ofstream& os, const Hatch& h) const {
    double scale = this->scalePxPerMm();
    double canvasH = this->canvasHeightPx();
    for (const auto& ln : h.lines) {
        double x1 = (static_cast<double>(ln.a.x) + static_cast<double>(m_opt.offsetX)) * scale;
        double y1 = (static_cast<double>(ln.a.y) + static_cast<double>(m_opt.offsetY)) * scale;
        double x2 = (static_cast<double>(ln.b.x) + static_cast<double>(m_opt.offsetX)) * scale;
        double y2 = (static_cast<double>(ln.b.y) + static_cast<double>(m_opt.offsetY)) * scale;
        if (m_opt.invertY) { y1 = canvasH - y1; y2 = canvasH - y2; }
        os << "      <line x1=\"" << x1 << "\" y1=\"" << y1 << "\" x2=\"" << x2 << "\" y2=\"" << y2 << "\"/>\n";
    }
}

void writeSVG::emitPolyline(std::ofstream& os, const Polyline& p) const {
    if (p.points.empty()) return;
    double scale = this->scalePxPerMm();
    double canvasH = this->canvasHeightPx();
    os << "      <polyline points=\"";
    for (std::size_t i = 0; i < p.points.size(); ++i) {
        const auto& pt = p.points[i];
        double x = (static_cast<double>(pt.x) + static_cast<double>(m_opt.offsetX)) * scale;
        double y = (static_cast<double>(pt.y) + static_cast<double>(m_opt.offsetY)) * scale;
        if (m_opt.invertY) y = canvasH - y;
        os << x << "," << y;
        if (i + 1 < p.points.size()) os << " ";
    }
    os << "\" fill=\"none\"/>\n";
}

void writeSVG::emitPolygon(std::ofstream& os, const Polygon& p) const {
    if (p.points.empty()) return;
    double scale = this->scalePxPerMm();
    double canvasH = this->canvasHeightPx();
    os << "      <polygon points=\"";
    for (std::size_t i = 0; i < p.points.size(); ++i) {
        const auto& pt = p.points[i];
        double x = (static_cast<double>(pt.x) + static_cast<double>(m_opt.offsetX)) * scale;
        double y = (static_cast<double>(pt.y) + static_cast<double>(m_opt.offsetY)) * scale;
        if (m_opt.invertY) y = canvasH - y;
        os << x << "," << y;
        if (i + 1 < p.points.size()) os << " ";
    }
    os << "\"/>\n";
}

void writeSVG::emitCircle(std::ofstream& os, const Circle& c) const {
    double scale = this->scalePxPerMm();
    double canvasH = this->canvasHeightPx();
    double cx = (static_cast<double>(c.center.x) + static_cast<double>(m_opt.offsetX)) * scale;
    double cy = (static_cast<double>(c.center.y) + static_cast<double>(m_opt.offsetY)) * scale;
    if (m_opt.invertY) cy = canvasH - cy;
    double r = static_cast<double>(c.radius) * scale;
    os << "      <circle cx=\"" << cx << "\" cy=\"" << cy << "\" r=\"" << r << "\"/>\n";
}

} // namespace marc
