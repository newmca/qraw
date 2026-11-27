#include "connect_common.h"

#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QPainter>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace qraw::ui::connect {

QRectF toQRect(const core::Rect& rect)
{
    return {rect.x, rect.y, rect.width, rect.height};
}

QPointF toQPoint(const core::Point& point)
{
    return {point.x, point.y};
}

core::Point toPoint(QPointF point)
{
    return {point.x(), point.y()};
}

const core::Page* currentPage(const ToolContext& context)
{
    return context.session != nullptr ? context.session->currentPage() : nullptr;
}

namespace {

commands::ops::ConnectionConstraint constraintAt(double x, double y, bool perimeter = true, double dx = 0,
                                                  double dy = 0)
{
    commands::ops::ConnectionConstraint c;
    c.point = core::Point{x, y};
    c.dx = dx;
    c.dy = dy;
    c.perimeter = perimeter;
    return c;
}

// Entries of the `points` style: [x, y, perimeter (0/1), dx, dy], the last
// three optional. Invalid JSON yields no points, as upstream ("ignore").
std::vector<commands::ops::ConnectionConstraint> parsePointsStyle(std::string_view text)
{
    std::vector<commands::ops::ConnectionConstraint> result;
    const QJsonDocument json = QJsonDocument::fromJson(QByteArray(text.data(), static_cast<qsizetype>(text.size())));
    if (!json.isArray())
        return result;
    for (const QJsonValue& entry : json.array()) {
        const QJsonArray values = entry.toArray();
        if (values.size() < 2)
            continue;
        const bool perimeter = values.size() > 2 ? values.at(2).toVariant().toString() != QLatin1String("0") : true;
        result.push_back(constraintAt(values.at(0).toDouble(), values.at(1).toDouble(), perimeter,
                                      values.size() > 3 ? values.at(3).toDouble() : 0,
                                      values.size() > 4 ? values.at(4).toDouble() : 0));
    }
    return result;
}

QPointF positionOf(const commands::ops::ConnectionConstraint& c, const render::CellState& state)
{
    const core::Rect& b = state.bounds;
    double x = c.point->x;
    double y = c.point->y;
    double dx = c.dx;
    double dy = c.dy;
    if (state.style.flipH()) {
        x = 1 - x;
        dx = -dx;
    }
    if (state.style.flipV()) {
        y = 1 - y;
        dy = -dy;
    }
    QPointF p(b.x + x * b.width + dx, b.y + y * b.height + dy);
    const double rotation = state.style.rotation();
    if (rotation != 0) {
        QTransform t;
        const QPointF center(b.x + b.width / 2, b.y + b.height / 2);
        t.translate(center.x(), center.y());
        t.rotate(rotation);
        t.translate(-center.x(), -center.y());
        p = t.map(p);
    }
    return p;
}

} // namespace

std::vector<ConnectionPoint> connectionPoints(const render::CellState& state)
{
    if (state.kind != render::CellState::Kind::Vertex)
        return {};
    std::vector<commands::ops::ConnectionConstraint> constraints;
    if (const std::string* points = state.style.map().find("points"))
        constraints = parsePointsStyle(*points);
    else
        constraints = {constraintAt(0.5, 0), constraintAt(1, 0.5), constraintAt(0.5, 1), constraintAt(0, 0.5)};
    std::vector<ConnectionPoint> result;
    result.reserve(constraints.size());
    for (const auto& c : constraints)
        result.push_back({c, positionOf(c, state)});
    return result;
}

std::optional<ConnectionPoint> nearestConnectionPoint(const std::vector<ConnectionPoint>& points, QPointF p,
                                                      double radius)
{
    std::optional<ConnectionPoint> best;
    double bestDistance = radius;
    for (const ConnectionPoint& point : points) {
        const double d = std::hypot(point.position.x() - p.x(), point.position.y() - p.y());
        if (d <= bestDistance) {
            bestDistance = d;
            best = point;
        }
    }
    return best;
}

bool isConnectableTerminal(const core::Page& page, const render::CellState& state)
{
    if (state.cell == nullptr || !state.cell->isConnectable())
        return false;
    if (state.style.map().find("connectable") != nullptr && !state.style.flag("connectable", true))
        return false;
    return isCellSelectable(page, state.id) && !isCellLocked(page, state.id);
}

std::string terminalAt(const ToolContext& context, const CanvasEvent& event, const std::vector<std::string>& exclude)
{
    const core::Page* page = currentPage(context);
    if (page == nullptr || context.scene == nullptr)
        return {};
    return context.scene->cellAt(event.scenePos, event.pixels(overlay::kHitTolerance),
                                 [&](const render::CellState& state) {
                                     return std::find(exclude.begin(), exclude.end(), state.id) == exclude.end() &&
                                            isConnectableTerminal(*page, state);
                                 });
}

void paintTerminalHighlight(QPainter& painter, const OverlayContext& context, const render::CellState& state)
{
    painter.save();
    QPen pen(overlay::kValid, 2);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    if (state.kind == render::CellState::Kind::Vertex) {
        const double pad = context.pixels(1);
        painter.drawRect(toQRect(state.bounds).adjusted(-pad, -pad, pad, pad));
    } else if (state.points.size() >= 2) {
        QPolygonF polyline;
        for (const core::Point& p : state.points)
            polyline << toQPoint(p);
        painter.drawPolyline(polyline);
    }
    painter.restore();
}

void paintConnectionPoint(QPainter& painter, const OverlayContext& context, QPointF position, bool highlighted)
{
    painter.save();
    const double r = context.pixels(highlighted ? 5 : 3.5);
    QPen pen(highlighted ? overlay::kValid : overlay::kHandleFill, highlighted ? 2 : 1.5);
    pen.setCosmetic(true);
    painter.setPen(pen);
    painter.drawLine(QPointF(position.x() - r, position.y() - r), QPointF(position.x() + r, position.y() + r));
    painter.drawLine(QPointF(position.x() - r, position.y() + r), QPointF(position.x() + r, position.y() - r));
    painter.restore();
}

} // namespace qraw::ui::connect
