#pragma once
// The resize and rotate gestures of a single selected vertex
// (mxVertexHandler.resizeVertex / rotateVertex). Private to the editor UI.

#include "select_support.h"
#include "qraw/core/style.h"
#include "select_vertex_handles.h"

#include <QCoreApplication>
#include <QRectF>

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace qraw::ui {

// The union box a drag of `sizer` makes of `original` (all in page units)
// -- Graph.MultiSelectionHandler.getResizedBounds without the pointer: the
// dragged sides move by `delta` (snapped to the grid when `grid` is set),
// Shift (`keepAspect`) keeps the proportions, Alt (`centered`) keeps the
// centre. The box keeps a minimum size instead of flipping; values are
// rounded to 1/100.
struct UnionResizeOptions {
    std::function<double(double)> snap; // empty: no grid
    bool keepAspect = false;
    bool centered = false;
};
[[nodiscard]] QRectF resizeUnionBounds(const QRectF& original, SizerSpec sizer, QPointF delta,
                                       const UnionResizeOptions& options);

// The rectangle that results from dragging `sizer` of `original` by `delta`,
// both in the vertex's own (unrotated) frame. The opposite side or corner
// stays fixed, the size never drops below 1, and `keepAspect` (Shift) keeps
// the original width/height ratio. No grid snapping (see ResizeGesture).
[[nodiscard]] QRectF resizeFromSizer(const QRectF& original, SizerSpec sizer, QPointF delta, bool keepAspect);

// For a vertex rotated by `rotationDegrees` around its centre: moves
// `resized` so that the point `sizer.opposite()` of the new rectangle stays at
// the same absolute position as on `original` (mxVertexHandler.resizeVertex
// compensates the shifted rotation centre the same way).
[[nodiscard]] QRectF keepFixedPoint(const QRectF& original, const QRectF& resized, SizerSpec sizer,
                                    double rotationDegrees);

class ResizeGesture : public Gesture {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::SelectionHandler)

public:
    ResizeGesture(ToolContext context, std::shared_ptr<SelectionUiState> state, const VertexHandles& handles,
                  SizerSpec sizer, const CanvasEvent& press);

    void move(const CanvasEvent& event) override;
    void release(const CanvasEvent& event) override;
    void cancel() override;
    void paint(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext& context) const override;
    [[nodiscard]] Qt::CursorShape cursor() const override { return sizerCursor(sizer_, rotation_); }

private:
    void update(const CanvasEvent& event);
    void showLive(); // the cell, resized, in the active layer
    void endLive(bool edited);

    ToolContext ctx_;
    GestureScope hidingHandles_;
    std::string id_;
    QRectF original_; // absolute, unrotated
    double rotation_;
    SizerSpec sizer_;
    QPointF pressScene_;
    QRectF result_;   // the previewed rectangle, absolute
    bool lifted_ = false; // the active layer holds the vertex
};

class RotateGesture : public Gesture {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::SelectionHandler)

public:
    RotateGesture(ToolContext context, std::shared_ptr<SelectionUiState> state, const VertexHandles& handles,
                  const CanvasEvent& press);

    void move(const CanvasEvent& event) override;
    void release(const CanvasEvent& event) override;
    void cancel() override;
    void paint(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext& context) const override;
    [[nodiscard]] Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

private:
    void showLive(); // the cell, rotated, in the active layer
    void endLive(bool edited);

    ToolContext ctx_;
    GestureScope hidingHandles_;
    std::string id_;
    bool lifted_ = false; // the active layer holds the vertex
    QRectF bounds_;
    double startRotation_;
    double startAngle_;    // direction of the rotation handle from the centre, degrees
    double startDistance_; // its distance from the centre in screen pixels
    double alpha_;         // the previewed rotation
};

} // namespace qraw::ui

namespace qraw::ui {

// What the previews of the union gestures need to know of a cell.
struct UnionCell {
    std::string id;
    bool vertex = true;
    QRectF bounds;         // absolute, unrotated (vertices)
    double rotation = 0;   // degrees (vertices)
    bool resizable = true;
    bool rotatable = true;
    core::Style style;     // for the per-axis scale of a rotated vertex
    std::vector<QPointF> points; // the routed polyline (edges)
};

// Resize gesture of the union box (Graph.MultiSelectionHandler, sizers):
// previews the outline of every cell, one `ops::scaleCellsToBounds` edit on
// release (Ctrl also scales fonts, spacings and stroke widths).
class MultiResizeGesture : public Gesture {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::SelectionHandler)

public:
    MultiResizeGesture(ToolContext context, std::shared_ptr<SelectionUiState> state, const VertexHandles& handles,
                       SizerSpec sizer, const CanvasEvent& press);

    void move(const CanvasEvent& event) override { update(event); }
    void release(const CanvasEvent& event) override;
    void cancel() override {}
    void paint(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] Qt::CursorShape cursor() const override { return sizerCursor(sizer_, 0); }

private:
    void update(const CanvasEvent& event);

    ToolContext ctx_;
    GestureScope hidingHandles_;
    std::vector<UnionCell> cells_;
    QRectF original_; // the union of the cells
    SizerSpec sizer_;
    QPointF pressScene_;
    double padding_;  // the union box's padding in page units
    QRectF result_;
};

// Rotate gesture of the union box: the cells turn around the centre of the
// union. A click on the handle without moving turns them by 90 degrees.
class MultiRotateGesture : public Gesture {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::SelectionHandler)

public:
    MultiRotateGesture(ToolContext context, std::shared_ptr<SelectionUiState> state, const VertexHandles& handles,
                       const CanvasEvent& press);

    void move(const CanvasEvent& event) override;
    void release(const CanvasEvent& event) override;
    void cancel() override {}
    void paint(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] Qt::CursorShape cursor() const override { return Qt::CrossCursor; }

private:
    void update(const CanvasEvent& event);

    ToolContext ctx_;
    GestureScope hidingHandles_;
    std::vector<UnionCell> cells_;
    QRectF original_;
    double padding_;
    double startAngle_;
    double startDistance_;
    bool moved_ = false;
    double alpha_ = 0;
};

} // namespace qraw::ui
