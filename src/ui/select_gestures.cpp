#include "select_gestures.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_layout.h"
#include "qraw/commands/ops_structure.h"
#include "qraw/ui/active_layer.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cell_transfer.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"

#include <algorithm>
#include <cmath>

namespace qraw::ui {

namespace {

constexpr double kRubberbandAlpha = 0.2; // mxRubberband default opacity

double distance(QPointF a, QPointF b)
{
    return std::hypot(a.x() - b.x(), a.y() - b.y());
}

} // namespace

// --- MoveGesture --------------------------------------------------------------

MoveGesture::MoveGesture(ToolContext context, std::shared_ptr<SelectionUiState> state,
                         std::vector<std::string> moving, const CanvasEvent& press, ClickAction click)
    : ctx_(context),
      state_(std::move(state)),
      moving_(std::move(moving)),
      pressScene_(press.scenePos),
      pressView_(press.viewPos),
      click_(std::move(click))
{
    if (ctx_.scene != nullptr)
        bounds_ = stateBoundsUnion(*ctx_.scene, moving_);
}

// mxGraphHandler.getDelta: the raw pointer translation; Shift constrains it
// to the dominant axis (mxGraphHandler.mouseMove, graph.isConstrainedEvent).
QPointF MoveGesture::constrainedDelta(const CanvasEvent& event) const
{
    QPointF delta = event.scenePos - pressScene_;
    if (event.modifiers & Qt::ShiftModifier) {
        if (std::abs(delta.x()) >= std::abs(delta.y()))
            delta.setY(0);
        else
            delta.setX(0);
    }
    return delta;
}

void MoveGesture::update(const CanvasEvent& event)
{
    if (!dragging_) {
        if (moving_.empty() || distance(event.viewPos, pressView_) <= kDragThreshold)
            return;
        dragging_ = true;
        hidingHandles_ = std::make_unique<GestureScope>(state_);
        // The moving cells leave the tiles and are drawn live above them
        // (Ctrl: the copies, the originals stay).
        if (ctx_.view != nullptr)
            lifted_ = ctx_.view->activeLayer().liftMove(moving_, event.modifiers & Qt::ControlModifier);
    }
    const bool guides = ctx_.view == nullptr || ctx_.view->guidesEnabled();
    const Snapper snapper(*ctx_.scene, guides);
    const SnapResult snapped =
        snapper.snapMove(bounds_, constrainedDelta(event), moving_, event.modifiers, event.pixels(1));
    delta_ = snapped.delta;
    guides_ = snapped.guides;
    duplicate_ = event.modifiers & Qt::ControlModifier;
    // Alt disables dropping into containers (Graph.getDropTarget).
    destination_ = moveDestination(*ctx_.session, *ctx_.scene, moving_, event.scenePos,
                                   !(event.modifiers & Qt::AltModifier));
    if (lifted_)
        ctx_.view->activeLayer().setMoveDelta(delta_, duplicate_);
}

void MoveGesture::move(const CanvasEvent& event)
{
    if (ctx_.scene != nullptr)
        update(event);
}

void MoveGesture::release(const CanvasEvent& event)
{
    if (ctx_.session == nullptr || ctx_.scene == nullptr)
        return;
    update(event);
    if (!dragging_) {
        applyClickAction();
        return;
    }
    const bool edited = commitMove(delta_, duplicate_, event.scenePos);
    if (lifted_) {
        // The cells stay on screen until the tiles of their new place are ready.
        edited ? ctx_.view->activeLayer().commit() : ctx_.view->activeLayer().cancel();
        lifted_ = false;
    }
}

void MoveGesture::cancel()
{
    // Preview only: nothing was changed. The cells go back to the tiles.
    if (lifted_)
        ctx_.view->activeLayer().cancel();
    lifted_ = false;
}

void MoveGesture::applyClickAction()
{
    SelectionModel& selection = ctx_.session->selection();
    switch (click_.kind) {
    case ClickAction::Kind::SelectOnly:
        selection.set({click_.id});
        break;
    case ClickAction::Kind::Deselect:
        selection.remove({click_.id});
        break;
    case ClickAction::Kind::None:
        break;
    }
}

// One edit: move, then drop into the container under the pointer (or out of
// the parent it was dragged away from), extend that container, and let a
// stack layout place the cells among their new siblings (mxGraphHandler.moveCells
// -> mxGraph.moveCells -> cellsAdded, then mxLayoutManager.cellsMoved).
bool MoveGesture::commitMove(QPointF delta, bool duplicate, QPointF pointer)
{
    DocumentSession& session = *ctx_.session;
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return false;
    const std::size_t pageIndex = session.currentPageIndex();
    const std::string& destination = destination_.parent;
    const core::Point layoutPoint{pointer.x(), pointer.y()};
    if (duplicate) {
        // Ctrl+drag copies (mxGraphHandler.isCloneEnabled / graph.isCloneEvent).
        const std::optional<CellBundle> bundle = decodeCells(encodeCells(*page, moving_));
        if (!bundle)
            return false;
        const std::string parent = destination.empty() ? session.insertionParent() : destination;
        const commands::Result result = session.edit(tr("Duplicate"), [&](commands::Transaction& tx) {
            commands::Result inserted = insertBundle(tx, pageIndex, *bundle, parent, delta.x(), delta.y());
            if (!inserted)
                return inserted;
            if (commands::Result r = commands::ops::extendParents(tx, pageIndex, inserted.cells); !r)
                return r;
            if (commands::Result r = commands::ops::moveInLayout(tx, pageIndex, inserted.cells, layoutPoint); !r)
                return r;
            return inserted;
        });
        return static_cast<bool>(result);
    }

    std::vector<std::string> reparent;
    if (!destination.empty()) {
        for (const std::string& id : moving_) {
            const core::Cell* cell = page->findCell(id);
            if (cell != nullptr && cell->parent() != destination)
                reparent.push_back(id);
        }
    }
    if (delta.isNull() && reparent.empty())
        return false;
    const commands::Result result = session.edit(tr("Move"), [&](commands::Transaction& tx) {
        commands::Result moved = commands::ops::moveCells(tx, pageIndex, moving_, delta.x(), delta.y());
        if (!moved)
            return moved;
        if (!reparent.empty()) {
            // Labels that follow their edge are not in moved.cells and keep their parent.
            std::erase_if(reparent, [&](const std::string& id) {
                return std::ranges::find(moved.cells, id) == moved.cells.end();
            });
            if (commands::Result r = commands::ops::reparentCells(tx, pageIndex, reparent, destination); !r)
                return r;
            if (commands::Result r = commands::ops::extendParents(tx, pageIndex, reparent); !r)
                return r;
        }
        if (commands::Result r = commands::ops::moveInLayout(tx, pageIndex, moved.cells, layoutPoint); !r)
            return r;
        return moved;
    });
    return static_cast<bool>(result);
}

void MoveGesture::paint(QPainter& painter, const OverlayContext& context) const
{
    if (!dragging_ || ctx_.scene == nullptr)
        return;
    if (!destination_.target.empty()) {
        if (const render::CellState* target = ctx_.scene->state(destination_.target))
            paintDropTarget(painter, *target, context.dark);
    }
    for (const std::string& id : moving_) {
        if (const render::CellState* state = ctx_.scene->state(id))
            paintCellOutline(painter, *state, delta_);
    }
    painter.save();
    QPen pen(overlay::kGuide, 1);
    pen.setCosmetic(true);
    painter.setPen(pen);
    for (const QLineF& guide : guides_)
        painter.drawLine(guide);
    painter.restore();
}

// The previews: outlines of the moving cells at their shifted place, the drop
// target, and the snap guides.
OverlayBounds MoveGesture::overlayBounds(const OverlayContext& context) const
{
    if (!dragging_ || ctx_.scene == nullptr)
        return QRectF();
    QRectF united = bounds_.translated(delta_);
    const auto add = [&united](const QRectF& r) { united = united.isNull() ? r : united.united(r); };
    if (!destination_.target.empty()) {
        if (const render::CellState* target = ctx_.scene->state(destination_.target))
            add(QRectF(target->paintBounds.x, target->paintBounds.y, target->paintBounds.width,
                       target->paintBounds.height));
    }
    for (const QLineF& guide : guides_) {
        add(QRectF(guide.p1(), guide.p2()).normalized().adjusted(-0.5, -0.5, 0.5, 0.5));
    }
    const double margin = context.pixels(8);
    return united.adjusted(-margin, -margin, margin, margin);
}

Qt::CursorShape MoveGesture::cursor() const
{
    return dragging_ ? Qt::SizeAllCursor : Qt::ArrowCursor;
}

// --- RubberbandGesture ------------------------------------------------------------

RubberbandGesture::RubberbandGesture(ToolContext context, const CanvasEvent& press)
    : ctx_(context),
      startScene_(press.scenePos),
      startView_(press.viewPos),
      currentScene_(press.scenePos),
      toggle_(press.toggle())
{
}

void RubberbandGesture::move(const CanvasEvent& event)
{
    currentScene_ = event.scenePos;
    if (!dragging_ && distance(event.viewPos, startView_) > kDragThreshold)
        dragging_ = true;
}

// mxRubberband.mouseUp: select what lies completely inside the rectangle.
void RubberbandGesture::release(const CanvasEvent& event)
{
    move(event);
    if (!dragging_ || ctx_.session == nullptr || ctx_.scene == nullptr)
        return;
    const QRectF rect = QRectF(startScene_, currentScene_).normalized();
    const std::vector<std::string> ids = ctx_.scene->cellsInRect(rect, ctx_.session->enteredGroup());
    if (toggle_)
        ctx_.session->selection().add(ids);
    else
        ctx_.session->selection().set(ids);
}

OverlayBounds RubberbandGesture::overlayBounds(const OverlayContext& context) const
{
    if (!dragging_)
        return QRectF();
    const double margin = context.pixels(2);
    return QRectF(startScene_, currentScene_).normalized().adjusted(-margin, -margin, margin, margin);
}

void RubberbandGesture::paint(QPainter& painter, const OverlayContext& context) const
{
    (void)context;
    if (!dragging_)
        return;
    const QRectF rect = QRectF(startScene_, currentScene_).normalized();
    QColor fill = overlay::kRubberband;
    fill.setAlphaF(kRubberbandAlpha);
    QPen pen(overlay::kRubberband, 1);
    pen.setCosmetic(true);
    painter.save();
    painter.setPen(pen);
    painter.setBrush(fill);
    painter.drawRect(rect);
    painter.restore();
}

} // namespace qraw::ui
