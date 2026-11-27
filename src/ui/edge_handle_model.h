#pragma once
// The handles of a selected edge as plain data: where they are and what they
// edit (mxEdgeHandler / mxElbowEdgeHandler). No widgets and no model
// mutation, so the geometry is unit-testable. Private to the editor UI.

#include "qraw/core/page.h"
#include "qraw/render/page_view.h"

#include <QPointF>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace qraw::ui::edgeh {

enum class HandleKind {
    Source,   // the source end
    Target,   // the target end
    Waypoint, // an existing waypoint; `index` = its index in the waypoint list
    Elbow,    // the single handle of elbow edge styles (sets exactly one waypoint)
    Virtual,  // a segment midpoint; `index` = where dragging inserts a waypoint
    Segment,  // orthogonal styles: the midpoint of segment `index` of segmentPoints()
};

struct Handle {
    HandleKind kind = HandleKind::Waypoint;
    QPointF position; // absolute page coordinates
    std::size_t index = 0;
    bool connected = false; // ends only
    bool verticalSegment = false; // Segment only: the segment runs vertically, so it moves horizontally
};

struct EdgeHandles {
    std::string edge;
    std::vector<QPointF> route;      // routed absolute polyline (CellState::points)
    std::vector<QPointF> waypoints;  // absolute positions of the waypoints
    std::vector<core::Point> points; // the waypoints as stored (edge parent coordinates)
    core::Point origin;              // absolute origin of the edge parent's coordinate system
    // Handles in hit-test order: ends, waypoints, elbow, segments / virtual.
    std::vector<Handle> handles;
    bool elbowStyle = false;   // elbow / sideToSide / topToBottom: one handle instead of segments
    bool segmentStyle = false; // orthogonal / segment connectors: a handle that moves each segment
};

// The points the segment handles of a route sit between
// (mxEdgeSegmentHandler.getCurrentPoints): the route, except that a straight
// route (two points, or three on one horizontal or vertical line) gets its
// middle doubled so that it has three segments: a zero-length one in the
// center that moves the whole line, and a half on each side.
[[nodiscard]] std::vector<QPointF> segmentPoints(const std::vector<QPointF>& route);

// Handles for `state` (an edge), or nullopt when it has no route.
// `pixel` is page units per screen pixel: segments too short to carry a
// virtual handle (< 2 handle sizes) get none.
//  * Straight styles (edgeStyle none): ends, a handle per waypoint, virtual
//    handles at every segment midpoint.
//  * elbow / sideToSide / topToBottom styles: ends and one elbow handle at
//    the waypoint, or at the middle of the route (mxElbowEdgeHandler).
//  * orthogonalEdgeStyle / segmentEdgeStyle: ends and one Segment handle at
//    the midpoint of every segment of the route (mxEdgeSegmentHandler);
//    dragging it moves the segment (edge_segments.h).
//  * other styles (entity relation, loops, ...): as straight, with the
//    virtual handles on the routed segments.
[[nodiscard]] std::optional<EdgeHandles> edgeHandlesFor(const core::Page& page, const render::CellState& state,
                                                       double pixel);

// The first handle within hit distance of `p`.
[[nodiscard]] const Handle* hitHandle(const EdgeHandles& handles, QPointF p, double pixel);

// Index of the polyline segment closest to `p`.
[[nodiscard]] std::size_t nearestSegment(const std::vector<QPointF>& route, QPointF p);

// Where a waypoint added on routed segment `segment` goes in the waypoint
// list: after every waypoint that lies on an earlier segment.
[[nodiscard]] std::size_t insertionIndex(const std::vector<QPointF>& route, const std::vector<QPointF>& waypoints,
                                         std::size_t segment);

// Distance from `p` to the route.
[[nodiscard]] double distanceToRoute(const std::vector<QPointF>& route, QPointF p);

} // namespace qraw::ui::edgeh
