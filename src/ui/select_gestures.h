#pragma once
// The drag gestures of the selection handler: move (with snapping, guides
// and Ctrl+drag duplication) and the rubber band. Private to the editor UI.

#include "drop_target.h"
#include "select_support.h"
#include "qraw/ui/snapping.h"

#include <QCoreApplication>
#include <QPointF>
#include <QRectF>

#include <memory>
#include <string>
#include <vector>

namespace qraw::ui {

// A press that moved less than this many view pixels is a click
// (mxGraphHandler.tolerance, mxGraph.tolerance).
inline constexpr double kDragThreshold = 3;

// What a press without drag does on release (mxGraphHandler.mouseUp
// selects or deselects the clicked cell when the pointer did not move).
struct ClickAction {
    enum class Kind { None, SelectOnly, Deselect };
    Kind kind = Kind::None;
    std::string id;
};

// mxGraphHandler: moves `moving` (already filtered for locked / movable=0
// cells) after the pointer left the click tolerance. Preview only; one
// edit on release.
class MoveGesture : public Gesture {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::SelectionHandler)

public:
    MoveGesture(ToolContext context, std::shared_ptr<SelectionUiState> state, std::vector<std::string> moving,
                const CanvasEvent& press, ClickAction click);

    void move(const CanvasEvent& event) override;
    void release(const CanvasEvent& event) override;
    void cancel() override;
    void paint(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext& context) const override;
    [[nodiscard]] Qt::CursorShape cursor() const override;

private:
    void update(const CanvasEvent& event);
    void applyClickAction();
    // True when an edit was applied.
    bool commitMove(QPointF delta, bool duplicate, QPointF pointer);
    [[nodiscard]] QPointF constrainedDelta(const CanvasEvent& event) const;

    ToolContext ctx_;
    std::shared_ptr<SelectionUiState> state_;
    std::unique_ptr<GestureScope> hidingHandles_; // set once the pointer left the click tolerance
    std::vector<std::string> moving_;
    QRectF bounds_;           // union of the moving cells' CellState::bounds
    QPointF pressScene_;
    QPointF pressView_;
    ClickAction click_;
    bool dragging_ = false;
    QPointF delta_;           // snapped translation shown by the preview
    std::vector<QLineF> guides_;
    bool duplicate_ = false;
    bool lifted_ = false;     // the active layer holds the moving cells
    MoveDestination destination_; // container under the pointer and the parent the cells get
};

// mxRubberband: selects the cells inside the rectangle on release.
class RubberbandGesture : public Gesture {
public:
    RubberbandGesture(ToolContext context, const CanvasEvent& press);

    void move(const CanvasEvent& event) override;
    void release(const CanvasEvent& event) override;
    void cancel() override {}
    void paint(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext& context) const override;

private:
    ToolContext ctx_;
    QPointF startScene_;
    QPointF startView_;
    QPointF currentScene_;
    bool toggle_;
    bool dragging_ = false;
};

} // namespace qraw::ui
