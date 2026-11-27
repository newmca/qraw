// Hand-drawn rendering (sketch=1, comic=1). The comic mode ports HandJiggle
// (mxgraph Shapes.js, Apache-2.0). The rough mode is original code that
// follows the options draw.io passes to rough.js (Editor.js RoughCanvas.getStyle)
// without using rough.js itself.

#include "qraw/render/sketch_canvas.h"

#include <algorithm>
#include <cmath>

namespace qraw::render {

namespace {

using geometry::Path;
using geometry::PathElement;
using Point = geometry::Point;

constexpr double kDegrees = 3.14159265358979323846 / 180.0;
// Editor.sketchDefaultJiggle for comic, rough.js' default roughness otherwise.
constexpr double kComicJiggle = 2;
constexpr double kRoughness = 1;
// rough.js' maxRandomnessOffset: how far control points of a line stray.
constexpr double kMaxOffset = 2;
// Mirrors RoughCanvas.maxFillSize / maxFillCount (Editor.js): larger shapes
// are filled solid, finer hachures are widened.
constexpr double kMaxFillSize = 200000;
constexpr double kMaxFillLines = 100000;

double length(Point a, Point b) { return std::hypot(b.x - a.x, b.y - a.y); }

bool isWhite(const std::optional<QColor>& color) { return color && (color->rgb() & 0xffffffu) == 0xffffffu; }

} // namespace

// --- settings -------------------------------------------------------------------------------

std::optional<SketchSettings> SketchSettings::fromStyle(const ResolvedStyle& style)
{
    const bool sketch = style.flag("sketch", false);
    const bool comic = style.flag("comic", false);
    if (!sketch && !comic)
        return std::nullopt;

    SketchSettings s;
    s.mode = (comic && !sketch) || (sketch && style.string("sketchStyle", "rough") == "comic") ? Mode::Comic
                                                                                               : Mode::Rough;
    s.jiggle = style.number("jiggle", s.mode == Mode::Comic ? kComicJiggle : kRoughness);
    s.bowing = style.number("bowing", s.bowing);
    s.curveFitting = style.number("curveFitting", s.curveFitting);
    s.hachureAngle = style.number("hachureAngle", s.hachureAngle);
    s.hachureGap = style.number("hachureGap", s.hachureGap); // "auto" parses as the fallback
    s.fillWeight = style.number("fillWeight", s.fillWeight);
    s.dashOffset = style.number("dashOffset", s.dashOffset);
    s.dashGap = style.number("dashGap", s.dashGap);
    s.fillStyle = std::string(style.string("fillStyle", "auto"));
    s.disableMultiStroke = style.flag("disableMultiStroke", false);
    s.disableMultiStrokeFill = style.flag("disableMultiStrokeFill", false);
    return s;
}

double SketchSettings::paintMargin() const
{
    return mode == Mode::Comic ? jiggle : 2 + 2 * std::abs(jiggle) + std::abs(bowing);
}

// --- construction and state -------------------------------------------------------------------

SketchCanvas::SketchCanvas(Canvas& target, SketchSettings settings, std::uint32_t seed)
    : target_(target), settings_(std::move(settings)), rng_(seed)
{
    target_.setLineJoin(LineJoin::Round);
    target_.setLineCap(LineCap::Round);
}

std::uint32_t SketchCanvas::seedFromId(std::string_view id)
{
    std::uint32_t seed = 1;
    for (const char c : id)
        seed = (seed << 5) - seed + static_cast<unsigned char>(c);
    return seed;
}

// mulberry32.
double SketchCanvas::random()
{
    rng_ += 0x6D2B79F5u;
    std::uint32_t t = rng_;
    t = (t ^ (t >> 15)) * (t | 1u);
    t ^= t + (t ^ (t >> 7)) * (t | 61u);
    return static_cast<double>((t ^ (t >> 14))) / 4294967296.0;
}

void SketchCanvas::save()
{
    target_.save();
    stack_.push_back(state_);
}

void SketchCanvas::restore()
{
    target_.restore();
    if (!stack_.empty()) {
        state_ = stack_.back();
        stack_.pop_back();
    }
}

void SketchCanvas::setFillAlpha(double alpha)
{
    state_.fillAlpha = alpha;
    target_.setFillAlpha(alpha);
}

void SketchCanvas::setFillColor(std::optional<QColor> color)
{
    state_.fill = color;
    state_.gradient = false;
    target_.setFillColor(color);
}

void SketchCanvas::setGradient(const QColor& c1, const QColor& c2, const Rect& bounds, GradientDirection direction,
                               double alpha1, double alpha2)
{
    state_.fill = c1;
    state_.gradient = true;
    target_.setGradient(c1, c2, bounds, direction, alpha1, alpha2);
}

void SketchCanvas::setStrokeColor(std::optional<QColor> color)
{
    state_.stroke = color;
    target_.setStrokeColor(color);
}

void SketchCanvas::setStrokeWidth(double width)
{
    state_.strokeWidth = width;
    target_.setStrokeWidth(width);
}

void SketchCanvas::setShadow(bool enabled)
{
    state_.shadow = enabled;
    target_.setShadow(enabled);
}

// --- path ops -------------------------------------------------------------------------------------

void SketchCanvas::begin()
{
    path_.clear();
    last_.reset();
    first_.reset();
    target_.begin();
}

void SketchCanvas::moveTo(double x, double y)
{
    if (settings_.mode == SketchSettings::Mode::Rough) {
        path_.moveTo({x, y});
        return;
    }
    target_.moveTo(x, y);
    last_ = first_ = Point{x, y};
}

void SketchCanvas::lineTo(double x, double y)
{
    if (settings_.mode == SketchSettings::Mode::Rough)
        path_.lineTo({x, y});
    else
        jiggleLineTo(x, y);
}

void SketchCanvas::quadTo(double x1, double y1, double x2, double y2)
{
    if (settings_.mode == SketchSettings::Mode::Rough) {
        path_.quadTo({x1, y1}, {x2, y2});
        return;
    }
    target_.quadTo(x1, y1, x2, y2);
    last_ = Point{x2, y2};
}

void SketchCanvas::curveTo(double x1, double y1, double x2, double y2, double x3, double y3)
{
    if (settings_.mode == SketchSettings::Mode::Rough) {
        path_.cubicTo({x1, y1}, {x2, y2}, {x3, y3});
        return;
    }
    target_.curveTo(x1, y1, x2, y2, x3, y3);
    last_ = Point{x3, y3};
}

void SketchCanvas::arcTo(double rx, double ry, double angle, bool largeArc, bool sweep, double x, double y)
{
    if (settings_.mode == SketchSettings::Mode::Rough) {
        path_.arcTo(rx, ry, angle, largeArc, sweep, {x, y});
        return;
    }
    target_.arcTo(rx, ry, angle, largeArc, sweep, x, y);
    last_ = Point{x, y};
}

void SketchCanvas::close()
{
    if (settings_.mode == SketchSettings::Mode::Rough) {
        path_.close();
        return;
    }
    // HandJiggle.close: the closing segment is jiggled like any line.
    if (first_)
        jiggleLineTo(first_->x, first_->y);
    target_.close();
}

void SketchCanvas::rect(double x, double y, double w, double h)
{
    Path p;
    p.addRect({x, y, w, h});
    addPath(p);
}

void SketchCanvas::roundrect(double x, double y, double w, double h, double dx, double dy)
{
    Path p;
    p.addRoundedRect({x, y, w, h}, dx, dy);
    addPath(p);
}

void SketchCanvas::ellipse(double x, double y, double w, double h)
{
    if (settings_.mode == SketchSettings::Mode::Rough) {
        Path p;
        p.addEllipse({x, y, w, h});
        path_.append(p);
    } else {
        target_.ellipse(x, y, w, h); // HandJiggle leaves ellipses alone
    }
}

void SketchCanvas::addPath(const Path& path)
{
    for (const PathElement& e : path.elements()) {
        switch (e.kind) {
        case PathElement::Kind::MoveTo:
            moveTo(e.p[0].x, e.p[0].y);
            break;
        case PathElement::Kind::LineTo:
            lineTo(e.p[0].x, e.p[0].y);
            break;
        case PathElement::Kind::QuadTo:
            quadTo(e.p[0].x, e.p[0].y, e.p[1].x, e.p[1].y);
            break;
        case PathElement::Kind::CubicTo:
            curveTo(e.p[0].x, e.p[0].y, e.p[1].x, e.p[1].y, e.p[2].x, e.p[2].y);
            break;
        case PathElement::Kind::Close:
            close();
            break;
        }
    }
}

void SketchCanvas::fill() { settings_.mode == SketchSettings::Mode::Rough ? paintRough(true, false) : target_.fill(); }
void SketchCanvas::stroke() { settings_.mode == SketchSettings::Mode::Rough ? paintRough(false, true) : target_.stroke(); }
void SketchCanvas::fillAndStroke()
{
    settings_.mode == SketchSettings::Mode::Rough ? paintRough(true, true) : target_.fillAndStroke();
}

// --- comic (HandJiggle.lineTo) ------------------------------------------------------------------------

void SketchCanvas::jiggleLineTo(double endX, double endY)
{
    constexpr int kMaxSegments = 10000;
    if (!last_) {
        target_.lineTo(endX, endY);
        last_ = Point{endX, endY};
        return;
    }
    const double dx = std::abs(endX - last_->x);
    const double dy = std::abs(endY - last_->y);
    const double dist = std::sqrt(dx * dx + dy * dy);
    if (dist < 2) {
        target_.lineTo(endX, endY);
        last_ = Point{endX, endY};
        return;
    }

    // Few segments for short lines, with a third of the variation.
    int segments = std::min(static_cast<int>(std::lround(dist / 10)), kMaxSegments);
    double variation = settings_.jiggle;
    if (segments < 5) {
        segments = 5;
        variation /= 3;
    }
    const double stepX = (endX - last_->x) / segments;
    const double stepY = (endY - last_->y) / segments;
    // Upstream pairs the offset with the swapped, unsigned direction
    // components; kept as is so the look matches.
    const double fx = dx / dist;
    const double fy = dy / dist;
    for (int s = 0; s < segments; ++s) {
        const double x = stepX * s + last_->x;
        const double y = stepY * s + last_->y;
        const double offset = (random() - 0.5) * variation;
        target_.lineTo(x - offset * fy, y - offset * fx);
    }
    target_.lineTo(endX, endY);
    last_ = Point{endX, endY};
}

// --- rough: geometry helpers --------------------------------------------------------------------------

std::vector<SketchCanvas::Polyline> SketchCanvas::flatten() const
{
    std::vector<Polyline> result;
    Polyline current;
    std::optional<Point> start;
    const auto finish = [&] {
        if (current.pts.size() > 1)
            result.push_back(std::move(current));
        current = {};
    };
    const auto ensureStarted = [&] {
        if (current.pts.empty() && start) {
            current.pts.push_back(*start);
            current.curved.push_back(0);
        }
    };
    const auto addLine = [&](Point p) {
        ensureStarted();
        current.pts.push_back(p);
        current.curved.push_back(0);
    };
    // Samples a cubic; the step count follows the control polygon length.
    const auto addCubic = [&](Point c1, Point c2, Point end) {
        ensureStarted();
        if (current.pts.empty())
            return;
        const Point p0 = current.pts.back();
        const double len = length(p0, c1) + length(c1, c2) + length(c2, end);
        const int steps = std::clamp(static_cast<int>(len / 5), 6, 40);
        for (int i = 1; i <= steps; ++i) {
            const double t = static_cast<double>(i) / steps;
            const double u = 1 - t;
            current.pts.push_back({u * u * u * p0.x + 3 * u * u * t * c1.x + 3 * u * t * t * c2.x + t * t * t * end.x,
                                   u * u * u * p0.y + 3 * u * u * t * c1.y + 3 * u * t * t * c2.y + t * t * t * end.y});
            current.curved.push_back(1);
        }
    };

    for (const PathElement& e : path_.elements()) {
        switch (e.kind) {
        case PathElement::Kind::MoveTo:
            finish();
            start = e.p[0];
            current.pts.push_back(e.p[0]);
            current.curved.push_back(0);
            break;
        case PathElement::Kind::LineTo:
            addLine(e.p[0]);
            break;
        case PathElement::Kind::QuadTo: {
            ensureStarted();
            if (current.pts.empty())
                break;
            const Point p0 = current.pts.back();
            const Point c = e.p[0];
            const Point end = e.p[1];
            addCubic({p0.x + 2.0 / 3 * (c.x - p0.x), p0.y + 2.0 / 3 * (c.y - p0.y)},
                     {end.x + 2.0 / 3 * (c.x - end.x), end.y + 2.0 / 3 * (c.y - end.y)}, end);
            break;
        }
        case PathElement::Kind::CubicTo:
            addCubic(e.p[0], e.p[1], e.p[2]);
            break;
        case PathElement::Kind::Close:
            if (current.pts.size() > 1) {
                const Point first = current.pts.front();
                if (length(current.pts.back(), first) > 1e-9)
                    addLine(first);
                current.closed = true;
            }
            finish();
            break;
        }
    }
    finish();
    return result;
}

namespace {

using Line = std::pair<Point, Point>;

// Even-odd scan of `shape` with parallel lines `gap` apart whose direction is
// `directionDegrees`; returns the inside pieces.
std::vector<Line> scanLines(const std::vector<std::vector<Point>>& polygons, double directionDegrees, double gap)
{
    const double dx = std::cos(directionDegrees * kDegrees);
    const double dy = std::sin(directionDegrees * kDegrees);
    // Along-line coordinate s = d . p, across coordinate t = n . p.
    const auto along = [&](Point p) { return dx * p.x + dy * p.y; };
    const auto across = [&](Point p) { return -dy * p.x + dx * p.y; };

    double tMin = INFINITY;
    double tMax = -INFINITY;
    for (const auto& polygon : polygons)
        for (const Point& p : polygon) {
            tMin = std::min(tMin, across(p));
            tMax = std::max(tMax, across(p));
        }
    std::vector<Line> lines;
    if (!(tMax > tMin) || !(gap > 0))
        return lines;

    std::vector<double> hits;
    for (double t = tMin + gap / 2; t < tMax; t += gap) {
        hits.clear();
        for (const auto& polygon : polygons) {
            for (std::size_t i = 0; i < polygon.size(); ++i) {
                const Point a = polygon[i];
                const Point b = polygon[(i + 1) % polygon.size()];
                const double ta = across(a);
                const double tb = across(b);
                // Half-open: a vertex on the scan line counts once.
                if ((ta <= t && t < tb) || (tb <= t && t < ta))
                    hits.push_back(along(a) + (t - ta) / (tb - ta) * (along(b) - along(a)));
            }
        }
        std::sort(hits.begin(), hits.end());
        for (std::size_t i = 0; i + 1 < hits.size(); i += 2)
            lines.push_back({{dx * hits[i] - dy * t, dy * hits[i] + dx * t},
                             {dx * hits[i + 1] - dy * t, dy * hits[i + 1] + dx * t}});
    }
    return lines;
}

// Cuts every line into dashes (fillStyle dashed).
std::vector<Line> dashLines(const std::vector<Line>& lines, double dash, double gap)
{
    std::vector<Line> result;
    if (!(dash > 0) || !(gap >= 0))
        return lines;
    for (const auto& [a, b] : lines) {
        const double len = length(a, b);
        if (len <= 0)
            continue;
        const double ux = (b.x - a.x) / len;
        const double uy = (b.y - a.y) / len;
        for (double s = 0; s < len; s += dash + gap) {
            const double e = std::min(s + dash, len);
            result.push_back({{a.x + ux * s, a.y + uy * s}, {a.x + ux * e, a.y + uy * e}});
        }
    }
    return result;
}

} // namespace

// --- rough: painting ---------------------------------------------------------------------------------------

void SketchCanvas::paintRough(bool fill, bool stroke)
{
    if (path_.empty())
        return;
    const std::vector<Polyline> shape = flatten();
    const bool filled = fill && state_.fill;
    if (filled)
        paintRoughFill(shape);
    if (stroke && state_.stroke && state_.strokeWidth > 0) {
        // The shadow belongs to the fill when there is one.
        target_.save();
        if (filled)
            target_.setShadow(false);
        paintRoughStroke(shape);
        target_.restore();
    }
}

void SketchCanvas::paintRoughStroke(const std::vector<Polyline>& shape)
{
    target_.begin();
    for (int pass = 0; pass < (settings_.disableMultiStroke ? 1 : 2); ++pass) {
        const bool second = pass == 1;
        for (const Polyline& poly : shape) {
            const std::size_t n = poly.pts.size();
            if (poly.closed && std::all_of(poly.curved.begin() + 1, poly.curved.end(), [](char c) { return c; })) {
                roughCurve(poly.pts, second, true);
                continue;
            }
            for (std::size_t i = 1; i < n;) {
                if (!poly.curved[i]) {
                    roughLine(poly.pts[i - 1], poly.pts[i], second);
                    ++i;
                    continue;
                }
                std::size_t j = i;
                while (j < n && poly.curved[j])
                    ++j;
                roughCurve({poly.pts.begin() + static_cast<std::ptrdiff_t>(i) - 1,
                            poly.pts.begin() + static_cast<std::ptrdiff_t>(j)},
                           second, false);
                i = j;
            }
        }
    }
    target_.stroke();
}

// One bowed Bezier from a to b. The end points stay put (draw.io passes
// preserveVertices), only the control points wander; the second pass wanders
// half as far.
void SketchCanvas::roughLine(Point a, Point b, bool second)
{
    const double len = length(a, b);
    target_.moveTo(a.x, a.y);
    if (len < 1e-6) {
        target_.lineTo(b.x, b.y);
        return;
    }
    const double roughness = std::abs(settings_.jiggle);
    double offset = kMaxOffset;
    if (offset * offset * 100 > len * len)
        offset = len / 10;
    if (second)
        offset /= 2;
    offset *= roughness;
    const auto wander = [&] { return (random() * 2 - 1) * offset; };

    const double diverge = 0.2 + random() * 0.2;
    const double bowX = settings_.bowing * kMaxOffset * (b.y - a.y) / 200 * roughness;
    const double bowY = settings_.bowing * kMaxOffset * (a.x - b.x) / 200 * roughness;
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    target_.curveTo(bowX + a.x + dx * diverge + wander(), bowY + a.y + dy * diverge + wander(),
                    bowX + a.x + 2 * dx * diverge + wander(), bowY + a.y + 2 * dy * diverge + wander(), b.x, b.y);
}

// A smooth (Catmull-Rom) curve through the points, the interior ones shifted
// a little; `curveFitting` 1 hugs the original curve.
void SketchCanvas::roughCurve(const std::vector<Point>& pts, bool second, bool loop)
{
    const std::size_t n = pts.size();
    if (n < 2)
        return;
    double runLength = 0;
    for (std::size_t i = 1; i < n; ++i)
        runLength += length(pts[i - 1], pts[i]);
    double amplitude = std::abs(settings_.jiggle) * (0.6 + 4 * (1 - settings_.curveFitting));
    amplitude = std::min(amplitude, runLength / 10) * (second ? 0.7 : 1);

    std::vector<Point> q = pts;
    for (std::size_t i = 1; i + 1 < n; ++i) {
        q[i].x += (random() * 2 - 1) * amplitude;
        q[i].y += (random() * 2 - 1) * amplitude;
    }
    // A loop repeats its first point at the end; neighbors wrap past it.
    const std::size_t unique = loop ? n - 1 : n;
    const auto at = [&](std::ptrdiff_t i) {
        if (loop) {
            const auto m = static_cast<std::ptrdiff_t>(unique);
            return q[static_cast<std::size_t>(((i % m) + m) % m)];
        }
        return q[static_cast<std::size_t>(std::clamp<std::ptrdiff_t>(i, 0, static_cast<std::ptrdiff_t>(n) - 1))];
    };
    target_.moveTo(q[0].x, q[0].y);
    for (std::size_t k = 0; k + 1 < n; ++k) {
        const auto i = static_cast<std::ptrdiff_t>(k);
        const Point p0 = at(i - 1);
        const Point p1 = at(i);
        const Point p2 = q[k + 1]; // the last point of a loop is the fixed start
        const Point p3 = at(i + 2);
        target_.curveTo(p1.x + (p2.x - p0.x) / 6, p1.y + (p2.y - p0.y) / 6, p2.x - (p3.x - p1.x) / 6,
                        p2.y - (p3.y - p1.y) / 6, p2.x, p2.y);
    }
}

// Rough.js' scan lines are drawn with the same wobbly line as strokes.
void SketchCanvas::emitLines(const std::vector<std::pair<Point, Point>>& lines, bool multi)
{
    target_.begin();
    for (const auto& [a, b] : lines) {
        roughLine(a, b, false);
        if (multi)
            roughLine(a, b, true);
    }
    target_.stroke();
}

void SketchCanvas::paintRoughFill(const std::vector<Polyline>& shape)
{
    // fillStyle "auto": solid for white and gradient fills, hachure otherwise
    // (RoughCanvas.getStyle). "dots" is switched off upstream as well.
    std::string style = settings_.fillStyle;
    if (style == "auto")
        style = (state_.gradient || isWhite(state_.fill)) ? "solid" : "hachure";

    std::vector<std::vector<Point>> polygons;
    double extent = 0;
    for (const Polyline& poly : shape) {
        polygons.push_back(poly.pts);
        double minX = INFINITY, minY = INFINITY, maxX = -INFINITY, maxY = -INFINITY;
        for (const Point& pt : poly.pts) {
            minX = std::min(minX, pt.x);
            maxX = std::max(maxX, pt.x);
            minY = std::min(minY, pt.y);
            maxY = std::max(maxY, pt.y);
        }
        extent = std::max(extent, (maxX - minX) + (maxY - minY));
    }
    if (style == "solid" || extent > kMaxFillSize) {
        target_.begin();
        target_.addPath(path_);
        target_.fill();
        return;
    }

    double gap = settings_.hachureGap < 0 ? 4 * state_.strokeWidth : settings_.hachureGap;
    const bool cross = style == "cross-hatch";
    gap = std::max({gap, 0.1, (cross ? 2 : 1) * extent / kMaxFillLines});

    // rough.js rotates the shape by angle + 90 and scans horizontally.
    const double direction = -(settings_.hachureAngle + 90);
    std::vector<Line> lines = scanLines(polygons, direction, gap);
    if (cross) {
        const std::vector<Line> second = scanLines(polygons, direction + 90, gap);
        lines.insert(lines.end(), second.begin(), second.end());
    }
    if (style == "dashed") {
        const double dash = settings_.dashOffset < 0 ? gap : settings_.dashOffset;
        const double space = settings_.dashGap < 0 ? gap : settings_.dashGap;
        lines = dashLines(lines, dash, space);
    }

    // The lines are strokes in the fill color (RoughCanvas.fillSketch).
    target_.save();
    target_.setStrokeColor(state_.fill);
    target_.setStrokeAlpha(state_.fillAlpha);
    target_.setStrokeWidth(settings_.fillWeight < 0 ? state_.strokeWidth / 2 : settings_.fillWeight);
    target_.setDashed(false, false);
    emitLines(lines, !settings_.disableMultiStrokeFill);
    target_.restore();
}

} // namespace qraw::render
