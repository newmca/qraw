#pragma once

// The drawing interface every painter (shapes, stencils, markers, labels)
// targets. It mirrors mxAbstractCanvas2D (mxgraph/src/util/mxAbstractCanvas2D.js)
// call for call, so upstream painting code ports line by line:
//   c.begin(); c.moveTo(x, y); c.lineTo(...); c.close(); c.fillAndStroke();
//
// Backends: PainterCanvas (QPainter: screen, PNG/JPEG, PDF) and SvgCanvas
// (own SVG writer). Tests use a recording canvas
// (tests/render/support/recording_canvas.h).
//
// Semantics shared by all backends:
//  - State (colors, alphas, dash, line cap/join, shadow, transform) is saved
//    and restored as a whole by save()/restore().
//  - The current path is built by begin() + path ops and consumed by the
//    next fill()/stroke()/fillAndStroke(); begin() discards any previous
//    path. Path coordinates go through the current transform.
//  - Colors: nullopt (or "none" upstream) disables fill/stroke. A gradient
//    replaces the fill color until the next setFillColor().
//  - Alpha: setAlpha is global (opacity), fill/stroke alphas multiply it.
//  - setShadow(true) paints a shadow (shadow color #000000 with alpha
//    0.25, offset (2, 3) scaled by the current scale) under every following
//    fill/stroke until setShadow(false). Shapes switch it off after the
//    background, as mxShape.paintVertexShape does.
//  - Dash pattern values are multiples of the stroke width unless fixDash.

#include "qraw/geometry/path.h"
#include "qraw/render/text.h"

#include <QByteArray>
#include <QColor>
#include <QString>

#include <memory>
#include <optional>
#include <string_view>

namespace qraw::render {

class CachedImage;

using geometry::Point;
using geometry::Rect;

enum class LineCap { Flat, Round, Square };   // style linecap: flat|round|square
enum class LineJoin { Miter, Round, Bevel };  // style linejoin: miter|round|bevel
enum class GradientDirection { South, North, East, West, Radial };

[[nodiscard]] constexpr LineCap parseLineCap(std::string_view v)
{
    return v == "round" ? LineCap::Round : v == "square" ? LineCap::Square : LineCap::Flat;
}
[[nodiscard]] constexpr LineJoin parseLineJoin(std::string_view v)
{
    return v == "round" ? LineJoin::Round : v == "bevel" ? LineJoin::Bevel : LineJoin::Miter;
}
[[nodiscard]] constexpr GradientDirection parseGradientDirection(std::string_view v)
{
    if (v == "north")
        return GradientDirection::North;
    if (v == "east")
        return GradientDirection::East;
    if (v == "west")
        return GradientDirection::West;
    if (v == "radial")
        return GradientDirection::Radial;
    return GradientDirection::South;
}

// An image ready for drawing. Never a remote reference. Either the encoded
// bytes (resolveImage), or a handle to the decoded, shared image of the image
// cache (cachedStyleImage; `bytes` is then empty and encodedBytes() reads them
// again, which only SVG export needs).
struct ImageData {
    QByteArray bytes;  // PNG, JPEG, GIF, BMP, WebP or SVG bytes
    QString mimeType;  // "image/png", "image/svg+xml", ...
    std::shared_ptr<const CachedImage> cached{};
    [[nodiscard]] bool empty() const { return bytes.isEmpty() && !cached; }
    // `bytes`, or the bytes of `cached` read again from their source.
    [[nodiscard]] QByteArray encodedBytes() const;
    friend bool operator==(const ImageData&, const ImageData&) = default;
};

class Canvas {
public:
    virtual ~Canvas() = default;

    // --- state ----------------------------------------------------------
    virtual void save() = 0;
    virtual void restore() = 0;
    // Uniform scale of subsequent coordinates and stroke widths.
    virtual void scale(double s) = 0;
    virtual void translate(double dx, double dy) = 0;
    // mxSvgCanvas2D.rotate: mirror (flipH: x around cx, flipV: y around cy),
    // then rotate by `degrees` (clockwise, y down) around (cx, cy).
    virtual void rotate(double degrees, bool flipH, bool flipV, double cx, double cy) = 0;

    virtual void setAlpha(double alpha) = 0;       // 0..1
    virtual void setFillAlpha(double alpha) = 0;   // 0..1
    virtual void setStrokeAlpha(double alpha) = 0; // 0..1
    virtual void setFillColor(std::optional<QColor> color) = 0;
    // Linear gradient from c1 to c2 across `bounds` in `direction` (south:
    // c1 at the top), or radial from the center. Alphas are 0..1.
    virtual void setGradient(const QColor& c1, const QColor& c2, const Rect& bounds, GradientDirection direction,
                             double alpha1 = 1, double alpha2 = 1) = 0;
    virtual void setStrokeColor(std::optional<QColor> color) = 0;
    virtual void setStrokeWidth(double width) = 0;
    virtual void setDashed(bool dashed, bool fixDash = false) = 0;
    // Space-separated numbers, e.g. "3 3" (the default when dashed).
    virtual void setDashPattern(std::string_view pattern) = 0;
    virtual void setLineCap(LineCap cap) = 0;
    virtual void setLineJoin(LineJoin join) = 0;
    virtual void setMiterLimit(double limit) = 0;
    virtual void setShadow(bool enabled) = 0;

    // --- path -------------------------------------------------------------
    virtual void begin() = 0;
    virtual void moveTo(double x, double y) = 0;
    virtual void lineTo(double x, double y) = 0;
    virtual void quadTo(double x1, double y1, double x2, double y2) = 0;
    virtual void curveTo(double x1, double y1, double x2, double y2, double x3, double y3) = 0;
    // SVG arc semantics (geometry::Path::arcTo); `angle` in degrees.
    virtual void arcTo(double rx, double ry, double angle, bool largeArc, bool sweep, double x, double y) = 0;
    virtual void close() = 0;
    // Closed subpaths added to the current path (see geometry::Path).
    virtual void rect(double x, double y, double w, double h) = 0;
    virtual void roundrect(double x, double y, double w, double h, double dx, double dy) = 0;
    virtual void ellipse(double x, double y, double w, double h) = 0;
    // Adds a prepared path (absolute coordinates, transformed like the ops).
    virtual void addPath(const geometry::Path& path) = 0;

    virtual void fill() = 0;
    virtual void stroke() = 0;
    virtual void fillAndStroke() = 0;

    // --- other primitives ---------------------------------------------------
    // Draws `image` into (x, y, w, h). aspect = keep the aspect ratio,
    // centered in the box (style imageAspect). flips mirror inside the box.
    // An undecodable image draws nothing.
    virtual void image(double x, double y, double w, double h, const ImageData& image, bool aspect, bool flipH,
                       bool flipV) = 0;
    // Draws a label (mxAbstractCanvas2D.text semantics, see TextBlock).
    virtual void text(const TextBlock& block) = 0;
    // Qraw extension (not in mxAbstractCanvas2D): single-line text along a
    // curve, see PathText. Does not touch the current path.
    virtual void textOnPath(const PathText& text) = 0;

    // --- structure ----------------------------------------------------------
    // Brackets the output of one cell. SvgCanvas writes
    // <g data-cell-id="..."> ... </g> like draw.io's export; other backends
    // ignore it. Groups nest.
    virtual void beginGroup(std::string_view cellId) = 0;
    virtual void endGroup() = 0;
};

} // namespace qraw::render
