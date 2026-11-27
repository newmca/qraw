#pragma once

// Qt-free vector paths. Every canvas backend (QPainter, SVG) consumes this
// type, so shapes, stencils and markers are written once.

#include "qraw/core/geometry.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace qraw::geometry {

using core::Point;
using core::Rect;

// Edge insets of a rectangle (top/left/bottom/right), e.g. label margins.
struct Insets {
    double top = 0;
    double left = 0;
    double bottom = 0;
    double right = 0;
    friend bool operator==(const Insets&, const Insets&) = default;
};

// One element of a Path. All coordinates are absolute.
struct PathElement {
    enum class Kind : std::uint8_t { MoveTo, LineTo, QuadTo, CubicTo, Close };

    Kind kind = Kind::MoveTo;
    // MoveTo / LineTo: p[0] is the point.
    // QuadTo:  p[0] control point, p[1] end point.
    // CubicTo: p[0] and p[1] control points, p[2] end point.
    // Close:   unused (zero).
    std::array<Point, 3> p{};

    friend bool operator==(const PathElement&, const PathElement&) = default;
};

// A sequence of subpaths, equivalent to mxAbstractCanvas2D's path model
// (begin / moveTo / lineTo / quadTo / curveTo / arcTo / close).
//
// Invariants:
//  - A drawing element (LineTo/QuadTo/CubicTo) without a current point
//    starts a subpath: it is preceded by an implicit MoveTo to its end point
//    (lineTo) or first point (quadTo/cubicTo), matching SVG/QPainterPath.
//  - Close returns the current point to the start of the subpath.
//  - Paths compare equal element by element (exact doubles).
class Path {
public:
    void moveTo(Point p)
    {
        elements_.push_back({PathElement::Kind::MoveTo, {p, {}, {}}});
        current_ = p;
        subpathStart_ = p;
    }
    void lineTo(Point p)
    {
        ensureCurrent(p);
        elements_.push_back({PathElement::Kind::LineTo, {p, {}, {}}});
        current_ = p;
    }
    void quadTo(Point control, Point end)
    {
        ensureCurrent(control);
        elements_.push_back({PathElement::Kind::QuadTo, {control, end, {}}});
        current_ = end;
    }
    void cubicTo(Point c1, Point c2, Point end)
    {
        ensureCurrent(c1);
        elements_.push_back({PathElement::Kind::CubicTo, {c1, c2, end}});
        current_ = end;
    }
    void close()
    {
        if (!current_)
            return;
        elements_.push_back({PathElement::Kind::Close, {}});
        current_ = subpathStart_;
    }

    // SVG elliptical arc (endpoint parameterization, SVG 1.1 appendix F.6)
    // from the current point to `end`, appended as cubic Béziers exactly as
    // mxUtils.arcToCurves does (mxgraph/src/util/mxUtils.js). This is what
    // mxAbstractCanvas2D.arcTo and the stencil <arc> op use.
    //  - No current point: behaves like moveTo(end).
    //  - rx == 0 or ry == 0, or end == current point: lineTo(end).
    //  - Radii too small are scaled up as SVG requires.
    // `xAxisRotation` is in degrees.
    void arcTo(double rx, double ry, double xAxisRotation, bool largeArc, bool sweep, Point end);

    // Convenience subpaths. Each is a closed subpath.
    // addRect: moveTo(x,y), lineTo(x+w,y), lineTo(x+w,y+h), lineTo(x,y+h), close.
    void addRect(const Rect& r);
    // addEllipse: four cubic quadrants with kappa 0.5522847498307936,
    // starting at (x + w, y + h/2) and running clockwise in screen
    // coordinates (y down): right, bottom, left, top, then close.
    void addEllipse(const Rect& r);
    // addRoundedRect: mxAbstractCanvas2D.roundrect(x, y, w, h, dx, dy):
    // moveTo(x+dx, y), lineTo(x+w-dx, y), quadTo((x+w, y), (x+w, y+dy)),
    // lineTo(x+w, y+h-dy), quadTo((x+w, y+h), (x+w-dx, y+h)),
    // lineTo(x+dx, y+h), quadTo((x, y+h), (x, y+h-dy)), lineTo(x, y+dy),
    // quadTo((x, y), (x+dx, y)), close. dx/dy are clamped to [0, w/2] / [0, h/2].
    void addRoundedRect(const Rect& r, double dx, double dy);

    // Appends all elements of `other` (its current point becomes ours).
    void append(const Path& other)
    {
        elements_.insert(elements_.end(), other.elements_.begin(), other.elements_.end());
        if (other.current_) {
            current_ = other.current_;
            subpathStart_ = other.subpathStart_;
        }
    }

    void clear()
    {
        elements_.clear();
        current_.reset();
        subpathStart_ = {};
    }

    [[nodiscard]] bool empty() const { return elements_.empty(); }
    [[nodiscard]] const std::vector<PathElement>& elements() const { return elements_; }
    [[nodiscard]] std::optional<Point> currentPoint() const { return current_; }

    friend bool operator==(const Path& a, const Path& b) { return a.elements_ == b.elements_; }

private:
    void ensureCurrent(Point p)
    {
        if (!current_)
            moveTo(p);
    }

    std::vector<PathElement> elements_;
    std::optional<Point> current_;
    Point subpathStart_;
};

// Axis-aligned box of every point in the path, control points included
// (conservative, cheap). An empty path yields {0, 0, 0, 0}.
[[nodiscard]] Rect controlBounds(const Path& path);

// Smallest rectangle containing both (an empty rectangle with zero size at
// the origin counts as a point, not as "nothing").
[[nodiscard]] Rect unite(const Rect& a, const Rect& b);

// Rotates `p` around `center` by `degrees` (clockwise in screen
// coordinates, i.e. mxUtils.getRotatedPoint with cos/sin of the angle).
[[nodiscard]] Point rotatePoint(Point p, double degrees, Point center);

// Bounding box of `r` rotated by `degrees` around its center
// (mxUtils.getBoundingBox).
[[nodiscard]] Rect rotatedBounds(const Rect& r, double degrees);

} // namespace qraw::geometry
