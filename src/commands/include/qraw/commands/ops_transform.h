#pragma once

// Transforms of several cells at once: the union resize and rotate handles
// (Graph.MultiSelectionHandler, js/grapheditor/Graph.js ~53234) commit
// through these. Each function runs inside a caller-provided Transaction
// (see ops_cells.h for the common rules). All coordinates are absolute.
//
// What is changed, per cell of `ids` (Graph.getTransformCells: groups with
// transparentBounds=1 are replaced by their child vertices and edges; such
// a group with a child layout is left alone):
//  * an edge: its waypoints and its free terminal points are mapped;
//  * a vertex with a relative geometry (a port, an edge label) is skipped;
//  * any other vertex: its centre is mapped and, for a scale, its size is
//    scaled along its own axes (so a rotated vertex keeps its rotation);
//    a vertex with resizable=0 only moves.

#include "qraw/commands/result.h"
#include "qraw/core/geometry.h"

#include <cstddef>
#include <string>
#include <vector>

namespace qraw::core {
class Style;
}

namespace qraw::commands {
class Transaction;
}

namespace qraw::commands::ops {

// Graph.getMultiSelectionVertexScale (Graph.js ~37391): the factors for the
// width and height of a vertex with `style` when the union box is scaled by
// (sx, sy): the scales along the axes of the rotated vertex, the smaller one
// for aspect=fixed, and 1 where resizeWidth / resizeHeight is 0.
[[nodiscard]] core::Point multiSelectionVertexScale(const core::Style& style, double sx, double sy);

// Graph.scaleCellsToBounds (Graph.js ~37423): maps the cells from `from` to
// `to`. Resizable vertices that are not swimlanes, are expanded, have
// children and neither recursiveResize=0 nor a childLayout also scale their
// children (Graph.isRecursiveVertexResize; relative children keep their
// geometry). With `scaleStyles` (Ctrl) fontSize, strokeWidth and the
// spacings of the scaled cells and those children are multiplied by the
// smaller factor (Graph.scaleCellStyles; the defaults are the draw.io
// stylesheet's 12, 1 and 2). Result.cells = ids.
Result scaleCellsToBounds(Transaction& tx, std::size_t page, const std::vector<std::string>& ids,
                          const core::Rect& from, const core::Rect& to, bool scaleStyles);

// Graph.rotateCellsAround (Graph.js ~37596): turns the cells by `angle`
// degrees (clockwise) around `center`. Vertices that have a rotation handle
// (not rotatable=0, tables and their rows and cells, transparentBounds
// groups, labelAutoRotate=1) also get `rotation` += angle (mod 360) and turn
// their children as a rigid body (Graph.rotateCell); the others only move.
// Result.cells = ids.
Result rotateCellsAround(Transaction& tx, std::size_t page, const std::vector<std::string>& ids, double angle,
                         const core::Point& center);

// Graph.turnShapes (js/grapheditor/Graph.js:36966), the Turn action (Ctrl+R),
// one call for the cells of `ids` (resizable=0 cells are skipped, as
// Graph.getResizableCells does):
//  * an edge is reversed: source and target swap, the waypoints and the
//    terminal points are reversed, and the exit*/entry* constraints and the
//    source/target perimeter spacings swap. A self-loop without waypoints
//    stays as it is (upstream first materialises the loop's routing; not
//    ported, see KNOWN_ISSUES.md);
//  * a vertex with children that is no table part or swimlane is rotated by
//    90 degrees (or -90 when `backwards`) about its centre (rotateCellsAround);
//  * any other vertex with a geometry swaps its width and height about its
//    centre and advances its `direction` style (east, south, west, north).
// Result.cells = ids.
Result turnCells(Transaction& tx, std::size_t page, const std::vector<std::string>& ids, bool backwards = false);

} // namespace qraw::commands::ops
