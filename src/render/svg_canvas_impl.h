#pragma once

// Private to svg_*.cpp: the state of SvgCanvas. The public class forwards
// every call here; text output lives in svg_text.cpp.

#include "svg_writer.h"

#include "qraw/render/svg_canvas.h"

#include <map>
#include <string>
#include <vector>

namespace qraw::render {

struct SvgCanvas::Impl {
    // mxSvgCanvas2D's canvas state (mxAbstractCanvas2D.createState plus the
    // SVG specific parts). Saved and restored as a whole.
    struct Gradient {
        QColor start;
        QColor end;
        double startAlpha = 1;
        double endAlpha = 1;
        GradientDirection direction = GradientDirection::South;
    };
    struct State {
        double alpha = 1;
        double fillAlpha = 1;
        double strokeAlpha = 1;
        std::optional<QColor> fill;
        std::optional<Gradient> gradient; // replaces `fill` until the next setFillColor
        std::optional<QColor> stroke;
        double strokeWidth = 1;
        bool dashed = false;
        bool fixDash = false;
        std::string dashPattern = "3 3";
        LineCap lineCap = LineCap::Flat;
        LineJoin lineJoin = LineJoin::Miter;
        double miterLimit = 10;
        bool shadow = false;
        double scale = 1;
        double dx = 0;
        double dy = 0;
        std::string transform; // SVG transform list built by rotate()
    };

    explicit Impl(SvgDocumentOptions o) : options(std::move(o)) {}

    // --- state ---------------------------------------------------------
    void rotate(double degrees, bool flipH, bool flipV, double cx, double cy);

    // --- path ----------------------------------------------------------
    void begin();
    void moveTo(double x, double y);
    void lineTo(double x, double y);
    void quadTo(double x1, double y1, double x2, double y2);
    void curveTo(double x1, double y1, double x2, double y2, double x3, double y3);
    void arcTo(double rx, double ry, double angle, bool largeArc, bool sweep, double x, double y);
    void close();
    void rect(double x, double y, double w, double h);
    void roundrect(double x, double y, double w, double h, double dx, double dy);
    void ellipse(double x, double y, double w, double h);
    void addPath(const geometry::Path& path);
    void paint(bool filled, bool stroked);

    // --- other primitives ------------------------------------------------
    void image(double x, double y, double w, double h, const ImageData& image, bool aspect, bool flipH, bool flipV);
    void text(const TextBlock& block); // svg_text.cpp
    void textOnPath(const PathText& text); // svg_text.cpp

    // --- helpers ---------------------------------------------------------
    [[nodiscard]] double tx(double x) const { return (x + state.dx) * state.scale; }
    [[nodiscard]] double ty(double y) const { return (y + state.dy) * state.scale; }
    std::string nextId() { return options.idPrefix + "-" + std::to_string(++idCounter); }
    // Adds a <clipPath> for the rectangle (or reuses an identical one) and
    // returns its id.
    std::string clipPathId(double x, double y, double w, double h);
    void emitLine(const std::string& line)
    {
        body += line;
        body += '\n';
    }

    SvgDocumentOptions options;
    State state;
    std::vector<State> stack;
    std::string defs;
    std::string body;
    int idCounter = 0;
    int groupDepth = 0;
    std::vector<std::string> cellIds; // open beginGroup() ids, innermost last
    std::map<std::string, std::string> gradientIds; // by description
    std::map<std::string, std::string> clipIds;     // by rectangle

private:
    // The current path. A lone rect/roundrect/ellipse right after begin()
    // stays a primitive (written as <rect>/<ellipse>); anything else is path
    // data. Coordinates are already transformed by dx/dy/scale.
    enum class Primitive { None, Rect, RoundRect, Ellipse };
    struct PrimitiveBox {
        double x = 0, y = 0, w = 0, h = 0;
        double rx = 0, ry = 0; // RoundRect corner radii
    };

    void appendOp(const std::string& op);
    void startSubpathAt(double x, double y); // implicit moveTo, see geometry::Path
    void appendElements(const geometry::Path& path, bool transform);
    void demotePrimitive(); // turns a pending primitive into path data
    void addPrimitive(Primitive kind, const PrimitiveBox& box);
    // The element for the current path, or "" when it would paint nothing.
    [[nodiscard]] std::string buildElement(bool filled, bool stroked, bool shadow);
    [[nodiscard]] std::string gradientId(const Gradient& gradient);
    void resetPath();

    Primitive primitive_ = Primitive::None;
    PrimitiveBox box_;
    std::string d_;      // path data
    Point current_{};    // last point, untransformed (arcTo, close)
    Point subpathStart_{};
    bool hasCurrent_ = false;
};

} // namespace qraw::render
