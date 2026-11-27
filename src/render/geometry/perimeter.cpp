// Port of mxgraph/src/view/mxPerimeter.js and the perimeters in
// js/grapheditor/Shapes.js (draw.io, Apache-2.0), modified: the functions take
// a plain PerimeterShape instead of a vertex state, and run at view scale 1.

#include "qraw/geometry/perimeter.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <numbers>
#include <vector>

namespace qraw::geometry {

namespace {

constexpr double kPi = std::numbers::pi;

// mxUtils.intersection: intersection of the segments (p0,p1) and (p2,p3).
std::optional<Point> intersection(double x0, double y0, double x1, double y1, double x2, double y2, double x3,
                                  double y3)
{
    const double denom = ((y3 - y2) * (x1 - x0)) - ((x3 - x2) * (y1 - y0));
    const double numeA = ((x3 - x2) * (y0 - y2)) - ((y3 - y2) * (x0 - x2));
    const double numeB = ((x1 - x0) * (y0 - y2)) - ((y1 - y0) * (x0 - x2));
    const double ua = numeA / denom;
    const double ub = numeB / denom;
    constexpr double eps = 0.000001;
    // NaN (parallel and coincident segments) fails every comparison: no hit.
    if (ua >= -eps && ua <= 1.0 + eps && ub >= -eps && ub <= 1.0 + eps)
        return Point{x0 + ua * (x1 - x0), y0 + ua * (y1 - y0)};
    return std::nullopt;
}

std::optional<Point> intersection(Point a, Point b, Point c, Point d)
{
    return intersection(a.x, a.y, b.x, b.y, c.x, c.y, d.x, d.y);
}

Point center(const Rect& b) { return {b.x + b.width / 2, b.y + b.height / 2}; }

bool isVertical(Direction d) { return d == Direction::North || d == Direction::South; }

// ---------------------------------------------------------------- rectangle

Point rectanglePerimeter(const Rect& bounds, Point next, bool orthogonal)
{
    const double cx = bounds.x + bounds.width / 2;
    const double cy = bounds.y + bounds.height / 2;
    const double dx = next.x - cx;
    const double dy = next.y - cy;
    const double alpha = std::atan2(dy, dx);
    Point p;
    const double pi2 = kPi / 2;
    const double beta = pi2 - alpha;
    const double t = std::atan2(bounds.height, bounds.width);

    if (alpha < -kPi + t || alpha > kPi - t) {
        p.x = bounds.x; // left edge
        p.y = cy - bounds.width * std::tan(alpha) / 2;
    } else if (alpha < -t) {
        p.y = bounds.y; // top edge
        p.x = cx - bounds.height * std::tan(beta) / 2;
    } else if (alpha < t) {
        p.x = bounds.x + bounds.width; // right edge
        p.y = cy + bounds.width * std::tan(alpha) / 2;
    } else {
        p.y = bounds.y + bounds.height; // bottom edge
        p.x = cx + bounds.height * std::tan(beta) / 2;
    }

    if (orthogonal) {
        if (next.x >= bounds.x && next.x <= bounds.x + bounds.width)
            p.x = next.x;
        else if (next.y >= bounds.y && next.y <= bounds.y + bounds.height)
            p.y = next.y;
        if (next.x < bounds.x)
            p.x = bounds.x;
        else if (next.x > bounds.x + bounds.width)
            p.x = bounds.x + bounds.width;
        if (next.y < bounds.y)
            p.y = bounds.y;
        else if (next.y > bounds.y + bounds.height)
            p.y = bounds.y + bounds.height;
    }
    return p;
}

// ------------------------------------------------------------------ ellipse

Point ellipsePerimeter(const Rect& bounds, Point next, bool orthogonal)
{
    const double x = bounds.x;
    const double y = bounds.y;
    const double a = bounds.width / 2;
    const double b = bounds.height / 2;
    const double cx = x + a;
    const double cy = y + b;
    const double px = next.x;
    const double py = next.y;

    // Upstream truncates the direction to whole model units (parseInt).
    const double dx = std::trunc(px - cx);
    const double dy = std::trunc(py - cy);

    if (dx == 0 && dy != 0)
        return {cx, cy + b * dy / std::abs(dy)};
    if (dx == 0 && dy == 0)
        return {px, py};

    if (orthogonal) {
        if (py >= y && py <= y + bounds.height) {
            const double ty = py - cy;
            double tx = std::sqrt(a * a * (1 - (ty * ty) / (b * b)));
            if (std::isnan(tx))
                tx = 0;
            if (px <= x)
                tx = -tx;
            return {cx + tx, py};
        }
        if (px >= x && px <= x + bounds.width) {
            const double tx = px - cx;
            double ty = std::sqrt(b * b * (1 - (tx * tx) / (a * a)));
            if (std::isnan(ty))
                ty = 0;
            if (py <= y)
                ty = -ty;
            return {px, cy + ty};
        }
    }

    const double t = a * b / std::sqrt(b * b * dx * dx + a * a * dy * dy);
    return {cx + t * dx, cy + t * dy};
}

// ------------------------------------------------------------------ rhombus

Point rhombusPerimeter(const Rect& bounds, Point next, bool orthogonal)
{
    const double x = bounds.x;
    const double y = bounds.y;
    const double w = bounds.width;
    const double h = bounds.height;
    const double cx = x + w / 2;
    const double cy = y + h / 2;
    const double px = next.x;
    const double py = next.y;

    // Special cases: the diamond's corners.
    if (cx == px)
        return cy > py ? Point{cx, y} : Point{cx, y + h};
    if (cy == py)
        return cx > px ? Point{x, cy} : Point{x + w, cy};

    double tx = cx;
    double ty = cy;
    if (orthogonal) {
        if (px >= x && px <= x + w)
            tx = px;
        else if (py >= y && py <= y + h)
            ty = py;
    }

    // The quadrant selects the border line.
    std::optional<Point> r;
    if (px < cx)
        r = py < cy ? intersection(px, py, tx, ty, cx, y, x, cy) : intersection(px, py, tx, ty, cx, y + h, x, cy);
    else if (py < cy)
        r = intersection(px, py, tx, ty, cx, y, x + w, cy);
    else
        r = intersection(px, py, tx, ty, cx, y + h, x + w, cy);
    // Upstream returns null here, which later callers replace by the center.
    return r.value_or(Point{cx, cy});
}

// ----------------------------------------------------------------- triangle

Point trianglePerimeter(const Rect& bounds, Direction direction, Point next, bool orthogonal)
{
    const bool vertical = isVertical(direction);
    const double x = bounds.x;
    const double y = bounds.y;
    const double w = bounds.width;
    const double h = bounds.height;
    double cx = x + w / 2;
    double cy = y + h / 2;

    Point start{x, y};
    Point corner{x + w, cy};
    Point end{x, y + h};

    if (direction == Direction::North) {
        start = end;
        corner = {cx, y};
        end = {x + w, y + h};
    } else if (direction == Direction::South) {
        corner = {cx, y + h};
        end = {x + w, y};
    } else if (direction == Direction::West) {
        start = {x + w, y};
        corner = {x, cy};
        end = {x + w, y + h};
    }

    double dx = next.x - cx;
    double dy = next.y - cy;
    const double alpha = vertical ? std::atan2(dx, dy) : std::atan2(dy, dx);
    const double t = vertical ? std::atan2(w, h) : std::atan2(h, w);

    bool base;
    if (direction == Direction::North || direction == Direction::West)
        base = alpha > -t && alpha < t;
    else
        base = alpha < -kPi + t || alpha > kPi - t;

    std::optional<Point> result;
    if (base) {
        if (orthogonal && ((vertical && next.x >= start.x && next.x <= end.x) ||
                           (!vertical && next.y >= start.y && next.y <= end.y))) {
            result = vertical ? Point{next.x, start.y} : Point{start.x, next.y};
        } else if (direction == Direction::North) {
            result = Point{x + w / 2 + h * std::tan(alpha) / 2, y + h};
        } else if (direction == Direction::South) {
            result = Point{x + w / 2 - h * std::tan(alpha) / 2, y};
        } else if (direction == Direction::West) {
            result = Point{x + w, y + h / 2 + w * std::tan(alpha) / 2};
        } else {
            result = Point{x, y + h / 2 - w * std::tan(alpha) / 2};
        }
    } else {
        if (orthogonal) {
            Point pt{cx, cy};
            if (next.y >= y && next.y <= y + h) {
                pt.x = vertical ? cx : (direction == Direction::West ? x + w : x);
                pt.y = next.y;
            } else if (next.x >= x && next.x <= x + w) {
                pt.x = next.x;
                pt.y = !vertical ? cy : (direction == Direction::North ? y + h : y);
            }
            dx = next.x - pt.x;
            dy = next.y - pt.y;
            cx = pt.x;
            cy = pt.y;
        }

        if ((vertical && next.x <= x + w / 2) || (!vertical && next.y <= y + h / 2))
            result = intersection(next.x, next.y, cx, cy, start.x, start.y, corner.x, corner.y);
        else
            result = intersection(next.x, next.y, cx, cy, corner.x, corner.y, end.x, end.y);
    }
    return result.value_or(Point{cx, cy});
}

// ------------------------------------------------------------------ hexagon

// mxPerimeter.getHexagonPerimeterPoint. Quirks (the stray `py > cy` in the
// horizontal bottom corner case, the unset a/b of uncovered orthogonal
// sectors) are kept; they only matter in degenerate positions.
Point hexagonPerimeter(const Rect& bounds, Direction direction, Point next, bool orthogonal)
{
    const double x = bounds.x;
    const double y = bounds.y;
    const double w = bounds.width;
    const double h = bounds.height;
    const double cx = x + w / 2;
    const double cy = y + h / 2;
    const double px = next.x;
    const double py = next.y;
    const double dx = px - cx;
    const double dy = py - cy;
    const double alpha = -std::atan2(dy, dx);
    const double pi2 = kPi / 2;
    const bool vertical = isVertical(direction);
    Point a;
    Point b;
    std::optional<Point> result = Point{cx, cy};
    auto fl = [](double v) { return std::floor(v); };

    // Only the correct quadrants are considered for the orthogonal case.
    if ((px < x && py < y) || (px < x && py > y + h) || (px > x + w && py < y) || (px > x + w && py > y + h))
        orthogonal = false;

    if (orthogonal) {
        if (vertical) {
            // Special cases where the line meets the hexagon corners.
            if (px == cx) {
                if (py <= y)
                    return {cx, y};
                if (py >= y + h)
                    return {cx, y + h};
            } else if (px < x) {
                if (py == y + h / 4)
                    return {x, y + h / 4};
                if (py == y + 3 * h / 4)
                    return {x, y + 3 * h / 4};
            } else if (px > x + w) {
                if (py == y + h / 4)
                    return {x + w, y + h / 4};
                if (py == y + 3 * h / 4)
                    return {x + w, y + 3 * h / 4};
            } else if (px == x) {
                if (py < cy)
                    return {x, y + h / 4};
                if (py > cy)
                    return {x, y + 3 * h / 4};
            } else if (px == x + w) {
                if (py < cy)
                    return {x + w, y + h / 4};
                if (py > cy)
                    return {x + w, y + 3 * h / 4};
            }
            if (py == y)
                return {cx, y};
            if (py == y + h)
                return {cx, y + h};

            if (px < cx) {
                if (py > y + h / 4 && py < y + 3 * h / 4) {
                    a = {x, y};
                    b = {x, y + h};
                } else if (py < y + h / 4) {
                    a = {x - fl(0.5 * w), y + fl(0.5 * h)};
                    b = {x + w, y - fl(0.25 * h)};
                } else if (py > y + 3 * h / 4) {
                    a = {x - fl(0.5 * w), y + fl(0.5 * h)};
                    b = {x + w, y + fl(1.25 * h)};
                }
            } else if (px > cx) {
                if (py > y + h / 4 && py < y + 3 * h / 4) {
                    a = {x + w, y};
                    b = {x + w, y + h};
                } else if (py < y + h / 4) {
                    a = {x, y - fl(0.25 * h)};
                    b = {x + fl(1.5 * w), y + fl(0.5 * h)};
                } else if (py > y + 3 * h / 4) {
                    a = {x + fl(1.5 * w), y + fl(0.5 * h)};
                    b = {x, y + fl(1.25 * h)};
                }
            }
        } else {
            if (py == cy) {
                if (px <= x)
                    return {x, y + h / 2};
                if (px >= x + w)
                    return {x + w, y + h / 2};
            } else if (py < y) {
                if (px == x + w / 4)
                    return {x + w / 4, y};
                if (px == x + 3 * w / 4)
                    return {x + 3 * w / 4, y};
            } else if (py > y + h) {
                if (px == x + w / 4)
                    return {x + w / 4, y + h};
                if (px == x + 3 * w / 4)
                    return {x + 3 * w / 4, y + h};
            } else if (py == y) {
                if (px < cx)
                    return {x + w / 4, y};
                if (px > cx)
                    return {x + 3 * w / 4, y};
            } else if (py == y + h) {
                if (px < cx)
                    return {x + w / 4, y + h};
                if (py > cy) // sic (upstream)
                    return {x + 3 * w / 4, y + h};
            }
            if (px == x)
                return {x, cy};
            if (px == x + w)
                return {x + w, cy};

            if (py < cy) {
                if (px > x + w / 4 && px < x + 3 * w / 4) {
                    a = {x, y};
                    b = {x + w, y};
                } else if (px < x + w / 4) {
                    a = {x - fl(0.25 * w), y + h};
                    b = {x + fl(0.5 * w), y - fl(0.5 * h)};
                } else if (px > x + 3 * w / 4) {
                    a = {x + fl(0.5 * w), y - fl(0.5 * h)};
                    b = {x + fl(1.25 * w), y + h};
                }
            } else if (py > cy) {
                if (px > x + w / 4 && px < x + 3 * w / 4) {
                    a = {x, y + h};
                    b = {x + w, y + h};
                } else if (px < x + w / 4) {
                    a = {x - fl(0.25 * w), y};
                    b = {x + fl(0.5 * w), y + fl(1.5 * h)};
                } else if (px > x + 3 * w / 4) {
                    a = {x + fl(0.5 * w), y + fl(1.5 * h)};
                    b = {x + fl(1.25 * w), y};
                }
            }
        }

        double tx = cx;
        double ty = cy;
        if (px >= x && px <= x + w) {
            tx = px;
            ty = py < cy ? y + h : y;
        } else if (py >= y && py <= y + h) {
            ty = py;
            tx = px < cx ? x + w : x;
        }
        result = intersection(tx, ty, next.x, next.y, a.x, a.y, b.x, b.y);
    } else {
        if (vertical) {
            const double beta = std::atan2(h / 4, w / 2);
            // Special cases where the line meets the hexagon corners.
            if (alpha == beta)
                return {x + w, y + fl(0.25 * h)};
            if (alpha == pi2)
                return {x + fl(0.5 * w), y};
            if (alpha == kPi - beta)
                return {x, y + fl(0.25 * h)};
            if (alpha == -beta)
                return {x + w, y + fl(0.75 * h)};
            if (alpha == -pi2)
                return {x + fl(0.5 * w), y + h};
            if (alpha == -kPi + beta)
                return {x, y + fl(0.75 * h)};

            if (alpha < beta && alpha > -beta) {
                a = {x + w, y};
                b = {x + w, y + h};
            } else if (alpha > beta && alpha < pi2) {
                a = {x, y - fl(0.25 * h)};
                b = {x + fl(1.5 * w), y + fl(0.5 * h)};
            } else if (alpha > pi2 && alpha < kPi - beta) {
                a = {x - fl(0.5 * w), y + fl(0.5 * h)};
                b = {x + w, y - fl(0.25 * h)};
            } else if ((alpha > kPi - beta && alpha <= kPi) || (alpha < -kPi + beta && alpha >= -kPi)) {
                a = {x, y};
                b = {x, y + h};
            } else if (alpha < -beta && alpha > -pi2) {
                a = {x + fl(1.5 * w), y + fl(0.5 * h)};
                b = {x, y + fl(1.25 * h)};
            } else if (alpha < -pi2 && alpha > -kPi + beta) {
                a = {x - fl(0.5 * w), y + fl(0.5 * h)};
                b = {x + w, y + fl(1.25 * h)};
            }
        } else {
            const double beta = std::atan2(h / 2, w / 4);
            if (alpha == beta)
                return {x + fl(0.75 * w), y};
            if (alpha == kPi - beta)
                return {x + fl(0.25 * w), y};
            if (alpha == kPi || alpha == -kPi)
                return {x, y + fl(0.5 * h)};
            if (alpha == 0)
                return {x + w, y + fl(0.5 * h)};
            if (alpha == -beta)
                return {x + fl(0.75 * w), y + h};
            if (alpha == -kPi + beta)
                return {x + fl(0.25 * w), y + h};

            if (alpha > 0 && alpha < beta) {
                a = {x + fl(0.5 * w), y - fl(0.5 * h)};
                b = {x + fl(1.25 * w), y + h};
            } else if (alpha > beta && alpha < kPi - beta) {
                a = {x, y};
                b = {x + w, y};
            } else if (alpha > kPi - beta && alpha < kPi) {
                a = {x - fl(0.25 * w), y + h};
                b = {x + fl(0.5 * w), y - fl(0.5 * h)};
            } else if (alpha < 0 && alpha > -beta) {
                a = {x + fl(0.5 * w), y + fl(1.5 * h)};
                b = {x + fl(1.25 * w), y};
            } else if (alpha < -beta && alpha > -kPi + beta) {
                a = {x, y + h};
                b = {x + w, y + h};
            } else if (alpha < -kPi + beta && alpha > -kPi) {
                a = {x - fl(0.25 * w), y};
                b = {x + fl(0.5 * w), y + fl(1.5 * h)};
            }
        }
        result = intersection(cx, cy, next.x, next.y, a.x, a.y, b.x, b.y);
    }
    return result.value_or(Point{cx, cy});
}

// ------------------------------------------- outline polygons (Shapes.js)

// getOutlinePerimeterPoint (Shapes.js:4555): the point of the closed polygon
// `points` nearest to `next` on the ray(s) from the center.
Point outlinePerimeterPoint(const Rect& bounds, const std::vector<Point>& points, Point next, bool orthogonal)
{
    // mxUtils.getPerimeterPoint: the hit nearest to `point`.
    auto polygonHit = [&](Point from, Point to) -> std::optional<Point> {
        std::optional<Point> best;
        double bestDist = 0;
        for (std::size_t i = 0; i + 1 < points.size(); ++i) {
            const auto pt = intersection(points[i], points[i + 1], from, to);
            if (!pt)
                continue;
            const double dx = to.x - pt->x;
            const double dy = to.y - pt->y;
            const double d = dy * dy + dx * dx;
            if (!best || bestDist > d) {
                best = pt;
                bestDist = d;
            }
        }
        return best;
    };

    const double x = bounds.x;
    const double y = bounds.y;
    const double w = bounds.width;
    const double h = bounds.height;
    const Point c = center(bounds);
    std::optional<Point> result;

    if (orthogonal) {
        Point p1 = c;
        if (next.x < x || next.x > x + w) {
            p1.y = next.y;
            result = polygonHit(p1, next);
            if (!result) {
                p1.x = next.x > p1.x ? x - 1 : x + w + 1;
                result = polygonHit(p1, next);
            }
        } else {
            p1.x = next.x;
            result = polygonHit(p1, next);
            if (!result && (next.y < y || next.y > y + h)) {
                p1.y = next.y > p1.y ? y - 1 : y + h + 1;
                result = polygonHit(p1, next);
            }
        }
    }
    if (!result)
        result = polygonHit(c, next);
    return result.value_or(c);
}

double sizeOrDefault(const PerimeterShape& s, double relativeDefault)
{
    if (s.size >= 0)
        return s.size;
    return s.fixedSize ? 20 : relativeDefault;
}

// Distance helpers that mirror the upstream clamping.
double clampRelative(double size, double extent) { return extent * std::max(0.0, std::min(1.0, size)); }
double clampFixed(double size, double limit) { return std::max(0.0, std::min(limit, size)); }
double offset(const PerimeterShape& s, double size, double extent, double fixedLimit)
{
    return s.fixedSize ? clampFixed(size, fixedLimit) : clampRelative(size, extent);
}

std::vector<Point> parallelogramOutline(const PerimeterShape& s, const Rect& r)
{
    const double size = sizeOrDefault(s, 0.2);
    const double x = r.x, y = r.y, w = r.width, h = r.height;
    if (isVertical(s.direction)) {
        const double dy = offset(s, size, h, h);
        return {{x, y}, {x + w, y + dy}, {x + w, y + h}, {x, y + h - dy}, {x, y}};
    }
    const double dx = offset(s, size, w, w * 0.5);
    return {{x + dx, y}, {x + w, y}, {x + w - dx, y + h}, {x, y + h}, {x + dx, y}};
}

std::vector<Point> trapezoidOutline(const PerimeterShape& s, const Rect& r)
{
    const double size = sizeOrDefault(s, 0.2);
    const double x = r.x, y = r.y, w = r.width, h = r.height;
    switch (s.direction) {
    case Direction::East: {
        const double dx = offset(s, size, w, w * 0.5);
        return {{x + dx, y}, {x + w - dx, y}, {x + w, y + h}, {x, y + h}, {x + dx, y}};
    }
    case Direction::West: {
        const double dx = offset(s, size, w, w);
        return {{x, y}, {x + w, y}, {x + w - dx, y + h}, {x + dx, y + h}, {x, y}};
    }
    case Direction::North: {
        const double dy = offset(s, size, h, h);
        return {{x, y + dy}, {x + w, y}, {x + w, y + h}, {x, y + h - dy}, {x, y + dy}};
    }
    case Direction::South:
        break;
    }
    const double dy = offset(s, size, h, h);
    return {{x, y}, {x + w, y + dy}, {x + w, y + h - dy}, {x, y + h}, {x, y}};
}

std::vector<Point> stepOutline(const PerimeterShape& s, const Rect& r)
{
    const double size = sizeOrDefault(s, 0.2);
    const double x = r.x, y = r.y, w = r.width, h = r.height;
    const Point c = center(r);
    switch (s.direction) {
    case Direction::East: {
        const double dx = offset(s, size, w, w);
        return {{x, y}, {x + w - dx, y}, {x + w, c.y}, {x + w - dx, y + h}, {x, y + h}, {x + dx, c.y}, {x, y}};
    }
    case Direction::West: {
        const double dx = offset(s, size, w, w);
        return {{x + dx, y}, {x + w, y}, {x + w - dx, c.y}, {x + w, y + h}, {x + dx, y + h}, {x, c.y}, {x + dx, y}};
    }
    case Direction::North: {
        const double dy = offset(s, size, h, h);
        return {{x, y + dy}, {c.x, y}, {x + w, y + dy}, {x + w, y + h}, {c.x, y + h - dy}, {x, y + h}, {x, y + dy}};
    }
    case Direction::South:
        break;
    }
    const double dy = offset(s, size, h, h);
    return {{x, y}, {c.x, y + dy}, {x + w, y}, {x + w, y + h - dy}, {c.x, y + h}, {x, y + h - dy}, {x, y}};
}

std::vector<Point> hexagon2Outline(const PerimeterShape& s, const Rect& r)
{
    const double size = sizeOrDefault(s, 0.25);
    const double x = r.x, y = r.y, w = r.width, h = r.height;
    const Point c = center(r);
    if (isVertical(s.direction)) {
        const double dy = offset(s, size, h, h);
        return {{c.x, y}, {x + w, y + dy}, {x + w, y + h - dy}, {c.x, y + h}, {x, y + h - dy}, {x, y + dy}, {c.x, y}};
    }
    const double dx = offset(s, size, w, w);
    return {{x + dx, y}, {x + w - dx, y}, {x + w, c.y}, {x + w - dx, y + h}, {x + dx, y + h}, {x, c.y}, {x + dx, y}};
}

bool finite(const Rect& r, Point p)
{
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height) &&
           std::isfinite(p.x) && std::isfinite(p.y);
}

Point perimeterOf(const PerimeterShape& shape, const Rect& bounds, Point next, bool orthogonal)
{
    switch (shape.kind) {
    case PerimeterKind::Rectangle:
        return rectanglePerimeter(bounds, next, orthogonal);
    case PerimeterKind::Ellipse:
        return ellipsePerimeter(bounds, next, orthogonal);
    case PerimeterKind::Rhombus:
        return rhombusPerimeter(bounds, next, orthogonal);
    case PerimeterKind::Triangle:
        return trianglePerimeter(bounds, shape.direction, next, orthogonal);
    case PerimeterKind::Hexagon:
        return hexagonPerimeter(bounds, shape.direction, next, orthogonal);
    case PerimeterKind::Hexagon2:
        return outlinePerimeterPoint(bounds, hexagon2Outline(shape, bounds), next, orthogonal);
    case PerimeterKind::Parallelogram:
        return outlinePerimeterPoint(bounds, parallelogramOutline(shape, bounds), next, orthogonal);
    case PerimeterKind::Trapezoid:
        return outlinePerimeterPoint(bounds, trapezoidOutline(shape, bounds), next, orthogonal);
    case PerimeterKind::Step:
        return outlinePerimeterPoint(bounds, stepOutline(shape, bounds), next, orthogonal);
    case PerimeterKind::Center:
        break;
    }
    return center(bounds);
}

} // namespace

std::optional<PerimeterKind> parsePerimeter(std::string_view value)
{
    if (value.empty() || value == "none")
        return std::nullopt;
    struct Entry {
        std::string_view name;
        PerimeterKind kind;
    };
    static constexpr std::array<Entry, 20> table{{
        {"rectanglePerimeter", PerimeterKind::Rectangle},
        {"rectangle", PerimeterKind::Rectangle},
        {"ellipsePerimeter", PerimeterKind::Ellipse},
        {"ellipse", PerimeterKind::Ellipse},
        {"rhombusPerimeter", PerimeterKind::Rhombus},
        {"rhombus", PerimeterKind::Rhombus},
        {"trianglePerimeter", PerimeterKind::Triangle},
        {"triangle", PerimeterKind::Triangle},
        {"hexagonPerimeter", PerimeterKind::Hexagon},
        {"hexagon", PerimeterKind::Hexagon},
        {"hexagonPerimeter2", PerimeterKind::Hexagon2},
        {"hexagon2", PerimeterKind::Hexagon2},
        {"parallelogramPerimeter", PerimeterKind::Parallelogram},
        {"parallelogram", PerimeterKind::Parallelogram},
        {"trapezoidPerimeter", PerimeterKind::Trapezoid},
        {"trapezoid", PerimeterKind::Trapezoid},
        {"stepPerimeter", PerimeterKind::Step},
        {"step", PerimeterKind::Step},
        {"centerPerimeter", PerimeterKind::Center},
        {"center", PerimeterKind::Center},
    }};
    for (const Entry& e : table)
        if (e.name == value)
            return e.kind;
    return PerimeterKind::Rectangle; // callout, lifeline, cloud, outline, ... (documented approximation)
}

Point perimeterPoint(const PerimeterShape& shape, const Rect& bounds, Point next, bool orthogonal)
{
    const Point c = center(bounds);
    // Like mxGraphView.getPerimeterPoint, a line (one zero side) still has a
    // perimeter; a point or an invalid size has only its center.
    if (!finite(bounds, next) || bounds.width < 0 || bounds.height < 0 || (bounds.width == 0 && bounds.height == 0))
        return c;
    const Point p = perimeterOf(shape, bounds, next, orthogonal);
    return std::isfinite(p.x) && std::isfinite(p.y) ? p : c;
}

} // namespace qraw::geometry
