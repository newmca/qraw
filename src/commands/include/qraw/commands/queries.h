#pragma once

// Read-only model queries shared by the operations, ported from mxGraphModel
// / mxGraph where noted. All take ids on one page; unknown ids are ignored
// (or yield the documented neutral value). Paths cite
// $DRAWIO_SRC/src/main/webapp/ (Apache-2.0).

#include "qraw/core/geometry.h"
#include "qraw/core/page.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace qraw::commands {

class Transaction;

// mxGraphModel.isAncestor (mxgraph/src/model/mxGraphModel.js): true when
// `ancestor` is `cell` or one of its parents. Cycle-safe.
[[nodiscard]] bool isAncestor(const core::Page& page, std::string_view ancestor, std::string_view cell);
// True when `cell` or one of its ancestors is in `ids`: isAncestor against a
// whole set, in O(depth) instead of O(|ids| * depth).
[[nodiscard]] bool isWithin(const core::Page& page, const std::unordered_set<std::string>& ids, std::string_view cell);

// mxGraphModel.getOrigin (mxGraphModel.js:896): the absolute position of the
// coordinate system `cellId`'s children use, i.e. the sum of geometry x/y of
// `cellId` and its ancestors, skipping edges and cells without geometry.
// Note: the relative flag is *not* checked, exactly as upstream. "" or an
// unknown id gives (0,0).
[[nodiscard]] core::Point origin(const core::Page& page, std::string_view cellId);

// mxGeometry.translate (mxgraph/src/model/mxGeometry.js:299) on our lossless
// geometry: moves x/y unless the geometry is relative or `ignorePosition`
// (draw.io passes isEdge(cell)), and always moves sourcePoint, targetPoint
// and the waypoints. Only the touched values are rewritten.
void translateGeometry(core::Geometry& geometry, double dx, double dy, bool ignorePosition);

// mxGraphModel.getTopmostCells: `ids` without the cells that have an
// ancestor in `ids`, deduplicated, in input order.
[[nodiscard]] std::vector<std::string> topmostCells(const core::Page& page, const std::vector<std::string>& ids);

// mxUtils.sortCells(cells, true) for one page: `ids` (deduplicated, unknown
// ids dropped) in ascending z-order, i.e. entry order.
[[nodiscard]] std::vector<std::string> sortByZOrder(const core::Page& page, const std::vector<std::string>& ids);

// Edges whose source or target is `cellId`, in z-order.
[[nodiscard]] std::vector<std::string> connectedEdges(const core::Page& page, std::string_view cellId);

// mxGraph.getAllEdges (mxgraph/src/view/mxGraph.js:5782): edges connected
// to any of `ids` or to any of their descendants, deduplicated, in z-order.
// May include edges that are themselves in `ids` or among the descendants.
[[nodiscard]] std::vector<std::string> allEdges(const core::Page& page, const std::vector<std::string>& ids);

// mxGraphModel.getNearestCommonAncestor: the deepest cell that is an
// ancestor of (or equal to) both; "" when none.
[[nodiscard]] std::string nearestCommonAncestor(const core::Page& page, std::string_view a, std::string_view b);

// The layer (child of the root cell) that contains `cellId`, or "" when the
// cell is the root, a layer's ancestor chain is broken, or the id is unknown.
// A layer is its own layer.
[[nodiscard]] std::string layerOf(const core::Page& page, std::string_view cellId);

// Sum of x/y over the ancestors of `cellId` (not the cell itself) that have
// a geometry that is neither relative nor on an edge: the absolute origin of
// the cell's coordinate system from geometries alone. This is the
// `geometryOrigin` helper of mxGraph.cellsRemoved (mxGraph.js:5161-5180);
// it differs from origin() by skipping relative geometries and the cell.
[[nodiscard]] core::Point geometryOrigin(const core::Page& page, std::string_view cellId);

// mxGraph.translateCell (mxGraph.js:6816-6866) as a pure function: returns a
// copy of `cell` with its geometry moved by (dx, dy). Non-relative or edge
// geometries use translateGeometry(geo, dx, dy, cell.isEdge()). A relative
// non-edge geometry instead gets offset += (round(dx'), round(dy')), where
// (dx', dy') is (dx, dy) rotated by -rotation (style `rotation` of the parent
// when the parent is a vertex, else 0); a missing offset is created as
// (round(dx'), round(dy')). A cell without geometry is returned unchanged.
// No clamping (draw.io allows negative coordinates).
[[nodiscard]] core::Cell translatedCell(const core::Page& page, const core::Cell& cell, double dx, double dy);

// Approximates mxGraph.isTransparentState (mxGraph.js:10140) without the
// render module's stylesheet: true when the effective strokeColor and
// fillColor are both "none" and the cell shows no image. Effective colors
// are the style's own `strokeColor` / `fillColor` keys; when a key is absent
// the default comes from the first bare style name: "group" and "text" are
// transparent (draw.io's default stylesheet gives both none/none), anything
// else -- including no name -- is opaque (default vertex white/black, edge
// stroke black). An `image` key or `shape=image` means not transparent.
[[nodiscard]] bool isTransparentCell(const core::Cell& cell);

// True when `id` is non-empty and names a cell on the page. Operations call
// this on every id they are asked to edit. Duplicate ids are not detected
// here (that would make every lookup O(n)); core::Page::validate() reports
// them on load, and the first cell with the id is the one edited.
[[nodiscard]] bool isAddressable(const core::Page& page, std::string_view id);

// Entry range [first, last] covered by `cellId` and all its descendants, as
// entry indices; nullopt for an unknown id. In files written by draw.io the
// subtree is contiguous (depth-first order), but this only reports the
// extremes.
struct EntryRange {
    std::size_t first = 0;
    std::size_t last = 0;
};
[[nodiscard]] std::optional<EntryRange> subtreeEntryRange(const core::Page& page, std::string_view cellId);

// --- containers --------------------------------------------------------------

// Graph.isSwimlane (js/grapheditor/Graph.js:34420, mxGraph.js:10277) judged
// by the cell's own style, without a stylesheet: a vertex or other non-edge
// cell below a layer whose `shape` is `swimlane` or, when the style has no
// `shape`, whose first bare style name is `swimlane` (the default
// stylesheet maps that name to the shape). Tables are not included (table
// support is a later package).
[[nodiscard]] bool isSwimlane(const core::Page& page, const core::Cell& cell);

// Graph.isContainer (Graph.js:19548): a swimlane unless `container=0`, any
// other cell only with `container=1`.
[[nodiscard]] bool isContainer(const core::Page& page, const core::Cell& cell);

// The swimlane title size: style `startSize`, else 23 for the named style
// `swimlane` (default.xml) and 40 (mxConstants.DEFAULT_STARTSIZE) otherwise.
// 0 for cells that are not swimlanes.
[[nodiscard]] double swimlaneStartSize(const core::Page& page, const core::Cell& cell);

// --- mutation helpers built on primitives ----------------------------------

// Removes `cellId` and all its descendants (deepest first) with RemoveCell
// primitives. Edges outside the subtree are not touched. Returns false (after
// rolling back what it removed) if any removal failed or the cell is unknown.
[[nodiscard]] bool removeSubtree(Transaction& tx, std::size_t page, std::string_view cellId);

// Moves `cellId` with all its descendants (keeping their relative entry
// order) so that the block starts at the position of entry
// `beforeEntryIndex` (see ReorderCells). Returns false on failure.
[[nodiscard]] bool moveSubtree(Transaction& tx, std::size_t page, std::string_view cellId,
                               std::size_t beforeEntryIndex);

} // namespace qraw::commands
