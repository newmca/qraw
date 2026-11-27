#include "qraw/commands/queries.h"

#include "qraw/commands/primitives.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/number.h"
#include "qraw/core/style.h"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <unordered_map>
#include <unordered_set>

namespace qraw::commands {

namespace {

constexpr std::initializer_list<std::string_view> kPointOrder = {"x", "y", "as"};

const core::Cell* parentOf(const core::Page& page, const core::Cell& cell)
{
    return cell.parent().empty() ? nullptr : page.findCell(cell.parent());
}

// Calls `visit` for `cell` and each ancestor, nearest first. Stops at the
// root, at a dangling parent, after a cycle, or when visit returns false.
template <class Visit>
void walkUp(const core::Page& page, const core::Cell* cell, Visit&& visit)
{
    std::size_t steps = 0;
    const std::size_t limit = page.cellCount() + 1;
    while (cell && steps++ < limit) {
        if (!visit(*cell))
            return;
        cell = parentOf(page, *cell);
    }
}

// Lossless edit of one coordinate of an <mxPoint>, as core::Geometry does:
// draw.io omits zero coordinates, so a missing zero stays missing.
void setPointCoordinate(core::XmlNode& node, std::string_view name, double v)
{
    if (v == 0 && !node.attributes.has(name))
        return;
    node.attributes.setOrdered(name, core::formatNumber(v), kPointOrder);
}

void translatePointNode(core::XmlNode& node, double dx, double dy)
{
    const core::Point p = core::pointFromXml(node);
    if (dx != 0)
        setPointCoordinate(node, "x", p.x + dx);
    if (dy != 0)
        setPointCoordinate(node, "y", p.y + dy);
}

// JavaScript Math.round: halves round towards +infinity.
double jsRound(double v) { return std::floor(v + 0.5); }

} // namespace

bool isAncestor(const core::Page& page, std::string_view ancestor, std::string_view cell)
{
    if (ancestor.empty())
        return false;
    bool found = false;
    walkUp(page, page.findCell(cell), [&](const core::Cell& c) {
        found = c.id() == ancestor;
        return !found;
    });
    return found;
}

bool isWithin(const core::Page& page, const std::unordered_set<std::string>& ids, std::string_view cell)
{
    bool found = false;
    walkUp(page, page.findCell(cell), [&](const core::Cell& c) {
        found = ids.contains(c.id());
        return !found;
    });
    return found;
}

core::Point origin(const core::Page& page, std::string_view cellId)
{
    core::Point result;
    walkUp(page, page.findCell(cellId), [&](const core::Cell& c) {
        if (!c.isEdge()) {
            if (const core::Geometry* g = c.geometry()) {
                result.x += g->x();
                result.y += g->y();
            }
        }
        return true;
    });
    return result;
}

core::Point geometryOrigin(const core::Page& page, std::string_view cellId)
{
    core::Point result;
    const core::Cell* cell = page.findCell(cellId);
    if (!cell)
        return result;
    walkUp(page, parentOf(page, *cell), [&](const core::Cell& c) {
        const core::Geometry* g = c.geometry();
        if (g && !g->relative() && !c.isEdge()) {
            result.x += g->x();
            result.y += g->y();
        }
        return true;
    });
    return result;
}

void translateGeometry(core::Geometry& geometry, double dx, double dy, bool ignorePosition)
{
    if (!geometry.relative() && !ignorePosition) {
        if (dx != 0)
            geometry.setX(geometry.x() + dx);
        if (dy != 0)
            geometry.setY(geometry.y() + dy);
    }
    for (core::XmlNode& child : geometry.children()) {
        if (!child.isElement())
            continue;
        const std::string& role = child.attributes.value("as");
        if (child.name == "mxPoint" && (role == "sourcePoint" || role == "targetPoint")) {
            translatePointNode(child, dx, dy);
        } else if (child.name == "Array" && role == "points") {
            for (core::XmlNode& point : child.children) {
                if (point.isElement("mxPoint"))
                    translatePointNode(point, dx, dy);
            }
        }
    }
}

core::Cell translatedCell(const core::Page& page, const core::Cell& cell, double dx, double dy)
{
    core::Cell out = cell;
    core::Geometry* geo = out.geometry();
    if (!geo)
        return out;
    if (!geo->relative() || cell.isEdge()) {
        translateGeometry(*geo, dx, dy, cell.isEdge());
        return out;
    }
    double angle = 0;
    if (const core::Cell* parent = parentOf(page, cell); parent && parent->isVertex())
        angle = core::parseNumber(parent->style().value("rotation", "0"), 0);
    if (angle != 0) {
        const double rad = -angle * std::numbers::pi / 180.0;
        const double c = std::cos(rad);
        const double s = std::sin(rad);
        const double rx = dx * c - dy * s;
        const double ry = dx * s + dy * c;
        dx = rx;
        dy = ry;
    }
    const core::Point offset = geo->offset().value_or(core::Point{});
    const bool hadOffset = geo->offset().has_value();
    const core::Point moved = hadOffset ? core::Point{jsRound(offset.x + dx), jsRound(offset.y + dy)}
                                        : core::Point{jsRound(dx), jsRound(dy)};
    geo->setOffset(moved);
    return out;
}

std::vector<std::string> topmostCells(const core::Page& page, const std::vector<std::string>& ids)
{
    const std::unordered_set<std::string> set(ids.begin(), ids.end());
    std::unordered_set<std::string> emitted;
    std::vector<std::string> out;
    for (const auto& id : ids) {
        const core::Cell* cell = page.findCell(id);
        if (!cell || emitted.contains(id))
            continue;
        bool nested = false;
        walkUp(page, parentOf(page, *cell), [&](const core::Cell& c) {
            nested = set.contains(c.id());
            return !nested;
        });
        if (!nested) {
            emitted.insert(id);
            out.push_back(id);
        }
    }
    return out;
}

std::vector<std::string> sortByZOrder(const core::Page& page, const std::vector<std::string>& ids)
{
    std::vector<std::pair<std::size_t, std::string>> located;
    std::unordered_set<std::string> seen;
    for (const auto& id : ids) {
        if (!seen.insert(id).second)
            continue;
        if (const auto index = page.entryIndex(id))
            located.emplace_back(*index, id);
    }
    std::sort(located.begin(), located.end());
    std::vector<std::string> out;
    out.reserve(located.size());
    for (auto& [index, id] : located)
        out.push_back(std::move(id));
    return out;
}

std::vector<std::string> connectedEdges(const core::Page& page, std::string_view cellId)
{
    std::vector<std::string> out;
    if (cellId.empty())
        return out;
    for (const core::Cell* edge : page.connectedEdges(cellId))
        out.push_back(edge->id());
    return out;
}

std::vector<std::string> allEdges(const core::Page& page, const std::vector<std::string>& ids)
{
    // Edges incident to a terminal come from the page's source/target
    // indexes (never a scan of the whole page), then go back to z-order.
    std::vector<std::pair<std::size_t, std::string>> found;
    std::unordered_set<std::string> seen;
    const auto collect = [&](const core::Cell& terminal) {
        for (const core::Cell* edge : page.connectedEdges(terminal.id())) {
            if (seen.insert(edge->id()).second)
                found.emplace_back(*page.entryIndex(edge->id()), edge->id());
        }
    };
    for (const auto& id : ids) {
        const core::Cell* cell = page.findCell(id);
        if (!cell)
            continue;
        collect(*cell);
        for (const core::Cell* d : page.descendants(id))
            collect(*d);
    }
    std::sort(found.begin(), found.end());
    std::vector<std::string> out;
    out.reserve(found.size());
    for (auto& [entry, id] : found)
        out.push_back(std::move(id));
    return out;
}

std::string nearestCommonAncestor(const core::Page& page, std::string_view a, std::string_view b)
{
    std::unordered_set<std::string> chain;
    walkUp(page, page.findCell(a), [&](const core::Cell& c) {
        chain.insert(c.id());
        return true;
    });
    std::string result;
    walkUp(page, page.findCell(b), [&](const core::Cell& c) {
        if (chain.contains(c.id())) {
            result = c.id();
            return false;
        }
        return true;
    });
    return result;
}

std::string layerOf(const core::Page& page, std::string_view cellId)
{
    std::string result;
    walkUp(page, page.findCell(cellId), [&](const core::Cell& c) {
        const core::Cell* parent = parentOf(page, c);
        if (parent && parent->parent().empty()) {
            result = c.id();
            return false;
        }
        return true;
    });
    return result;
}

bool isTransparentCell(const core::Cell& cell)
{
    const core::Style style = cell.style();
    if (style.has("image") || style.value("shape", "") == "image")
        return false;
    const auto names = style.names();
    const bool transparentDefault = !names.empty() && (names.front() == "group" || names.front() == "text");
    const auto effective = [&](std::string_view key) {
        if (const auto v = style.value(key))
            return *v == "none";
        return transparentDefault;
    };
    return effective("strokeColor") && effective("fillColor");
}

bool isSwimlane(const core::Page& page, const core::Cell& cell)
{
    const core::Cell* root = page.rootCell();
    if (cell.isEdge() || cell.parent().empty() || (root && cell.parent() == root->id()))
        return false;
    const core::Style style = cell.style();
    if (const auto shape = style.value("shape"))
        return *shape == "swimlane";
    const std::vector<std::string> names = style.names();
    return !names.empty() && names.front() == "swimlane";
}

bool isContainer(const core::Page& page, const core::Cell& cell)
{
    const core::Style style = cell.style();
    if (isSwimlane(page, cell))
        return style.value("container", "") != "0";
    return style.value("container", "") == "1";
}

double swimlaneStartSize(const core::Page& page, const core::Cell& cell)
{
    if (!isSwimlane(page, cell))
        return 0;
    const core::Style style = cell.style();
    if (const auto value = style.value("startSize"))
        return core::parseNumber(*value, 40);
    const std::vector<std::string> names = style.names();
    return !names.empty() && names.front() == "swimlane" ? 23 : 40;
}

bool isAddressable(const core::Page& page, std::string_view id) { return !id.empty() && page.findCell(id); }

std::optional<EntryRange> subtreeEntryRange(const core::Page& page, std::string_view cellId)
{
    const auto self = page.entryIndex(cellId);
    if (!self)
        return std::nullopt;
    EntryRange range{*self, *self};
    for (const core::Cell* d : page.descendants(cellId)) {
        if (const auto index = page.entryIndex(d->id())) {
            range.first = std::min(range.first, *index);
            range.last = std::max(range.last, *index);
        }
    }
    return range;
}

bool removeSubtree(Transaction& tx, std::size_t page, std::string_view cellId)
{
    const core::Page* p = tx.page(page);
    if (!p || !isAddressable(*p, cellId))
        return false;
    std::vector<std::string> order{std::string(cellId)};
    for (const core::Cell* d : p->descendants(cellId))
        order.push_back(d->id());
    const std::size_t savepoint = tx.savepoint();
    // Pre-order reversed: children go before their parents.
    for (auto it = order.rbegin(); it != order.rend(); ++it) {
        if (!tx.emplace<RemoveCell>(page, *it)) {
            tx.rollbackTo(savepoint);
            return false;
        }
    }
    return true;
}

bool moveSubtree(Transaction& tx, std::size_t page, std::string_view cellId, std::size_t beforeEntryIndex)
{
    const core::Page* p = tx.page(page);
    if (!p || !isAddressable(*p, cellId))
        return false;
    std::vector<std::string> ids{std::string(cellId)};
    for (const core::Cell* d : p->descendants(cellId))
        ids.push_back(d->id());
    return tx.emplace<ReorderCells>(page, std::move(ids), beforeEntryIndex);
}

} // namespace qraw::commands
