#pragma once

// Perimeter functions (mxPerimeter): where an edge meets a vertex outline.

#include "qraw/core/geometry.h"

#include <optional>
#include <string_view>

namespace qraw::geometry {

using core::Point;
using core::Rect;

// The draw.io `direction` style. Absent or unknown values mean East.
enum class Direction { East, South, West, North };

[[nodiscard]] constexpr Direction parseDirection(std::string_view value)
{
    if (value == "south")
        return Direction::South;
    if (value == "west")
        return Direction::West;
    if (value == "north")
        return Direction::North;
    return Direction::East;
}

enum class PerimeterKind {
    Rectangle,     // mxPerimeter.RectanglePerimeter
    Ellipse,       // mxPerimeter.EllipsePerimeter
    Rhombus,       // mxPerimeter.RhombusPerimeter
    Triangle,      // mxPerimeter.TrianglePerimeter (direction-aware)
    Hexagon,       // mxPerimeter.HexagonPerimeter (classic, direction-aware)
    Hexagon2,      // hexagonPerimeter2 (js/grapheditor/Shapes.js:4762, uses size/fixedSize)
    Parallelogram, // parallelogramPerimeter (Shapes.js:4597)
    Trapezoid,     // trapezoidPerimeter (Shapes.js:4643)
    Step,          // stepPerimeter (Shapes.js:4699)
    Center,        // centerPerimeter (Shapes.js:4142): always the center
};

// Maps a `perimeter` style value. Accepted: rectanglePerimeter,
// ellipsePerimeter, rhombusPerimeter, trianglePerimeter, hexagonPerimeter,
// hexagonPerimeter2, parallelogramPerimeter, trapezoidPerimeter,
// stepPerimeter, centerPerimeter, and the bare names without the
// "Perimeter" suffix (mxStyleRegistry also registers "rectangle", ...).
// Empty or "none" yields nullopt (no perimeter: the terminal point is the
// center or constraint point, unprojected). Any other name (callout,
// lifeline, orthogonal, backbone, cloud, outline, ...) falls back to
// Rectangle, a documented approximation.
[[nodiscard]] std::optional<PerimeterKind> parsePerimeter(std::string_view value);

// Shape parameters that some perimeters need, read from the vertex style by
// the caller.
struct PerimeterShape {
    PerimeterKind kind = PerimeterKind::Rectangle;
    Direction direction = Direction::East;
    // `size` style for Hexagon2/Parallelogram/Trapezoid/Step. Negative means
    // the upstream default of that shape: 0.25 for hexagon (20 when
    // fixedSize), 0.2 for parallelogram/trapezoid/step (20 when fixedSize).
    double size = -1;
    bool fixedSize = false; // `fixedSize=1`: size is absolute, not relative
    // `rounded=1;roundedPerimeter=1` is not modeled yet (KNOWN_ISSUES).
};

// The point where the ray from the center of `bounds` towards `next` leaves
// the outline described by `shape`, ported from the mxPerimeter functions
// with identical arguments (bounds, vertex, next, orthogonal):
// mxgraph/src/view/mxPerimeter.js:84 (rectangle), :161 (ellipse),
// :232 (rhombus), :313 (triangle), :464 (hexagon) and
// js/grapheditor/Shapes.js lines above for the draw.io additions.
// `orthogonal` = true projects horizontally/vertically when `next` lies
// within the x or y extent of the bounds (used by orthogonal edge styles).
// `bounds` is unrotated; callers handle vertex rotation (routing.h).
// A degenerate (zero-size) bounds returns its center.
[[nodiscard]] Point perimeterPoint(const PerimeterShape& shape, const Rect& bounds, Point next, bool orthogonal);

} // namespace qraw::geometry
