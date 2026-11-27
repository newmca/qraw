#pragma once
// The resize sizers and the rotation handle of a single selected vertex
// (mxVertexHandler) and the handler that draws the selection outlines and
// starts resize / rotate gestures. Private to the editor UI.

#include "select_support.h"

#include <QPainter>
#include <QPointF>
#include <QRectF>
#include <Qt>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace qraw::ui {

// A resize sizer by the position it grips on the vertex: fx, fy are 0, 0.5
// or 1 along the unrotated width and height (0,0 = top-left corner).
struct SizerSpec {
    double fx = 1;
    double fy = 1;
    [[nodiscard]] bool movesLeft() const { return fx == 0; }
    [[nodiscard]] bool movesRight() const { return fx == 1; }
    [[nodiscard]] bool movesTop() const { return fy == 0; }
    [[nodiscard]] bool movesBottom() const { return fy == 1; }
    [[nodiscard]] bool isCorner() const { return fx != 0.5 && fy != 0.5; }
    // The point that stays fixed while this sizer is dragged.
    [[nodiscard]] SizerSpec opposite() const { return {fx == 0.5 ? 0.5 : 1 - fx, fy == 0.5 ? 0.5 : 1 - fy}; }
};

inline constexpr std::array<SizerSpec, 8> kAllSizers = {{
    {0, 0}, {0.5, 0}, {1, 0}, {0, 0.5}, {1, 0.5}, {0, 1}, {0.5, 1}, {1, 1},
}};

struct HandleHit {
    bool rotation = false;
    SizerSpec sizer;
};

// Cursor for a sizer on a vertex rotated by `rotationDegrees` (the four
// resize cursors in 45-degree sectors).
[[nodiscard]] Qt::CursorShape sizerCursor(SizerSpec sizer, double rotationDegrees);

// The handles of one vertex, or of the union of several selected cells
// (Graph.MultiSelectionHandler), at the current zoom. Value type; recomputed
// on every event (cheap) so nothing is cached across model changes.
class VertexHandles {
public:
    // The handles for the session's selection when it is exactly one vertex
    // that has absolute geometry and is not locked; nullopt otherwise.
    [[nodiscard]] static std::optional<VertexHandles> forSelection(const ToolContext& context, double scale);

    // The handles of the union box when the selection is several cells that
    // can be transformed together (Graph.getMultiSelectionCells: at least two
    // movable topmost vertices / edges including a vertex, no table rows or
    // cells, none in a layout); nullopt otherwise. bounds() is the padded
    // box, contentBounds() the union of the cells, cells() the cells.
    [[nodiscard]] static std::optional<VertexHandles> forMultiSelection(const ToolContext& context, double scale);
    // forSelection, else forMultiSelection.
    [[nodiscard]] static std::optional<VertexHandles> forCurrentSelection(const ToolContext& context, double scale);

    [[nodiscard]] bool isMulti() const { return !cells_.empty(); }
    [[nodiscard]] const std::vector<std::string>& cells() const { return cells_; }
    [[nodiscard]] const QRectF& contentBounds() const { return contentBounds_; }

    [[nodiscard]] const std::string& id() const { return id_; }
    [[nodiscard]] const QRectF& bounds() const { return bounds_; }
    [[nodiscard]] double rotation() const { return rotation_; }
    [[nodiscard]] bool resizable() const { return resizable_; }
    [[nodiscard]] bool rotatable() const { return rotatable_; }

    // Absolute position of a sizer / of the rotation handle (rotated with the vertex).
    [[nodiscard]] QPointF sizerPosition(SizerSpec sizer) const;
    [[nodiscard]] QPointF rotationHandlePosition() const;
    // The rotation handle's offset from the centre in the unrotated frame:
    // 16 px right of and above the top-right corner
    // (mxVertexHandler.getRotationHandlePosition, Graph.js:47384).
    [[nodiscard]] QPointF rotationHandleOffset() const;

    // The sizers that are shown: all eight, or only the corners when the
    // vertex is smaller than 2.5 handles on screen.
    [[nodiscard]] std::vector<SizerSpec> visibleSizers() const;

    [[nodiscard]] std::optional<HandleHit> hitTest(QPointF point) const;
    void paint(QPainter& painter) const;

private:
    std::string id_;
    std::vector<std::string> cells_; // the union's cells; empty for a single vertex
    QRectF contentBounds_;           // the union without the padding
    QRectF bounds_;
    double rotation_ = 0;
    bool resizable_ = true;
    bool rotatable_ = true;
    double scale_ = 1;
};

// PriorityVertexHandles: selection outlines of every selected cell, handles
// of a single selected vertex or of the union of several, resize and rotate
// gestures.
class VertexHandlesHandler : public InteractionHandler {
public:
    VertexHandlesHandler(ToolContext context, std::shared_ptr<SelectionUiState> state);

    std::optional<Qt::CursorShape> hover(const CanvasEvent& event) override;
    std::unique_ptr<Gesture> press(const CanvasEvent& event) override;
    void paintOverlay(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext& context) const override;

private:
    [[nodiscard]] bool ready() const;

    ToolContext ctx_;
    std::shared_ptr<SelectionUiState> state_;
};

} // namespace qraw::ui
