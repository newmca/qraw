#include "select_vertex_handles.h"

#include "select_resize.h"
#include "qraw/commands/ops_layout.h"
#include "qraw/commands/ops_tables.h"
#include "qraw/commands/queries.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"

#include <QPen>
#include <QPolygonF>

#include <algorithm>
#include <cmath>

namespace qraw::ui {

namespace {

// Pixels around a handle that still count as a hit.
constexpr double kHandleHitSlop = 2;
// Vertices smaller than this many handles on screen show corner sizers only.
constexpr double kMinHandlesForEdgeSizers = 2.5;
// Screen pixels between the union box and the cells (MultiSelectionHandler.padding).
constexpr double kUnionPadding = 6;

QPen handlePen()
{
    QPen pen(overlay::kHandleStroke, 1);
    pen.setCosmetic(true);
    return pen;
}

} // namespace

// --- sizer cursors ----------------------------------------------------------------

Qt::CursorShape sizerCursor(SizerSpec sizer, double rotationDegrees)
{
    const QPointF direction(sizer.fx - 0.5, sizer.fy - 0.5);
    // The sizer's axis folded into [0, 180) and cut into four 45 degree sectors.
    const double axis = std::fmod(normalizeAngle(angleOf(direction) + rotationDegrees), 180.0);
    switch (static_cast<int>(std::lround(axis / 45)) % 4) {
    case 0:
        return Qt::SizeHorCursor;
    case 1:
        return Qt::SizeFDiagCursor; // "\"
    case 2:
        return Qt::SizeVerCursor;
    default:
        return Qt::SizeBDiagCursor; // "/"
    }
}

// --- VertexHandles -------------------------------------------------------------------

std::optional<VertexHandles> VertexHandles::forSelection(const ToolContext& context, double scale)
{
    if (context.session == nullptr || context.scene == nullptr)
        return std::nullopt;
    const SelectionModel& selection = context.session->selection();
    const core::Page* page = context.session->currentPage();
    if (selection.size() != 1 || page == nullptr)
        return std::nullopt;
    const render::CellState* state = context.scene->state(selection.first());
    if (state == nullptr || state->kind != render::CellState::Kind::Vertex || isCellLocked(*page, state->id))
        return std::nullopt;
    const core::Cell* cell = page->findCell(state->id);
    if (cell == nullptr || cell->geometry() == nullptr || cell->geometry()->relative())
        return std::nullopt;
    // The table layout sizes rows and cells; the table's own handles resize them.
    if (commands::ops::isTableRow(*page, *cell) || commands::ops::isTableCell(*page, *cell))
        return std::nullopt;

    VertexHandles handles;
    handles.id_ = state->id;
    handles.bounds_ = toQRect(state->bounds);
    handles.rotation_ = state->style.rotation();
    handles.resizable_ = styleAllows(*state, "resizable");
    handles.rotatable_ = styleAllows(*state, "rotatable");
    handles.scale_ = scale;
    return handles;
}

// Graph.getMultiSelectionCells (Graph.js ~37208).
std::optional<VertexHandles> VertexHandles::forMultiSelection(const ToolContext& context, double scale)
{
    if (context.session == nullptr || context.scene == nullptr)
        return std::nullopt;
    const SelectionModel& selection = context.session->selection();
    const core::Page* page = context.session->currentPage();
    if (selection.size() < 2 || page == nullptr)
        return std::nullopt;
    for (const std::string& id : selection.cells()) {
        const core::Cell* cell = page->findCell(id);
        if (cell == nullptr)
            return std::nullopt;
        // Layouts and the table layout position these cells.
        const core::Cell* parent = page->findCell(cell->parent());
        if (commands::ops::isTableRow(*page, *cell) || commands::ops::isTableCell(*page, *cell) ||
            (parent != nullptr && parent->isVertex() && commands::ops::hasChildLayout(*parent)))
            return std::nullopt;
    }

    VertexHandles handles;
    int vertices = 0;
    QRectF content;
    for (const std::string& id : commands::topmostCells(*page, selection.cells())) {
        const render::CellState* state = context.scene->state(id);
        const core::Cell* cell = page->findCell(id);
        if (state == nullptr || cell == nullptr || cell->geometry() == nullptr || !isMovable(*page, *state))
            continue;
        QRectF box = toQRect(state->bounds);
        if (state->kind == render::CellState::Kind::Vertex) {
            if (cell->geometry()->relative())
                continue;
            // The bounding box of the rotated vertex.
            QPolygonF corners;
            for (const QPointF corner : {box.topLeft(), box.topRight(), box.bottomLeft(), box.bottomRight()})
                corners << rotateAround(corner, box.center(), state->style.rotation());
            const QRectF rotated = corners.boundingRect();
            box = rotated;
            ++vertices;
        }
        content = handles.cells_.empty() ? box : content.united(box);
        handles.cells_.push_back(id);
    }
    if (vertices == 0 || handles.cells_.size() < 2)
        return std::nullopt;

    const double padding = kUnionPadding / scale;
    handles.contentBounds_ = content;
    handles.bounds_ = content.adjusted(-padding, -padding, padding, padding);
    handles.scale_ = scale;
    return handles;
}

std::optional<VertexHandles> VertexHandles::forCurrentSelection(const ToolContext& context, double scale)
{
    if (auto single = forSelection(context, scale))
        return single;
    return forMultiSelection(context, scale);
}

QPointF VertexHandles::sizerPosition(SizerSpec sizer) const
{
    const QPointF p(bounds_.left() + sizer.fx * bounds_.width(), bounds_.top() + sizer.fy * bounds_.height());
    return rotateAround(p, bounds_.center(), rotation_);
}

QPointF VertexHandles::rotationHandleOffset() const
{
    const double spacing = overlay::kRotationHandleSpacing / scale_;
    return {bounds_.width() / 2 + spacing, -(bounds_.height() / 2 + spacing)};
}

QPointF VertexHandles::rotationHandlePosition() const
{
    return rotateAround(bounds_.center() + rotationHandleOffset(), bounds_.center(), rotation_);
}

std::vector<SizerSpec> VertexHandles::visibleSizers() const
{
    std::vector<SizerSpec> sizers;
    if (!resizable_)
        return sizers;
    const double handle = overlay::kHandleSize;
    const bool small = bounds_.width() * scale_ < kMinHandlesForEdgeSizers * handle ||
                       bounds_.height() * scale_ < kMinHandlesForEdgeSizers * handle;
    for (const SizerSpec& sizer : kAllSizers) {
        if (!small || sizer.isCorner())
            sizers.push_back(sizer);
    }
    return sizers;
}

std::optional<HandleHit> VertexHandles::hitTest(QPointF point) const
{
    const double radius = (overlay::kHandleSize / 2 + kHandleHitSlop) / scale_;
    if (rotatable_) {
        const QPointF d = point - rotationHandlePosition();
        if (std::hypot(d.x(), d.y()) <= radius)
            return HandleHit{true, {}};
    }
    for (const SizerSpec& sizer : visibleSizers()) {
        const QPointF d = point - sizerPosition(sizer);
        if (std::abs(d.x()) <= radius && std::abs(d.y()) <= radius)
            return HandleHit{false, sizer};
    }
    return std::nullopt;
}

void VertexHandles::paint(QPainter& painter) const
{
    const double size = overlay::kHandleSize / scale_;
    painter.save();
    painter.setPen(handlePen());
    painter.setBrush(overlay::kHandleFill);
    for (const SizerSpec& sizer : visibleSizers()) {
        const QPointF c = sizerPosition(sizer);
        painter.drawRect(QRectF(c.x() - size / 2, c.y() - size / 2, size, size));
    }
    if (rotatable_) {
        const QPointF c = rotationHandlePosition();
        painter.drawEllipse(QRectF(c.x() - size / 2, c.y() - size / 2, size, size));
    }
    painter.restore();
}

// --- VertexHandlesHandler ----------------------------------------------------------------

VertexHandlesHandler::VertexHandlesHandler(ToolContext context, std::shared_ptr<SelectionUiState> state)
    : ctx_(context), state_(std::move(state))
{
}

bool VertexHandlesHandler::ready() const
{
    return ctx_.session != nullptr && ctx_.scene != nullptr && ctx_.session->currentPage() != nullptr;
}

std::optional<Qt::CursorShape> VertexHandlesHandler::hover(const CanvasEvent& event)
{
    if (!ready())
        return std::nullopt;
    const auto handles = VertexHandles::forCurrentSelection(ctx_, event.scale);
    if (!handles)
        return std::nullopt;
    const auto hit = handles->hitTest(event.scenePos);
    if (!hit)
        return std::nullopt;
    return hit->rotation ? Qt::CrossCursor : sizerCursor(hit->sizer, handles->rotation());
}

std::unique_ptr<Gesture> VertexHandlesHandler::press(const CanvasEvent& event)
{
    if (event.button != Qt::LeftButton || !ready())
        return nullptr;
    const auto handles = VertexHandles::forCurrentSelection(ctx_, event.scale);
    if (!handles)
        return nullptr;
    const auto hit = handles->hitTest(event.scenePos);
    if (!hit)
        return nullptr;
    if (handles->isMulti()) {
        if (hit->rotation)
            return std::make_unique<MultiRotateGesture>(ctx_, state_, *handles, event);
        return std::make_unique<MultiResizeGesture>(ctx_, state_, *handles, hit->sizer, event);
    }
    if (hit->rotation)
        return std::make_unique<RotateGesture>(ctx_, state_, *handles, event);
    return std::make_unique<ResizeGesture>(ctx_, state_, *handles, hit->sizer, event);
}

// mxVertexHandler / mxEdgeHandler selection border: dashed #00a8ff outlines
// for every selected cell, the sizers for a single vertex or the union box
// of several cells on top.
void VertexHandlesHandler::paintOverlay(QPainter& painter, const OverlayContext& context) const
{
    if (!ready() || state_->gestureRunning())
        return;
    for (const std::string& id : ctx_.session->selection().cells()) {
        if (const render::CellState* state = ctx_.scene->state(id))
            paintCellOutline(painter, *state);
    }
    if (const auto handles = VertexHandles::forCurrentSelection(ctx_, context.scale)) {
        if (handles->isMulti())
            paintRotatedRect(painter, handles->bounds(), 0); // the dashed union box
        handles->paint(painter);
    }
}

// The outlines of the selected cells and the handles around them: the union of
// the cells' paint bounds grown by the distance of the farthest handle (the
// rotation handle, 16 px beyond the corner) and the size of a handle.
OverlayBounds VertexHandlesHandler::overlayBounds(const OverlayContext& context) const
{
    if (!ready() || state_->gestureRunning())
        return QRectF();
    QRectF united;
    for (const std::string& id : ctx_.session->selection().cells()) {
        if (const render::CellState* state = ctx_.scene->state(id)) {
            const QRectF b(state->paintBounds.x, state->paintBounds.y, state->paintBounds.width,
                           state->paintBounds.height);
            united = united.isNull() ? b : united.united(b);
        }
    }
    if (united.isNull())
        return QRectF();
    const double margin = context.pixels(overlay::kRotationHandleSpacing + 2 * overlay::kHandleSize + 4);
    return united.adjusted(-margin, -margin, margin, margin);
}

} // namespace qraw::ui
