#include "qraw/commands/ops_transform.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_layout.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/commands/ops_tables.h"
#include "qraw/commands/primitives.h"
#include "qraw/commands/queries.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/cell.h"
#include "qraw/core/number.h"
#include "qraw/core/page.h"
#include "qraw/core/style.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <functional>
#include <string_view>
#include <optional>
#include <unordered_set>

namespace qraw::commands::ops {

namespace {

using core::Cell;
using core::Geometry;
using core::Page;
using core::Point;
using core::Rect;

constexpr double kPi = 3.14159265358979323846;

double toRadians(double degrees) { return degrees * kPi / 180; }
// Math.round(value * 100) / 100 as upstream rounds mapped coordinates.
double round2(double value) { return std::floor(value * 100 + 0.5) / 100; }
// JavaScript's Math.round (mxGeometry.rotate rounds to whole units).
double jsRound(double value) { return std::floor(value + 0.5); }

using PointMap = std::function<Point(Point)>;

Result unknownCell(const std::string& id) { return Result::failure("unknown cell '" + id + "'"); }

double styleNumber(const core::Style& style, const char* key, double fallback)
{
    return core::parseNumber(style.value(key, ""), fallback);
}

// Graph.getMultiSelectionRotation: the style's rotation when it is a number.
double rotationOf(const core::Style& style)
{
    const double rotation = styleNumber(style, "rotation", 0);
    return std::isfinite(rotation) ? rotation : 0;
}

bool isTransparentBounds(const core::Style& style) { return style.value("transparentBounds", "0") == "1"; }

// Writes the cell back when `edit` changed it.
bool editCell(Transaction& tx, std::size_t pageIndex, const std::string& id, const std::function<void(Cell&)>& edit)
{
    const Cell* current = tx.page(pageIndex)->findCell(id);
    if (current == nullptr)
        return false;
    Cell changed = *current;
    edit(changed);
    return changed == *current || tx.emplace<ReplaceCell>(pageIndex, std::move(changed));
}

// Graph.getTransformCells: groups with transparentBounds are replaced by
// their child vertices and edges (nested ones too); with a child layout the
// layout positions the children, so the group is dropped.
std::vector<std::string> transformCells(const Page& page, const std::vector<std::string>& ids)
{
    std::vector<std::string> result;
    std::vector<std::string> stack(ids.rbegin(), ids.rend());
    while (!stack.empty()) {
        const std::string id = std::move(stack.back());
        stack.pop_back();
        const Cell* cell = page.findCell(id);
        if (cell == nullptr)
            continue;
        if (cell->isVertex() && isTransparentBounds(cell->style())) {
            if (hasChildLayout(*cell))
                continue;
            const auto children = page.children(id);
            for (auto it = children.rbegin(); it != children.rend(); ++it) {
                if ((*it)->isVertex() || (*it)->isEdge())
                    stack.push_back((*it)->id());
            }
        } else {
            result.push_back(id);
        }
    }
    return result;
}

// Graph.getMappedEdgeGeometry: waypoints and terminal points through `map`,
// which takes and returns absolute coordinates.
void mapEdgeGeometry(Geometry& geo, Point origin, const PointMap& map)
{
    const auto mapPoint = [&](const Point& p) {
        const Point q = map({origin.x + p.x, origin.y + p.y});
        return Point{round2(q.x - origin.x), round2(q.y - origin.y)};
    };
    if (const auto source = geo.sourcePoint())
        geo.setSourcePoint(mapPoint(*source));
    if (const auto target = geo.targetPoint())
        geo.setTargetPoint(mapPoint(*target));
    std::vector<Point> points = geo.points();
    if (!points.empty()) {
        for (Point& p : points)
            p = mapPoint(p);
        geo.setPoints(points);
    }
}

// Moves a vertex geometry (parent coordinates) so that its centre is
// `map`ped and gives it the size (w, h).
void placeVertex(Geometry& geo, Point origin, double w, double h, const PointMap& map)
{
    const Point center = map({origin.x + geo.x() + geo.width() / 2, origin.y + geo.y() + geo.height() / 2});
    const double x = round2(center.x - origin.x - w / 2);
    const double y = round2(center.y - origin.y - h / 2);
    if (x != geo.x())
        geo.setX(x);
    if (y != geo.y())
        geo.setY(y);
    if (w != geo.width())
        geo.setWidth(w);
    if (h != geo.height())
        geo.setHeight(h);
}

// Graph.isRecursiveVertexResize.
bool isRecursiveResize(const Page& page, const Cell& cell)
{
    const core::Style style = cell.style();
    return !isSwimlane(page, cell) && !page.childIds(cell.id()).empty() && !cell.isCollapsed() &&
           style.value("recursiveResize", "1") == "1" && !style.has("childLayout");
}

// mxGraph.resizeChildCells / scaleCell: the children follow a resized group.
void scaleChildren(Transaction& tx, std::size_t pageIndex, const std::string& parent, double fx, double fy)
{
    const Page& page = *tx.page(pageIndex);
    std::vector<std::string> children;
    for (const Cell* child : page.children(parent))
        children.push_back(child->id());
    for (const std::string& id : children) {
        const Cell& cell = *tx.page(pageIndex)->findCell(id);
        const bool recurse = cell.isVertex() && cell.geometry() != nullptr && !cell.geometry()->relative() &&
                             isRecursiveResize(*tx.page(pageIndex), cell);
        const bool fixed = cell.style().value("aspect", "") == "fixed";
        double newW = 0;
        double newH = 0;
        editCell(tx, pageIndex, id, [&](Cell& c) {
            Geometry* geo = c.geometry();
            if (geo == nullptr)
                return;
            if (const auto p = geo->sourcePoint())
                geo->setSourcePoint(Point{round2(p->x * fx), round2(p->y * fy)});
            if (const auto p = geo->targetPoint())
                geo->setTargetPoint(Point{round2(p->x * fx), round2(p->y * fy)});
            std::vector<Point> points = geo->points();
            if (!points.empty()) {
                for (Point& p : points)
                    p = {round2(p.x * fx), round2(p.y * fy)};
                geo->setPoints(points);
            }
            if (geo->relative() || c.isEdge())
                return;
            const double sw = fixed ? std::min(fx, fy) : fx;
            const double sh = fixed ? std::min(fx, fy) : fy;
            geo->setX(round2(geo->x() * fx));
            geo->setY(round2(geo->y() * fy));
            newW = round2(geo->width() * sw);
            newH = round2(geo->height() * sh);
            const double oldW = geo->width();
            const double oldH = geo->height();
            geo->setWidth(newW);
            geo->setHeight(newH);
            newW = oldW > 0 ? newW / oldW : 1;
            newH = oldH > 0 ? newH / oldH : 1;
        });
        if (recurse)
            scaleChildren(tx, pageIndex, id, newW, newH);
    }
}

// The cell and all its descendants.
void collectDescendants(const Page& page, const std::string& id, std::vector<std::string>& out)
{
    out.push_back(id);
    for (const std::string& child : page.childIds(id))
        collectDescendants(page, child, out);
}

// Graph.scaleCellStyles: fontSize, strokeWidth and the spacings times
// `factor`. The default values count (12, 1, 2, 0...), spacings are rounded
// to whole units and the font size of autosizeText cells is left alone.
Result scaleCellStyles(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& ids, double factor)
{
    if (!(factor > 0) || factor == 1)
        return Result::success(ids);
    struct Key {
        const char* name;
        double fallback;
    };
    static constexpr Key kKeys[] = {{"fontSize", 12}, {"strokeWidth", 1},   {"spacing", 2},       {"spacingTop", 0},
                                    {"spacingRight", 0}, {"spacingBottom", 0}, {"spacingLeft", 0}};
    std::unordered_set<std::string> seen;
    for (const std::string& id : ids) {
        const Cell* cell = tx.page(pageIndex)->findCell(id);
        if (cell == nullptr || !seen.insert(id).second)
            continue;
        const core::Style style = cell->style();
        std::vector<StyleEdit> edits;
        for (std::size_t j = 0; j < std::size(kKeys); ++j) {
            const double value = styleNumber(style, kKeys[j].name, kKeys[j].fallback);
            if (!std::isfinite(value) || value == 0 || (j == 0 && style.value("autosizeText", "") == "1"))
                continue;
            double scaled = j > 1 ? jsRound(value * factor) : round2(value * factor);
            if (j == 0)
                scaled = std::max(1.0, scaled);
            if (scaled != value)
                edits.push_back({kKeys[j].name, core::formatNumber(scaled)});
        }
        if (!edits.empty()) {
            if (const Result r = setStyles(tx, pageIndex, {id}, edits); !r.ok)
                return r;
        }
    }
    return Result::success(ids);
}

// Graph.isMultiSelectionRotatable.
bool isRotatable(const Page& page, const Cell& cell)
{
    const core::Style style = cell.style();
    return style.value("rotatable", "1") != "0" && !isTable(cell) && !isTableRow(page, cell) &&
           !isTableCell(page, cell) && !isTransparentBounds(style) && style.value("labelAutoRotate", "0") != "1";
}

double mod360(double value)
{
    const double m = std::fmod(value, 360.0);
    return m < 0 ? m + 360.0 : m;
}

// mxGeometry.rotate: the centre of a non-relative geometry, the edge points
// and the label offset turn around `center` (whole units).
void rotateGeometry(Geometry& geo, double angle, Point center)
{
    const double cos = std::cos(toRadians(angle));
    const double sin = std::sin(toRadians(angle));
    const auto turn = [&](Point p, Point around) {
        const double dx = p.x - around.x;
        const double dy = p.y - around.y;
        return Point{around.x + dx * cos - dy * sin, around.y + dx * sin + dy * cos};
    };
    if (!geo.relative()) {
        const Point c = turn({geo.x() + geo.width() / 2, geo.y() + geo.height() / 2}, center);
        geo.setX(jsRound(c.x - geo.width() / 2));
        geo.setY(jsRound(c.y - geo.height() / 2));
    }
    const auto turnRounded = [&](const Point& p) {
        const Point q = turn(p, center);
        return Point{jsRound(q.x), jsRound(q.y)};
    };
    if (const auto p = geo.sourcePoint())
        geo.setSourcePoint(turnRounded(*p));
    if (const auto p = geo.targetPoint())
        geo.setTargetPoint(turnRounded(*p));
    std::vector<Point> points = geo.points();
    if (!points.empty()) {
        for (Point& p : points)
            p = turnRounded(p);
        geo.setPoints(points);
    }
    if (const auto offset = geo.offset(); offset && !geo.relative()) {
        const Point q = turn(*offset, {0, 0});
        geo.setOffset(Point{jsRound(q.x), jsRound(q.y)});
    }
}

// Graph.rotateCell: turns a child of a rotated vertex as part of a rigid body.
void rotateChild(Transaction& tx, std::size_t pageIndex, const std::string& id, double angle,
                 const std::string& parent)
{
    const Cell* cell = tx.page(pageIndex)->findCell(id);
    if (cell == nullptr || !(cell->isVertex() || cell->isEdge()))
        return;
    const Cell* parentCell = tx.page(pageIndex)->findCell(parent);
    const Geometry* parentGeo = parentCell != nullptr ? parentCell->geometry() : nullptr;
    const bool isEdge = cell->isEdge();
    const bool isVertex = cell->isVertex();
    const bool relative = cell->geometry() != nullptr && cell->geometry()->relative();
    editCell(tx, pageIndex, id, [&](Cell& c) {
        if (!isEdge) {
            core::Style style = c.style();
            style.set("rotation", core::formatNumber(mod360(round2(rotationOf(style) + angle))));
            c.setStyle(style);
        }
        if (c.geometry() != nullptr && parentGeo != nullptr && !parentCell->isEdge())
            rotateGeometry(*c.geometry(), angle, {parentGeo->width() / 2, parentGeo->height() / 2});
    });
    if ((isVertex && !relative) || isEdge) {
        const std::vector<std::string> children = tx.page(pageIndex)->childIds(id);
        for (const std::string& child : children)
            rotateChild(tx, pageIndex, child, angle, id);
    }
}

// Graph.isRotatableGroup: a vertex with children that is no table part or swimlane.
bool isRotatableGroup(const Page& page, const Cell& cell)
{
    return cell.isVertex() && !page.children(cell.id()).empty() && !isTable(cell) && !isTableRow(page, cell) &&
           !isTableCell(page, cell) && !isSwimlane(page, cell);
}

// Graph.turnShapes: the next (or previous) direction of the vertex, starting
// from east as the default. The direction is stored whether or not the shape
// uses it, as upstream does.
std::string turnedDirection(const core::Style& style, bool backwards)
{
    static constexpr std::array<std::string_view, 4> kDirections{"east", "south", "west", "north"};
    const std::string current = style.value("direction", "east");
    const auto found = std::find(kDirections.begin(), kDirections.end(), current);
    const std::size_t index = found == kDirections.end() ? 0 : static_cast<std::size_t>(found - kDirections.begin());
    const std::size_t count = kDirections.size();
    return std::string(kDirections[backwards ? (index + count - 1) % count : (index + 1) % count]);
}

// Turns a vertex by 90 degrees about its centre: width and height swap and
// the top-left corner moves so that the centre stays put (turnShapes).
void turnVertexBox(Transaction& tx, std::size_t pageIndex, const std::string& id)
{
    editCell(tx, pageIndex, id, [&](Cell& c) {
        Geometry* geo = c.geometry();
        if (geo != nullptr) {
            const double w = geo->width();
            const double h = geo->height();
            geo->setX(geo->x() + w / 2 - h / 2);
            geo->setY(geo->y() + h / 2 - w / 2);
            geo->setWidth(h);
            geo->setHeight(w);
        }
    });
}

// The edge-end properties that swap when an edge is reversed (turnShapes):
// connection constraints (exit* <-> entry*) and the perimeter spacings.
constexpr std::array<std::array<std::string_view, 2>, 6> kReversedEdgeKeys{{
    {"exitX", "entryX"},
    {"exitY", "entryY"},
    {"exitDx", "entryDx"},
    {"exitDy", "entryDy"},
    {"exitPerimeter", "entryPerimeter"},
    {"sourcePerimeterSpacing", "targetPerimeterSpacing"},
}};

// Swaps source and target, reverses the waypoints, swaps the terminal points
// and the constraints of the edge.
void reverseEdge(Transaction& tx, std::size_t pageIndex, const std::string& id)
{
    editCell(tx, pageIndex, id, [](Cell& c) {
        const std::string source = c.source();
        c.setSource(c.target());
        c.setTarget(source);
        if (Geometry* geo = c.geometry()) {
            std::vector<Point> points = geo->points();
            if (!points.empty()) {
                std::reverse(points.begin(), points.end());
                geo->setPoints(points);
            }
            const auto sourcePoint = geo->sourcePoint();
            geo->setSourcePoint(geo->targetPoint());
            geo->setTargetPoint(sourcePoint);
        }
        core::Style style = c.style();
        bool changed = false;
        for (const auto& [first, second] : kReversedEdgeKeys) {
            const auto a = style.value(first);
            const auto b = style.value(second);
            if (a == b)
                continue;
            changed = true;
            if (b)
                style.set(first, *b);
            else
                style.remove(first);
            if (a)
                style.set(second, *a);
            else
                style.remove(second);
        }
        if (changed)
            c.setStyle(style);
    });
}

} // namespace

core::Point multiSelectionVertexScale(const core::Style& style, double sx, double sy)
{
    const double alpha = toRadians(rotationOf(style));
    const double cos = std::cos(alpha);
    const double sin = std::sin(alpha);
    double ex = std::sqrt(sx * sx * cos * cos + sy * sy * sin * sin);
    double ey = std::sqrt(sx * sx * sin * sin + sy * sy * cos * cos);
    if (style.value("aspect", "") == "fixed") {
        ex = std::min(ex, ey);
        ey = ex;
    }
    return {style.value("resizeWidth", "1") != "0" ? ex : 1, style.value("resizeHeight", "1") != "0" ? ey : 1};
}

Result scaleCellsToBounds(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& ids,
                          const Rect& from, const Rect& to, bool scaleStyles)
{
    const Page* page = tx.page(pageIndex);
    if (page == nullptr)
        return Result::failure("unknown page");
    for (const std::string& id : ids) {
        if (!isAddressable(*page, id))
            return unknownCell(id);
    }
    const double sx = from.width > 0 ? to.width / from.width : 1;
    const double sy = from.height > 0 ? to.height / from.height : 1;
    const PointMap map = [&](Point p) { return Point{to.x + (p.x - from.x) * sx, to.y + (p.y - from.y) * sy}; };

    std::vector<std::string> styled;
    for (const std::string& id : transformCells(*page, ids)) {
        page = tx.page(pageIndex);
        const Cell& cell = *page->findCell(id);
        if (cell.geometry() == nullptr)
            continue;
        const Point origin = commands::origin(*page, cell.parent());
        if (cell.isEdge()) {
            editCell(tx, pageIndex, id, [&](Cell& c) { mapEdgeGeometry(*c.geometry(), origin, map); });
            if (scaleStyles)
                collectDescendants(*page, id, styled);
        } else if (!cell.geometry()->relative()) {
            const core::Style style = cell.style();
            const bool resizable = style.value("resizable", "1") != "0";
            const Rect old = cell.geometry()->bounds();
            double w = old.width;
            double h = old.height;
            if (resizable) {
                const Point factor = multiSelectionVertexScale(style, sx, sy);
                w = round2(w * factor.x);
                h = round2(h * factor.y);
            }
            const bool recurse = resizable && isRecursiveResize(*page, cell);
            if (scaleStyles && resizable) {
                if (recurse)
                    collectDescendants(*page, id, styled);
                else
                    styled.push_back(id);
            }
            // resizeCells also grows the parents that no longer contain the cell.
            const Point center = map({origin.x + old.x + old.width / 2, origin.y + old.y + old.height / 2});
            const Rect bounds{round2(center.x - origin.x - w / 2), round2(center.y - origin.y - h / 2), w, h};
            if (const Result r = resizeCells(tx, pageIndex, {id}, {bounds}); !r.ok)
                return r;
            if (recurse && old.width > 0 && old.height > 0)
                scaleChildren(tx, pageIndex, id, w / old.width, h / old.height);
        }
    }
    if (scaleStyles) {
        if (const Result r = scaleCellStyles(tx, pageIndex, styled, std::min(sx, sy)); !r.ok)
            return r;
    }
    return Result::success(ids);
}

Result rotateCellsAround(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& ids, double angle,
                         const Point& center)
{
    const Page* page = tx.page(pageIndex);
    if (page == nullptr)
        return Result::failure("unknown page");
    for (const std::string& id : ids) {
        if (!isAddressable(*page, id))
            return unknownCell(id);
    }
    const double cos = std::cos(toRadians(angle));
    const double sin = std::sin(toRadians(angle));
    const PointMap map = [&](Point p) {
        const double dx = p.x - center.x;
        const double dy = p.y - center.y;
        return Point{center.x + dx * cos - dy * sin, center.y + dx * sin + dy * cos};
    };

    for (const std::string& id : transformCells(*page, ids)) {
        page = tx.page(pageIndex);
        const Cell& cell = *page->findCell(id);
        if (cell.geometry() == nullptr)
            continue;
        const Point origin = commands::origin(*page, cell.parent());
        if (cell.isEdge()) {
            editCell(tx, pageIndex, id, [&](Cell& c) { mapEdgeGeometry(*c.geometry(), origin, map); });
        } else if (!cell.geometry()->relative()) {
            const bool rotatable = isRotatable(*page, cell);
            editCell(tx, pageIndex, id, [&](Cell& c) {
                Geometry& geo = *c.geometry();
                placeVertex(geo, origin, geo.width(), geo.height(), map);
                if (rotatable) {
                    core::Style style = c.style();
                    style.set("rotation", core::formatNumber(mod360(round2(rotationOf(style) + angle))));
                    c.setStyle(style);
                }
            });
            if (rotatable) {
                const std::vector<std::string> children = tx.page(pageIndex)->childIds(id);
                for (const std::string& child : children)
                    rotateChild(tx, pageIndex, child, angle, id);
            }
        }
    }
    return Result::success(ids);
}

Result turnCells(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& ids, bool backwards)
{
    const Page* page = tx.page(pageIndex);
    if (page == nullptr)
        return Result::failure("unknown page");
    for (const std::string& id : ids) {
        if (!isAddressable(*page, id))
            return unknownCell(id);
    }
    for (const std::string& id : ids) {
        const Cell* cell = tx.page(pageIndex)->findCell(id);
        if (cell == nullptr || cell->style().value("resizable", "1") == "0")
            continue;
        if (cell->isEdge()) {
            reverseEdge(tx, pageIndex, id);
            continue;
        }
        if (!cell->isVertex())
            continue;
        if (isRotatableGroup(*tx.page(pageIndex), *cell)) {
            // Groups turn as a rigid body about their centre, like the rotation handle.
            const Geometry* geo = cell->geometry();
            if (geo == nullptr)
                continue;
            const Point origin = commands::origin(*tx.page(pageIndex), cell->parent());
            const Point center{origin.x + geo->x() + geo->width() / 2, origin.y + geo->y() + geo->height() / 2};
            if (Result r = rotateCellsAround(tx, pageIndex, {id}, backwards ? -90 : 90, center); !r)
                return r;
            continue;
        }
        if (cell->geometry() != nullptr && !isTable(*cell) && !isTableRow(*tx.page(pageIndex), *cell) &&
            !isTableCell(*tx.page(pageIndex), *cell) && !isSwimlane(*tx.page(pageIndex), *cell))
            turnVertexBox(tx, pageIndex, id);
        if (cell->geometry() != nullptr) {
            const std::string direction = turnedDirection(cell->style(), backwards);
            editCell(tx, pageIndex, id, [&](Cell& c) {
                core::Style style = c.style();
                style.set("direction", direction);
                c.setStyle(style);
            });
        }
    }
    return Result::success(ids);
}

} // namespace qraw::commands::ops
