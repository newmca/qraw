#pragma once
// Private helpers shared by the selection and vertex-handle handlers
// Not part of the public headers.

#include "qraw/core/page.h"
#include "qraw/render/page_view.h"
#include "qraw/ui/interaction.h"

#include <QPainter>
#include <QPen>
#include <QPointF>
#include <QRectF>

#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace qraw::ui {

class DiagramScene;

// State shared by the two handlers so that the vertex handles (and the
// selection outlines) are hidden while a move, resize or rotate preview is
// on screen, as draw.io hides the handler of the cells being dragged.
struct SelectionUiState {
    int activeGestures = 0;
    [[nodiscard]] bool gestureRunning() const { return activeGestures > 0; }
};

// RAII marker for SelectionUiState::activeGestures.
class GestureScope {
public:
    explicit GestureScope(std::shared_ptr<SelectionUiState> state) : state_(std::move(state)) { ++state_->activeGestures; }
    ~GestureScope() { --state_->activeGestures; }
    GestureScope(const GestureScope&) = delete;
    GestureScope& operator=(const GestureScope&) = delete;

private:
    std::shared_ptr<SelectionUiState> state_;
};

[[nodiscard]] QRectF toQRect(const core::Rect& rect);
[[nodiscard]] core::Rect toCoreRect(const QRectF& rect);

// Rotates `p` around `center` by `degrees` (clockwise on screen: y points down).
[[nodiscard]] QPointF rotateAround(QPointF p, QPointF center, double degrees);
// Angle of the vector in degrees, atan2(y, x) with y pointing down.
[[nodiscard]] double angleOf(QPointF vector);
// degrees folded into [0, 360).
[[nodiscard]] double normalizeAngle(double degrees);

// style flag with draw.io's default of "allowed" (`movable`, `resizable`, `rotatable`).
[[nodiscard]] bool styleAllows(const render::CellState& state, const char* key);

// A cell the user may move: not locked (itself or an ancestor) and movable != 0.
[[nodiscard]] bool isMovable(const core::Page& page, const render::CellState& state);

// queries.h topmostCells(selection) without the cells that cannot move.
[[nodiscard]] std::vector<std::string> movableSelection(const DiagramScene& scene, const core::Page& page,
                                                         const std::vector<std::string>& selection);

// Union of the CellState::bounds (not paintBounds) of `ids`.
[[nodiscard]] QRectF stateBoundsUnion(const DiagramScene& scene, const std::vector<std::string>& ids);

// The child of `ancestor` that contains `descendant` (or `descendant` itself
// when it is a direct child); "" when `descendant` is not below `ancestor`.
// One drill-down step of Graph.selectCellForEvent.
[[nodiscard]] std::string childToward(const core::Page& page, const std::string& ancestor,
                                      const std::string& descendant);

// Dashed overlay::kSelection pen, one screen pixel wide (cosmetic).
[[nodiscard]] QPen selectionPen();

// Dashed outline of a cell translated by `delta`: the rotated bounds of a
// vertex, the polyline of an edge.
void paintCellOutline(QPainter& painter, const render::CellState& state, QPointF delta = {});
// Dashed outline of a rectangle rotated around its centre.
void paintRotatedRect(QPainter& painter, const QRectF& rect, double rotationDegrees);
// The same with another pen.
void paintRotatedRect(QPainter& painter, const QRectF& rect, double rotationDegrees, const QPen& pen);
// The drop target highlight (mxCellHighlight, mxConstants.DROP_TARGET_COLOR: blue, green in the dark
// scheme): a solid outline, three screen pixels wide, around the container a drag would land in.
void paintDropTarget(QPainter& painter, const render::CellState& state, bool dark);

} // namespace qraw::ui
