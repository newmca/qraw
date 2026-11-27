#pragma once
// Factories for the canvas interaction handlers. Each group of tools has its
// own factory; installDefaultHandlers (interaction.cpp) calls them all.

#include "qraw/ui/interaction.h"

#include <memory>
#include <utility>
#include <vector>

namespace qraw::ui {

using HandlerList = std::vector<std::pair<std::unique_ptr<InteractionHandler>, int /*priority*/>>;

// Selection tools (src/ui/select_*.cpp):
//  * PrioritySelection: click / shift / ctrl selection, group drill-down,
//    rubber band, move (with Snapper grid + guides, Ctrl+drag or
//    Alt+drag duplicates, drop into containers optional), keyboard nudge,
//    Escape clears, double click on a group enters it, double click on a
//    blank area inside an entered group exits it.
//  * PriorityVertexHandles: the 8 resize handles and the rotation handle of
//    a single selected vertex (or the bounding box of several), drawing the
//    selection outlines of all selected cells.
HandlerList createSelectionHandlers(const ToolContext& context);

// Edge and connect tools (src/ui/edge_*.cpp, src/ui/connect_*.cpp):
//  * PriorityEdgeHandles: selected edges' end, waypoint and virtual
//    (segment midpoint) handles: reconnect ends, move/add/remove waypoints.
//  * PriorityConnect: hover arrows (HoverIcons) and connection points of
//    the vertex under the mouse; drag from them creates an edge.
HandlerList createEdgeHandlers(const ToolContext& context);

} // namespace qraw::ui
