#include "select_table_handles.h"

#include "select_vertex_handles.h"
#include "qraw/commands/ops_tables.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"

#include <QPainter>

#include <algorithm>
#include <cmath>

namespace qraw::ui {

namespace {

namespace tables = commands::ops;

// Distance from `p` to the segment a-b.
double distanceToSegment(QPointF p, QPointF a, QPointF b)
{
    const QPointF ab = b - a;
    const double length2 = ab.x() * ab.x() + ab.y() * ab.y();
    const double t = length2 == 0 ? 0 : std::clamp(QPointF::dotProduct(p - a, ab) / length2, 0.0, 1.0);
    const QPointF nearest = a + ab * t;
    return std::hypot(p.x() - nearest.x(), p.y() - nearest.y());
}

} // namespace

// --- TableHandles --------------------------------------------------------------------

std::optional<TableHandles> TableHandles::forSelection(const ToolContext& context)
{
    if (context.session == nullptr || context.scene == nullptr)
        return std::nullopt;
    const core::Page* page = context.session->currentPage();
    const SelectionModel& selection = context.session->selection();
    if (page == nullptr || selection.size() != 1)
        return std::nullopt;
    const core::Cell* selected = page->findCell(selection.first());
    const std::string tableId = selected != nullptr && selected->isVertex() ? tables::tableOf(*page, selected->id()) : "";
    if (tableId.empty() || isCellLocked(*page, tableId))
        return std::nullopt;
    const render::CellState* table = context.scene->state(tableId);
    if (table == nullptr || table->style.rotation() != 0 || !styleAllows(*table, "movable"))
        return std::nullopt;

    const auto rows = tables::childVertices(*page, tableId);
    if (rows.empty())
        return std::nullopt;

    TableHandles handles;
    handles.table_ = tableId;
    handles.tableSelected_ = selected->id() == tableId;

    // Absolute bounds of the cells and rows (CellState bounds are absolute).
    std::vector<QRectF> rowBounds;
    for (const core::Cell* row : rows) {
        const render::CellState* state = context.scene->state(row->id());
        if (state == nullptr)
            return std::nullopt;
        rowBounds.push_back(toQRect(state->bounds));
    }
    for (std::size_t i = 0; i < rows.size(); ++i) {
        TableLine line;
        line.row = true;
        line.cell = rows[i]->id();
        line.from = {rowBounds[i].left(), rowBounds[i].bottom()};
        line.to = {rowBounds[i].right(), rowBounds[i].bottom()};
        line.size = rowBounds[i].height();
        line.nextSize = i + 1 < rows.size() ? rowBounds[i + 1].height() : 0;
        handles.lines_.push_back(std::move(line));
    }

    // The columns come from the first row's cells, covered ones included (a merged cell in
    // that row hides its neighbours, which have no state): each is as wide as its own column
    // (the alternate bounds), not as the merged cell that grew over it.
    const auto cells = tables::childVertices(*page, rows.front()->id());
    std::vector<QRectF> cellBounds;
    for (const core::Cell* cell : cells) {
        const core::Geometry* geometry = cell->geometry();
        if (geometry == nullptr)
            return std::nullopt;
        const auto alternate = geometry->alternateBounds();
        const double width = alternate ? alternate->width : geometry->width();
        cellBounds.push_back({rowBounds.front().left() + geometry->x(), rowBounds.front().top(), width, geometry->height()});
    }
    const double top = rowBounds.front().top();
    const double bottom = rowBounds.back().bottom();
    for (std::size_t i = 0; i < cells.size(); ++i) {
        TableLine line;
        line.row = false;
        line.cell = cells[i]->id();
        line.from = {cellBounds[i].right(), top};
        line.to = {cellBounds[i].right(), bottom};
        line.size = cellBounds[i].width();
        line.nextSize = i + 1 < cells.size() ? cellBounds[i + 1].width() : 0;
        handles.lines_.push_back(std::move(line));
    }
    return handles;
}

std::optional<TableLine> TableHandles::hitTest(QPointF point, double tolerance) const
{
    std::optional<TableLine> best;
    double bestDistance = tolerance;
    for (const TableLine& line : lines_) {
        const double distance = distanceToSegment(point, line.from, line.to);
        if (distance <= bestDistance) {
            bestDistance = distance;
            best = line;
        }
    }
    return best;
}

// --- TableResizeGesture -------------------------------------------------------------------

TableResizeGesture::TableResizeGesture(ToolContext context, std::shared_ptr<SelectionUiState> state, TableLine line,
                                       const CanvasEvent& press)
    : ctx_(context), hidingHandles_(std::move(state)), line_(std::move(line)), pressScene_(press.scenePos)
{
}

// handle.setPosition (Graph.js:47720, :47840): the line cannot shrink its
// row or column below the minimum size; with Shift the neighbour may not
// shrink below it either.
void TableResizeGesture::update(const CanvasEvent& event)
{
    shift_ = event.modifiers & Qt::ShiftModifier;
    const QPointF moved = event.scenePos - pressScene_;
    const double minimum = line_.row ? commands::ops::kMinTableRowHeight : commands::ops::kMinTableColumnWidth;
    double delta = std::round(line_.row ? moved.y() : moved.x());
    delta = std::max(delta, minimum - line_.size);
    if (shift_ && line_.nextSize > 0)
        delta = std::min(delta, line_.nextSize - minimum);
    delta_ = delta;
}

void TableResizeGesture::move(const CanvasEvent& event)
{
    update(event);
}

// handle.execute: Shift trades the size with the next row or column instead
// of growing the table. (Ctrl variants that resize or distribute all rows or
// columns are not implemented.)
void TableResizeGesture::release(const CanvasEvent& event)
{
    update(event);
    if (ctx_.session == nullptr)
        return;
    if (delta_ == 0) {
        // A click on a border selects the cell under it, as handle.execute does.
        if (ctx_.scene == nullptr)
            return;
        std::string target = ctx_.scene->cellAt(event.scenePos, event.pixels(overlay::kHitTolerance));
        if (target.empty() && !line_.cell.empty()) {
            if (const core::Page* page = ctx_.session->currentPage())
                target = commands::ops::tableOf(*page, line_.cell);
        }
        if (!target.empty())
            ctx_.session->selection().set({target});
        return;
    }
    const std::size_t pageIndex = ctx_.session->currentPageIndex();
    const bool extend = !shift_;
    ctx_.session->edit(line_.row ? tr("Resize Row") : tr("Resize Column"), [&](commands::Transaction& tx) {
        return line_.row ? commands::ops::setTableRowHeight(tx, pageIndex, line_.cell, delta_, extend)
                         : commands::ops::setTableColumnWidth(tx, pageIndex, line_.cell, delta_, extend);
    });
}

void TableResizeGesture::paint(QPainter& painter, const OverlayContext& context) const
{
    (void)context;
    painter.save();
    painter.setPen(selectionPen());
    const QPointF shift = line_.row ? QPointF(0, delta_) : QPointF(delta_, 0);
    painter.drawLine(line_.from + shift, line_.to + shift);
    painter.restore();
}

// --- TableHandlesHandler -----------------------------------------------------------------------

std::optional<TableLine> TableHandlesHandler::lineAt(const CanvasEvent& event) const
{
    const auto handles = TableHandles::forSelection(ctx_);
    if (!handles)
        return std::nullopt;
    // The sizers of a selected table lie on its outer lines: they win.
    if (handles->tableSelected()) {
        if (const auto sizers = VertexHandles::forSelection(ctx_, event.scale); sizers && sizers->hitTest(event.scenePos))
            return std::nullopt;
    }
    return handles->hitTest(event.scenePos, event.pixels(overlay::kHitTolerance));
}

std::optional<Qt::CursorShape> TableHandlesHandler::hover(const CanvasEvent& event)
{
    const auto line = lineAt(event);
    if (!line)
        return std::nullopt;
    return line->row ? Qt::SizeVerCursor : Qt::SizeHorCursor;
}

std::unique_ptr<Gesture> TableHandlesHandler::press(const CanvasEvent& event)
{
    if (event.button != Qt::LeftButton)
        return nullptr;
    const auto line = lineAt(event);
    if (!line)
        return nullptr;
    return std::make_unique<TableResizeGesture>(ctx_, state_, *line, event);
}

} // namespace qraw::ui
