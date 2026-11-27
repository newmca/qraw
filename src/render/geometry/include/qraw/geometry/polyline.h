#pragma once

// Polyline helpers for edges: rounded/curved outlines, lengths, and the
// relative positions draw.io uses for edge labels and edge children.

#include "qraw/core/geometry.h"
#include "qraw/geometry/path.h"

#include <optional>
#include <span>

namespace qraw::geometry {

// mxConstants.LINE_ARCSIZE: default `arcSize` of rounded edges.
inline constexpr double kLineArcSize = 20;

// The outline of a polyline, a line-for-line port of
// mxShape.prototype.addPoints(c, pts, rounded, arcSize, close, exclude,
// initialMove) (mxgraph/src/shape/mxShape.js:1483):
//  - rounded == false: moveTo(pts[0]) (lineTo when !initialMove) and lineTo
//    through the rest.
//  - rounded == true: each corner not listed in `exclude` (indices i - 1 as
//    upstream) becomes lineTo + quadTo with the corner as control point,
//    cut back by min(arcSize, segment length / 2); overlapping next points
//    are skipped as upstream does.
//  - close == true closes the outline; with rounding, a virtual point in
//    the middle of the closing segment is inserted first, as upstream.
// NOTE: `arcSize` is the value upstream passes to addPoints, which callers
// compute as style arcSize / 2 (mxPolyline.paintLine passes
// LINE_ARCSIZE / 2 = 10 by default).
// An empty input yields an empty path.
[[nodiscard]] Path polylinePath(std::span<const Point> points, bool rounded, double arcSize = kLineArcSize / 2,
                                bool close = false, std::span<const int> exclude = {}, bool initialMove = true);

// `curved=1` edges: mxPolyline.prototype.paintCurvedLine
// (mxgraph/src/shape/mxPolyline.js): quadratic segments through the
// midpoints of the inner segments. Two points yield a straight line.
[[nodiscard]] Path curvedPath(std::span<const Point> points);

// Sum of the segment lengths.
[[nodiscard]] double polylineLength(std::span<const Point> points);

// Relative edge geometry (an edge label, or a child vertex of an edge with
// relative="1"): the point mxGraphView.prototype.getPoint
// (mxgraph/src/view/mxGraphView.js:4093) returns.
//   x:      geometry x in [-1, 1]; -1 = source end, 0 = middle, 1 = target end
//   y:      geometry y, an offset orthogonal to the segment at that position
//   offset: geometry <mxPoint as="offset">, added as is
// The distance along the path is rounded to whole units as upstream does
// (Math.round). An empty polyline yields `offset` (or the origin).
[[nodiscard]] Point relativePointOnPolyline(std::span<const Point> points, double x, double y,
                                            std::optional<Point> offset);

// Label anchor of an edge: mxGraphView.prototype.updateEdgeLabelOffset
// (:4327). Relative geometry (the normal case) uses relativePointOnPolyline;
// an absolute edge geometry uses the midpoint between the first and last
// point plus `offset`. With no points the result is `fallbackCenter`.
[[nodiscard]] Point edgeLabelAnchor(std::span<const Point> points, bool relative, double x, double y,
                                    std::optional<Point> offset, Point fallbackCenter);

// The inverse used when the user drags an edge label (M4): the relative
// (x in [-1, 1], y orthogonal distance) coordinates of `p` against the
// polyline, as in mxGraphView.prototype.getRelativePoint. Returns {0, 0}
// for fewer than 2 points.
[[nodiscard]] Point relativeCoordinates(std::span<const Point> points, Point p);

// Squared distance from `p` to the polyline (hit testing, edge-to-edge
// terminals).
[[nodiscard]] double distanceSquaredToPolyline(std::span<const Point> points, Point p);

// Bounding box of the points; {0,0,0,0} when empty.
[[nodiscard]] Rect pointsBounds(std::span<const Point> points);

} // namespace qraw::geometry
