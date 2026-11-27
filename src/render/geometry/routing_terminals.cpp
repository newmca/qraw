// Port of mxgraph/src/view/mxGraphView.js (getFloatingTerminalPoint,
// getPerimeterPoint, getPerimeterBounds), mxgraph/src/view/mxGraph.js
// (getConnectionPoint) and mxgraph/src/util/mxUtils.js (getPortConstraints)
// (draw.io, Apache-2.0), modified: view scale 1, translate 0, no ports, no
// label perimeter, routing center 0.

#include "routing_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <string_view>

namespace qraw::geometry {

namespace detail {

double jsRound(double v) { return std::floor(v + 0.5); }
double round10(double v) { return jsRound(v * 10) / 10; }

int reversePortConstraints(int constraint)
{
    int result = (constraint & kMaskWest) << 3;
    result |= (constraint & kMaskNorth) << 1;
    result |= (constraint & kMaskSouth) >> 1;
    result |= (constraint & kMaskEast) >> 3;
    return result;
}

int portConstraints(const Terminal& terminal, int defaultValue)
{
    if (terminal.portConstraint.empty())
        return defaultValue;

    const std::string_view directions = terminal.portConstraint;
    const double rotation = terminal.portConstraintRotation ? terminal.rotation : 0;
    int quad = 0;
    if (rotation > 45) {
        quad = rotation >= 135 ? 2 : 1;
    } else if (rotation < -45) {
        quad = rotation <= -135 ? 2 : 3;
    }

    // Row per quadrant: where north, west, south and east end up.
    static constexpr int rotated[4][4] = {
        {kMaskNorth, kMaskWest, kMaskSouth, kMaskEast}, // quad 0
        {kMaskEast, kMaskNorth, kMaskWest, kMaskSouth}, // quad 1
        {kMaskSouth, kMaskEast, kMaskNorth, kMaskWest}, // quad 2
        {kMaskWest, kMaskSouth, kMaskEast, kMaskNorth}, // quad 3
    };
    int result = kMaskNone;
    if (directions.find("north") != std::string_view::npos)
        result |= rotated[quad][0];
    if (directions.find("west") != std::string_view::npos)
        result |= rotated[quad][1];
    if (directions.find("south") != std::string_view::npos)
        result |= rotated[quad][2];
    if (directions.find("east") != std::string_view::npos)
        result |= rotated[quad][3];
    return result;
}

namespace {

// mxUtils.getRotatedPoint
Point rotated(Point pt, double cos, double sin, Point c)
{
    const double x = pt.x - c.x;
    const double y = pt.y - c.y;
    return {x * cos - y * sin + c.x, y * cos + x * sin + c.y};
}

Point rotatedDegrees(Point pt, double degrees, Point c)
{
    const double rad = degrees * std::numbers::pi / 180;
    return rotated(pt, std::cos(rad), std::sin(rad), c);
}

Point centerOf(const Rect& r) { return {r.x + r.width / 2, r.y + r.height / 2}; }

// mxCellState.getPerimeterBounds: the bounds grown by the perimeter spacing.
Rect perimeterBounds(const Terminal& terminal)
{
    const double b = terminal.perimeterSpacing;
    const Rect& r = terminal.bounds;
    return {r.x - b, r.y - b, r.width + 2 * b, r.height + 2 * b};
}

Direction directionOf(const Terminal& terminal)
{
    return terminal.perimeter ? terminal.perimeter->direction : Direction::East;
}

// mxGraphView.getPerimeterPoint for a vertex terminal (no label perimeter).
Point perimeterPointOf(const Terminal& terminal, Point next, bool orthogonal)
{
    const Point fallback = centerOf(terminal.bounds); // mxGraphView.getPoint
    if (!terminal.perimeter)
        return fallback;
    const Rect bounds = perimeterBounds(terminal);
    if (!(bounds.width > 0 || bounds.height > 0))
        return fallback;

    Point point = next;
    const Point c = centerOf(bounds);
    if (terminal.flipH)
        point.x = 2 * c.x - point.x;
    if (terminal.flipV)
        point.y = 2 * c.y - point.y;
    point = perimeterPoint(*terminal.perimeter, bounds, point, orthogonal);
    if (terminal.flipH)
        point.x = 2 * c.x - point.x;
    if (terminal.flipV)
        point.y = 2 * c.y - point.y;
    return point;
}

} // namespace

Point floatingVertexPoint(const Terminal& start, Point next, bool orthogonal)
{
    const double rotation = start.rotation;
    const double alpha = rotation * std::numbers::pi / 180;
    const Point center = centerOf(start.bounds);
    // Quarter turns keep vertical and horizontal lines, so orthogonal ends
    // are projected onto the unrotated shape (with exact sines and cosines).
    const bool quarter = std::fmod(rotation, 90.0) == 0;

    if (alpha != 0) {
        double cos = std::cos(-alpha);
        double sin = std::sin(-alpha);
        if (quarter) {
            cos = std::round(cos);
            sin = std::round(sin);
        }
        next = rotated(next, cos, sin, center);
    }

    Point pt = perimeterPointOf(start, next, quarter && orthogonal);

    if (alpha != 0) {
        double cos = std::cos(alpha);
        double sin = std::sin(alpha);
        if (quarter) {
            cos = std::round(cos);
            sin = std::round(sin);
        }
        pt = rotated(pt, cos, sin, center);
    }
    return pt;
}

Point nearestPolylinePoint(const std::vector<Point>& polyline, Point next)
{
    // Private segment projection; the shared polyline helpers belong to
    // another package.
    Point best = polyline.front();
    double bestDist = std::numeric_limits<double>::infinity();
    auto consider = [&](Point candidate) {
        const double dx = candidate.x - next.x;
        const double dy = candidate.y - next.y;
        const double d = dx * dx + dy * dy;
        if (d < bestDist) {
            bestDist = d;
            best = candidate;
        }
    };
    if (polyline.size() == 1)
        consider(polyline.front());
    for (std::size_t i = 0; i + 1 < polyline.size(); ++i) {
        const Point a = polyline[i];
        const Point b = polyline[i + 1];
        const double sx = b.x - a.x;
        const double sy = b.y - a.y;
        const double len2 = sx * sx + sy * sy;
        double t = len2 > 0 ? ((next.x - a.x) * sx + (next.y - a.y) * sy) / len2 : 0;
        t = std::clamp(t, 0.0, 1.0);
        consider({a.x + t * sx, a.y + t * sy});
    }
    return best;
}

} // namespace detail

Point constraintPoint(const Terminal& terminal, const ConnectionConstraint& constraint)
{
    using namespace detail;
    // mxGraph.getConnectionPoint (mxGraph.js:7413), round = false.
    Rect bounds = perimeterBounds(terminal);
    const Direction direction = directionOf(terminal);
    const Point cx = centerOf(bounds);
    const bool vertical = direction == Direction::North || direction == Direction::South;

    // Bounds need to be rotated by 90 degrees for further computation.
    if (vertical) {
        const double t = (bounds.width - bounds.height) / 2;
        bounds.x += t;
        bounds.y -= t;
        std::swap(bounds.width, bounds.height);
    }

    Point point{bounds.x + constraint.point.x * bounds.width + constraint.dx,
                bounds.y + constraint.point.y * bounds.height + constraint.dy};

    // Horizontal and vertical shape flipping (vertices only).
    if (!terminal.isEdge()) {
        bool flipH = terminal.flipH;
        bool flipV = terminal.flipV;
        if (vertical)
            std::swap(flipH, flipV);
        if (flipH)
            point.x = 2 * (bounds.x + bounds.width / 2) - point.x;
        if (flipV)
            point.y = 2 * (bounds.y + bounds.height / 2) - point.y;
    }

    // Shape direction (anchorPointDirection is assumed to be its default 1).
    // Only 90 degree steps are possible, so no trigonometry is needed.
    double r1 = 0;
    switch (direction) {
    case Direction::North: r1 = 270; break;
    case Direction::West: r1 = 180; break;
    case Direction::South: r1 = 90; break;
    case Direction::East: break;
    }
    if (r1 != 0) {
        const double cos = r1 == 180 ? -1 : 0;
        const double sin = r1 == 90 ? 1 : (r1 == 270 ? -1 : 0);
        point = rotated(point, cos, sin, cx);
    }

    // Projection on the perimeter.
    if (constraint.perimeter)
        point = perimeterPointOf(terminal, point, false);

    // General shape rotation.
    if (terminal.rotation != 0)
        point = rotatedDegrees(point, terminal.rotation, cx);
    return point;
}

} // namespace qraw::geometry
