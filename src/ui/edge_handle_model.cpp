#include "edge_handle_model.h"

#include "qraw/commands/queries.h"
#include "qraw/ui/interaction.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace qraw::ui::edgeh {

namespace {

double distanceToSegment(QPointF a, QPointF b, QPointF p)
{
    const QPointF ab = b - a;
    const double lengthSquared = ab.x() * ab.x() + ab.y() * ab.y();
    double t = 0;
    if (lengthSquared > 0)
        t = std::clamp(((p.x() - a.x()) * ab.x() + (p.y() - a.y()) * ab.y()) / lengthSquared, 0.0, 1.0);
    const QPointF closest = a + ab * t;
    return std::hypot(p.x() - closest.x(), p.y() - closest.y());
}

bool usesElbowHandle(geometry::EdgeStyleKind kind)
{
    // mxEdgeHandler.createHandler / Graph.createEdgeHandler use
    // mxElbowEdgeHandler for these connectors.
    return kind == geometry::EdgeStyleKind::Elbow || kind == geometry::EdgeStyleKind::SideToSide ||
           kind == geometry::EdgeStyleKind::TopToBottom;
}

// graph.createEdgeHandler: the connectors mxEdgeSegmentHandler serves.
bool usesSegmentHandles(const render::CellState& state)
{
    const geometry::EdgeStyleKind kind = state.style.edgeStyle();
    return !state.style.flag("noEdgeStyle", false) &&
           (kind == geometry::EdgeStyleKind::Orthogonal || kind == geometry::EdgeStyleKind::Segment);
}

QPointF midpoint(QPointF a, QPointF b)
{
    return (a + b) / 2;
}

} // namespace

std::vector<QPointF> segmentPoints(const std::vector<QPointF>& route)
{
    const std::size_t n = route.size();
    const auto near = [](double a, double b) { return std::abs(a - b) < 1; };
    if (n == 2 || (n == 3 && ((near(route[0].x(), route[1].x()) && near(route[1].x(), route[2].x())) ||
                              (near(route[0].y(), route[1].y()) && near(route[1].y(), route[2].y()))))) {
        const QPointF center = midpoint(route.front(), route.back());
        return {route.front(), center, center, route.back()};
    }
    return route;
}

double distanceToRoute(const std::vector<QPointF>& route, QPointF p)
{
    double best = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i + 1 < route.size(); ++i)
        best = std::min(best, distanceToSegment(route[i], route[i + 1], p));
    return best;
}

std::size_t nearestSegment(const std::vector<QPointF>& route, QPointF p)
{
    std::size_t best = 0;
    double bestDistance = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i + 1 < route.size(); ++i) {
        const double d = distanceToSegment(route[i], route[i + 1], p);
        if (d < bestDistance - 1e-9) { // ties keep the earlier segment
            bestDistance = d;
            best = i;
        }
    }
    return best;
}

std::size_t insertionIndex(const std::vector<QPointF>& route, const std::vector<QPointF>& waypoints,
                           std::size_t segment)
{
    std::size_t index = 0;
    for (const QPointF& waypoint : waypoints) {
        if (nearestSegment(route, waypoint) < segment)
            ++index;
    }
    return index;
}

std::optional<EdgeHandles> edgeHandlesFor(const core::Page& page, const render::CellState& state, double pixel)
{
    if (state.kind != render::CellState::Kind::Edge || state.cell == nullptr || state.points.size() < 2)
        return std::nullopt;

    EdgeHandles result;
    result.edge = state.id;
    for (const core::Point& p : state.points)
        result.route.emplace_back(p.x, p.y);
    result.origin = commands::origin(page, state.cell->parent());
    if (const core::Geometry* geometry = state.cell->geometry())
        result.points = geometry->points();
    for (const core::Point& p : result.points)
        result.waypoints.emplace_back(p.x + result.origin.x, p.y + result.origin.y);
    result.elbowStyle = usesElbowHandle(state.style.edgeStyle());
    result.segmentStyle = !result.elbowStyle && usesSegmentHandles(state);

    Handle source;
    source.kind = HandleKind::Source;
    source.position = result.route.front();
    source.connected = !state.cell->source().empty();
    Handle target;
    target.kind = HandleKind::Target;
    target.position = result.route.back();
    target.connected = !state.cell->target().empty();
    result.handles = {source, target};

    if (result.elbowStyle) {
        Handle elbow;
        elbow.kind = HandleKind::Elbow;
        if (!result.waypoints.empty()) {
            elbow.position = result.waypoints.front();
        } else {
            const std::size_t segment = (result.route.size() - 1) / 2;
            elbow.position = midpoint(result.route[segment], result.route[segment + 1]);
        }
        result.handles.push_back(elbow);
        return result;
    }

    if (result.segmentStyle) {
        const std::vector<QPointF> current = segmentPoints(result.route);
        const double minimumLength = 2 * overlay::kHandleSize * pixel;
        // A straight line's zero-length middle segment goes first so that it
        // wins the hit test against the half-line handles beside it.
        std::vector<std::size_t> order;
        for (std::size_t i = 0; i + 1 < current.size(); ++i)
            order.push_back(i);
        const bool straight = current.size() == 4 && current[1] == current[2];
        if (straight)
            std::swap(order[0], order[1]);
        for (const std::size_t i : order) {
            const QPointF a = current[i];
            const QPointF b = current[i + 1];
            const bool centerOfStraight = straight && i == 1;
            if (!centerOfStraight && std::hypot(b.x() - a.x(), b.y() - a.y()) < minimumLength)
                continue;
            Handle segment;
            segment.kind = HandleKind::Segment;
            segment.position = midpoint(a, b);
            segment.index = i;
            // Which way the segment runs (createBends, with the zero-length special case).
            bool vertical = std::round(a.x() - b.x()) == 0;
            if (std::round(a.y() - b.y()) == 0 && i + 2 < current.size())
                vertical = std::round(a.x() - current[i + 2].x()) == 0;
            segment.verticalSegment = vertical;
            result.handles.push_back(segment);
        }
        return result;
    }

    for (std::size_t i = 0; i < result.waypoints.size(); ++i) {
        Handle waypoint;
        waypoint.kind = HandleKind::Waypoint;
        waypoint.position = result.waypoints[i];
        waypoint.index = i;
        result.handles.push_back(waypoint);
    }
    const double minimumLength = 2 * overlay::kHandleSize * pixel;
    for (std::size_t segment = 0; segment + 1 < result.route.size(); ++segment) {
        const QPointF a = result.route[segment];
        const QPointF b = result.route[segment + 1];
        if (std::hypot(b.x() - a.x(), b.y() - a.y()) < minimumLength)
            continue;
        Handle virtualHandle;
        virtualHandle.kind = HandleKind::Virtual;
        virtualHandle.position = midpoint(a, b);
        virtualHandle.index = insertionIndex(result.route, result.waypoints, segment);
        result.handles.push_back(virtualHandle);
    }
    return result;
}

const Handle* hitHandle(const EdgeHandles& handles, QPointF p, double pixel)
{
    for (const Handle& handle : handles.handles) {
        const bool small = handle.kind == HandleKind::Virtual || handle.kind == HandleKind::Segment;
        const double radius = (small ? 5 : overlay::kHandleSize / 2 + 3) * pixel;
        if (std::abs(handle.position.x() - p.x()) <= radius && std::abs(handle.position.y() - p.y()) <= radius)
            return &handle;
    }
    return nullptr;
}

} // namespace qraw::ui::edgeh
