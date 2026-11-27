#pragma once

// Private to the routing implementation (routing*.cpp): the shared types of
// the edge style functions. Not installed, not part of the public API.

#include "qraw/geometry/routing.h"

#include <optional>
#include <vector>

namespace qraw::geometry::detail {

using OptPoint = std::optional<Point>;

// The mxPoint[] of an edge state with null entries. Edge style functions
// append to it exactly like mxEdgeStyle appends to `result`; the pipeline
// starts it with absolutePoints[0] (the fixed source point, possibly null).
using PointList = std::vector<OptPoint>;

// mxConstants.DIRECTION_MASK_*
inline constexpr int kMaskNone = 0;
inline constexpr int kMaskWest = 1;
inline constexpr int kMaskNorth = 2;
inline constexpr int kMaskSouth = 4;
inline constexpr int kMaskEast = 8;
inline constexpr int kMaskAll = 15;

// What an mxEdgeStyle function sees: the edge state (request), the terminal
// states and the control hints. p0/pe are absolutePoints[0] and [last]:
// fixed terminal points (constraints) or dangling end points; nullopt for a
// floating end that is computed after the style ran.
struct StyleInput {
    const EdgeRouteRequest& request;
    const Terminal* source = nullptr; // null: no terminal at this end
    const Terminal* target = nullptr;
    OptPoint p0;
    OptPoint pe;
    const std::vector<Point>& hints; // geometry waypoints, absolute
};

// JavaScript Math.round (ties go towards +infinity).
[[nodiscard]] double jsRound(double v);
// Rounds to one decimal like mxEdgeStyle.scalePointArray at scale 1.
[[nodiscard]] double round10(double v);

// mxUtils.getPortConstraints / reversePortConstraints.
[[nodiscard]] int portConstraints(const Terminal& terminal, int defaultValue);
[[nodiscard]] int reversePortConstraints(int constraint);

// mxEdgeStyle functions (scale 1, translate 0). Each appends the inner
// route points to `result`.
void entityRelation(const StyleInput& in, PointList& result);
void loopStyle(const StyleInput& in, PointList& result);
void sideToSide(const StyleInput& in, PointList& result);
void topToBottom(const StyleInput& in, PointList& result);
void elbowConnector(const StyleInput& in, PointList& result);
void segmentConnector(const StyleInput& in, PointList& result);
void orthConnector(const StyleInput& in, PointList& result);

// mxGraphView.getFloatingTerminalPoint for a vertex terminal and for an edge
// terminal (nearest point of its polyline). `next` is the already resolved
// next point (getNextPoint).
[[nodiscard]] Point floatingVertexPoint(const Terminal& start, Point next, bool orthogonal);
[[nodiscard]] Point nearestPolylinePoint(const std::vector<Point>& polyline, Point next);

} // namespace qraw::geometry::detail
