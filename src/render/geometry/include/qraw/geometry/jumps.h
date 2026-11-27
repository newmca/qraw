#pragma once

// Line jumps: where an edge crosses the edges painted before it, so that the
// connector painter can draw an arc, gap, sharp or line jump there. A port
// of mxGraphView.prototype.updateLineJumps and getLineJumpSegments
// (js/grapheditor/Graph.js) for straight and rounded polylines, at view
// scale 1.

#include "qraw/core/geometry.h"

#include <optional>
#include <span>
#include <vector>

namespace qraw::geometry {

using core::Point;

// One point of an edge's routed polyline: a plain vertex, or the center of
// a jump (an intersection with an earlier edge). Upstream: routedPoints with
// type 0 / 1.
struct RoutedPoint {
    Point point;
    bool jump = false;
    friend bool operator==(const RoutedPoint&, const RoutedPoint&) = default;
};

// Bounds of a segment with the tolerance margin of the intersection test
// (getLineJumpBounds).
struct JumpBounds {
    double x0 = 0;
    double x1 = 0;
    double y0 = 0;
    double y1 = 0;
};

struct JumpSegment {
    Point p2;
    Point p3;
    std::optional<Point> previous; // the vertex before p2 (upstream pl)
    std::optional<Point> next;     // the vertex after p3 (upstream pn)
    JumpBounds bounds;
};

// The segments of an earlier edge that later edges are tested against
// (waypoints on straight stretches merged, as upstream does).
struct JumpTarget {
    std::vector<JumpSegment> segments;
    std::optional<JumpBounds> bounds; // union of the segment bounds
};

[[nodiscard]] JumpTarget makeJumpTarget(std::span<const Point> polyline);

// The polyline with the jumps over `earlier`, in the order along the line.
// Empty when the line crosses none of them (it is then drawn unchanged).
// First and last entries are the polyline's ends.
[[nodiscard]] std::vector<RoutedPoint> routeJumps(std::span<const Point> polyline,
                                                  std::span<const JumpTarget* const> earlier);

} // namespace qraw::geometry
