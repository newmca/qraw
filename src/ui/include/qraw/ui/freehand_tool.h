#pragma once
// The Freehand tool (draw.io's insertFreehand): while it is on, dragging on the
// canvas draws a stroke, and releasing the button turns it into one vertex.
//
// The stroke is a vertex with shape=stencil(...) whose single path follows the
// smoothed pointer path (commands/freehand.h), so it scales with the cell and
// is edited like any shape. It takes the stored default style (Set as Default
// Style: stroke colour, width, dash...) and is one undo step. Escape while
// drawing cancels the stroke and leaves the model untouched; Escape between
// strokes turns the tool off. While drawing, the overlay shows the path of the
// pointer in the stroke's colour and width, with exact overlay bounds.

#include "qraw/core/cell.h"
#include "qraw/ui/interaction.h"

#include <QPointF>

#include <functional>
#include <span>

namespace qraw::ui {

class DocumentSession;

// What the stroke is drawn with: the style of the new cell and what it looks
// like on the page.
struct FreehandBrush {
    core::Style style;     // the style of the new cell, without its shape
    QColor color;          // the stroke colour as the diagram shows it (invalid: no stroke)
    double width = 1;      // page units
};

// The style a new stroke takes: fill none, the stored default style applied, and a
// stroke width of kDefaultFreehandWidth when the defaults give none.
inline constexpr double kDefaultFreehandWidth = 2;
// The simplification tolerance of a stroke, in screen pixels.
inline constexpr double kFreehandSmoothingPixels = 2;

[[nodiscard]] FreehandBrush freehandBrush(const DocumentSession& session);

namespace actions {

// Inserts the stroke of `points` (page coordinates) as one vertex in the current layer or
// entered group, in one edit named "Freehand", and selects it. `tolerance` is the smoothing
// (commands::freehand::buildStroke). False when the points do not move or nothing can be inserted.
bool insertFreehandStroke(DocumentSession& session, std::span<const QPointF> points, double tolerance);

} // namespace actions

class FreehandTool final : public InteractionHandler {
public:
    explicit FreehandTool(ToolContext context);

    [[nodiscard]] bool isActive() const { return active_; }
    void setActive(bool active);
    // Called when the tool turns on or off, also by Escape.
    void setActiveChangedCallback(std::function<void(bool)> callback) { activeChanged_ = std::move(callback); }

    std::optional<Qt::CursorShape> hover(const CanvasEvent& event) override;
    std::unique_ptr<Gesture> press(const CanvasEvent& event) override;
    bool keyPress(QKeyEvent& event) override;
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext& context) const override;

private:
    ToolContext context_;
    bool active_ = false;
    std::function<void(bool)> activeChanged_;
};

} // namespace qraw::ui
