#pragma once
// PriorityConnect: the hover arrows and connection points of the vertex
// under the mouse (draw.io HoverIcons, js/grapheditor/Graph.js:23620, and
// mxConstraintHandler). Private to the editor UI.

#include "connect_arrows.h"
#include "connect_common.h"

#include "qraw/ui/interaction.h"

#include <optional>
#include <string>

namespace qraw::ui::connect {

class ConnectHandler : public InteractionHandler {
public:
    explicit ConnectHandler(const ToolContext& context) : context_(context) {}

    std::optional<Qt::CursorShape> hover(const CanvasEvent& event) override;
    void hoverLeft() override;
    std::unique_ptr<Gesture> press(const CanvasEvent& event) override;
    void paintOverlay(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext& context) const override;
    void reset() override;

private:
    struct Hit {
        enum class Kind { None, Body, Arrow, Point };
        Kind kind = Kind::None;
        std::string cell;
        Side side = Side::East;
        std::optional<ConnectionPoint> point;
    };

    // `search`: also look for a vertex whose arrows are under the pointer
    // when no hover state exists yet (a press without a preceding move).
    [[nodiscard]] Hit hitTest(const CanvasEvent& event, bool search) const;
    [[nodiscard]] Hit hitOfCell(const render::CellState& state, const CanvasEvent& event) const;
    [[nodiscard]] const render::CellState* hoveredState() const;
    // Move cursor over a selected, movable cell (the selection handler is behind this one), else arrow.
    [[nodiscard]] Qt::CursorShape bodyCursor() const;

    ToolContext context_;
    std::string hoverCell_;
    Hit hit_;
};

} // namespace qraw::ui::connect
