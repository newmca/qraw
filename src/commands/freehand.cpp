#include "qraw/commands/freehand.h"

#include "qraw/core/number.h"

#include <algorithm>
#include <cmath>

namespace qraw::commands::freehand {

namespace {

using core::Point;

double distanceToSegment(const Point& p, const Point& a, const Point& b)
{
    const double dx = b.x - a.x;
    const double dy = b.y - a.y;
    const double lengthSquared = dx * dx + dy * dy;
    if (lengthSquared == 0)
        return std::hypot(p.x - a.x, p.y - a.y);
    const double t = std::clamp(((p.x - a.x) * dx + (p.y - a.y) * dy) / lengthSquared, 0.0, 1.0);
    return std::hypot(p.x - (a.x + t * dx), p.y - (a.y + t * dy));
}

// Marks the points of [first, last] to keep: the farthest point from the chord
// and, recursively, the farthest points of both halves. Iterative, so that a
// long stroke cannot overflow the stack.
void markKept(std::span<const Point> points, double tolerance, std::vector<bool>& keep)
{
    std::vector<std::pair<std::size_t, std::size_t>> pending{{0, points.size() - 1}};
    while (!pending.empty()) {
        const auto [first, last] = pending.back();
        pending.pop_back();
        double farthest = 0;
        std::size_t at = first;
        for (std::size_t i = first + 1; i < last; ++i) {
            const double d = distanceToSegment(points[i], points[first], points[last]);
            if (d > farthest) {
                farthest = d;
                at = i;
            }
        }
        if (at != first && farthest > tolerance) {
            keep[at] = true;
            pending.emplace_back(first, at);
            pending.emplace_back(at, last);
        }
    }
}

std::vector<Point> withoutRepeats(std::span<const Point> points)
{
    std::vector<Point> result;
    result.reserve(points.size());
    for (const Point& p : points) {
        if (result.empty() || !(result.back() == p))
            result.push_back(p);
    }
    return result;
}

// One piece of the drawn path: a quadratic curve to `end` with control point `control`.
struct Piece {
    Point control;
    Point end;
};

Point midpoint(const Point& a, const Point& b) { return {(a.x + b.x) / 2, (a.y + b.y) / 2}; }

// The points of the stroke after simplification; empty when it does not move.
std::vector<Point> keptPoints(std::span<const Point> input, double smoothing)
{
    const std::vector<Point> points = withoutRepeats(input);
    if (points.size() < 2)
        return {};
    return simplify(points, smoothing);
}

// Through the midpoints of the segments with the points as control points:
// smooth, and it never leaves the hull of the points. The last piece is the
// straight rest of the last segment, written as a quad like the others.
std::vector<Piece> piecesOf(const std::vector<Point>& points)
{
    std::vector<Piece> pieces;
    Point from = points.front();
    for (std::size_t i = 1; i + 1 < points.size(); ++i) {
        const Point end = midpoint(points[i], points[i + 1]);
        pieces.push_back({points[i], end});
        from = end;
    }
    pieces.push_back({midpoint(from, points.back()), points.back()});
    return pieces;
}

std::string number(double value) { return core::formatNumber(std::round(value * 100) / 100); }

} // namespace

std::vector<Point> simplify(std::span<const Point> points, double tolerance)
{
    const std::vector<Point> unique = withoutRepeats(points);
    if (unique.size() < 3)
        return unique;
    std::vector<bool> keep(unique.size(), false);
    keep.front() = true;
    keep.back() = true;
    markKept(unique, std::max(0.0, tolerance), keep);
    std::vector<Point> result;
    for (std::size_t i = 0; i < unique.size(); ++i) {
        if (keep[i])
            result.push_back(unique[i]);
    }
    return result;
}

std::optional<Stroke> buildStroke(std::span<const Point> points, double smoothing)
{
    const std::vector<Point> kept = keptPoints(points, smoothing);
    if (kept.size() < 2)
        return std::nullopt;

    double minX = kept.front().x;
    double maxX = minX;
    double minY = kept.front().y;
    double maxY = minY;
    for (const Point& p : kept) {
        minX = std::min(minX, p.x);
        maxX = std::max(maxX, p.x);
        minY = std::min(minY, p.y);
        maxY = std::max(maxY, p.y);
    }
    Stroke stroke;
    stroke.bounds = {minX, minY, std::max(maxX - minX, kMinExtent), std::max(maxY - minY, kMinExtent)};

    const auto x = [&](const Point& p) { return number(p.x - minX); };
    const auto y = [&](const Point& p) { return number(p.y - minY); };
    std::string& xml = stroke.stencilXml;
    xml = "<shape name=\"Freehand\" w=\"" + number(stroke.bounds.width) + "\" h=\"" + number(stroke.bounds.height) +
          "\" aspect=\"variable\" strokewidth=\"inherit\"><foreground><linecap value=\"round\"/><linejoin value=\"round\"/><path>"
          "<move x=\"" + x(kept.front()) + "\" y=\"" + y(kept.front()) + "\"/>";
    for (const Piece& piece : piecesOf(kept)) {
        xml += "<quad x1=\"" + x(piece.control) + "\" y1=\"" + y(piece.control) + "\" x2=\"" + x(piece.end) + "\" y2=\"" +
               y(piece.end) + "\"/>";
    }
    xml += "</path><stroke/></foreground></shape>";
    return stroke;
}

std::vector<Point> flattenStroke(std::span<const Point> points, double smoothing, int steps)
{
    const std::vector<Point> kept = keptPoints(points, smoothing);
    if (kept.size() < 2)
        return kept;
    std::vector<Point> line{kept.front()};
    Point from = kept.front();
    for (const Piece& piece : piecesOf(kept)) {
        for (int s = 1; s <= steps; ++s) {
            const double t = static_cast<double>(s) / steps;
            const double u = 1 - t;
            line.push_back({u * u * from.x + 2 * u * t * piece.control.x + t * t * piece.end.x,
                            u * u * from.y + 2 * u * t * piece.control.y + t * t * piece.end.y});
        }
        from = piece.end;
    }
    return line;
}

} // namespace qraw::commands::freehand
