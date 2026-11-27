#pragma once
// The model side of the Freehand tool: a drawn stroke as a vertex whose shape is
// an inline stencil. Pure functions on points (no Qt): the UI collects the
// pointer positions, builds the stroke, and inserts one cell.
//
// draw.io's mxFreehand is not part of the vendored sources, so the stored form
// is Qraw's own (COMPATIBILITY.md): shape=stencil(<compressed XML>) with one
// path, which every draw.io renders and Edit Shape edits, and which scales with
// the cell. The smoothing is a Douglas-Peucker simplification followed by a
// quadratic Bezier through the midpoints of the remaining segments.

#include "qraw/core/geometry.h"

#include <optional>
#include <span>
#include <string>
#include <vector>

namespace qraw::commands::freehand {

// Douglas-Peucker: the first and last point and every point that lies further
// than `tolerance` from the line through its neighbours that were kept. Order
// is kept; `tolerance` 0 only removes repeated points.
[[nodiscard]] std::vector<core::Point> simplify(std::span<const core::Point> points, double tolerance);

struct Stroke {
    // Where the stroke lies on the page: the geometry of the cell (at least
    // kMinExtent wide and high, so that a horizontal line still has an area to scale in).
    core::Rect bounds;
    // The <shape> element of the stencil, in the cell's own coordinates:
    // strokewidth="inherit" and one path of quadratic segments.
    std::string stencilXml;
};

inline constexpr double kMinExtent = 1;

// Builds the stroke of the points the pointer visited, in page coordinates.
// `smoothing` is the simplification tolerance in page units (0: none; larger
// values give fewer, rounder segments). nullopt when the points do not move
// (fewer than two different points), as a click draws nothing.
[[nodiscard]] std::optional<Stroke> buildStroke(std::span<const core::Point> points, double smoothing);

// The segments buildStroke draws, as a polyline of `steps` points per curve, in
// the page coordinates of the input. For previews and tests: the line that the
// stencil's path follows.
[[nodiscard]] std::vector<core::Point> flattenStroke(std::span<const core::Point> points, double smoothing,
                                                     int steps = 8);

} // namespace qraw::commands::freehand
