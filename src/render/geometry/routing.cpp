// Port of mxGraphView.updateEdgeState and its helpers (mxgraph/src/view/
// mxGraphView.js:2767-3560) and mxGraph.isOrthogonalEdgeStyle (draw.io,
// Apache-2.0), modified: view scale 1, translate 0, routing center 0, no
// ports, no label perimeter, no fixedPointSpacing. The edge styles live in
// routing_elbow.cpp and routing_orthogonal.cpp, the terminal geometry in
// routing_terminals.cpp.

#include "qraw/geometry/routing.h"

#include "routing_internal.h"

#include <algorithm>
#include <cmath>

namespace qraw::geometry {

EdgeStyleKind parseEdgeStyle(std::string_view value)
{
    if (value == "orthogonalEdgeStyle")
        return EdgeStyleKind::Orthogonal;
    if (value == "elbowEdgeStyle")
        return EdgeStyleKind::Elbow;
    if (value == "entityRelationEdgeStyle")
        return EdgeStyleKind::EntityRelation;
    if (value == "sideToSideEdgeStyle")
        return EdgeStyleKind::SideToSide;
    if (value == "topToBottomEdgeStyle")
        return EdgeStyleKind::TopToBottom;
    if (value == "segmentEdgeStyle")
        return EdgeStyleKind::Segment;
    if (value == "loopEdgeStyle")
        return EdgeStyleKind::Loop;
    return EdgeStyleKind::Straight; // none, isometricEdgeStyle, unknown
}

bool isOrthogonalStyle(EdgeStyleKind kind)
{
    switch (kind) {
    case EdgeStyleKind::Segment:
    case EdgeStyleKind::Elbow:
    case EdgeStyleKind::SideToSide:
    case EdgeStyleKind::TopToBottom:
    case EdgeStyleKind::EntityRelation:
    case EdgeStyleKind::Orthogonal:
        return true;
    case EdgeStyleKind::Straight:
    case EdgeStyleKind::Loop:
        break;
    }
    return false;
}

namespace {

using detail::OptPoint;
using detail::PointList;

bool finite(double v) { return std::isfinite(v); }
bool finite(Point p) { return finite(p.x) && finite(p.y); }
bool finite(const Rect& r) { return finite(r.x) && finite(r.y) && finite(r.width) && finite(r.height); }

bool finite(const Terminal& t)
{
    const bool constraintOk =
        !t.constraint || (finite(t.constraint->point) && finite(t.constraint->dx) && finite(t.constraint->dy));
    return finite(t.bounds) && finite(t.rotation) && finite(t.perimeterSpacing) && constraintOk &&
           std::all_of(t.edgePoints.begin(), t.edgePoints.end(), [](Point p) { return finite(p); });
}

bool finite(const EdgeRouteRequest& r)
{
    const bool numbers = finite(r.segment) && finite(r.gridSize) && finite(r.startMarkerSize) &&
                         finite(r.endMarkerSize) && finite(r.strokeWidth) &&
                         (!r.jettySize || finite(*r.jettySize)) &&
                         (!r.sourceJettySize || finite(*r.sourceJettySize)) &&
                         (!r.targetJettySize || finite(*r.targetJettySize));
    return numbers && (!r.source || finite(*r.source)) && (!r.target || finite(*r.target)) &&
           (!r.sourcePoint || finite(*r.sourcePoint)) && (!r.targetPoint || finite(*r.targetPoint)) &&
           std::all_of(r.waypoints.begin(), r.waypoints.end(), [](Point p) { return finite(p); });
}

Point centerOf(const Rect& r) { return {r.x + r.width / 2, r.y + r.height / 2}; }

// A constraint on an edge terminal is ignored (the terminal has no
// perimeter), as is one on a missing terminal.
bool hasConstraint(const Terminal* t) { return t && t->constraint && !t->isEdge(); }

// mxGraphView.updateFixedTerminalPoint / getFixedTerminalPoint, plus the
// fallback for an end with neither terminal nor point: the
// nearest waypoint.
OptPoint fixedTerminalPoint(const Terminal* terminal, const std::optional<Point>& danglingPoint,
                            const std::vector<Point>& waypoints, bool source)
{
    if (hasConstraint(terminal))
        return constraintPoint(*terminal, *terminal->constraint);
    if (terminal)
        return std::nullopt; // floating
    if (danglingPoint)
        return danglingPoint;
    if (!waypoints.empty())
        return source ? waypoints.front() : waypoints.back();
    return std::nullopt;
}

// mxGraphView.isLoopStyleEnabled and getEdgeStyle.
EdgeStyleKind effectiveStyle(const EdgeRouteRequest& request, const Terminal* source, const Terminal* target)
{
    const bool loop = request.isSelfLoop && source && request.waypoints.size() < 2 &&
                      (!request.orthogonalLoop || (!hasConstraint(source) && !hasConstraint(target)));
    if (loop)
        return EdgeStyleKind::Loop; // mxGraph.defaultLoopStyle
    return request.noEdgeStyle ? EdgeStyleKind::Straight : request.style;
}

// mxGraphView.updatePoints: runs the edge style. `pts` starts with p0 and
// ends with pe, like absolutePoints.
PointList updatePoints(const EdgeRouteRequest& request, EdgeStyleKind kind, const Terminal* source,
                       const Terminal* target, const OptPoint& p0, const OptPoint& pe)
{
    PointList pts;
    pts.emplace_back(p0);

    const bool sourceKnown = source || p0;
    const bool targetKnown = target || pe;
    detail::StyleInput in{request, source, target, p0, pe, request.waypoints};

    if (kind == EdgeStyleKind::Straight || !sourceKnown || !targetKnown) {
        // Straight routes copy the waypoints. An end that has neither a
        // terminal nor a point cannot be handed to a style (upstream would
        // drop such an edge); the waypoints are the best remaining route.
        for (const Point& p : request.waypoints)
            pts.emplace_back(p);
    } else {
        switch (kind) {
        case EdgeStyleKind::Orthogonal: detail::orthConnector(in, pts); break;
        case EdgeStyleKind::Elbow: detail::elbowConnector(in, pts); break;
        case EdgeStyleKind::EntityRelation: detail::entityRelation(in, pts); break;
        case EdgeStyleKind::SideToSide: detail::sideToSide(in, pts); break;
        case EdgeStyleKind::TopToBottom: detail::topToBottom(in, pts); break;
        case EdgeStyleKind::Segment: detail::segmentConnector(in, pts); break;
        case EdgeStyleKind::Loop: detail::loopStyle(in, pts); break;
        case EdgeStyleKind::Straight: break;
        }
    }
    pts.emplace_back(pe);
    return pts;
}

// mxGraphView.getNextPoint
OptPoint nextPoint(const PointList& pts, const Terminal* opposite, bool source)
{
    OptPoint point;
    const std::size_t count = pts.size();
    if (count >= 2)
        point = pts[source ? std::min<std::size_t>(1, count - 1) : std::max<std::size_t>(0, count - 2)];
    if (!point && opposite)
        point = centerOf(opposite->bounds);
    return point;
}

// mxGraphView.updateFloatingTerminalPoint / getFloatingTerminalPoint.
Point floatingTerminalPoint(const Terminal& start, const Terminal* opposite, const PointList& pts, bool source,
                            bool orthogonal)
{
    const OptPoint next = nextPoint(pts, opposite, source);
    if (!next)
        return centerOf(start.bounds);
    if (start.isEdge())
        return detail::nearestPolylinePoint(start.edgePoints, *next);
    return detail::floatingVertexPoint(start, *next, orthogonal);
}

// mxGraphView.updateFloatingTerminalPoints
void updateFloatingTerminalPoints(PointList& pts, const Terminal* source, const Terminal* target, bool orthogonal)
{
    const bool needTarget = !pts.back() && target;
    const bool needSource = !pts.front() && source;
    if (needTarget)
        pts.back() = floatingTerminalPoint(*target, source, pts, false, orthogonal);
    if (needSource)
        pts.front() = floatingTerminalPoint(*source, target, pts, true, orthogonal);
}

// mxGraphView.removeDuplicatePoints / isDuplicatePointsRemoved
void removeDuplicatePoints(PointList& pts, EdgeStyleKind kind, bool hasWaypoints)
{
    if (pts.size() <= 2)
        return;
    constexpr double tol = 0.01;
    auto equals = [](const OptPoint& a, const OptPoint& b) {
        return a && b && std::abs(a->x - b->x) < tol && std::abs(a->y - b->y) < tol;
    };
    bool duplicates = false;
    for (std::size_t i = 1; i < pts.size() && !duplicates; ++i)
        duplicates = equals(pts[i - 1], pts[i]);

    const bool removable = (kind == EdgeStyleKind::Orthogonal && !hasWaypoints) || kind == EdgeStyleKind::Elbow ||
                           kind == EdgeStyleKind::SideToSide || kind == EdgeStyleKind::TopToBottom ||
                           kind == EdgeStyleKind::EntityRelation;
    if (!duplicates || !removable)
        return;

    for (auto i = static_cast<std::ptrdiff_t>(pts.size()) - 2; i > 0 && pts.size() > 2; --i) {
        const auto idx = static_cast<std::size_t>(i);
        if (equals(pts[idx], pts[idx + 1]) || equals(pts[idx], pts[idx - 1]))
            pts.erase(pts.begin() + i);
    }
}

} // namespace

EdgeRoute routeEdge(const EdgeRouteRequest& request)
{
    if (!finite(request))
        return {};

    const Terminal* source = request.source ? &*request.source : nullptr;
    const Terminal* target = request.target ? &*request.target : nullptr;

    // 1. updateFixedTerminalPoints
    const OptPoint p0 = fixedTerminalPoint(source, request.sourcePoint, request.waypoints, true);
    const OptPoint pe = fixedTerminalPoint(target, request.targetPoint, request.waypoints, false);

    // 2. updatePoints
    const EdgeStyleKind kind = effectiveStyle(request, source, target);
    PointList pts = updatePoints(request, kind, source, target, p0, pe);

    // 3. updateFloatingTerminalPoints; graph.isOrthogonal(edge) decides the
    // projection (without the loop special case).
    const bool orthogonal =
        request.orthogonal.value_or(isOrthogonalStyle(request.noEdgeStyle ? EdgeStyleKind::Straight : request.style));
    updateFloatingTerminalPoints(pts, source, target, orthogonal);

    removeDuplicatePoints(pts, kind, !request.waypoints.empty());

    // An end that is still open has neither terminal, point nor waypoint:
    // it collapses onto the other end.
    if (!pts.front())
        pts.front() = pts.back();
    if (!pts.back())
        pts.back() = pts.front();
    if (!pts.front() || !pts.back())
        return {};

    EdgeRoute route;
    constexpr double tol = 1e-6;
    for (const OptPoint& p : pts) {
        if (!p || !finite(*p))
            return {};
        if (!route.points.empty() && std::abs(route.points.back().x - p->x) < tol &&
            std::abs(route.points.back().y - p->y) < tol)
            continue;
        route.points.push_back(*p);
    }
    if (!route.valid())
        route.points.clear();
    return route;
}

} // namespace qraw::geometry
