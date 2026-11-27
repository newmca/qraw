#pragma once

// Helpers shared by the child layouts (ops_layout.cpp: stackLayout,
// ops_tables.cpp: tableLayout). Internal to qraw_commands.

#include "qraw/commands/result.h"
#include "qraw/core/cell.h"
#include "qraw/core/page.h"
#include "qraw/core/style.h"

#include <cstddef>
#include <string>
#include <string_view>

namespace qraw::commands {
class Transaction;
}

namespace qraw::commands::ops::detail {

// Numeric style value (a missing or unparsable value gives `fallback`).
double number(const core::Style& style, std::string_view key, double fallback);
// mxUtils.getValue(style, key, fallback) == '1'
bool flag(const core::Style& style, std::string_view key, bool fallback);

// mxGraph.getActualStartSize / getActualFooterSize result: left, top, right
// and bottom strip sizes (upstream returns them as x, y, width and height).
struct Insets {
    double left = 0, top = 0, right = 0, bottom = 0;
};

// Graph.isSwimlane (Graph.js:34420): swimlanes, and tables and table rows,
// whose strips are laid out like a swimlane's title. queries.h isSwimlane
// leaves tables out on purpose (they are no drop-target containers there).
bool isSwimlaneLike(const core::Page& page, const core::Cell& cell);

// mxGraph.getActualStartSize(cell, true) (mxGraph.js:10029): the title strip
// of a swimlane, table or table row on the side its direction puts it.
Insets startInsets(const core::Page& page, const core::Cell& cell);

// mxGraph.getActualFooterSize (mxGraph.js:10075): opposite the title of a
// swimlane or table, at the bottom of any other cell.
Insets footerInsets(const core::Page& page, const core::Cell& cell);

// mxGraph.isCellMovable: a cell is locked when it or an ancestor has `locked=1`;
// it is movable when it is not locked and has no `movable=0`.
bool isLocked(const core::Page& page, const core::Cell& cell);
bool isMovable(const core::Page& page, const core::Cell& cell);

// What a layout does with the edge style of the edges it resets.
enum class EdgeRouting {
    Keep,       // the style stays
    Straight,   // noEdgeStyle=1 (mxGraphLayout.setEdgeStyleEnabled(edge, false))
    Elbow,      // elbowEdgeStyle with a vertical elbow
    Orthogonal, // orthogonalEdgeStyle (the edgeStyle of mxHierarchicalLayout's ELK replacement)
};

void applyRouting(core::Style& style, EdgeRouting routing);

// ops_layout_graph.cpp: the Graph.js initLayoutManager configurations of
// `childLayout=treeLayout` (mxCompactTreeLayout) and `circleLayout`
// (mxCircleLayout) for the children of `parent`.
Result executeTreeChildLayout(Transaction& tx, std::size_t page, const std::string& parent);
Result executeCircleChildLayout(Transaction& tx, std::size_t page, const std::string& parent);
// and `childLayout=flowLayout` (mxHierarchicalLayout).
Result executeFlowChildLayout(Transaction& tx, std::size_t page, const std::string& parent);

// ops_tables.cpp:
// Graph.js TableLayout.execute for the children of the table `parent`.
Result executeTableLayout(Transaction& tx, std::size_t page, const std::string& parent);

} // namespace qraw::commands::ops::detail
