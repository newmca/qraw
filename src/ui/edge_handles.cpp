#include "edge_handles.h"

#include "edge_gestures.h"

#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/snapping.h"

#include <QPainter>

#include <algorithm>

namespace qraw::ui::edgeh {

namespace {

// A press that only did its work immediately (adding or removing a point).
class NoopGesture : public Gesture {
public:
    void move(const CanvasEvent&) override {}
    void release(const CanvasEvent&) override {}
    void cancel() override {}
};

Qt::CursorShape cursorFor(const Handle& handle)
{
    if (handle.kind == HandleKind::Segment)
        return handle.verticalSegment ? Qt::SizeHorCursor : Qt::SizeVerCursor;
    return handle.kind == HandleKind::Source || handle.kind == HandleKind::Target ? Qt::PointingHandCursor
                                                                                  : Qt::SizeAllCursor;
}

void paintHandle(QPainter& painter, const OverlayContext& context, const Handle& handle, bool hot)
{
    const QPointF c = handle.position;
    QPen pen(overlay::kHandleStroke, 1);
    pen.setCosmetic(true);
    painter.setPen(pen);
    switch (handle.kind) {
    case HandleKind::Source:
    case HandleKind::Target: {
        // Connected ends are filled, dangling ends white with a blue border.
        painter.setBrush(handle.connected ? overlay::kHandleFill : QColor(Qt::white));
        if (!handle.connected) {
            pen.setColor(overlay::kHandleFill);
            painter.setPen(pen);
        }
        const double r = context.pixels(overlay::kHandleSize / 2);
        painter.drawEllipse(c, r, r);
        break;
    }
    case HandleKind::Waypoint:
    case HandleKind::Elbow: {
        painter.setBrush(hot ? overlay::kHandleStroke : overlay::kHandleFill);
        const double r = context.pixels(overlay::kHandleSize / 2);
        painter.drawRect(QRectF(c.x() - r, c.y() - r, 2 * r, 2 * r));
        break;
    }
    case HandleKind::Segment:
    case HandleKind::Virtual: {
        // Smaller and semi-transparent (mxEdgeHandler virtual bends).
        QColor fill = overlay::kHandleFill;
        fill.setAlphaF(hot ? 0.8 : 0.4);
        painter.setBrush(fill);
        const double r = context.pixels(3.5);
        painter.drawEllipse(c, r, r);
        break;
    }
    }
}

} // namespace

std::optional<EdgeHandles> EdgeHandlesHandler::selectedEdgeHandles(const ToolContext& context, double pixel)
{
    const core::Page* page = context.session != nullptr ? context.session->currentPage() : nullptr;
    if (page == nullptr || context.scene == nullptr || context.session->selection().size() != 1)
        return std::nullopt;
    const std::string id = context.session->selection().first();
    const render::CellState* state = context.scene->state(id);
    if (state == nullptr || state->kind != render::CellState::Kind::Edge || isCellLocked(*page, id) ||
        !isCellSelectable(*page, id))
        return std::nullopt;
    return edgeHandlesFor(*page, *state, pixel);
}

std::optional<Qt::CursorShape> EdgeHandlesHandler::hover(const CanvasEvent& event)
{
    std::optional<Handle> previous = hovered_;
    hovered_.reset();
    std::optional<Qt::CursorShape> cursor;
    if (const auto handles = selectedEdgeHandles(context_, event.pixels(1))) {
        if (const Handle* handle = hitHandle(*handles, event.scenePos, event.pixels(1))) {
            hovered_ = *handle;
            cursor = cursorFor(*handle);
        }
    }
    const bool changed = previous.has_value() != hovered_.has_value() ||
                         (previous && hovered_ && (previous->kind != hovered_->kind || previous->index != hovered_->index));
    if (changed && context_.view != nullptr)
        context_.view->requestOverlayUpdate();
    return cursor;
}

void EdgeHandlesHandler::hoverLeft()
{
    if (hovered_ && context_.view != nullptr)
        context_.view->requestOverlayUpdate();
    hovered_.reset();
}

void EdgeHandlesHandler::reset()
{
    hovered_.reset();
}

bool EdgeHandlesHandler::removeWaypoint(const EdgeHandles& handles, std::size_t index)
{
    std::vector<core::Point> points = handles.points;
    if (index >= points.size())
        return false;
    points.erase(points.begin() + static_cast<std::ptrdiff_t>(index));
    return applyWaypoints(context_, handles.edge, points);
}

bool EdgeHandlesHandler::addWaypointAt(const EdgeHandles& handles, QPointF position)
{
    const std::size_t segment = nearestSegment(handles.route, position);
    const std::size_t index = insertionIndex(handles.route, handles.waypoints, segment);
    const QPointF p = Snapper(*context_.scene, context_.view->guidesEnabled()).snap(position);
    std::vector<core::Point> points = handles.points;
    points.insert(points.begin() + static_cast<std::ptrdiff_t>(index),
                  core::Point{p.x() - handles.origin.x, p.y() - handles.origin.y});
    return applyWaypoints(context_, handles.edge, points);
}

std::unique_ptr<Gesture> EdgeHandlesHandler::press(const CanvasEvent& event)
{
    if (event.button != Qt::LeftButton)
        return nullptr;
    const auto handles = selectedEdgeHandles(context_, event.pixels(1));
    if (!handles)
        return nullptr;

    const bool shift = event.modifiers & Qt::ShiftModifier;
    if (const Handle* handle = hitHandle(*handles, event.scenePos, event.pixels(1))) {
        switch (handle->kind) {
        case HandleKind::Source:
        case HandleKind::Target:
            return std::make_unique<EdgeEndGesture>(context_, *handles, *handle, event);
        case HandleKind::Waypoint:
            if (shift) { // isRemovePointEvent
                removeWaypoint(*handles, handle->index);
                return std::make_unique<NoopGesture>();
            }
            return std::make_unique<EdgePointGesture>(context_, *handles, *handle, event);
        case HandleKind::Segment:
            return std::make_unique<EdgeSegmentGesture>(context_, *handles, *handle, event);
        case HandleKind::Elbow:
        case HandleKind::Virtual:
            return std::make_unique<EdgePointGesture>(context_, *handles, *handle, event);
        }
    }

    // Shift+click on the edge adds a waypoint there (isAddPointEvent).
    if (shift && !handles->elbowStyle && !handles->segmentStyle &&
        distanceToRoute(handles->route, event.scenePos) <= event.pixels(overlay::kHitTolerance)) {
        addWaypointAt(*handles, event.scenePos);
        return std::make_unique<NoopGesture>();
    }
    return nullptr;
}

bool EdgeHandlesHandler::doubleClick(const CanvasEvent& event)
{
    const auto handles = selectedEdgeHandles(context_, event.pixels(1));
    if (!handles)
        return false;
    const Handle* handle = hitHandle(*handles, event.scenePos, event.pixels(1));
    if (handle == nullptr || handle->kind != HandleKind::Waypoint)
        return false;
    removeWaypoint(*handles, handle->index);
    return true;
}

void EdgeHandlesHandler::paintOverlay(QPainter& painter, const OverlayContext& context) const
{
    const auto handles = selectedEdgeHandles(context_, context.pixels(1));
    if (!handles)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    // Virtual handles first so the real ones stay on top.
    for (auto it = handles->handles.rbegin(); it != handles->handles.rend(); ++it) {
        const bool hot = hovered_ && hovered_->kind == it->kind && hovered_->index == it->index;
        paintHandle(painter, context, *it, hot);
    }
    painter.restore();
}

// The handles of the selected edge: its route and handle positions, grown by a handle.
OverlayBounds EdgeHandlesHandler::overlayBounds(const OverlayContext& context) const
{
    const auto handles = selectedEdgeHandles(context_, context.pixels(1));
    if (!handles)
        return QRectF();
    // QRectF::united ignores rectangles without size, so collect the extremes.
    if (handles->route.empty() && handles->handles.empty())
        return QRectF();
    QPointF low = handles->route.empty() ? handles->handles.front().position : handles->route.front();
    QPointF high = low;
    const auto add = [&](QPointF p) {
        low = QPointF(std::min(low.x(), p.x()), std::min(low.y(), p.y()));
        high = QPointF(std::max(high.x(), p.x()), std::max(high.y(), p.y()));
    };
    for (const QPointF& p : handles->route)
        add(p);
    for (const Handle& handle : handles->handles)
        add(handle.position);
    const QRectF united(low, high);
    const double margin = context.pixels(2 * overlay::kHandleSize + 4);
    return united.adjusted(-margin, -margin, margin, margin);
}

} // namespace qraw::ui::edgeh
