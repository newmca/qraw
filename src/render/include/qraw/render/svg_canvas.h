#pragma once

// Canvas backend that writes SVG text directly (not QSvgGenerator, see
// DECISIONS.md D-021): structured output (<g data-cell-id>), HTML labels as
// <foreignObject> with a <text> fallback, gradients/clip paths in <defs>,
// the editable diagram in the root `content` attribute, and byte-identical
// output for identical calls (golden tests).

#include "qraw/render/canvas.h"

#include <QColor>

#include <memory>
#include <optional>
#include <string>

namespace qraw::render {

struct SvgDocumentOptions {
    // Root size in px (width/height attributes) and the viewBox
    // "<originX> <originY> <width> <height>". draw.io uses an origin of
    // -0.5 -0.5 for crisp 1px strokes.
    double width = 0;
    double height = 0;
    double originX = -0.5;
    double originY = -0.5;
    // Painted as the first element (<rect width="100%" height="100%">) and
    // as style="background-color: ...". nullopt = transparent.
    std::optional<QColor> background;
    // The mxfile XML for the root `content` attribute (attribute-escaped by
    // the writer, exactly like draw.io's svgRoot.setAttribute('content',
    // xml)). Empty = no attribute.
    std::string content;
    // Prefix of generated ids (gradients, clip paths, filters):
    // "<prefix>-<counter>". Counter-based, never random.
    std::string idPrefix = "qraw";
};

class SvgCanvas final : public Canvas {
public:
    explicit SvgCanvas(SvgDocumentOptions options);
    ~SvgCanvas() override;
    SvgCanvas(const SvgCanvas&) = delete;
    SvgCanvas& operator=(const SvgCanvas&) = delete;

    void save() override;
    void restore() override;
    void scale(double s) override;
    void translate(double dx, double dy) override;
    void rotate(double degrees, bool flipH, bool flipV, double cx, double cy) override;

    void setAlpha(double alpha) override;
    void setFillAlpha(double alpha) override;
    void setStrokeAlpha(double alpha) override;
    void setFillColor(std::optional<QColor> color) override;
    void setGradient(const QColor& c1, const QColor& c2, const Rect& bounds, GradientDirection direction,
                     double alpha1, double alpha2) override;
    void setStrokeColor(std::optional<QColor> color) override;
    void setStrokeWidth(double width) override;
    void setDashed(bool dashed, bool fixDash) override;
    void setDashPattern(std::string_view pattern) override;
    void setLineCap(LineCap cap) override;
    void setLineJoin(LineJoin join) override;
    void setMiterLimit(double limit) override;
    void setShadow(bool enabled) override;

    void begin() override;
    void moveTo(double x, double y) override;
    void lineTo(double x, double y) override;
    void quadTo(double x1, double y1, double x2, double y2) override;
    void curveTo(double x1, double y1, double x2, double y2, double x3, double y3) override;
    void arcTo(double rx, double ry, double angle, bool largeArc, bool sweep, double x, double y) override;
    void close() override;
    void rect(double x, double y, double w, double h) override;
    void roundrect(double x, double y, double w, double h, double dx, double dy) override;
    void ellipse(double x, double y, double w, double h) override;
    void addPath(const geometry::Path& path) override;
    void fill() override;
    void stroke() override;
    void fillAndStroke() override;

    // <image> with a data: URI of the bytes (never a link).
    void image(double x, double y, double w, double h, const ImageData& image, bool aspect, bool flipH,
               bool flipV) override;
    // HTML labels: <switch><foreignObject requiredFeatures=
    // "http://www.w3.org/TR/SVG11/feature#Extensibility"> with draw.io's
    // nested flex <div> layout, then a <text> fallback (one <tspan> per
    // RichText line, no measuring). Plain labels: <text> only.
    void text(const TextBlock& block) override;
    // <g><defs><path id="..."/></defs><text><textPath .../></text></g> as
    // draw.io's CurvedTextShape writes it; the path id derives from the
    // enclosing beginGroup() cell id (stable across edits of other cells).
    void textOnPath(const PathText& text) override;

    void beginGroup(std::string_view cellId) override;
    void endGroup() override;

    // The complete document: XML declaration, SVG 1.1 DOCTYPE, root <svg>
    // with xmlns, xmlns:xlink, version="1.1", width/height ("<n>px"),
    // viewBox, optional content and background, then <defs> and the drawn
    // elements. Numbers use at most two decimals (mxSvgCanvas2D.format).
    // Unbalanced groups are closed.
    [[nodiscard]] std::string toSvg() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::render
