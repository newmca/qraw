#pragma once

// Structural operations: z-order, group/ungroup, reparent, and the
// edge-parent maintenance draw.io's model performs whenever a cell changes
// parent. Same conventions as ops_cells.h (runs inside a Transaction,
// composes primitives, failure means "roll back").
//
// Child order and the flat entry list: draw.io keeps a tree (mxCell
// children arrays) and serializes it depth-first, so a parent's children
// appear after it, each followed by its own subtree. Our <root> keeps the
// flat entry list. "Child number k of P" therefore means: the block (cell +
// descendants, queries.h subtreeEntryRange / moveSubtree) placed before the
// first entry of the subtree of P's current k-th child (siblings in entry
// order, the moved cell excluded), or right after the last entry of P's
// subtree when k >= that sibling count. Foreign XmlNode entries never move.
// Upstream paths are relative to
// $DRAWIO_SRC/src/main/webapp/.

#include "qraw/commands/result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::commands {
class Transaction;
}

namespace qraw::commands::ops {

// --- z-order ---------------------------------------------------------------

enum class ZOrder {
    ToFront,      // Actions.js:582 toFront      -> orderCells(false)
    ToBack,       // Actions.js:586 toBack       -> orderCells(true)
    BringForward, // Actions.js:590 bringForward -> orderCells(false, null, true)
    SendBackward, // Actions.js:594 sendBackward -> orderCells(true, null, true)
};

// mxGraph.cellsOrdered (mxgraph/src/view/mxGraph.js:4043). `ids` are first
// sorted by z-order (sortByZOrder; draw.io passes sortCells(selection,
// true)). Then for the i-th cell, with P its parent, n = number of P's
// children and k = its current child index, the new child index (counted
// after taking the cell out, as mxCell.insert does) is:
//   ToBack        i            (clamped to n-1)
//   SendBackward  max(0, k-1)
//   ToFront       n-1
//   BringForward  min(n-1, k+1)
// Cells are processed one after another, each seeing the previous moves.
// A cell already at its target position produces no command. Cells without
// a parent (the root) are skipped. Result.cells = ids in z-order.
// Not modeled: Graph.orderCells replacing table cells by their table.
Result orderCells(Transaction& tx, std::size_t page, const std::vector<std::string>& ids, ZOrder order);

// --- edge parents ----------------------------------------------------------

// mxGraphModel.updateEdgeParents/updateEdgeParent
// (mxgraph/src/model/mxGraphModel.js:787, 835), run by draw.io after every
// parent change of `cellId` (model.add with a new parent, mxGraphModel.js:675):
// for every edge connected to `cellId` or one of its descendants (edges of
// descendants first), except edges listed in `skipEdges`:
//   * source/target: climb from the terminal while it is a non-edge cell
//     with a relative geometry (both ends; ignoreRelativeEdgeParent is true);
//   * both must exist; nca = parent(source) when source == target, else
//     nearestCommonAncestor(source, target);
//   * if nca exists, (parent(nca) is not the root cell, or nca is an
//     ancestor of the edge), and parent(edge) != nca: the edge geometry is
//     translated by -(origin(nca) - origin(parent(edge))) with
//     translateGeometry(..., ignorePosition = false) and the edge becomes the
//     last child of nca.
// Each edge is handled at most once per call.
Result updateEdgeParents(Transaction& tx, std::size_t page, std::string_view cellId,
                         const std::vector<std::string>& skipEdges = {});

// --- reparent --------------------------------------------------------------

// Moves cells into another parent keeping their absolute position: draw.io's
// moveCells(cells, 0, 0, false, target) (mxGraph.js:6634) followed by
// cellsAdded(cells, target, childCount(target), ..., absolute=true)
// (mxGraph.js:4925):
//  * ids -> topmostCells(), then relative edge labels with a selected
//    terminal are dropped (as ops::moveCells step 1).
//  * `target` must be the root's child (a layer) or a vertex, must exist,
//    and must not be one of the cells or a descendant of one (failure).
//  * For each cell i (in the filtered order) whose parent is not already
//    `target`: its geometry is translated by origin(oldParent) -
//    origin(target) (queries.h origin; with translateGeometry(geo, d,
//    isEdge)), it gets parent=target.
//  * Cell i becomes child number index+i of target (`index` defaults to the
//    child count before the operation); for a cell already in target whose
//    index+i exceeds the child count, the index is decremented first, as
//    upstream.
//  * updateEdgeParents(cell, skipEdges = the cells themselves) for every cell
//    whose parent changed.
// Result.cells = the cells moved.
Result reparentCells(Transaction& tx, std::size_t page, const std::vector<std::string>& ids,
                     const std::string& target, std::optional<std::size_t> index = std::nullopt);

// Actions.js:718 removeFromGroup -> mxGraph.removeCellsFromParent
// (mxGraph.js:4383): reparentCells(ids, layer) where `layer` is the UI's
// default parent; "" means layerOf(first id). Table rows/cells are skipped
// upstream (M6: not detected here).
Result removeFromGroup(Transaction& tx, std::size_t page, const std::vector<std::string>& ids,
                       const std::string& layer = {});

// --- group / ungroup -------------------------------------------------------

struct GroupOptions {
    double border = 0; // Actions.js:672 passes 0
    // Graph.createGroupCell (Graph.js:35880): "group", or
    // "group;transparentBounds=1;" with Editor.defaultTransparentGroups
    // (false by default, Editor.js:461; not supported here).
    std::string style = "group";
};

// The Ctrl+G action (Actions.js:657) with mxGraph.groupCells
// (mxGraph.js:4113):
//  * ids -> sortByZOrder(); exactly one cell: set style container=1 on it
//    and return Result.cells = {it} (no group is created).
//  * getCellsForGroup: keep the cells with the same parent as the first one.
//    Fewer than two left -> failure.
//  * bounds = mxGraph.getBoundingBoxFromGeometry(cells, true)
//    (mxGraph.js:8069), parent coordinates: union of the non-relative vertex
//    geometries (x, y, w, h); for edges, the free terminal points (only for
//    an end without terminal) and the waypoints as zero-size rects. Relative
//    vertex geometries and rotation are ignored. Grown by `border` on all
//    sides. No bounds at all -> failure.
//  * A new vertex is created: id newCellId(), value "", style options.style,
//    vertex=1, connectable=0, parent = the cells' parent, geometry = bounds
//    (attribute order as Cell::makeVertex + setConnectable(false):
//    id, value, style, vertex, connectable, parent). It becomes the last
//    child of the parent.
//  * Each cell (z-order) becomes the last child of the group, in order
//    (parent attribute set, subtree block moved), and
//    updateEdgeParents(cell, skipEdges = cells) runs after each one.
//  * Every cell is translated by (-bounds.x, -bounds.y) with
//    queries.h translatedCell (cellsMoved without disconnect).
// Result.cells = {group id}.
Result groupCells(Transaction& tx, std::size_t page, const std::vector<std::string>& ids,
                  const GroupOptions& options = {});

// The Ctrl+Shift+U action (Actions.js:678) with mxGraph.ungroupCells
// (mxGraph.js:4274) and Graph.removeCellsAfterUngroup (Graph.js:38382):
//  * groups = the ids that are vertices with at least one child.
//  * For each group G (in ids order) with parent P: G's direct children
//    (z-order) become the last children of P, in order, keeping their
//    absolute position: geometry translated by origin(G) - origin(P) with
//    translateGeometry(geo, d, isEdge), then updateEdgeParents(child,
//    skipEdges = the children). A child vertex with a relative geometry
//    instead gets x = origin(G).x + gx * G.width + offset.x - origin(P).x
//    (same for y with height), relative removed, computed from its values
//    before the move (this replaces upstream's view-state origin).
//  * Each G that is transparent (queries.h isTransparentCell) and not
//    `deletable=0` is removed together with every edge connected to it
//    (mxGraph.removeCellsAfterUngroup = cellsRemoved(addAllEdges(cells))):
//    those edges are deleted, not disconnected. Other groups stay.
//  * Remaining ids that are vertices without children get container=0
//    (Actions.js:690-698).
// Result.cells = all moved children followed by the remaining ids.
// No vertex with children among ids -> success, no change, Result.cells =
// remaining ids.
Result ungroupCells(Transaction& tx, std::size_t page, const std::vector<std::string>& ids);

// --- collapse / expand ---------------------------------------------------------

struct FoldOptions {
    // mxGraph.foldCells(collapse, recurse, cells): also fold the descendants.
    bool recurse = false;
};

// mxGraph.foldCells/cellsFolded/swapBounds/updateAlternateBounds
// (mxgraph/src/view/mxGraph.js:5573, 5615, 5671, 5705), without the
// isCellFoldable check (the caller decides which cells show a fold control;
// ui::isFoldable). For each vertex in `ids` (in order) whose `collapsed`
// state differs from `collapse` and that has a geometry:
//  * `collapsed=1` is set, or removed when expanding (core::Cell::setCollapsed);
//  * the geometry swaps with its <mxRectangle as="alternateBounds"> child.
//    Before the swap the alternate bounds are created when missing and
//    their x/y set to the geometry's (rotated around the centre by the
//    style `rotation`), as updateAlternateBounds does. Divergence: upstream
//    sizes a missing alternate bounds to the label's preferred size
//    (collapseToPreferredSize, needs text metrics); we keep the width and
//    shrink to the title (swimlane: startSize on the title axis) or keep
//    the size (other containers);
//  * the parent grows when the swapped bounds stick out of it (extendParent);
//  * with `recurse`, the children fold the same way.
// Result.cells = the cells whose state changed.
Result foldCells(Transaction& tx, std::size_t page, const std::vector<std::string>& ids, bool collapse,
                 const FoldOptions& options = {});

} // namespace qraw::commands::ops
