#include "edge_gestures.h"

#include "connect_edit.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/snapping.h"

#include <QCoreApplication>
#include <QPainter>
#include <QPolygonF>

#include <cmath>

namespace qraw::ui::edgeh {

namespace {

constexpr double kDragThresholdPixels = 4; // mxGraph.tolerance

QString translated(const char* text)
{
    return QCoreApplication::translate("qraw::ui::EdgeHandles", text);
}

QPointF snapped(const ToolContext& context, QPointF p)
{
    return Snapper(*context.scene, context.view->guidesEnabled()).snap(p);
}

bool exceedsDragThreshold(QPointF pressViewPos, QPointF viewPos)
{
    const QPointF d = viewPos - pressViewPos;
    return std::hypot(d.x(), d.y()) > kDragThresholdPixels;
}

void paintPreviewLine(QPainter& painter, const std::vector<QPointF>& points)
{
    QPen pen(overlay::kHandleFill, 1, Qt::DashLine);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(QPolygonF(QList<QPointF>(points.begin(), points.end())));
}

} // namespace

bool applyWaypoints(const ToolContext& context, const std::string& edge, const std::vector<core::Point>& points)
{
    const std::size_t page = context.session->currentPageIndex();
    return context.session
        ->edit(translated("Change Points"),
               [&](commands::Transaction& tx) { return commands::ops::setWaypoints(tx, page, edge, points); })
        .ok;
}

// --- end handles ---------------------------------------------------------------

EdgeEndGesture::EdgeEndGesture(const ToolContext& context, EdgeHandles handles, Handle handle,
                               const CanvasEvent& pressEvent)
    : context_(context),
      handles_(std::move(handles)),
      handle_(handle),
      pressViewPos_(pressEvent.viewPos),
      current_(pressEvent.scenePos),
      endPosition_(handle.position)
{
}

void EdgeEndGesture::update(const CanvasEvent& event)
{
    current_ = event.scenePos;
    moved_ = moved_ || exceedsDragThreshold(pressViewPos_, event.viewPos);
    target_.clear();
    targetPoint_.reset();
    endPosition_ = current_;
    if (!moved_)
        return;
    target_ = connect::terminalAt(context_, event, {handles_.edge});
    if (target_.empty()) {
        endPosition_ = snapped(context_, current_);
        return;
    }
    if (const render::CellState* state = context_.scene->state(target_)) {
        targetPoint_ = connect::nearestConnectionPoint(connect::connectionPoints(*state), current_,
                                                       connect::kPointSnapPixels * event.pixels(1));
        if (targetPoint_)
            endPosition_ = targetPoint_->position;
    }
}

void EdgeEndGesture::move(const CanvasEvent& event)
{
    update(event);
    context_.view->requestOverlayUpdate();
}

void EdgeEndGesture::release(const CanvasEvent& event)
{
    update(event);
    if (!moved_)
        return;
    const bool source = handle_.kind == HandleKind::Source;
    const std::size_t page = context_.session->currentPageIndex();
    const std::string edge = handles_.edge;
    if (!target_.empty()) {
        std::optional<commands::ops::ConnectionConstraint> constraint;
        if (targetPoint_)
            constraint = targetPoint_->constraint;
        context_.session->edit(translated("Connect"), [&](commands::Transaction& tx) {
            return connect::reconnectEnd(tx, page, edge, target_, source, constraint);
        });
    } else {
        const core::Point point = connect::toPoint(endPosition_);
        context_.session->edit(translated("Disconnect"), [&](commands::Transaction& tx) {
            return connect::detachEnd(tx, page, edge, source, point);
        });
    }
}

std::vector<QPointF> EdgeEndGesture::routedPreview() const
{
    const bool source = handle_.kind == HandleKind::Source;
    const render::CellState* edge = context_.scene->state(handles_.edge);
    std::vector<QPointF> line;
    if (edge != nullptr) {
        render::RouteEnd end;
        if (!target_.empty()) {
            end.kind = render::RouteEnd::Kind::Cell;
            end.cell = target_;
            if (targetPoint_ && targetPoint_->constraint.point) {
                const commands::ops::ConnectionConstraint& c = targetPoint_->constraint;
                geometry::ConnectionConstraint fixed;
                fixed.point = *c.point;
                fixed.dx = c.dx;
                fixed.dy = c.dy;
                fixed.perimeter = c.perimeter;
                end.constraint = fixed;
            }
        } else {
            end.kind = render::RouteEnd::Kind::Point;
            end.point = {endPosition_.x(), endPosition_.y()};
        }
        render::RouteOverride change;
        (source ? change.source : change.target) = end;
        for (const core::Point& p : context_.scene->pageView().previewRoute(*edge, change))
            line.emplace_back(p.x, p.y);
    }
    if (line.size() >= 2)
        return line;
    // Not routable: the straight line from the neighbouring point.
    const QPointF neighbour = !handles_.waypoints.empty()
                                  ? (source ? handles_.waypoints.front() : handles_.waypoints.back())
                                  : (source ? handles_.route.back() : handles_.route.front());
    return {neighbour, endPosition_};
}

void EdgeEndGesture::paint(QPainter& painter, const OverlayContext& context) const
{
    if (!moved_)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    if (!target_.empty()) {
        if (const render::CellState* state = context_.scene->state(target_)) {
            connect::paintTerminalHighlight(painter, context, *state);
            for (const connect::ConnectionPoint& point : connect::connectionPoints(*state))
                connect::paintConnectionPoint(painter, context, point.position,
                                              targetPoint_ && targetPoint_->position == point.position);
        }
    }
    paintPreviewLine(painter, routedPreview());
    painter.restore();
}

// --- waypoint handles -------------------------------------------------------

EdgePointGesture::EdgePointGesture(const ToolContext& context, EdgeHandles handles, Handle handle,
                                   const CanvasEvent& pressEvent)
    : context_(context),
      handles_(std::move(handles)),
      handle_(handle),
      pressViewPos_(pressEvent.viewPos),
      current_(handle.position)
{
}

void EdgePointGesture::update(const CanvasEvent& event)
{
    moved_ = moved_ || exceedsDragThreshold(pressViewPos_, event.viewPos);
    current_ = moved_ ? snapped(context_, event.scenePos) : handle_.position;
}

std::vector<core::Point> EdgePointGesture::pointsWithDrag() const
{
    const core::Point local{current_.x() - handles_.origin.x, current_.y() - handles_.origin.y};
    std::vector<core::Point> points = handles_.points;
    switch (handle_.kind) {
    case HandleKind::Waypoint:
        points[handle_.index] = local;
        break;
    case HandleKind::Virtual:
        points.insert(points.begin() + static_cast<std::ptrdiff_t>(handle_.index), local);
        break;
    case HandleKind::Elbow:
        points = {local};
        break;
    case HandleKind::Source:
    case HandleKind::Target:
    case HandleKind::Segment: // EdgeSegmentGesture
        break;
    }
    return points;
}

std::vector<QPointF> EdgePointGesture::previewWaypoints() const
{
    std::vector<QPointF> points;
    for (const core::Point& p : pointsWithDrag())
        points.emplace_back(p.x + handles_.origin.x, p.y + handles_.origin.y);
    return points;
}

void EdgePointGesture::move(const CanvasEvent& event)
{
    update(event);
    context_.view->requestOverlayUpdate();
}

void EdgePointGesture::release(const CanvasEvent& event)
{
    update(event);
    if (moved_)
        applyWaypoints(context_, handles_.edge, pointsWithDrag());
}

void EdgePointGesture::paint(QPainter& painter, const OverlayContext& context) const
{
    if (!moved_)
        return;
    (void)context;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    std::vector<QPointF> line{handles_.route.front()};
    for (const QPointF& p : previewWaypoints())
        line.push_back(p);
    line.push_back(handles_.route.back());
    paintPreviewLine(painter, line);
    painter.restore();
}

// --- segment handles --------------------------------------------------------

EdgeSegmentGesture::EdgeSegmentGesture(const ToolContext& context, EdgeHandles handles, Handle handle,
                                       const CanvasEvent& pressEvent)
    : context_(context),
      handles_(std::move(handles)),
      handle_(handle),
      pressViewPos_(pressEvent.viewPos),
      current_(handle.position)
{
}

void EdgeSegmentGesture::update(const CanvasEvent& event)
{
    moved_ = moved_ || exceedsDragThreshold(pressViewPos_, event.viewPos);
    if (!moved_)
        return;
    current_ = snapped(context_, event.scenePos);
    if (const render::CellState* edge = context_.scene->state(handles_.edge))
        drag_ = dragSegment(context_.scene->pageView(), *edge, handles_, handle_.index, current_);
}

void EdgeSegmentGesture::move(const CanvasEvent& event)
{
    update(event);
    context_.view->requestOverlayUpdate();
}

void EdgeSegmentGesture::release(const CanvasEvent& event)
{
    update(event);
    if (!moved_ || !drag_)
        return;
    // Nothing to change when the edge has no waypoints and routes without any.
    if (drag_->points.empty() && handles_.points.empty())
        return;
    applyWaypoints(context_, handles_.edge, drag_->points);
}

void EdgeSegmentGesture::paint(QPainter& painter, const OverlayContext&) const
{
    if (!moved_ || !drag_)
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    paintPreviewLine(painter, drag_->route);
    painter.restore();
}

} // namespace qraw::ui::edgeh
