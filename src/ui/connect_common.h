#pragma once
// Helpers shared by the connect (HoverIcons) and edge handle handlers:
// connection points, terminal hit testing and the overlay drawing of both.
// Private to the editor UI.

#include "qraw/commands/ops_cells.h"
#include "qraw/core/page.h"
#include "qraw/render/page_view.h"
#include "qraw/ui/interaction.h"

#include <QPointF>
#include <QRectF>

#include <optional>
#include <string>
#include <vector>

class QPainter;

namespace qraw::ui::connect {

// A connection point snaps within 10 px of the pointer (mxConstraintHandler
// tolerance, mxgraph/src/handler/mxConstraintHandler.js).
inline constexpr double kPointSnapPixels = 10;

// A fixed connection point of a terminal: the constraint as it is written
// into the edge style plus the absolute position it denotes.
struct ConnectionPoint {
    commands::ops::ConnectionConstraint constraint;
    QPointF position;
};

[[nodiscard]] QRectF toQRect(const core::Rect& rect);
[[nodiscard]] QPointF toQPoint(const core::Point& point);
[[nodiscard]] core::Point toPoint(QPointF point);

[[nodiscard]] const core::Page* currentPage(const ToolContext& context);

// The connection points of a vertex (Graph.getAllConnectionConstraints,
// js/grapheditor/Graph.js:35331): the entries of the resolved style `points`
// ("[[x,y,perimeter,dx,dy],...]"), or the four side midpoints when the style
// has none. Flip and rotation of the shape are applied. Edges have none.
[[nodiscard]] std::vector<ConnectionPoint> connectionPoints(const render::CellState& state);

// The point closest to `p` within `radius` (page units).
[[nodiscard]] std::optional<ConnectionPoint> nearestConnectionPoint(const std::vector<ConnectionPoint>& points,
                                                                    QPointF p, double radius);

// Not locked, connectable (cell attribute and style) and selectable
// (mxGraph.isCellConnectable plus Graph.isCellLocked).
[[nodiscard]] bool isConnectableTerminal(const core::Page& page, const render::CellState& state);

// The topmost connectable cell under the pointer other than `exclude`.
// Vertices and edges (edge-to-edge connections are allowed).
[[nodiscard]] std::string terminalAt(const ToolContext& context, const CanvasEvent& event,
                                     const std::vector<std::string>& exclude);

// 2 px kValid outline around a terminal that would accept the connection.
void paintTerminalHighlight(QPainter& painter, const OverlayContext& context, const render::CellState& state);
// A connection point as a small cross (HoverIcons / mxConstraintHandler
// draw an 'x'); `highlighted` draws it larger in kValid.
void paintConnectionPoint(QPainter& painter, const OverlayContext& context, QPointF position, bool highlighted);

} // namespace qraw::ui::connect
