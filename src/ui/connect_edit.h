#pragma once
// The model side of connecting: builds the edge (and the cloned target
// vertex) of a connect gesture and reconnects edge ends, entirely through
// commands::ops. No Qt widgets here, so it is testable on its own.
// Private to the editor UI.

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/result.h"
#include "qraw/commands/transaction.h"

#include <cstddef>
#include <optional>
#include <string>

namespace qraw::ui::connect {

// Graph.createCurrentEdgeStyle with draw.io's default connector
// (js/grapheditor/Graph.js:34078, Graph.prototype.defaultEdgeStyle).
inline constexpr char kDefaultEdgeStyle[] = "edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;";
// Graph.defaultEdgeLength: the gap between a vertex and the clone a click on
// a hover arrow creates (js/grapheditor/Graph.js, EditorUi.defaultEdgeLength).
inline constexpr double kDefaultEdgeLength = 80;

// One new edge starting at `source`. Exactly one end kind is used, in this
// order of precedence: `target` (existing terminal), `cloneBounds` (a copy
// of the source is created there), else a dangling end at `freePoint`.
struct NewConnection {
    std::string source;
    std::optional<commands::ops::ConnectionConstraint> sourceConstraint;

    std::string target;
    std::optional<commands::ops::ConnectionConstraint> targetConstraint;

    std::optional<core::Rect> cloneBounds; // absolute page coordinates
    std::optional<core::Point> freePoint;  // absolute page coordinates

    // Parent of the clone and of dangling edges (DocumentSession::insertionParent).
    std::string insertionParent;
};

// Adds the edge with the default style (and the clone) and connects it
// (mxConnectionHandler.connect :1918 / HoverIcons connectVertex). The edge's
// parent is the nearest common ancestor of its terminals (the layer of the
// source when that would be the root). Result.cells = the clone when one was
// created (draw.io selects it), else the edge.
[[nodiscard]] commands::Result addConnection(commands::Transaction& tx, std::size_t page,
                                             const NewConnection& request);

// Removes exit*/entry* style keys of one end (mxEdgeHandler.connect with an
// empty constraint = floating end). A no-op when the edge has none.
[[nodiscard]] commands::Result clearEndConstraint(commands::Transaction& tx, std::size_t page,
                                                  const std::string& edge, bool source);

// mxEdgeHandler.connect: connects an end to `terminal`; without a
// constraint the end floats (stale exit/entry keys are removed).
[[nodiscard]] commands::Result reconnectEnd(commands::Transaction& tx, std::size_t page, const std::string& edge,
                                            const std::string& terminal, bool source,
                                            const std::optional<commands::ops::ConnectionConstraint>& constraint);

// mxEdgeHandler.changeTerminalPoint: detaches an end at `point` (absolute
// page coordinates; stored in the edge parent's coordinates).
[[nodiscard]] commands::Result detachEnd(commands::Transaction& tx, std::size_t page, const std::string& edge,
                                         bool source, const core::Point& point);

} // namespace qraw::ui::connect
