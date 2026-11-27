#pragma once

// Canvas backend on QPainter: screen (M4), QImage (PNG/JPEG export) and
// QPdfWriter (PDF export). Requires a QGuiApplication for text.

#include "qraw/render/canvas.h"

#include <memory>

class QPainter;

namespace qraw::render {

// How raster images are scaled onto the target.
enum class ImageQuality {
    // The decoded full-resolution image goes through QPainter's own scaling:
    // what export, print and thumbnails use, byte-identical to drawing
    // without a cache. This is the default.
    Exact,
    // The smallest mip level that is at least as large as the destination in
    // device pixels, then QPainter's smooth scaling: what the screen uses (an
    // image is never bilinearly scaled by more than 2x, which aliases, and a
    // megabyte image is not scaled per paint). SVG images are drawn from a
    // power-of-two raster of their destination size instead of as vectors.
    Interactive,
};

class PainterCanvas final : public Canvas {
public:
    // Paints through `painter`; its transform at construction time is the
    // base transform (save()/restore() never pop above it). The painter
    // must outlive the canvas. Antialiasing and smooth pixmap transforms
    // are switched on.
    explicit PainterCanvas(QPainter& painter);
    ~PainterCanvas() override;
    PainterCanvas(const PainterCanvas&) = delete;
    PainterCanvas& operator=(const PainterCanvas&) = delete;

    void setImageQuality(ImageQuality quality);
    [[nodiscard]] ImageQuality imageQuality() const;

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

    void image(double x, double y, double w, double h, const ImageData& image, bool aspect, bool flipH,
               bool flipV) override;
    // Plain text: QTextLayout lines; HTML: QTextDocument (setHtml with the
    // sanitized markup). Wrapping at the box width when wrap is on, overflow
    // hidden clips, background/border drawn behind the laid-out text.
    void text(const TextBlock& block) override;
    // Glyph by glyph along the curve (QPainterPath::percentAtLength and
    // angleAtPercent), each glyph rotated to the tangent. Glyphs that fall
    // outside the curve are not drawn; kerning and ligatures are not applied.
    void textOnPath(const PathText& text) override;

    void beginGroup(std::string_view cellId) override;
    void endGroup() override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::render
