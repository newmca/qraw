#pragma once

// Edge routing: terminal points, connection constraints and edge styles,
// i.e. the geometry part of mxGraphView.updateEdgeState
// (mxgraph/src/view/mxGraphView.js:2767) and mxEdgeStyle
// (mxgraph/src/view/mxEdgeStyle.js, with the draw.io overrides in
// js/grapheditor/Graph.js:32200-32446).
//
// Everything here is in absolute page coordinates at scale 1 (draw.io's
// view scale and translate are 1 and 0 for export; the M4 canvas applies
// zoom as a transform, never to these numbers).

#include "qraw/core/geometry.h"
#include "qraw/geometry/perimeter.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::geometry {

// A fixed connection point (mxConnectionConstraint), from the edge style
// exitX/exitY/exitDx/exitDy/exitPerimeter (source) or
// entryX/entryY/entryDx/entryDy/entryPerimeter (target).
struct ConnectionConstraint {
    Point point;           // fractions (0..1) of the terminal's unrotated bounds
    bool perimeter = true; // exitPerimeter/entryPerimeter (default 1): project onto the perimeter
    double dx = 0;         // absolute offsets added after scaling (exitDx, ...)
    double dy = 0;
    std::string name;      // optional constraint name (stencil <constraint name>), informational
    friend bool operator==(const ConnectionConstraint&, const ConnectionConstraint&) = default;
};

// One end of an edge that is attached to a cell.
struct Terminal {
    Rect bounds;             // absolute, unrotated bounds of the terminal vertex
    double rotation = 0;     // terminal style `rotation` (degrees)
    bool flipH = false;      // terminal style flipH/flipV (mirror constraint points)
    bool flipV = false;
    // Terminal style `perimeter` resolved with parsePerimeter(); nullopt
    // means "no perimeter": floating ends attach to the center.
    std::optional<PerimeterShape> perimeter = PerimeterShape{};
    // Terminal `perimeterSpacing` + the edge's sourcePerimeterSpacing or
    // targetPerimeterSpacing (the caller adds them).
    double perimeterSpacing = 0;
    // Fixed connection point, if the edge style defines one for this end.
    std::optional<ConnectionConstraint> constraint;
    // Effective port constraint: the edge's sourcePortConstraint /
    // targetPortConstraint when set, else the terminal's `portConstraint`.
    // Comma-separated directions ("north,south"), empty = any side.
    std::string portConstraint;
    bool portConstraintRotation = false; // terminal `portConstraintRotation=1`
    // Non-empty when the terminal is itself an edge (edge-to-edge
    // connection): its routed absolute polyline. `bounds` must then be the
    // bounding box of these points.
    std::vector<Point> edgePoints;

    [[nodiscard]] bool isEdge() const { return !edgePoints.empty(); }
};

enum class EdgeStyleKind {
    Straight,       // edgeStyle absent/none/unknown: polyline through waypoints
    Orthogonal,     // orthogonalEdgeStyle (mxEdgeStyle.OrthConnector)
    Elbow,          // elbowEdgeStyle (draw.io ElbowConnector override)
    EntityRelation, // entityRelationEdgeStyle
    SideToSide,     // sideToSideEdgeStyle
    TopToBottom,    // topToBottomEdgeStyle
    Segment,        // segmentEdgeStyle (mxEdgeStyle.SegmentConnector)
    Loop,           // loopEdgeStyle (also used automatically for self loops)
};

// Maps an `edgeStyle` value. isometricEdgeStyle and any unknown value map
// to Straight (documented gap).
[[nodiscard]] EdgeStyleKind parseEdgeStyle(std::string_view value);

// True for the styles draw.io treats as orthogonal (mxGraph.isOrthogonalEdgeStyle:
// Segment, Elbow, SideToSide, TopToBottom, EntityRelation, Orthogonal).
[[nodiscard]] bool isOrthogonalStyle(EdgeStyleKind kind);

enum class ElbowAxis { Horizontal, Vertical }; // `elbow` style, default horizontal

struct EdgeRouteRequest {
    std::optional<Terminal> source;
    std::optional<Terminal> target;
    // Geometry sourcePoint/targetPoint, absolute. Used for an end that has no
    // terminal (dangling edge). Missing both terminal and point: the end
    // collapses onto the nearest waypoint or the other end.
    std::optional<Point> sourcePoint;
    std::optional<Point> targetPoint;
    // Geometry <Array as="points">, absolute (the caller adds the parent origin).
    std::vector<Point> waypoints;

    EdgeStyleKind style = EdgeStyleKind::Straight;
    bool noEdgeStyle = false;            // `noEdgeStyle=1`: ignore `style`
    std::optional<bool> orthogonal;      // `orthogonal` style override for floating terminal projection
    ElbowAxis elbow = ElbowAxis::Horizontal;
    // `jettySize`, `sourceJettySize`, `targetJettySize`; nullopt = "auto".
    std::optional<double> jettySize;
    std::optional<double> sourceJettySize;
    std::optional<double> targetJettySize;
    double segment = 30;                 // `segment` style (mxConstants.ENTITY_SEGMENT)
    double gridSize = 10;                // page grid size (Loop default size)
    bool orthogonalLoop = false;         // `orthogonalLoop=1`
    bool isSelfLoop = false;             // source and target are the same cell
    // Marker extents for the automatic jetty size (mxEdgeStyle.getJettySize,
    // mxEdgeStyle.js:981): startSize/endSize when the marker is not none,
    // else 0; plus the edge strokeWidth.
    double startMarkerSize = 0;
    double endMarkerSize = 0;
    double strokeWidth = 1;
};

struct EdgeRoute {
    // Absolute polyline: first = source terminal point, last = target
    // terminal point, waypoints/edge-style points in between. Consecutive
    // duplicates are removed. Fewer than 2 points means the edge cannot be
    // drawn (no terminals and no points).
    std::vector<Point> points;
    [[nodiscard]] bool valid() const { return points.size() >= 2; }
};

// Full pipeline, in the order of mxGraphView.updateEdgeState:
//  1. updateFixedTerminalPoints: constraint points (mxGraph.getConnectionPoint,
//     mxGraph.js:7413: fraction of bounds + dx/dy, flip, rotation, optional
//     projection onto the perimeter).
//  2. updatePoints: the edge style function (or Loop for self loops per
//     mxGraphView.isLoopStyleEnabled :3170) with the waypoints as hints;
//     Straight copies the waypoints.
//  3. updateFloatingTerminalPoints: ends without a constraint meet the
//     perimeter on the ray towards the next point (getFloatingTerminalPoint
//     :3460, getPerimeterPoint :3559 with rotation and perimeterSpacing).
//     An edge terminal attaches to the nearest point of its polyline.
[[nodiscard]] EdgeRoute routeEdge(const EdgeRouteRequest& request);

// The absolute point of a constraint on a terminal (step 1 alone), for
// connection-point display in M4.
[[nodiscard]] Point constraintPoint(const Terminal& terminal, const ConnectionConstraint& constraint);

} // namespace qraw::geometry
