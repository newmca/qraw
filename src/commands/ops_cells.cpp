// Cell operations (ops_cells.h): add, move, resize, connect, waypoints.
// removeCells/removeLayers live in ops_remove.cpp and share detachTerminal().
#include "qraw/commands/ops_cells.h"

#include "qraw/commands/ops_layout.h"
#include "qraw/commands/primitives.h"
#include "qraw/commands/queries.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/number.h"
#include "qraw/core/page.h"
#include "qraw/core/style.h"

#include <algorithm>
#include <string>
#include <unordered_map>
#include <unordered_set>
#include <utility>

namespace qraw::commands {

namespace {

using core::Cell;
using core::Geometry;
using core::Page;
using core::Point;

Result unknownCell(const std::string& id) { return Result::failure("unknown cell '" + id + "'"); }

// A geometry that defines a coordinate system with absolute x/y: present,
// not relative, not an edge.
bool hasPlainGeometry(const Cell& cell)
{
    const Geometry* g = cell.geometry();
    return g && !g->relative() && !cell.isEdge();
}

Point centerInPageSpace(const Page& page, const Cell& cell)
{
    const Point origin = geometryOrigin(page, cell.id());
    const Geometry& g = *cell.geometry();
    return {origin.x + g.x() + g.width() / 2, origin.y + g.y() + g.height() / 2};
}

// The stored geometry of an edge or label child; a missing one is created
// relative, as draw.io does for edges.
Geometry& ensureRelativeGeometry(Cell& cell)
{
    if (!cell.geometry()) {
        Geometry created = Geometry::create();
        created.setRelative(true);
        cell.setGeometry(std::move(created));
    }
    return *cell.geometry();
}

// True when `id` is, or lies inside, one of the `selection` cells.
bool isSelected(const Page& page, const std::unordered_set<std::string>& selection, const std::string& id)
{
    return !id.empty() && isWithin(page, selection, id);
}

// Entry ranges (queries.h subtreeEntryRange) of the parents that receive the
// cells of one addCells batch. Walking the subtree of a layer with thousands
// of cells for every inserted cell would be quadratic, so each range is
// computed once and then updated as cells are inserted.
class ParentRanges {
public:
    // The range of `parent`'s subtree on the current page; nullopt when unknown.
    std::optional<EntryRange> of(const Page& page, const std::string& parent)
    {
        if (const auto it = ranges_.find(parent); it != ranges_.end())
            return it->second;
        const auto range = subtreeEntryRange(page, parent);
        if (range)
            ranges_.emplace(parent, *range);
        return range;
    }

    // The cell with `parent` was inserted before the entry `entry`; `page`
    // already contains it.
    void inserted(const Page& page, std::size_t entry, const std::string& parent)
    {
        std::unordered_set<std::string> ancestors; // the new cell is a descendant of these
        for (const Cell* a = page.findCell(parent); a && ancestors.insert(a->id()).second;
             a = a->parent().empty() ? nullptr : page.findCell(a->parent())) {
        }
        for (auto& [id, range] : ranges_) {
            const std::size_t first = range.first + (range.first >= entry ? 1 : 0);
            const std::size_t last = range.last + (range.last >= entry ? 1 : 0);
            const bool within = ancestors.contains(id);
            range.first = within ? std::min(first, entry) : first;
            range.last = within ? std::max(last, entry) : last;
        }
    }

private:
    std::unordered_map<std::string, EntryRange> ranges_;
};

} // namespace

// Shared with ops_remove.cpp (declared there). See ops_cells.h, removeCells step 3.
namespace detail {

void detachTerminal(const Page& page, Cell& edge, bool source)
{
    const std::string terminalId = source ? edge.source() : edge.target();
    if (terminalId.empty())
        return;
    if (Geometry* egeo = edge.geometry()) {
        const Point edgeOrigin = geometryOrigin(page, edge.id());
        std::optional<Point> pt;
        // a) the terminal itself, b) its nearest ancestor with a plain geometry.
        const Cell* candidate = page.findCell(terminalId);
        for (std::size_t steps = 0; candidate && steps <= page.cellCount(); ++steps) {
            if (hasPlainGeometry(*candidate)) {
                const Point c = centerInPageSpace(page, *candidate);
                pt = Point{c.x - edgeOrigin.x, c.y - edgeOrigin.y};
                break;
            }
            candidate = candidate->parent().empty() ? nullptr : page.findCell(candidate->parent());
        }
        // c) nothing usable: a short stub next to the edge's own position.
        if (!pt)
            pt = Point{egeo->x() + (source ? -60.0 : 60.0), egeo->y()};
        if (source)
            egeo->setSourcePoint(pt);
        else
            egeo->setTargetPoint(pt);
    }
    if (source)
        edge.setSource("");
    else
        edge.setTarget("");
}

} // namespace detail

namespace ops {

// --- add --------------------------------------------------------------------

Result addCells(Transaction& tx, std::size_t page, std::vector<Cell> cells, std::optional<std::size_t> index)
{
    const Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");

    // Validate the whole batch before emitting any command.
    std::unordered_set<std::string> batch;
    std::vector<std::string> ids;
    for (Cell& cell : cells) {
        if (cell.id().empty())
            cell.setId(tx.newCellId(page));
        const std::string& id = cell.id();
        if (id.empty() || p->findCell(id) || batch.contains(id))
            return Result::failure("cell id '" + id + "' is already in use");
        auto exists = [&](const std::string& ref) { return p->findCell(ref) || batch.contains(ref); };
        if (cell.parent().empty() || !exists(cell.parent()))
            return Result::failure("unknown parent '" + cell.parent() + "' of cell '" + id + "'");
        if (!cell.source().empty() && !exists(cell.source()))
            return Result::failure("unknown source '" + cell.source() + "' of cell '" + id + "'");
        if (!cell.target().empty() && !exists(cell.target()))
            return Result::failure("unknown target '" + cell.target() + "' of cell '" + id + "'");
        batch.insert(id);
        ids.push_back(id);
    }

    std::unordered_set<std::string> inserted;
    ParentRanges ranges;
    std::size_t topLevel = 0;
    for (Cell& cell : cells) {
        p = tx.page(page);
        const std::string parent = cell.parent();
        const auto parentRange = ranges.of(*p, parent);
        if (!parentRange)
            return unknownCell(parent);
        std::size_t entry = parentRange->last + 1;
        if (!inserted.contains(parent)) {
            if (index) {
                // Child number index+k, counting the batch cells already placed.
                const auto& siblings = p->childIds(parent);
                if (*index + topLevel < siblings.size()) {
                    if (const auto range = subtreeEntryRange(*p, siblings[*index + topLevel]))
                        entry = range->first;
                }
            }
            ++topLevel;
        }
        const std::string id = cell.id();
        if (!tx.emplace<InsertCell>(page, entry, std::move(cell)))
            return Result::failure("could not insert cell '" + id + "'");
        ranges.inserted(*tx.page(page), entry, parent);
        inserted.insert(id);
    }
    return Result::success(std::move(ids));
}

// --- move / resize ----------------------------------------------------------

Result moveCells(Transaction& tx, std::size_t page, const std::vector<std::string>& ids, double dx, double dy,
                 const MoveOptions& options)
{
    const Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");
    for (const auto& id : ids) {
        if (!isAddressable(*p, id))
            return unknownCell(id);
    }

    const std::vector<std::string> selection = topmostCells(*p, ids);
    const std::unordered_set<std::string> selectionSet(selection.begin(), selection.end());
    std::vector<std::string> cells = selection;
    // Edge labels follow a moved terminal implicitly.
    std::erase_if(cells, [&](const std::string& id) {
        const Cell* cell = p->findCell(id);
        const Geometry* geo = cell->geometry();
        const Cell* parent = cell->parent().empty() ? nullptr : p->findCell(cell->parent());
        return geo && geo->relative() && parent && parent->isEdge() &&
               (isSelected(*p, selectionSet, parent->source()) || isSelected(*p, selectionSet, parent->target()));
    });
    if (dx == 0 && dy == 0)
        return Result::success(std::move(cells));

    const std::unordered_set<std::string> movedSet(cells.begin(), cells.end());
    for (const auto& id : cells) {
        p = tx.page(page);
        const Cell& original = *p->findCell(id);
        Cell moved = original;
        if (options.disconnect && moved.isEdge()) {
            if (!moved.source().empty() && !isSelected(*p, movedSet, moved.source()))
                detail::detachTerminal(*p, moved, true);
            if (!moved.target().empty() && !isSelected(*p, movedSet, moved.target()))
                detail::detachTerminal(*p, moved, false);
        }
        moved = translatedCell(*p, moved, dx, dy);
        if (moved != original && !tx.emplace<ReplaceCell>(page, std::move(moved)))
            return Result::failure("could not move cell '" + id + "'");
    }
    return Result::success(std::move(cells));
}

namespace {

// mxGraph.extendParent, simplified: grows the parent chain of `id` until it
// contains the (non-relative) child.
bool extendParentChain(Transaction& tx, std::size_t page, std::string id)
{
    for (std::size_t steps = 0;; ++steps) {
        const Page* p = tx.page(page);
        if (steps > p->cellCount())
            return true; // parent cycle
        const Cell* child = p->findCell(id);
        const Geometry* cg = child ? child->geometry() : nullptr;
        const Cell* parent = child && !child->parent().empty() ? p->findCell(child->parent()) : nullptr;
        if (!cg || cg->relative() || !parent || !parent->geometry() || parent->isCollapsed())
            return true;
        const double needW = cg->x() + cg->width();
        const double needH = cg->y() + cg->height();
        Cell grown = *parent;
        Geometry& pg = *grown.geometry();
        if (pg.width() < needW)
            pg.setWidth(needW);
        if (pg.height() < needH)
            pg.setHeight(needH);
        if (grown == *parent)
            return true;
        id = grown.id();
        if (!tx.emplace<ReplaceCell>(page, std::move(grown)))
            return false;
    }
}

} // namespace

Result extendParents(Transaction& tx, std::size_t page, const std::vector<std::string>& ids)
{
    const Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");
    for (const auto& id : ids) {
        if (!isAddressable(*p, id))
            return unknownCell(id);
    }
    for (const auto& id : ids) {
        p = tx.page(page);
        const Cell& cell = *p->findCell(id);
        const Cell* parent = cell.parent().empty() ? nullptr : p->findCell(cell.parent());
        // Graph.isExtendParentsOnAdd (Graph.js:36254): stack layouts size their parent themselves.
        if (cell.isEdge() || (parent && hasChildLayout(*parent)))
            continue;
        if (!extendParentChain(tx, page, id))
            return Result::failure("could not extend the parent of '" + id + "'");
    }
    return Result::success(ids);
}

Result resizeCells(Transaction& tx, std::size_t page, const std::vector<std::string>& ids,
                   const std::vector<core::Rect>& bounds)
{
    const Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");
    if (ids.size() != bounds.size())
        return Result::failure("ids and bounds differ in size");
    for (const auto& id : ids) {
        if (!isAddressable(*p, id))
            return unknownCell(id);
    }

    for (std::size_t i = 0; i < ids.size(); ++i) {
        p = tx.page(page);
        const Cell& original = *p->findCell(ids[i]);
        if (!original.geometry())
            continue;
        Cell resized = original;
        Geometry& geo = *resized.geometry();
        const core::Rect& b = bounds[i];
        const double dx = b.x - geo.x();
        const double dy = b.y - geo.y();
        if (geo.relative()) {
            if (const auto offset = geo.offset(); offset && (dx != 0 || dy != 0))
                geo.setOffset(Point{offset->x + dx, offset->y + dy});
        } else {
            if (dx != 0)
                geo.setX(b.x);
            if (dy != 0)
                geo.setY(b.y);
        }
        if (b.width != geo.width())
            geo.setWidth(b.width);
        if (b.height != geo.height())
            geo.setHeight(b.height);
        if (resized != original && !tx.emplace<ReplaceCell>(page, std::move(resized)))
            return Result::failure("could not resize cell '" + ids[i] + "'");
        if (!extendParentChain(tx, page, ids[i]))
            return Result::failure("could not extend the parent of '" + ids[i] + "'");
    }
    return Result::success(ids);
}

// --- connections ------------------------------------------------------------

Result connectEdge(Transaction& tx, std::size_t page, const std::string& edge, const std::string& terminal,
                   bool source, std::optional<ConnectionConstraint> constraint)
{
    const Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");
    if (!isAddressable(*p, edge))
        return unknownCell(edge);
    if (!isAddressable(*p, terminal))
        return unknownCell(terminal);
    const Cell& original = *p->findCell(edge);
    const Cell& terminalCell = *p->findCell(terminal);
    if (!original.isEdge())
        return Result::failure("'" + edge + "' is not an edge");
    if (terminal == edge || !(terminalCell.isVertex() || terminalCell.isEdge()))
        return Result::failure("'" + terminal + "' cannot be an edge terminal");

    Cell connected = original;
    if (constraint) {
        const std::string prefix = source ? "exit" : "entry";
        core::Style style = connected.style();
        if (constraint->point) {
            style.set(prefix + "X", core::formatNumber(constraint->point->x));
            style.set(prefix + "Y", core::formatNumber(constraint->point->y));
            style.set(prefix + "Dx", core::formatNumber(constraint->dx));
            style.set(prefix + "Dy", core::formatNumber(constraint->dy));
            if (constraint->perimeter)
                style.remove(prefix + "Perimeter");
            else
                style.set(prefix + "Perimeter", "0");
        } else {
            for (const char* key : {"X", "Y", "Dx", "Dy", "Perimeter"})
                style.remove(prefix + key);
        }
        if (style != connected.style())
            connected.setStyle(style);
    }
    if (source)
        connected.setSource(terminal);
    else
        connected.setTarget(terminal);
    if (connected != original && !tx.emplace<ReplaceCell>(page, std::move(connected)))
        return Result::failure("could not connect edge '" + edge + "'");
    return Result::success();
}

Result disconnectEdge(Transaction& tx, std::size_t page, const std::string& edge, bool source,
                      const core::Point& point)
{
    const Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");
    if (!isAddressable(*p, edge))
        return unknownCell(edge);
    const Cell& original = *p->findCell(edge);
    if (!original.isEdge())
        return Result::failure("'" + edge + "' is not an edge");

    Cell detached = original;
    if (source) {
        detached.setSource("");
        ensureRelativeGeometry(detached).setSourcePoint(point);
    } else {
        detached.setTarget("");
        ensureRelativeGeometry(detached).setTargetPoint(point);
    }
    if (detached != original && !tx.emplace<ReplaceCell>(page, std::move(detached)))
        return Result::failure("could not disconnect edge '" + edge + "'");
    return Result::success();
}

Result setWaypoints(Transaction& tx, std::size_t page, const std::string& edge,
                    const std::vector<core::Point>& points)
{
    const Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");
    if (!isAddressable(*p, edge))
        return unknownCell(edge);
    const Cell& original = *p->findCell(edge);
    if (!original.isEdge())
        return Result::failure("'" + edge + "' is not an edge");

    const std::vector<Point> current = original.geometry() ? original.geometry()->points() : std::vector<Point>{};
    if (current == points)
        return Result::success();
    Cell changed = original;
    ensureRelativeGeometry(changed).setPoints(points);
    if (!tx.emplace<ReplaceCell>(page, std::move(changed)))
        return Result::failure("could not set waypoints of '" + edge + "'");
    return Result::success();
}

} // namespace ops
} // namespace qraw::commands
