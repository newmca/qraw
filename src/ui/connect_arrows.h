#pragma once
// Geometry of the four hover arrows of a vertex (HoverIcons, Graph.js:23620).
// Private to the editor UI.

#include <QPointF>
#include <QRectF>

#include <array>
#include <cmath>
#include <optional>

namespace qraw::ui::connect {

enum class Side { North, East, South, West };

inline constexpr std::array<Side, 4> kSides = {Side::North, Side::East, Side::South, Side::West};

// Arrow centre: 20 px outside the side midpoint; the triangle is 14 px
// wide; the pointer hits it within 9 px of the centre.
inline constexpr double kArrowDistance = 20;
inline constexpr double kArrowSize = 14;
inline constexpr double kArrowHit = 9;

[[nodiscard]] inline QPointF sideDirection(Side side)
{
    switch (side) {
    case Side::North: return {0, -1};
    case Side::East: return {1, 0};
    case Side::South: return {0, 1};
    case Side::West: return {-1, 0};
    }
    return {1, 0};
}

[[nodiscard]] inline QPointF sideMidpoint(const QRectF& bounds, Side side)
{
    const QPointF c = bounds.center();
    switch (side) {
    case Side::North: return {c.x(), bounds.top()};
    case Side::East: return {bounds.right(), c.y()};
    case Side::South: return {c.x(), bounds.bottom()};
    case Side::West: return {bounds.left(), c.y()};
    }
    return c;
}

// `pixel` is page units per screen pixel.
[[nodiscard]] inline QPointF arrowCenter(const QRectF& bounds, Side side, double pixel)
{
    return sideMidpoint(bounds, side) + sideDirection(side) * (kArrowDistance * pixel);
}

[[nodiscard]] inline std::optional<Side> arrowAt(const QRectF& bounds, QPointF p, double pixel)
{
    for (const Side side : kSides) {
        const QPointF c = arrowCenter(bounds, side, pixel);
        if (std::abs(p.x() - c.x()) <= kArrowHit * pixel && std::abs(p.y() - c.y()) <= kArrowHit * pixel)
            return side;
    }
    return std::nullopt;
}

// The area in which the arrows of a hovered vertex stay visible: the bounds
// grown far enough to reach every arrow.
[[nodiscard]] inline QRectF hoverArea(const QRectF& bounds, double pixel)
{
    const double margin = (kArrowDistance + kArrowHit) * pixel;
    return bounds.adjusted(-margin, -margin, margin, margin);
}

} // namespace qraw::ui::connect
