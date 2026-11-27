#include "connect_handler.h"

#include "connect_gesture.h"
#include "select_support.h"

#include "qraw/ui/canvas_view.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"

#include <QGuiApplication>
#include <QPainter>
#include <QPolygonF>

namespace qraw::ui::connect {

namespace {

// The connection point closest to the pointer that is within hit distance;
// stricter than the 10 px snap used while connecting, so that a press near
// a vertex border still selects or moves it.
constexpr double kPointHitPixels = 5;

QPolygonF arrowTriangle(QPointF center, Side side, double pixel)
{
    const QPointF d = sideDirection(side);
    const QPointF n(-d.y(), d.x());
    const double half = kArrowSize / 2 * pixel;
    return QPolygonF({center + d * half, center - d * half + n * half, center - d * half - n * half});
}

} // namespace

const render::CellState* ConnectHandler::hoveredState() const
{
    const core::Page* page = currentPage(context_);
    if (hoverCell_.empty() || page == nullptr || context_.scene == nullptr)
        return nullptr;
    const render::CellState* state = context_.scene->state(hoverCell_);
    return state != nullptr && state->kind == render::CellState::Kind::Vertex && isConnectableTerminal(*page, *state)
               ? state
               : nullptr;
}

ConnectHandler::Hit ConnectHandler::hitOfCell(const render::CellState& state, const CanvasEvent& event) const
{
    Hit hit;
    hit.cell = state.id;
    hit.kind = Hit::Kind::Body;
    if (const auto side = arrowAt(toQRect(state.bounds), event.scenePos, event.pixels(1))) {
        hit.kind = Hit::Kind::Arrow;
        hit.side = *side;
    } else if (const auto point = nearestConnectionPoint(connectionPoints(state), event.scenePos,
                                                         event.pixels(kPointHitPixels))) {
        hit.kind = Hit::Kind::Point;
        hit.point = point;
    }
    return hit;
}

ConnectHandler::Hit ConnectHandler::hitTest(const CanvasEvent& event, bool search) const
{
    const core::Page* page = currentPage(context_);
    if (page == nullptr || context_.scene == nullptr)
        return {};

    // The arrows lie outside the vertex: keep the hovered one while the
    // pointer is on one of them.
    const render::CellState* hovered = hoveredState();
    if (hovered != nullptr) {
        Hit hit = hitOfCell(*hovered, event);
        if (hit.kind == Hit::Kind::Arrow)
            return hit;
    }

    const std::string id = context_.scene->cellAt(
        event.scenePos, event.pixels(overlay::kHitTolerance), [&](const render::CellState& state) {
            return state.kind == render::CellState::Kind::Vertex && isConnectableTerminal(*page, state);
        });
    if (!id.empty())
        return hitOfCell(*context_.scene->state(id), event);

    // Between the vertex and its arrows the hover stays.
    if (hovered != nullptr && hoverArea(toQRect(hovered->bounds), event.pixels(1)).contains(event.scenePos))
        return hitOfCell(*hovered, event);

    if (search) {
        // The arrows lie within hoverArea's margin of the bounds.
        const double margin = (connect::kArrowDistance + connect::kArrowHit) * event.pixels(1);
        const auto& states = context_.scene->pageView().states();
        const auto candidates = context_.scene->statesIn(
            QRectF(event.scenePos, QSizeF(0, 0)).adjusted(-margin, -margin, margin, margin));
        for (const std::uint32_t index : candidates) {
            const render::CellState& state = states[index];
            if (state.kind == render::CellState::Kind::Vertex && isConnectableTerminal(*page, state) &&
                hoverArea(toQRect(state.bounds), event.pixels(1)).contains(event.scenePos)) {
                Hit hit = hitOfCell(state, event);
                if (hit.kind == Hit::Kind::Arrow)
                    return hit;
            }
        }
    }
    return {};
}

Qt::CursorShape ConnectHandler::bodyCursor() const
{
    const core::Page* page = context_.session != nullptr ? context_.session->currentPage() : nullptr;
    const render::CellState* state = hoveredState();
    if (page != nullptr && state != nullptr && context_.session->selection().contains(hit_.cell) &&
        isMovable(*page, *state))
        return Qt::SizeAllCursor;
    return Qt::ArrowCursor;
}

std::optional<Qt::CursorShape> ConnectHandler::hover(const CanvasEvent& event)
{
    hit_ = hitTest(event, false);
    hoverCell_ = hit_.cell;
    if (context_.view != nullptr)
        context_.view->requestOverlayUpdate();
    switch (hit_.kind) {
    case Hit::Kind::None: return std::nullopt;
    case Hit::Kind::Body: return bodyCursor();
    case Hit::Kind::Arrow:
    case Hit::Kind::Point: return Qt::PointingHandCursor;
    }
    return std::nullopt;
}

void ConnectHandler::hoverLeft()
{
    if (hoverCell_.empty())
        return;
    hoverCell_.clear();
    hit_ = {};
    if (context_.view != nullptr)
        context_.view->requestOverlayUpdate();
}

std::unique_ptr<Gesture> ConnectHandler::press(const CanvasEvent& event)
{
    if (event.button != Qt::LeftButton || (event.modifiers & (Qt::ControlModifier | Qt::AltModifier)))
        return nullptr;
    const Hit hit = hitTest(event, true);
    if (hit.kind != Hit::Kind::Arrow && hit.kind != Hit::Kind::Point)
        return nullptr;
    const render::CellState* state = context_.scene->state(hit.cell);
    if (state == nullptr)
        return nullptr;

    ConnectStart start;
    start.source = hit.cell;
    if (hit.kind == Hit::Kind::Arrow) {
        start.fromArrow = true;
        start.side = hit.side;
        start.anchor = sideMidpoint(toQRect(state->bounds), hit.side);
    } else {
        start.point = hit.point;
        start.anchor = hit.point->position;
    }
    hoverCell_.clear();
    hit_ = {};
    return std::make_unique<ConnectGesture>(context_, std::move(start), event);
}

void ConnectHandler::reset()
{
    hoverCell_.clear();
    hit_ = {};
}

void ConnectHandler::paintOverlay(QPainter& painter, const OverlayContext& context) const
{
    const render::CellState* state = hoveredState();
    // Not while another drag runs (moving a cell keeps the hover state).
    if (state == nullptr || (QGuiApplication::mouseButtons() & Qt::LeftButton))
        return;
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing);
    const QRectF bounds = toQRect(state->bounds);
    QPen pen(overlay::kHandleStroke, 1);
    pen.setCosmetic(true);
    painter.setPen(pen);
    for (const Side side : kSides) {
        const bool hot = hit_.kind == Hit::Kind::Arrow && hit_.side == side;
        painter.setBrush(hot ? overlay::kHandleStroke : overlay::kHandleFill);
        painter.drawPolygon(arrowTriangle(arrowCenter(bounds, side, context.pixels(1)), side, context.pixels(1)));
    }
    painter.restore();

    for (const ConnectionPoint& point : connectionPoints(*state)) {
        const bool hot = hit_.kind == Hit::Kind::Point && hit_.point && hit_.point->position == point.position;
        paintConnectionPoint(painter, context, point.position, hot);
    }
}

// The hovered vertex with its four arrows (20 px outside the side midpoints, 14 px wide).
OverlayBounds ConnectHandler::overlayBounds(const OverlayContext& context) const
{
    const render::CellState* state = hoveredState();
    if (state == nullptr)
        return QRectF();
    const double margin = context.pixels(kArrowDistance + kArrowSize + 4);
    return toQRect(state->bounds).adjusted(-margin, -margin, margin, margin);
}

} // namespace qraw::ui::connect
