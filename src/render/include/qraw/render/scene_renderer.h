#pragma once

// Scene projection, part 2: paints CellStates onto any Canvas. This is the
// seam between rendering and the M4 canvas: a QGraphicsItem per state calls
// paintCell() with a PainterCanvas on the item's QPainter and uses
// CellState::paintBounds as its boundingRect().

#include "qraw/render/canvas.h"
#include "qraw/render/page_view.h"

#include <span>
#include <string>
#include <vector>

namespace qraw::render {

struct PaintOptions {
    // Paint only these cells and their descendants (selection export).
    // Empty: every state.
    std::vector<std::string> cellIds;
    bool labels = true;
    // Wrap cells in Canvas::beginGroup/endGroup, nested like the cell tree
    // (draw.io's <g data-cell-id> structure).
    bool groups = true;
};

class SceneRenderer {
public:
    explicit SceneRenderer(RenderContext context);

    [[nodiscard]] const RenderContext& context() const { return context_; }

    // The shape only. Vertices: paintVertex() with the state's painter.
    // Edges: configureCanvas, the polyline (rounded/curved per style,
    // mxPolyline/mxConnector semantics) shortened by the markers, then the
    // start/end markers (createMarker) with the fill set to the stroke color
    // (startFillColor/endFillColor override) and dashes/shadow off.
    // sketch=1 and comic=1 cells are painted through SketchCanvas.
    //
    // `view` is the view that owns `state`. Container shapes (table, tableRow)
    // list the children of the state from it; without a view (and outside
    // paintPage) they see no children and paint no inner lines.
    // `valueHidden`: the value is being edited on screen; shapes that paint
    // the value themselves (curvedText) skip it.
    void paintShape(Canvas& canvas, const CellState& state, const PageView* view = nullptr,
                    bool valueHidden = false) const;
    // The label only (Canvas::text with the state's LabelBox and RichText,
    // font from the style, textOpacity applied).
    void paintLabel(Canvas& canvas, const CellState& state) const;
    // Shape, then label; `view` as for paintShape().
    void paintCell(Canvas& canvas, const CellState& state, const PageView* view = nullptr) const;
    // Every (selected) state in paint order.
    void paintPage(Canvas& canvas, const PageView& view, const PaintOptions& options = {}) const;

private:
    // paintShape() without the sketch/comic decorator.
    void paintShapeOn(Canvas& canvas, const CellState& state, const PageView* view, bool valueHidden) const;

    RenderContext context_;
};

} // namespace qraw::render
