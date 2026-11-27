#pragma once

// Child layouts (draw.io's mxLayoutManager with the configuration of
// js/grapheditor/Graph.js initLayoutManager, :12656): a cell whose style has
// `childLayout=<name>` arranges its child vertices whenever they, it or its
// ancestors change. Supported: `stackLayout` (mxStackLayout), `tableLayout`
// (ops_tables.h), `treeLayout`, `circleLayout` and `flowLayout` (layout_graph.h).
// Other values (rack, organicLayout ...) are kept in the file and not executed; add one by writing its handler and listing it
// in handlerFor() in ops_layout.cpp.
//
// Layouts run only when a user edit asks for them (DocumentSession::edit
// calls applyLayouts inside the edit's transaction, so the edit and its
// layout are one undo step). Opening, saving and the CLI never run them:
// that keeps an untouched file byte-identical (ARCHITECTURE.md lossless rules).
// Upstream paths are relative to
// $DRAWIO_SRC/src/main/webapp/.

#include "qraw/commands/change_set.h"
#include "qraw/commands/result.h"
#include "qraw/core/cell.h"
#include "qraw/core/geometry.h"
#include "qraw/core/page.h"

#include <cstddef>
#include <string>
#include <vector>

namespace qraw::commands {
class Transaction;
}

namespace qraw::commands::ops {

// True when the cell's style has a childLayout this module executes.
[[nodiscard]] bool hasChildLayout(const core::Cell& cell);

// mxLayoutManager.executeLayoutForCells(getCellsForChanges(changes))
// (mxgraph/src/view/mxLayoutManager.js) for the cells of page `page` that
// `changes` names:
//  * every added, changed or reordered cell, and every cell that gained,
//    lost or reordered a child (PageChanges::childrenChanged; the former
//    parent of a removed or reparented cell is only known from there)
//    contributes itself and its ancestors that have a layout (bubbling),
//    and, when it has a layout itself, its descendants that have one;
//  * collapsed cells do not lay out (Graph.js getLayout);
//  * in z-order: first the layouts nested in another layout, deepest first
//    (BEGIN_UPDATE pass: a child layout runs before its parent resizes it),
//    then all layouts, outermost first (END_UPDATE pass); each at most once
//    per pass.
// A no-op, and success, when no cell with a supported layout is affected or
// the page was reset wholesale.
Result applyLayouts(Transaction& tx, std::size_t page, const ChangeSet& changes);

// mxLayoutManager.cellsMoved (mxLayoutManager.js): after cells were dropped
// or moved at `point` (page coordinates), lets the layout of each cell's
// parent decide its position among its siblings. For stackLayout that is
// mxStackLayout.moveCell: the cell becomes the child before the first
// sibling whose centre lies beyond the point on the stack axis. The cells
// keep their geometry; applyLayouts then arranges them. Cells whose parent
// has no such layout are ignored. Result.cells = ids.
Result moveInLayout(Transaction& tx, std::size_t page, const std::vector<std::string>& ids, const core::Point& point);

// --- Arrange > Layout ---------------------------------------------------------------
// The layouts of the Arrange menu (layout_graph.h), run on demand inside an
// edit: vertices move, the waypoints of the edges between them are reset and
// the edge style follows the kind. Locked vertices stay where they are.

enum class LayoutKind {
    VerticalTree,   // tidy tree, levels top to bottom; edges keep their style
    HorizontalTree, // tidy tree, levels left to right; edges keep their style
    OrgChart,       // vertical tree, more room, elbow edges
    RadialTree,     // root in the centre, levels on rings; straight edges
    Circle,         // mxCircleLayout; straight edges
    HorizontalFlow, // layered, edges point right; orthogonal edges
    VerticalFlow,   // layered, edges point down; orthogonal edges
    Organic,        // force directed; edges keep their style
};

// What a layout acts on.
struct LayoutScope {
    std::string parent;                // the cell whose children are laid out (a layer, group or container)
    std::vector<std::string> vertices; // restricts the layout to these children; empty: all visible vertices
    std::string root;                  // trees: lay out only this vertex and what follows it; empty: the tree roots
};

// The scope draw.io's layout menu uses for the selection (Menus.js layout
// items): one selected vertex with children is a container whose children are
// laid out; several vertices with the same parent are laid out among
// themselves; one vertex without children is the root of a tree in its parent
// (circle layouts take the whole parent); no vertex selected means everything
// in `defaultParent` (the entered group or the current layer).
[[nodiscard]] LayoutScope layoutScopeForSelection(const core::Page& page, const std::vector<std::string>& selection,
                                                  const std::string& defaultParent);

// True when `kind` has something to arrange in `scope`: a tree layout needs a
// root with at least one outgoing edge (graph.findTreeRoots, or the given
// root); flows, organic and circle need two vertices and take every vertex of
// the scope's parent (or the scope's own vertices).
[[nodiscard]] bool canArrangeLayout(const core::Page& page, LayoutKind kind, const LayoutScope& scope);

// Runs the layout. Failure (nothing changes) when canArrangeLayout is false.
Result arrangeLayout(Transaction& tx, std::size_t page, LayoutKind kind, const LayoutScope& scope);

} // namespace qraw::commands::ops
