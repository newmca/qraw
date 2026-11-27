#include "select_support.h"

#include "qraw/commands/ops_tables.h"
#include "qraw/commands/queries.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/diagram_scene.h"

#include <QPolygonF>

#include <cmath>

namespace qraw::ui {

namespace {
constexpr double kPi = 3.14159265358979323846;
}

QRectF toQRect(const core::Rect& rect)
{
    return {rect.x, rect.y, rect.width, rect.height};
}

core::Rect toCoreRect(const QRectF& rect)
{
    return {rect.x(), rect.y(), rect.width(), rect.height()};
}

QPointF rotateAround(QPointF p, QPointF center, double degrees)
{
    const double radians = degrees * kPi / 180;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    const QPointF d = p - center;
    return {center.x() + d.x() * c - d.y() * s, center.y() + d.x() * s + d.y() * c};
}

double angleOf(QPointF vector)
{
    return std::atan2(vector.y(), vector.x()) * 180 / kPi;
}

double normalizeAngle(double degrees)
{
    double a = std::fmod(degrees, 360.0);
    if (a < 0)
        a += 360;
    return a >= 360 ? 0 : a;
}

bool styleAllows(const render::CellState& state, const char* key)
{
    return state.style.flag(key, true);
}

bool isMovable(const core::Page& page, const render::CellState& state)
{
    return !isCellLocked(page, state.id) && styleAllows(state, "movable");
}

std::vector<std::string> movableSelection(const DiagramScene& scene, const core::Page& page,
                                          const std::vector<std::string>& selection)
{
    std::vector<std::string> result;
    for (const std::string& id : commands::topmostCells(page, selection)) {
        const render::CellState* state = scene.state(id);
        if (state != nullptr && isMovable(page, *state))
            result.push_back(id);
    }
    return result;
}

QRectF stateBoundsUnion(const DiagramScene& scene, const std::vector<std::string>& ids)
{
    QRectF result;
    bool first = true;
    for (const std::string& id : ids) {
        const render::CellState* state = scene.state(id);
        if (state == nullptr)
            continue;
        const QRectF bounds = toQRect(state->bounds);
        result = first ? bounds : result.united(bounds);
        first = false;
    }
    return result;
}

std::string childToward(const core::Page& page, const std::string& ancestor, const std::string& descendant)
{
    // Rows and cells are parts of their table: the next click below a table
    // selects the cell, not the row in between (Graph.selectCellForEvent).
    const core::Cell* table = page.findCell(ancestor);
    if (table != nullptr && commands::ops::isTable(*table)) {
        for (std::string id = descendant; !id.empty();) {
            const core::Cell* cell = page.findCell(id);
            if (cell == nullptr)
                break;
            if (commands::ops::isTableCell(page, *cell) || (commands::ops::isTableRow(page, *cell) && cell->parent() == ancestor))
                return id;
            id = cell->parent();
        }
    }
    std::string current = descendant;
    for (std::size_t guard = 0; guard <= page.cellCount(); ++guard) {
        const core::Cell* cell = page.findCell(current);
        if (cell == nullptr)
            return {};
        if (cell->parent() == ancestor)
            return current;
        current = cell->parent();
    }
    return {};
}

QPen selectionPen()
{
    QPen pen(overlay::kSelection, 1, Qt::DashLine);
    pen.setCosmetic(true);
    return pen;
}

void paintRotatedRect(QPainter& painter, const QRectF& rect, double rotationDegrees)
{
    paintRotatedRect(painter, rect, rotationDegrees, selectionPen());
}

void paintRotatedRect(QPainter& painter, const QRectF& rect, double rotationDegrees, const QPen& pen)
{
    painter.save();
    painter.setPen(pen);
    painter.setBrush(Qt::NoBrush);
    painter.translate(rect.center());
    painter.rotate(rotationDegrees);
    painter.drawRect(QRectF(-rect.width() / 2, -rect.height() / 2, rect.width(), rect.height()));
    painter.restore();
}

void paintDropTarget(QPainter& painter, const render::CellState& state, bool dark)
{
    QPen pen(overlay::dropTarget(dark), 3);
    pen.setCosmetic(true);
    paintRotatedRect(painter, toQRect(state.bounds), state.style.rotation(), pen);
}

void paintCellOutline(QPainter& painter, const render::CellState& state, QPointF delta)
{
    if (state.kind == render::CellState::Kind::Vertex) {
        paintRotatedRect(painter, toQRect(state.bounds).translated(delta), state.style.rotation());
        return;
    }
    QPolygonF polyline;
    for (const auto& p : state.points)
        polyline << QPointF(p.x, p.y) + delta;
    painter.save();
    painter.setPen(selectionPen());
    painter.setBrush(Qt::NoBrush);
    painter.drawPolyline(polyline);
    painter.restore();
}

} // namespace qraw::ui
