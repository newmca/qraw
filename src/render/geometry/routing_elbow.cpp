// Port of mxgraph/src/view/mxEdgeStyle.js (EntityRelation, Loop) and of the
// draw.io overrides SideToSide, TopToBottom and ElbowConnector in
// js/grapheditor/Graph.js (draw.io, Apache-2.0), modified: view scale 1,
// translate 0, routing center 0, no cell geometry (so the `relative` port
// special case of EntityRelation is not modeled).
//
// As upstream, `result` already holds absolutePoints[0] as its first entry,
// which is why "result.size() == 1" below means "no inner point added yet".

#include "routing_internal.h"

#include <algorithm>
#include <cmath>

namespace qraw::geometry::detail {

namespace {

// mxUtils.contains
bool contains(const Rect& b, double x, double y)
{
    return b.x <= x && b.x + b.width >= x && b.y <= y && b.y + b.height >= y;
}

double centerX(const Rect& r) { return r.x + r.width / 2; }
double centerY(const Rect& r) { return r.y + r.height / 2; }

// The state mxEdgeStyle puts in place of a terminal that has a fixed point.
Rect pointState(Point p) { return {p.x, p.y, 0, 0}; }

std::optional<Rect> boundsOf(const Terminal* t)
{
    if (!t)
        return std::nullopt;
    return t->bounds;
}

std::optional<Point> firstHint(const std::vector<Point>& hints)
{
    if (hints.empty())
        return std::nullopt;
    return hints.front();
}

} // namespace

void entityRelation(const StyleInput& in, PointList& result)
{
    const double segment = in.request.segment;
    std::optional<Rect> source = boundsOf(in.source);
    std::optional<Rect> target = boundsOf(in.target);

    bool isSourceLeft = false;
    if (source && target)
        isSourceLeft = (in.pe ? in.pe->x : target->x + target->width) < (in.p0 ? in.p0->x : source->x);

    if (in.p0) {
        source = pointState(*in.p0);
    } else if (in.source) {
        const int constraint = portConstraints(*in.source, kMaskNone);
        if (constraint != kMaskNone && constraint != kMaskWest + kMaskEast)
            isSourceLeft = constraint == kMaskWest;
    } else {
        return;
    }

    bool isTargetLeft = true;
    if (target && source)
        isTargetLeft = (in.p0 ? in.p0->x : source->x + source->width) < (in.pe ? in.pe->x : target->x);

    if (in.pe) {
        target = pointState(*in.pe);
    } else if (in.target) {
        const int constraint = portConstraints(*in.target, kMaskNone);
        if (constraint != kMaskNone && constraint != kMaskWest + kMaskEast)
            isTargetLeft = constraint == kMaskWest;
    }

    if (!source || !target)
        return;

    const double x0 = isSourceLeft ? source->x : source->x + source->width;
    const double y0 = centerY(*source);
    const double xe = isTargetLeft ? target->x : target->x + target->width;
    const double ye = centerY(*target);

    const Point dep{x0 + (isSourceLeft ? -segment : segment), y0};
    const Point arr{xe + (isTargetLeft ? -segment : segment), ye};

    if (isSourceLeft == isTargetLeft) {
        // Both go out on the same side.
        const double x = isSourceLeft ? std::min(x0, xe) - segment : std::max(x0, xe) + segment;
        result.emplace_back(Point{x, y0});
        result.emplace_back(Point{x, ye});
    } else if ((dep.x < arr.x) == isSourceLeft) {
        const double midY = y0 + (ye - y0) / 2;
        result.emplace_back(dep);
        result.emplace_back(Point{dep.x, midY});
        result.emplace_back(Point{arr.x, midY});
        result.emplace_back(arr);
    } else {
        result.emplace_back(dep);
        result.emplace_back(arr);
    }
}

void loopStyle(const StyleInput& in, PointList& result)
{
    if (in.p0 && in.pe) {
        for (const Point& pt : in.hints)
            result.emplace_back(pt);
        return;
    }
    if (!in.source)
        return;

    const Rect source = in.source->bounds;
    std::optional<Point> pt = firstHint(in.hints);
    if (pt && contains(source, pt->x, pt->y))
        pt.reset();

    // `segment` defaults to the grid size for loops. The request cannot say
    // whether the style set it, so the entity default (30) counts as unset.
    const double seg = in.request.segment != 30 ? in.request.segment : in.request.gridSize;
    // The `direction` style of the edge is not part of the request: west.
    const Direction dir = Direction::West;

    double x = 0;
    double dx = 0;
    double y = 0;
    double dy = 0;
    if (dir == Direction::North || dir == Direction::South) {
        x = centerX(source);
        dx = seg;
    } else {
        y = centerY(source);
        dy = seg;
    }

    if (!pt || pt->x < source.x || pt->x > source.x + source.width) {
        if (pt) {
            x = pt->x;
            dy = std::max(std::abs(y - pt->y), dy);
        } else if (dir == Direction::North) {
            y = source.y - 2 * dx;
        } else if (dir == Direction::South) {
            y = source.y + source.height + 2 * dx;
        } else if (dir == Direction::East) {
            x = source.x - 2 * dy;
        } else {
            x = source.x + source.width + 2 * dy;
        }
    } else {
        x = centerX(source);
        dx = std::max(std::abs(x - pt->x), dy);
        y = pt->y;
        dy = 0;
    }

    result.emplace_back(Point{x - dx, y - dy});
    result.emplace_back(Point{x + dx, y + dy});
}

void sideToSide(const StyleInput& in, PointList& result)
{
    const std::optional<Point> pt = firstHint(in.hints);
    std::optional<Rect> source = boundsOf(in.source);
    std::optional<Rect> target = boundsOf(in.target);
    if (in.p0)
        source = pointState(*in.p0);
    if (in.pe)
        target = pointState(*in.pe);
    if (!source || !target)
        return;

    const double l = std::max(source->x, target->x);
    const double r = std::min(source->x + source->width, target->x + target->width);
    const double x = pt ? pt->x : jsRound(r + (l - r) / 2);
    double y1 = centerY(*source);
    double y2 = centerY(*target);

    if (pt) {
        // Waypoints level with a terminal give the y of that end.
        if (pt->y >= source->y && pt->y <= source->y + source->height)
            y1 = pt->y;
        if (pt->y >= target->y && pt->y <= target->y + target->height)
            y2 = pt->y;
    }

    if (!contains(*target, x, y1) && !contains(*source, x, y1))
        result.emplace_back(Point{x, y1});
    if (!contains(*target, x, y2) && !contains(*source, x, y2))
        result.emplace_back(Point{x, y2});

    if (result.size() == 1) {
        if (pt) {
            if (!contains(*target, x, pt->y) && !contains(*source, x, pt->y))
                result.emplace_back(Point{x, pt->y});
        } else {
            const double t = std::max(source->y, target->y);
            const double b = std::min(source->y + source->height, target->y + target->height);
            result.emplace_back(Point{x, t + (b - t) / 2});
        }
    }
}

void topToBottom(const StyleInput& in, PointList& result)
{
    const std::optional<Point> pt = firstHint(in.hints);
    std::optional<Rect> source = boundsOf(in.source);
    std::optional<Rect> target = boundsOf(in.target);
    if (in.p0)
        source = pointState(*in.p0);
    if (in.pe)
        target = pointState(*in.pe);
    if (!source || !target)
        return;

    const double t = std::max(source->y, target->y);
    const double b = std::min(source->y + source->height, target->y + target->height);
    double x = centerX(*source);

    if (pt && pt->x >= source->x && pt->x <= source->x + source->width)
        x = pt->x;

    const double y = pt ? pt->y : jsRound(b + (t - b) / 2);
    if (!contains(*target, x, y) && !contains(*source, x, y))
        result.emplace_back(Point{x, y});

    if (pt && pt->x >= target->x && pt->x <= target->x + target->width)
        x = pt->x;
    else
        x = centerX(*target);

    if (!contains(*target, x, y) && !contains(*source, x, y))
        result.emplace_back(Point{x, y});

    if (result.size() == 1) {
        if (pt) {
            if (!contains(*target, pt->x, y) && !contains(*source, pt->x, y))
                result.emplace_back(Point{pt->x, y});
        } else {
            const double l = std::max(source->x, target->x);
            const double r = std::min(source->x + source->width, target->x + target->width);
            result.emplace_back(Point{l + (r - l) / 2, y});
        }
    }
}

void elbowConnector(const StyleInput& in, PointList& result)
{
    const std::optional<Point> pt = firstHint(in.hints);
    bool vertical = false;
    bool horizontal = false;

    if (in.source && in.target) {
        const Rect& s = in.source->bounds;
        const Rect& t = in.target->bounds;
        if (pt) {
            const double left = std::min(s.x, t.x);
            const double right = std::max(s.x + s.width, t.x + t.width);
            const double top = std::min(s.y, t.y);
            const double bottom = std::max(s.y + s.height, t.y + t.height);
            vertical = pt->y < top || pt->y > bottom;
            horizontal = pt->x < left || pt->x > right;
        } else {
            const double left = std::max(s.x, t.x);
            const double right = std::min(s.x + s.width, t.x + t.width);
            vertical = left == right;
            if (!vertical) {
                const double top = std::max(s.y, t.y);
                const double bottom = std::min(s.y + s.height, t.y + t.height);
                horizontal = top == bottom;
            }
        }
    }

    if (!horizontal && (vertical || in.request.elbow == ElbowAxis::Vertical))
        topToBottom(in, result);
    else
        sideToSide(in, result);
}

} // namespace qraw::geometry::detail
