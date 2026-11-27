#pragma once
// The link icon of a cell that has a link: a small badge in the top-right corner of
// the vertex under the pointer and of every selected vertex with a link; a click
// on it follows the link (actions::followLink: a page link switches the page,
// http, https and mailto open in the desktop's handler; nothing is fetched).
// Drawn in the editing overlay only, so exports and prints never show it. draw.io
// has the same icon (Graph.createLinkOverlay, shown with Editor.showLinkIcons)
// and its Open Link action; Ctrl+click stays the selection toggle here.
// Private to the editor UI.

#include "qraw/render/page_view.h"
#include "qraw/ui/interaction.h"
#include "qraw/ui/tool_factories.h"

#include <QRectF>

#include <string>
#include <vector>

namespace qraw::ui {

// The badge of `state` for a view at `scale` (page units per... view pixels per page unit), or a
// null rectangle when the cell is too small to carry one. Unrotated, in scene coordinates.
[[nodiscard]] QRectF linkBadgeRect(const render::CellState& state, double scale);

class LinkBadgeHandler : public InteractionHandler {
public:
    explicit LinkBadgeHandler(ToolContext context) : ctx_(context) {}

    std::optional<Qt::CursorShape> hover(const CanvasEvent& event) override;
    void hoverLeft() override { hovered_.clear(); }
    std::unique_ptr<Gesture> press(const CanvasEvent& event) override;
    void paintOverlay(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext& context) const override;
    void reset() override { hovered_.clear(); }

private:
    struct Badge {
        std::string id;
        QRectF rect;
    };
    // The badges shown now: the hovered vertex and the selected ones that have a link.
    [[nodiscard]] std::vector<Badge> badges(double scale) const;
    // The id of the vertex whose link covers the point: the topmost cell under it, or its nearest ancestor with a link.
    [[nodiscard]] std::string linkedCellAt(QPointF scenePos, double tolerance) const;
    [[nodiscard]] const Badge* badgeAt(const std::vector<Badge>& all, QPointF scenePos) const;

    ToolContext ctx_;
    std::string hovered_;
    friend class LinkClickGesture;
};

} // namespace qraw::ui
