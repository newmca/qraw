#pragma once
// The drag that starts on a hover arrow or a connection point
// (mxConnectionHandler). Private to the editor UI.

#include "connect_arrows.h"
#include "connect_common.h"
#include "connect_edit.h"

#include "qraw/ui/interaction.h"

#include <optional>
#include <string>

namespace qraw::ui::connect {

struct ConnectStart {
    std::string source;
    bool fromArrow = false;                 // else from a connection point
    Side side = Side::East;                 // arrow only
    std::optional<ConnectionPoint> point;   // connection point only
    QPointF anchor;                         // where the preview line starts
};

// Preview: dashed line from the anchor to the pointer; the connectable cell
// under the pointer gets a kValid outline and its nearest connection point
// (within 10 px) is highlighted. Release performs one edit "Connect":
//  * on a terminal: edge source -> terminal (constraint when started from or
//    released on a connection point);
//  * on blank from an arrow: a copy of the source centred under the pointer
//    (HoverIcons / mxConnectionHandler.createTargetVertex) plus the edge;
//  * on blank from a connection point: an edge with a dangling target;
//  * no movement: from an arrow, connects/clones in that direction at
//    Graph.defaultEdgeLength (Graph.connectVertex); from a point nothing.
class ConnectGesture : public Gesture {
public:
    ConnectGesture(const ToolContext& context, ConnectStart start, const CanvasEvent& pressEvent);

    void move(const CanvasEvent& event) override;
    void release(const CanvasEvent& event) override;
    void cancel() override {}
    void paint(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

private:
    void updateTarget(const CanvasEvent& event);
    [[nodiscard]] QRectF cloneBoundsAt(const CanvasEvent& event) const;
    [[nodiscard]] QRectF cloneBoundsInDirection() const;
    void connectInDirection();
    void execute(const NewConnection& request);

    ToolContext context_;
    ConnectStart start_;
    QPointF pressViewPos_;
    QPointF current_;
    double pixel_ = 1;
    bool moved_ = false;
    std::string target_;
    std::optional<ConnectionPoint> targetPoint_;
};

} // namespace qraw::ui::connect
