// Port of mxGraphView.prototype.updateLineJumps / getLineJumpSegments
// (js/grapheditor/Graph.js, draw.io, Apache-2.0), modified: no bucket index,
// no test budget, view scale fixed at 1.

#include "qraw/geometry/jumps.h"

#include <algorithm>
#include <cmath>

namespace qraw::geometry {

namespace {

constexpr double kThreshold = 0.5; // 0.5 * view scale
constexpr double kEpsilon = 0.000001;

// mxUtils.ptSegDistSq.
double ptSegDistSq(Point a, Point b, Point p)
{
    double x2 = b.x - a.x;
    double y2 = b.y - a.y;
    double px = p.x - a.x;
    double py = p.y - a.y;
    double dot = px * x2 + py * y2;
    double projectedSq = 0;
    if (dot > 0) {
        px = x2 - px;
        py = y2 - py;
        dot = px * x2 + py * y2;
        if (dot > 0)
            projectedSq = dot * dot / (x2 * x2 + y2 * y2);
    }
    return std::max(0.0, px * px + py * py - projectedSq);
}

// mxUtils.ptLineDist (NaN for a zero-length line, which fails every `>` test).
double ptLineDist(Point a, Point b, Point p)
{
    return std::abs((b.y - a.y) * p.x - (b.x - a.x) * p.y + b.x * a.y - b.y * a.x) /
           std::sqrt((b.y - a.y) * (b.y - a.y) + (b.x - a.x) * (b.x - a.x));
}

// mxUtils.intersection: the crossing of two segments; parallel and collinear
// segments never cross (the ratios are NaN or infinite).
std::optional<Point> intersection(Point p0, Point p1, Point p2, Point p3)
{
    const double denom = (p3.y - p2.y) * (p1.x - p0.x) - (p3.x - p2.x) * (p1.y - p0.y);
    const double numeA = (p3.x - p2.x) * (p0.y - p2.y) - (p3.y - p2.y) * (p0.x - p2.x);
    const double numeB = (p1.x - p0.x) * (p0.y - p2.y) - (p1.y - p0.y) * (p0.x - p2.x);
    const double ua = numeA / denom;
    const double ub = numeB / denom;
    if (ua >= -kEpsilon && ua <= 1 + kEpsilon && ub >= -kEpsilon && ub <= 1 + kEpsilon)
        return Point{p0.x + ua * (p1.x - p0.x), p0.y + ua * (p1.y - p0.y)};
    return std::nullopt;
}

JumpBounds boundsOf(Point a, Point b)
{
    const double margin = 1e-5 * (std::abs(b.x - a.x) + std::abs(b.y - a.y)) + 1;
    return {std::min(a.x, b.x) - margin, std::max(a.x, b.x) + margin, std::min(a.y, b.y) - margin,
            std::max(a.y, b.y) + margin};
}

bool apart(const JumpBounds& a, const JumpBounds& b)
{
    return a.x0 > b.x1 || a.x1 < b.x0 || a.y0 > b.y1 || a.y1 < b.y0;
}

std::optional<Point> pointAt(std::span<const Point> points, std::size_t i)
{
    if (i < points.size())
        return points[i];
    return std::nullopt;
}

bool far(Point a, Point b)
{
    return std::abs(a.x - b.x) > kThreshold || std::abs(a.y - b.y) > kThreshold;
}

struct Crossing {
    double distanceSquared;
    Point point;
};

// The crossings of segment p0-p1 with `segment`, if any and not at a joint
// or on an overlapping neighbour.
std::optional<Point> crossing(Point p0, Point p1, const JumpSegment& segment)
{
    const auto pt = intersection(p0, p1, segment.p2, segment.p3);
    if (!pt || !far(*pt, p0) || !far(*pt, p1))
        return std::nullopt;
    // Overlapping incoming and outgoing segments.
    if (segment.previous && !(ptLineDist(p0, p1, *segment.previous) > kThreshold ||
                              ptLineDist(p0, p1, segment.p2) > kThreshold))
        return std::nullopt;
    if (segment.next && !(ptLineDist(p0, p1, *segment.next) > kThreshold ||
                          ptLineDist(p0, p1, segment.p3) > kThreshold))
        return std::nullopt;
    return pt;
}

} // namespace

JumpTarget makeJumpTarget(std::span<const Point> pts)
{
    JumpTarget target;
    std::optional<Point> previous;
    for (std::size_t j = 0; j + 1 < pts.size(); ++j) {
        Point p3 = pts[j + 1];
        const Point p2 = pts[j];
        std::optional<Point> next = pointAt(pts, j + 2);
        // Ignores waypoints on straight segments.
        while (j + 2 < pts.size() && ptSegDistSq(p2, *next, p3) < 1) {
            p3 = *next;
            ++j;
            next = pointAt(pts, j + 2);
        }
        const JumpBounds b = boundsOf(p2, p3);
        target.segments.push_back({p2, p3, previous, next, b});
        previous = p2;
        if (!target.bounds) {
            target.bounds = b;
        } else {
            target.bounds = JumpBounds{std::min(target.bounds->x0, b.x0), std::max(target.bounds->x1, b.x1),
                                       std::min(target.bounds->y0, b.y0), std::max(target.bounds->y1, b.y1)};
        }
    }
    return target;
}

std::vector<RoutedPoint> routeJumps(std::span<const Point> pts, std::span<const JumpTarget* const> earlier)
{
    std::vector<RoutedPoint> routed;
    bool any = false;
    for (std::size_t i = 0; i + 1 < pts.size(); ++i) {
        Point p1 = pts[i + 1];
        const Point p0 = pts[i];
        std::optional<Point> pn = pointAt(pts, i + 2);
        while (i + 2 < pts.size() && ptSegDistSq(p0, *pn, p1) < 1) {
            p1 = *pn;
            ++i;
            pn = pointAt(pts, i + 2);
        }
        routed.push_back({p0, false});

        const JumpBounds bounds = boundsOf(p0, p1);
        std::vector<Crossing> list;
        for (const JumpTarget* target : earlier) {
            if (!target->bounds || apart(bounds, *target->bounds))
                continue;
            for (const JumpSegment& segment : target->segments) {
                if (apart(bounds, segment.bounds))
                    continue;
                const auto pt = crossing(p0, p1, segment);
                if (!pt)
                    continue;
                const double dx = pt->x - p0.x;
                const double dy = pt->y - p0.y;
                const Crossing entry{dx * dx + dy * dy, *pt};
                // Ordered by distance from the start, after equal distances.
                const auto at = std::upper_bound(list.begin(), list.end(), entry, [](const Crossing& a, const Crossing& b) {
                    return a.distanceSquared < b.distanceSquared;
                });
                // Ignores crossings at segment joints and where overlapping
                // edges cross this segment.
                if ((at != list.begin() && !far(std::prev(at)->point, entry.point)) ||
                    (at != list.end() && !far(at->point, entry.point)))
                    continue;
                list.insert(at, entry);
            }
        }
        for (const Crossing& c : list) {
            routed.push_back({c.point, true});
            any = true;
        }
    }
    if (!any)
        return {};
    routed.push_back({pts.back(), false});
    return routed;
}

} // namespace qraw::geometry
