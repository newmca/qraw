#pragma once
// PriorityEdgeHandles: the handles of the single selected edge
// (mxEdgeHandler, mxgraph/src/handler/mxEdgeHandler.js). Private to
// the editor UI.
#include "edge_handle_model.h"

#include "qraw/ui/interaction.h"

#include <optional>

namespace qraw::ui::edgeh {

// Handles exist while exactly one edge is selected and it is not locked.
// Behaviour:
//  * end handles reconnect or disconnect (EdgeEndGesture);
//  * waypoint, virtual and elbow handles move, add or set waypoints
//    (EdgePointGesture);
//  * Shift+click on the edge adds a waypoint (isAddPointEvent :439),
//    Shift+click or double click on a waypoint handle removes it
//    (removePoint :2552).
class EdgeHandlesHandler : public InteractionHandler {
public:
    explicit EdgeHandlesHandler(const ToolContext& context) : context_(context) {}

    std::optional<Qt::CursorShape> hover(const CanvasEvent& event) override;
    void hoverLeft() override;
    std::unique_ptr<Gesture> press(const CanvasEvent& event) override;
    bool doubleClick(const CanvasEvent& event) override;
    void paintOverlay(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext& context) const override;
    void reset() override;

    // The handles of the selected edge for pixel size `pixel`; nullopt when
    // there is none. Shared with the label editor, which must leave double
    // clicks on a waypoint handle to this handler.
    [[nodiscard]] static std::optional<EdgeHandles> selectedEdgeHandles(const ToolContext& context, double pixel);

private:
    bool removeWaypoint(const EdgeHandles& handles, std::size_t index);
    bool addWaypointAt(const EdgeHandles& handles, QPointF position);

    ToolContext context_;
    std::optional<Handle> hovered_;
};

} // namespace qraw::ui::edgeh
