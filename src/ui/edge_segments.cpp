// Port of mxgraph/src/handler/mxEdgeSegmentHandler.js (draw.io, Apache-2.0), modified.
#include "edge_segments.h"

#include <cmath>

namespace qraw::ui::edgeh {

namespace {

// Math.round (halves round up) and mxEdgeHandler.roundLength.
double jsRound(double v)
{
    return std::floor(v + 0.5);
}

double roundLength(double v)
{
    return jsRound(v * 100) / 100;
}

bool sameRoutePoint(QPointF a, QPointF b)
{
    return std::abs(a.x() - b.x()) < 1 && std::abs(a.y() - b.y()) < 1;
}

// isSegmentPoint: `pt` lies between `p0` and `p1` on a horizontal or vertical line.
bool onSegment(QPointF p0, QPointF pt, QPointF p1)
{
    return (std::abs(p0.x() - pt.x()) < 1 && std::abs(pt.x() - p1.x()) < 1 &&
            (pt.y() - p0.y()) * (p1.y() - pt.y()) >= 0) ||
           (std::abs(p0.y() - pt.y()) < 1 && std::abs(pt.y() - p1.y()) < 1 &&
            (pt.x() - p0.x()) * (p1.x() - pt.x()) >= 0);
}

// getRoutePoints: no duplicates and no points inside a straight stretch.
std::vector<QPointF> routePoints(const std::vector<QPointF>& points)
{
    std::vector<QPointF> result;
    for (const QPointF& pt : points) {
        if (!result.empty() && sameRoutePoint(result.back(), pt))
            continue;
        while (result.size() > 1 && onSegment(result[result.size() - 2], result.back(), pt))
            result.pop_back();
        result.push_back(pt);
    }
    return result;
}

bool sameRoute(const std::vector<QPointF>& a, const std::vector<QPointF>& b)
{
    const std::vector<QPointF> pa = routePoints(a);
    const std::vector<QPointF> pb = routePoints(b);
    if (pa.size() != pb.size())
        return false;
    for (std::size_t i = 0; i < pa.size(); ++i) {
        if (!sameRoutePoint(pa[i], pb[i]))
            return false;
    }
    return true;
}

QPointF toQ(const core::Point& p)
{
    return {p.x, p.y};
}

std::vector<QPointF> toQ(const std::vector<core::Point>& points)
{
    std::vector<QPointF> result;
    for (const core::Point& p : points)
        result.push_back(toQ(p));
    return result;
}

bool contains(const core::Rect& r, QPointF p)
{
    return p.x() >= r.x && p.x() <= r.x + r.width && p.y() >= r.y && p.y() <= r.y + r.height;
}

class Dragger {
public:
    Dragger(const render::PageView& view, const render::CellState& edge, const EdgeHandles& handles)
        : view_(view), edge_(edge), handles_(handles), origin_(handles.origin.x, handles.origin.y)
    {
    }

    SegmentDrag run(std::size_t segment, QPointF pointer) const
    {
        const QPointF point = local(pointer);
        std::vector<QPointF> hints = previewPoints(segment + 1, point);

        std::vector<QPointF> route = routed(hints);
        if (route.size() < 2)
            return {handles_.points, handles_.route};

        // updatePreviewState: merges the points of the new route that lie on a
        // straight stretch, then the special cases of straight edges.
        hints = cornersOf(route);
        const bool straightRoute = jsRound(route.front().x() - route.back().x()) == 0 ||
                                   jsRound(route.front().y() - route.back().y()) == 0;
        if (hints.empty() && straightRoute) {
            hints = {point, point};
        } else if (route.size() == 5 && hints.size() == 2 && !edge_.cell->source().empty() &&
                   !edge_.cell->target().empty() && jsRound(handles_.route.front().x() - handles_.route.back().x()) == 0) {
            // A straight vertical edge turned into a Z: the two corners sit at
            // the heights of the terminals' routing centers.
            hints = {{point.x(), terminalY(true)}, {point.x(), terminalY(false)}};
        }
        route = routed(hints);
        if (route.size() < 2)
            return {handles_.points, handles_.route};

        SegmentDrag drag;
        drag.route = route;
        // Waypoints that route the same as none are dropped (isDefaultRoute).
        if (!hints.empty() && sameRoute(route, routed({})))
            hints.clear();
        for (const QPointF& p : hints)
            drag.points.push_back({p.x(), p.y()});
        return drag;
    }

private:
    QPointF local(QPointF absolute) const
    {
        return {roundLength(absolute.x() - origin_.x()), roundLength(absolute.y() - origin_.y())};
    }

    std::vector<QPointF> routed(const std::vector<QPointF>& hints) const
    {
        render::RouteOverride change;
        std::vector<core::Point> absolute;
        for (const QPointF& p : hints)
            absolute.push_back({p.x() + origin_.x(), p.y() + origin_.y()});
        change.waypoints = std::move(absolute);
        return toQ(view_.previewRoute(edge_, change));
    }

    // getPreviewPoints: the inner points of the current route with the
    // dragged segment's coordinates replaced by the pointer's. `bend` is the
    // index of the segment's end point in segmentPoints().
    std::vector<QPointF> previewPoints(std::size_t bend, QPointF point) const
    {
        const std::vector<QPointF> current = segmentPoints(handles_.route);
        std::vector<QPointF> result;
        QPointF last = local(current.front());
        for (std::size_t i = 1; i < current.size(); ++i) {
            QPointF pt = local(current[i]);
            if (i == bend) {
                // Tolerance for sub-pixel skew between terminal points and waypoints.
                const bool vertical = std::abs(last.x() - pt.x()) < 1;
                const bool horizontal = std::abs(last.y() - pt.y()) < 1;
                if (vertical) {
                    last.setX(point.x());
                    pt.setX(point.x());
                }
                if (horizontal) {
                    last.setY(point.y());
                    pt.setY(point.y());
                }
                // `last` is the previous inner point: it moves with the segment.
                if (i >= 2) {
                    if (vertical)
                        result[i - 2].setX(point.x());
                    if (horizontal)
                        result[i - 2].setY(point.y());
                }
            }
            if (i + 1 < current.size())
                result.push_back(pt);
            last = pt;
        }
        // A single point inside a terminal would collapse the route.
        if (result.size() == 1) {
            const QPointF absolute = result[0] + origin_;
            for (const bool source : {true, false}) {
                const render::CellState* terminal =
                    view_.find(source ? edge_.cell->source() : edge_.cell->target());
                if (terminal != nullptr && contains(terminal->bounds, absolute))
                    return {point, point};
            }
        }
        return result;
    }

    // The inner points of `route` that are corners, in local coordinates.
    std::vector<QPointF> cornersOf(const std::vector<QPointF>& route) const
    {
        std::vector<QPointF> result;
        QPointF p0 = route[0];
        QPointF p1 = route[1];
        for (std::size_t i = 2; i < route.size(); ++i) {
            const QPointF p2 = route[i];
            if ((jsRound(p0.x() - p1.x()) != 0 || jsRound(p1.x() - p2.x()) != 0) &&
                (jsRound(p0.y() - p1.y()) != 0 || jsRound(p1.y() - p2.y()) != 0))
                result.push_back(local(p1));
            p0 = p1;
            p1 = p2;
        }
        return result;
    }

    // The height of a terminal's fixed connection point, else of its center.
    double terminalY(bool source) const
    {
        if (const auto fixed = view_.fixedTerminalPoint(edge_, source))
            return local({fixed->x, fixed->y}).y();
        if (const render::CellState* terminal = view_.find(source ? edge_.cell->source() : edge_.cell->target()))
            return local({0, terminal->bounds.y + terminal->bounds.height / 2}).y();
        return 0;
    }

    const render::PageView& view_;
    const render::CellState& edge_;
    const EdgeHandles& handles_;
    QPointF origin_;
};

} // namespace

SegmentDrag dragSegment(const render::PageView& view, const render::CellState& edge, const EdgeHandles& handles,
                        std::size_t segment, QPointF pointer)
{
    return Dragger(view, edge, handles).run(segment, pointer);
}

} // namespace qraw::ui::edgeh
