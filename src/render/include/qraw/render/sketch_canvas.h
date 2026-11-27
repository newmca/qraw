#pragma once

// Hand-drawn rendering for `sketch=1` and `comic=1`: a Canvas decorator that
// sits between a painter and the real backend and turns its exact geometry
// into wobbly strokes and hachure fills.
//
// Two modes, as in draw.io:
//  - Comic: a port of HandJiggle (mxgraph Shapes.js). Straight segments are
//    split into pieces that are pushed sideways by up to `jiggle`; curves,
//    ellipses and everything else are untouched.
//  - Rough: draw.io draws sketch shapes with rough.js. rough.js is not
//    vendored (and never fetched), so this is a clean-room simplification
//    with the same style vocabulary: strokes are doubled and bowed,
//    fills become hachure lines (fillStyle hachure, cross-hatch, dashed;
//    solid for white or gradient fills; zigzag, zigzag-line and dots are
//    drawn as hachure).
//
// Randomness comes from a PRNG seeded from the cell id (the same hash
// draw.io seeds rough.js with), so a cell looks the same on every repaint
// and exports are byte-stable. draw.io's comic mode uses Math.random and
// differs on every render (DECISIONS D-027).
//
// Text and images are passed through unchanged.

#include "qraw/render/canvas.h"
#include "qraw/render/resolved_style.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::render {

struct SketchSettings {
    enum class Mode { Rough, Comic };

    Mode mode = Mode::Rough;
    double jiggle = 1; // comic: maximum sideways offset; rough: roughness
    double bowing = 1;
    double curveFitting = 0.95;
    double hachureAngle = -41;
    double hachureGap = -1; // <0: 4 x stroke width
    double fillWeight = -1; // <0: stroke width / 2
    double dashOffset = -1; // <0: the hachure gap
    double dashGap = -1;    // <0: the hachure gap
    std::string fillStyle = "auto";
    bool disableMultiStroke = false;
    bool disableMultiStrokeFill = false;

    // nullopt when the cell is not hand-drawn. Editor.js createHandJiggle:
    // sketch=1 picks rough (or comic with sketchStyle=comic), else comic=1.
    [[nodiscard]] static std::optional<SketchSettings> fromStyle(const ResolvedStyle& style);

    // Distance the hand-drawn output can stray beyond the shape bounds
    // (mxShape.updateBoundingBox adds the painted bounds for sketch shapes).
    [[nodiscard]] double paintMargin() const;
};

class SketchCanvas final : public Canvas {
public:
    // Paints on `target`, which must outlive this canvas. The caller owns the
    // surrounding save()/restore() of the target (the constructor sets round
    // line joins and caps, "to avoid spikes" as upstream).
    SketchCanvas(Canvas& target, SketchSettings settings, std::uint32_t seed);

    // The seed draw.io derives from the cell id (31-hash, starting at 1).
    [[nodiscard]] static std::uint32_t seedFromId(std::string_view id);

    void save() override;
    void restore() override;
    void scale(double s) override { target_.scale(s); }
    void translate(double dx, double dy) override { target_.translate(dx, dy); }
    void rotate(double degrees, bool flipH, bool flipV, double cx, double cy) override
    {
        target_.rotate(degrees, flipH, flipV, cx, cy);
    }

    void setAlpha(double alpha) override { target_.setAlpha(alpha); }
    void setFillAlpha(double alpha) override;
    void setStrokeAlpha(double alpha) override { target_.setStrokeAlpha(alpha); }
    void setFillColor(std::optional<QColor> color) override;
    void setGradient(const QColor& c1, const QColor& c2, const Rect& bounds, GradientDirection direction,
                     double alpha1, double alpha2) override;
    void setStrokeColor(std::optional<QColor> color) override;
    void setStrokeWidth(double width) override;
    void setDashed(bool dashed, bool fixDash) override { target_.setDashed(dashed, fixDash); }
    void setDashPattern(std::string_view pattern) override { target_.setDashPattern(pattern); }
    void setLineCap(LineCap cap) override { target_.setLineCap(cap); }
    void setLineJoin(LineJoin join) override { target_.setLineJoin(join); }
    void setMiterLimit(double limit) override { target_.setMiterLimit(limit); }
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
               bool flipV) override
    {
        target_.image(x, y, w, h, image, aspect, flipH, flipV);
    }
    void text(const TextBlock& block) override { target_.text(block); }
    void textOnPath(const PathText& text) override { target_.textOnPath(text); }
    void beginGroup(std::string_view cellId) override { target_.beginGroup(cellId); }
    void endGroup() override { target_.endGroup(); }

private:
    using Point = geometry::Point;

    // The paint state the rough renderer needs (what the target would also
    // track), saved and restored together with the target's.
    struct State {
        std::optional<QColor> fill;
        std::optional<QColor> stroke;
        bool gradient = false;
        bool shadow = false;
        double strokeWidth = 1;
        double fillAlpha = 1;
    };

    // A flattened subpath: curved[i] says whether the segment ending at
    // pts[i] came from a curve.
    struct Polyline {
        std::vector<Point> pts;
        std::vector<char> curved;
        bool closed = false;
    };

    // Comic mode.
    void jiggleLineTo(double x, double y);

    // Rough mode.
    void paintRough(bool fill, bool stroke);
    [[nodiscard]] std::vector<Polyline> flatten() const;
    void paintRoughFill(const std::vector<Polyline>& shape);
    void paintRoughStroke(const std::vector<Polyline>& shape);
    void roughLine(Point a, Point b, bool second);
    void roughCurve(const std::vector<Point>& pts, bool second, bool loop);
    void emitLines(const std::vector<std::pair<Point, Point>>& lines, bool multi);

    double random(); // 0 <= r < 1

    Canvas& target_;
    SketchSettings settings_;
    std::uint32_t rng_;
    State state_;
    std::vector<State> stack_;
    geometry::Path path_; // rough mode: the path being built
    std::optional<Point> last_;  // comic mode
    std::optional<Point> first_; // comic mode
};

} // namespace qraw::render
