#pragma once
// The collapse/expand control (mxCellRenderer.createControl,
// mxGraph.getFoldingImage): a 9 x 9 box with a minus (expanded) or a plus
// (collapsed) near the top-left corner of foldable containers. Drawn in the
// editing overlay only, so exports and prints never show it. Private to the
// editor UI.

#include "qraw/render/page_view.h"
#include "qraw/ui/interaction.h"
#include "qraw/ui/tool_factories.h"

#include <QCoreApplication>
#include <QPointF>
#include <QRectF>

#include <memory>

namespace qraw::ui {

// Graph.isCellFoldable (js/grapheditor/Graph.js:21559) without tree folding:
// a vertex that is resizable, is not a transparentBounds group and is not
// locked, and is a container with collapsible != 0 or any cell with
// collapsible=1.
[[nodiscard]] bool isFoldable(const core::Page& page, const render::CellState& state);

// mxCellRenderer.getControlBounds for the 9 x 9 icon: centred 9 units from
// the top-left corner, in the unrotated frame of the state.
[[nodiscard]] QRectF foldControlRect(const render::CellState& state);

// True when `scenePos` is on the control of `state` (rotation respected).
[[nodiscard]] bool hitsFoldControl(const render::CellState& state, QPointF scenePos);

// Paints the control of every foldable visible cell; a click on it folds the
// cell as one edit ("Collapse" / "Expand").
class FoldControlHandler : public InteractionHandler {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::FoldControlHandler)

public:
    explicit FoldControlHandler(ToolContext context) : ctx_(context) {}

    std::optional<Qt::CursorShape> hover(const CanvasEvent& event) override;
    std::unique_ptr<Gesture> press(const CanvasEvent& event) override;
    void paintOverlay(QPainter& painter, const OverlayContext& context) const override;
    // The controls change only with the cells they sit on, whose damage covers them.
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext& context) const override
    {
        (void)context;
        return QRectF();
    }

private:
    // The foldable state whose control is under the point, topmost first; nullptr if none.
    [[nodiscard]] const render::CellState* controlAt(QPointF scenePos) const;
    void toggle(const std::string& id);

    ToolContext ctx_;
    friend class FoldClickGesture;
};

} // namespace qraw::ui
