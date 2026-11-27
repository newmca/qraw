#include "connect_gesture.h"

#include "connect_edit.h"

#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/snapping.h"

#include <QCoreApplication>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace qraw::ui::connect {

namespace {

// Pixels the pointer must travel before a press counts as a drag
// (mxGraph.tolerance).
constexpr double kDragThresholdPixels = 4;

QString connectName()
{
    return QCoreApplication::translate("qraw::ui::ConnectGesture", "Connect");
}

} // namespace

ConnectGesture::ConnectGesture(const ToolContext& context, ConnectStart start, const CanvasEvent& pressEvent)
    : context_(context),
      start_(std::move(start)),
      pressViewPos_(pressEvent.viewPos),
      current_(pressEvent.scenePos)
{
}

void ConnectGesture::updateTarget(const CanvasEvent& event)
{
    current_ = event.scenePos;
    pixel_ = event.pixels(1);
    const QPointF d = event.viewPos - pressViewPos_;
    moved_ = moved_ || std::hypot(d.x(), d.y()) > kDragThresholdPixels;

    target_.clear();
    targetPoint_.reset();
    if (!moved_)
        return;
    target_ = terminalAt(context_, event, {start_.source});
    if (target_.empty())
        return;
    if (const render::CellState* state = context_.scene->state(target_))
        targetPoint_ = nearestConnectionPoint(connectionPoints(*state), current_, kPointSnapPixels * pixel_);
}

void ConnectGesture::move(const CanvasEvent& event)
{
    updateTarget(event);
    context_.view->requestOverlayUpdate();
}

QRectF ConnectGesture::cloneBoundsAt(const CanvasEvent& event) const
{
    // mxConnectionHandler.createTargetVertex: the clone is centred under the
    // pointer, rounded, then aligned with the source within the alignment
    // tolerance (grid size / 2 with the grid on, else the hit tolerance).
    const render::CellState* source = context_.scene->state(start_.source);
    if (source == nullptr)
        return {};
    const QRectF sourceBounds = toQRect(source->bounds);
    const Snapper snapper(*context_.scene, context_.view->guidesEnabled());
    const core::Page* page = currentPage(context_);
    const double tolerance = page != nullptr && page->gridEnabled() ? page->gridSize() / 2
                                                                    : event.pixels(overlay::kHitTolerance);
    const QPointF topLeft(std::round(event.scenePos.x() - sourceBounds.width() / 2),
                          std::round(event.scenePos.y() - sourceBounds.height() / 2));
    QPointF snapped = snapper.snap(topLeft);
    if (std::abs(sourceBounds.x() - snapped.x()) <= tolerance)
        snapped.setX(std::round(sourceBounds.x()));
    if (std::abs(sourceBounds.y() - snapped.y()) <= tolerance)
        snapped.setY(std::round(sourceBounds.y()));
    return QRectF(snapped, sourceBounds.size());
}

QRectF ConnectGesture::cloneBoundsInDirection() const
{
    // Graph.connectVertex: the clone sits Graph.defaultEdgeLength beyond the
    // chosen side, centred on the source's axis.
    const render::CellState* source = context_.scene->state(start_.source);
    if (source == nullptr)
        return {};
    const QRectF b = toQRect(source->bounds);
    const QPointF direction = sideDirection(start_.side);
    const QPointF center = b.center() + QPointF(direction.x() * (b.width() + kDefaultEdgeLength),
                                                direction.y() * (b.height() + kDefaultEdgeLength));
    return QRectF(center.x() - b.width() / 2, center.y() - b.height() / 2, b.width(), b.height());
}

void ConnectGesture::execute(const NewConnection& request)
{
    const std::size_t pageIndex = context_.session->currentPageIndex();
    context_.session->edit(connectName(), [&](commands::Transaction& tx) { return addConnection(tx, pageIndex, request); });
}

void ConnectGesture::connectInDirection()
{
    const render::CellState* source = context_.scene->state(start_.source);
    const core::Page* page = currentPage(context_);
    if (source == nullptr || page == nullptr)
        return;
    NewConnection request;
    request.source = start_.source;
    request.insertionParent = context_.session->insertionParent();
    const QRectF bounds = cloneBoundsInDirection();

    // Graph.connectVertex connects to an existing vertex at the end point
    // instead of cloning when one is there and does not touch the source.
    const QRectF sourceBounds = toQRect(source->bounds);
    const std::string existing = context_.scene->cellAt(
        bounds.center(), 0, [&](const render::CellState& state) {
            return state.kind == render::CellState::Kind::Vertex && state.id != start_.source &&
                   isConnectableTerminal(*page, state) && !sourceBounds.intersects(toQRect(state.bounds));
        });
    if (!existing.empty())
        request.target = existing;
    else
        request.cloneBounds = core::Rect{bounds.x(), bounds.y(), bounds.width(), bounds.height()};
    execute(request);
}

void ConnectGesture::release(const CanvasEvent& event)
{
    updateTarget(event);
    if (!moved_) {
        if (start_.fromArrow)
            connectInDirection();
        return;
    }
    NewConnection request;
    request.source = start_.source;
    request.insertionParent = context_.session->insertionParent();
    if (start_.point)
        request.sourceConstraint = start_.point->constraint;
    if (!target_.empty()) {
        request.target = target_;
        if (targetPoint_)
            request.targetConstraint = targetPoint_->constraint;
    } else if (start_.fromArrow) {
        const QRectF bounds = cloneBoundsAt(event);
        if (bounds.isNull())
            return;
        request.cloneBounds = core::Rect{bounds.x(), bounds.y(), bounds.width(), bounds.height()};
    } else {
        const Snapper snapper(*context_.scene, context_.view->guidesEnabled());
        request.freePoint = toPoint(snapper.snap(event.scenePos));
    }
    execute(request);
}

void ConnectGesture::paint(QPainter& painter, const OverlayContext& context) const
{
    if (!moved_)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);

    QPointF end = current_;
    if (!target_.empty()) {
        if (const render::CellState* state = context_.scene->state(target_)) {
            paintTerminalHighlight(painter, context, *state);
            for (const ConnectionPoint& point : connectionPoints(*state))
                paintConnectionPoint(painter, context, point.position,
                                     targetPoint_ && targetPoint_->position == point.position);
            if (targetPoint_)
                end = targetPoint_->position;
        }
    }

    QPen pen(overlay::kHandleFill, 1, Qt::DashLine);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawLine(start_.anchor, end);

    if (target_.empty() && start_.fromArrow) {
        // The copy of the source the release would create.
        CanvasEvent event;
        event.scenePos = current_;
        event.scale = 1 / context.pixels(1);
        painter.drawRect(cloneBoundsAt(event));
    }
    painter.restore();
}

} // namespace qraw::ui::connect
