#pragma once
// Row height and column width handles of a selected table, row or table cell
// (the custom handles of mxVertexHandler.createCustomHandles for tables,
// Graph.js:47628). Private to the editor UI.

#include "select_support.h"

#include <QCoreApplication>
#include <QPointF>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace qraw::ui {

// One draggable border: the bottom edge of a row or the right edge of a
// column, in absolute page coordinates.
struct TableLine {
    bool row = true;
    std::string cell;      // the row, or the cell of the first row that is in the column
    QPointF from, to;      // the edge
    double size = 0;       // height of the row / width of the column
    double nextSize = 0;   // the same for the next row / column; 0 for the last one
};

class TableHandles {
public:
    // The lines of the table that the single selected vertex belongs to (the
    // table, one of its rows or cells); nullopt for any other selection, for
    // locked or rotated tables and for tables that are not movable.
    [[nodiscard]] static std::optional<TableHandles> forSelection(const ToolContext& context);

    [[nodiscard]] const std::string& table() const { return table_; }
    [[nodiscard]] bool tableSelected() const { return tableSelected_; }
    // The line nearest to `point` within `tolerance` page units.
    [[nodiscard]] std::optional<TableLine> hitTest(QPointF point, double tolerance) const;

private:
    std::string table_;
    bool tableSelected_ = false;
    std::vector<TableLine> lines_;
};

// Dragging one line: the preview is a dashed line, the edit one undo step.
class TableResizeGesture : public Gesture {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::TableHandles)

public:
    TableResizeGesture(ToolContext context, std::shared_ptr<SelectionUiState> state, TableLine line,
                       const CanvasEvent& press);

    void move(const CanvasEvent& event) override;
    void release(const CanvasEvent& event) override;
    void cancel() override {}
    void paint(QPainter& painter, const OverlayContext& context) const override;
    [[nodiscard]] Qt::CursorShape cursor() const override
    {
        return line_.row ? Qt::SizeVerCursor : Qt::SizeHorCursor;
    }

private:
    void update(const CanvasEvent& event);

    ToolContext ctx_;
    GestureScope hidingHandles_;
    TableLine line_;
    QPointF pressScene_;
    double delta_ = 0;   // the previewed growth of the row / column
    bool shift_ = false; // the neighbour shrinks instead of the table growing
};

// Handler order: before the vertex handles (see PriorityTableHandles).
class TableHandlesHandler : public InteractionHandler {
public:
    TableHandlesHandler(ToolContext context, std::shared_ptr<SelectionUiState> state)
        : ctx_(context), state_(std::move(state))
    {
    }

    std::optional<Qt::CursorShape> hover(const CanvasEvent& event) override;
    std::unique_ptr<Gesture> press(const CanvasEvent& event) override;
    [[nodiscard]] OverlayBounds overlayBounds(const OverlayContext&) const override { return QRectF(); }

private:
    [[nodiscard]] std::optional<TableLine> lineAt(const CanvasEvent& event) const;

    ToolContext ctx_;
    std::shared_ptr<SelectionUiState> state_;
};

} // namespace qraw::ui
