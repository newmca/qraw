#pragma once

// Cell operations: add, remove, move, resize, connect/disconnect, waypoints.
// Each function runs inside a caller-provided Transaction (normally through
// Editor::edit) and composes primitives (primitives.h). On failure it returns
// Result::failure and the caller rolls back; an operation never leaves a
// half-applied state behind on its own success path.
//
// Common preconditions (failure otherwise): `page` exists; every id the
// caller names is addressable (queries.h isAddressable). Behavior follows
// draw.io; upstream paths are relative to
// $DRAWIO_SRC/src/main/webapp/.

#include "qraw/commands/result.h"
#include "qraw/core/cell.h"
#include "qraw/core/geometry.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <vector>

namespace qraw::commands {
class Transaction;
}

namespace qraw::commands::ops {

// --- add --------------------------------------------------------------------

// Adds new cells (mxGraph.addCells / cellsAdded without coordinate
// conversion, mxgraph/src/view/mxGraph.js:4925).
//
// `cells` are inserted in vector order. A cell with an empty id gets
// Transaction::newCellId(). Every cell's parent must be an existing cell or
// a cell earlier in `cells`; source/target, when set, must be existing cells
// or earlier cells in `cells`. Ids must be unused (failure otherwise).
//
// Position (draw.io's model.add(parent, cell, index) serialized depth-first):
// a cell whose parent is not in the batch becomes the parent's last child,
// i.e. it is inserted right after the last entry of the parent's subtree
// (queries.h subtreeEntryRange) -- unless `index` is given: then the k-th
// such top-level cell of the batch becomes child number index+k of its
// parent (inserted before the first entry of that child's subtree, or as last
// child when index+k >= child count). A cell whose parent is in the batch
// always becomes that parent's last child.
//
// Result.cells = ids of the added cells, in order (draw.io selects them).
Result addCells(Transaction& tx, std::size_t page, std::vector<core::Cell> cells,
                std::optional<std::size_t> index = std::nullopt);

// --- remove -----------------------------------------------------------------

struct RemoveOptions {
    // Ctrl+Delete / "deleteAll": also delete every edge connected to the
    // removed cells or their descendants (mxGraph.removeCells includeEdges,
    // mxGraph.js:5085). Plain Delete passes false: such edges are kept and
    // disconnected instead (Actions.js:411-429 deleteCells).
    bool includeEdges = false;
    // Drop cells whose own style has `deletable=0` (mxGraph.getDeletableCells,
    // mxGraph.js:10627), as the Delete action does. removeLayers passes false
    // (the layers dialog calls graph.removeCells directly, Dialogs.js:5818).
    bool respectDeletable = true;
    // Graph.deleteCells (Graph.js:44229): a deleted table row takes its
    // height off the table. The table operations (ops_tables.h), which
    // change the table size themselves, pass false.
    bool shrinkTables = true;
};

// draw.io's Graph.deleteCells (js/grapheditor/Graph.js:44229):
//  1. ids not addressable -> failure. Duplicates are dropped (first
//     occurrence kept). With respectDeletable, cells with `deletable=0` in
//     their own style are dropped; with includeEdges the set is extended by
//     queries.h allEdges() (appended in z-order) and filtered again.
//  1b. With shrinkTables, every cell of the set (step 1) whose parent is a
//     table (ops_tables.h isTable) and is not deleted itself reduces the
//     table's height by the cell's height (the cell is a row; deleting a
//     table cell does not touch the table, as upstream). Cell geometry
//     without a table geometry is skipped.
//  2. Graph.cellsRemoved override (Graph.js:38250-38300): every parent P of a
//     cell in the set that is not in the set itself is appended to the set
//     when P is a vertex or an edge, deletable (if respectDeletable),
//     transparent (queries.h isTransparentCell) and all of P's direct
//     children are in the set. One level only ("LATER: Recurse up the cell
//     hierarchy" upstream). P then gets the full treatment below.
//  3. For each cell of the set (topmostCells order), every edge from
//     allEdges({cell}) that is neither in the set nor a descendant of a cell
//     in the set, and was not handled for an earlier cell, is disconnected
//     from each end whose terminal is the cell or one of its descendants
//     (mxGraph.cellsRemoved disconnectTerminal, mxGraph.js:5151ff.). The end
//     gets a terminal point in the edge parent's coordinates, computed with
//     the view-less fallback of mxGraph.js:5279-5350 (we have no view):
//       geometryOrigin() as in queries.h.
//       a) the terminal has a geometry that is not relative and is not an
//          edge: geometryOrigin(terminal) + (x + w/2, y + h/2)
//          - geometryOrigin(edge)
//       b) otherwise the nearest ancestor of the terminal with such a
//          geometry: geometryOrigin(anc) + its center - geometryOrigin(edge)
//       c) otherwise (edge.x - 60, edge.y) for the source end and
//          (edge.x + 60, edge.y) for the target end
//     and its source/target attribute is removed (one ReplaceCell per edge).
//     Divergence: upstream skips edges without geometry entirely, leaving a
//     dangling reference. We remove the terminal attribute without adding a
//     point, so the saved file has no dangling ids.
//     Then the cell is removed with its whole subtree (queries.h
//     removeSubtree).
// Not modeled: contractParent (mxGraph.contractParents is false in draw.io).
// Result.cells is empty (Graph.selectParentAfterDelete = false,
// Graph.js:8697). An empty set is a successful no-op.
Result removeCells(Transaction& tx, std::size_t page, const std::vector<std::string>& ids,
                   const RemoveOptions& options = {});

// --- move / resize ----------------------------------------------------------

struct MoveOptions {
    // mxGraph.disconnectOnMove && allowDanglingEdges (both true by default):
    // a moved edge whose terminal is not moved along is disconnected from it.
    bool disconnect = true;
};

// mxGraph.moveCells without clone/target (mxgraph/src/view/mxGraph.js:6634)
// and cellsMoved/translateCell (mxGraph.js:6761, 6816):
//  1. ids -> topmostCells(); then relative edge labels (relative geometry,
//     parent is an edge) whose parent edge has a selected source or target
//     (the terminal or one of its ancestors is in the set) are dropped:
//     they move implicitly.
//  2. dx == dy == 0 -> success, no change.
//  3. With options.disconnect, for each edge in the set and each end whose
//     terminal is set but neither it nor any of its ancestors is in the set:
//     the end becomes a terminal point at the fallback position of
//     removeCells step 3 and the
//     terminal attribute is removed (mxGraph.disconnectGraph,
//     mxGraph.js:7645; upstream uses the rendered end point, which we do not
//     have). The point is computed before step 4, so it moves with the edge.
//  4. Each cell's geometry is translated with translateGeometry(geo, dx, dy,
//     isEdge). A relative non-edge geometry (e.g. a label child) instead
//     gets offset += (round(dx'), round(dy')) where (dx', dy') is (dx, dy)
//     rotated by -rotation of the parent's style when the parent is a vertex
//     (mxGraph.js:6833-6862; the offset is created when missing).
//     Cells without geometry are skipped.
// No coordinate clamping (draw.io allows negative coordinates). Moving into
// another parent is ops::reparentCells (ops_structure.h).
// Result.cells = the cells actually moved.
Result moveCells(Transaction& tx, std::size_t page, const std::vector<std::string>& ids, double dx, double dy,
                 const MoveOptions& options = {});

// mxGraph.cellsResized(cells, bounds, false) (mxGraph.js:6217, 6274).
// `ids` and `bounds` must have the same size. For each cell with geometry
// whose bounds differ: a relative geometry keeps x/y and gets offset +=
// (bounds.x - x, bounds.y - y) (only if it has an offset); otherwise x/y
// are set. width/height are always set. Then extendParent (mxGraph.js:6456,
// simplified): if the parent has a geometry, is not collapsed, and the
// non-relative child now ends beyond it, the parent's width/height grow to
// max(parent.w, child.x + child.w) / max(parent.h, child.y + child.h),
// recursively upwards (groupPadding, rotation and constrainChild are M6).
// Cells without geometry are skipped. Result.cells = ids.
Result resizeCells(Transaction& tx, std::size_t page, const std::vector<std::string>& ids,
                   const std::vector<core::Rect>& bounds);

// mxGraph.extendParent for cells that were just added to or moved into a
// parent (cellsAdded with extendParentsOnAdd, mxGraph.js:4925, :6456):
// grows the parent chain of each non-edge cell so that it contains the cell,
// with the same simplified rule as resizeCells. Cells whose parent has a
// supported childLayout are skipped (Graph.isExtendParentsOnAdd,
// Graph.js:36254: the layout sizes the parent). Result.cells = ids.
Result extendParents(Transaction& tx, std::size_t page, const std::vector<std::string>& ids);

// --- connections ------------------------------------------------------------

// A fixed connection point (mxConnectionConstraint): relative position on
// the terminal's bounds plus absolute offset, as stored in the edge style.
struct ConnectionConstraint {
    std::optional<core::Point> point; // nullopt: floating (removes the keys)
    double dx = 0;
    double dy = 0;
    bool perimeter = true;
};

// mxGraph.cellConnected (mxGraph.js:7587) with draw.io's
// resetEdgesOnConnect = false (Graph.js:1023), ports disabled:
//  * `terminal` must be an existing vertex or edge other than `edge` itself
//    (loops to the same vertex as both ends are allowed); `edge` must be an
//    edge.
//  * When `constraint` is given (mxGraph.setConnectionConstraint,
//    mxGraph.js:7349): source end writes exitX/exitY/exitDx/exitDy, target
//    end entryX/entryY/entryDx/entryDy into the edge style (numbers via
//    core::formatNumber), and exitPerimeter/entryPerimeter=0 only when
//    !perimeter (the key is removed when perimeter is true); a constraint
//    without point removes all five keys. No constraint: style untouched.
//  * The source/target attribute is set. The stored terminal point and the
//    waypoints are kept.
Result connectEdge(Transaction& tx, std::size_t page, const std::string& edge, const std::string& terminal,
                   bool source, std::optional<ConnectionConstraint> constraint = std::nullopt);

// Detaches one end of `edge` (mxEdgeHandler disconnect): removes the
// source/target attribute and stores `point` (parent coordinates) as the
// sourcePoint/targetPoint. Failure when `edge` is not an edge. Disconnecting
// an end that is already free only updates the point.
Result disconnectEdge(Transaction& tx, std::size_t page, const std::string& edge, bool source,
                      const core::Point& point);

// Replaces the waypoints (<Array as="points">) of `edge`; an empty list
// removes the array (core::Geometry::setPoints). Creates the geometry
// (relative="1", as="geometry") when missing. Failure when `edge` is not an
// edge. Unchanged points -> success without a command.
Result setWaypoints(Transaction& tx, std::size_t page, const std::string& edge,
                    const std::vector<core::Point>& points);

} // namespace qraw::commands::ops
