#pragma once
// The drags of edge handles. Private to the editor UI.

#include "connect_common.h"
#include "edge_handle_model.h"
#include "edge_segments.h"

#include "qraw/ui/interaction.h"

#include <optional>
#include <string>
#include <vector>

namespace qraw::ui::edgeh {

// One edit "Change Points" (ops::setWaypoints). Returns false when it failed.
bool applyWaypoints(const ToolContext& context, const std::string& edge, const std::vector<core::Point>& points);

// Drag of the source or target handle (mxEdgeHandler terminal handles).
// Preview: the dashed route the edge will have with the end at the pointer
// or the hovered terminal (its style and waypoints applied); the
// connectable cell under the pointer is outlined in kValid. Release over a
// terminal: ops::connectEdge (a fixed constraint when within 10 px of a
// connection point), over blank canvas ops::disconnectEdge with the snapped
// pointer as the terminal point. One edit, "Connect" or "Disconnect".
// A press released without movement changes nothing.
class EdgeEndGesture : public Gesture {
public:
    EdgeEndGesture(const ToolContext& context, EdgeHandles handles, Handle handle, const CanvasEvent& pressEvent);

    void move(const CanvasEvent& event) override;
    void release(const CanvasEvent& event) override;
    void cancel() override {}
    void paint(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] Qt::CursorShape cursor() const override { return Qt::PointingHandCursor; }

private:
    void update(const CanvasEvent& event);
    // The route with the dragged end at its current target.
    [[nodiscard]] std::vector<QPointF> routedPreview() const;

    ToolContext context_;
    EdgeHandles handles_;
    Handle handle_;
    QPointF pressViewPos_;
    QPointF current_;
    bool moved_ = false;
    std::string target_;
    QPointF endPosition_; // where the preview line ends (snapped point or connection point)
    std::optional<connect::ConnectionPoint> targetPoint_;
};

// Drag of a waypoint, a virtual (segment midpoint) handle or the elbow
// handle (mxEdgeHandler.changePoints / addPointAt, mxElbowEdgeHandler).
// A virtual handle inserts a waypoint at its index once the pointer moved.
// Points snap to the grid. One edit "Change Points" on release; a press
// without movement changes nothing.
class EdgePointGesture : public Gesture {
public:
    EdgePointGesture(const ToolContext& context, EdgeHandles handles, Handle handle, const CanvasEvent& pressEvent);

    void move(const CanvasEvent& event) override;
    void release(const CanvasEvent& event) override;
    void cancel() override {}
    void paint(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] Qt::CursorShape cursor() const override { return Qt::SizeAllCursor; }

private:
    void update(const CanvasEvent& event);
    // The waypoint list with the dragged point applied, in the edge parent's
    // coordinates (untouched points keep their stored values).
    [[nodiscard]] std::vector<core::Point> pointsWithDrag() const;
    [[nodiscard]] std::vector<QPointF> previewWaypoints() const;

    ToolContext context_;
    EdgeHandles handles_;
    Handle handle_;
    QPointF pressViewPos_;
    QPointF current_;
    bool moved_ = false;
};

// Drag of a segment handle of an orthogonal edge (mxEdgeSegmentHandler): the
// segment moves perpendicular to its direction and the edge is re-routed;
// the preview is that route. Snaps to the grid. One edit "Change Points" on
// release (none when the edge routes the same way as before).
class EdgeSegmentGesture : public Gesture {
public:
    EdgeSegmentGesture(const ToolContext& context, EdgeHandles handles, Handle handle, const CanvasEvent& pressEvent);

    void move(const CanvasEvent& event) override;
    void release(const CanvasEvent& event) override;
    void cancel() override {}
    void paint(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] Qt::CursorShape cursor() const override
    {
        return handle_.verticalSegment ? Qt::SizeHorCursor : Qt::SizeVerCursor;
    }

private:
    void update(const CanvasEvent& event);

    ToolContext context_;
    EdgeHandles handles_;
    Handle handle_;
    QPointF pressViewPos_;
    QPointF current_;
    bool moved_ = false;
    std::optional<SegmentDrag> drag_;
};

} // namespace qraw::ui::edgeh
