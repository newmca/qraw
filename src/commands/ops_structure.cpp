#include "qraw/commands/ops_structure.h"

#include "qraw/commands/ops_cells.h"

#include "qraw/commands/primitives.h"
#include "qraw/commands/queries.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/cell.h"
#include "qraw/core/number.h"
#include "qraw/core/page.h"
#include "qraw/core/style.h"

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace qraw::commands::ops {

namespace {

using core::Cell;
using core::Page;
using core::Point;
using core::Rect;

// --- small page helpers ------------------------------------------------------

bool allAddressable(const Page& page, const std::vector<std::string>& ids)
{
    return std::ranges::all_of(ids, [&](const std::string& id) { return isAddressable(page, id); });
}

std::string rootCellId(const Page& page)
{
    const Cell* root = page.rootCell();
    return root ? root->id() : std::string();
}

std::size_t childCount(const Page& page, std::string_view parent) { return page.childCount(parent); }

std::size_t childIndex(const Page& page, const Cell& cell)
{
    return page.childPosition(cell.id()).value_or(page.childCount(cell.parent()));
}

// Entry index in front of which a block must be put to become child number
// `k` of `parent`, the cell `excluding` not counted (see the top comment of
// ops_structure.h). nullopt when `parent` is unknown and has no children.
std::optional<std::size_t> beforeEntryForChildIndex(const Page& page, std::string_view parent, std::size_t k,
                                                    std::string_view excluding)
{
    const std::vector<std::string>& all = page.childIds(parent);
    std::optional<std::size_t> skipped; // position of `excluding` among the children
    if (const Cell* excluded = excluding.empty() ? nullptr : page.findCell(excluding);
        excluded && excluded->parent() == parent)
        skipped = page.childPosition(excluding);
    const std::size_t count = all.size() - (skipped ? 1 : 0);
    const auto sibling = [&](std::size_t i) -> const std::string& { return all[skipped && i >= *skipped ? i + 1 : i]; };

    if (k < count) {
        const auto range = subtreeEntryRange(page, sibling(k));
        return range ? std::optional<std::size_t>(range->first) : std::nullopt;
    }
    const auto range = subtreeEntryRange(page, count == 0 ? std::string(parent) : sibling(count - 1));
    return range ? std::optional<std::size_t>(range->last + 1) : std::nullopt;
}

// Moves the block of `cellId` before `anchor` unless it already sits there
// (contiguous block and the anchor on its boundary or inside): no command.
bool placeBefore(Transaction& tx, std::size_t pageIndex, std::string_view cellId, std::size_t anchor)
{
    const Page* page = tx.page(pageIndex);
    if (!page)
        return false;
    const auto range = subtreeEntryRange(*page, cellId);
    if (!range)
        return false;
    const bool contiguous = range->last - range->first == page->descendants(cellId).size();
    if (contiguous && anchor >= range->first && anchor <= range->last + 1)
        return true;
    return moveSubtree(tx, pageIndex, cellId, anchor);
}

// Replaces the stored cell by `cell` when they differ.
bool replaceIfChanged(Transaction& tx, std::size_t pageIndex, Cell cell)
{
    const Page* page = tx.page(pageIndex);
    const Cell* current = page ? page->findCell(cell.id()) : nullptr;
    if (!current)
        return false;
    if (*current == cell)
        return true;
    return tx.emplace<ReplaceCell>(pageIndex, std::move(cell));
}

// Stores `cell` (the edited copy of an existing cell) with parent
// `newParent` and puts it, with its subtree, at child number `k` of that
// parent. Emits nothing for what is already as wanted.
bool setParentAndPlace(Transaction& tx, std::size_t pageIndex, Cell cell, std::string_view newParent, std::size_t k)
{
    const std::string id = cell.id();
    cell.setParent(newParent);
    if (!replaceIfChanged(tx, pageIndex, std::move(cell)))
        return false;
    const Page* page = tx.page(pageIndex);
    const auto anchor = page ? beforeEntryForChildIndex(*page, newParent, k, id) : std::nullopt;
    return anchor && placeBefore(tx, pageIndex, id, *anchor);
}

// The cell as a copy, or nullopt when it is gone.
std::optional<Cell> copyOf(Transaction& tx, std::size_t pageIndex, std::string_view id)
{
    const Page* page = tx.page(pageIndex);
    const Cell* cell = page ? page->findCell(id) : nullptr;
    return cell ? std::optional<Cell>(*cell) : std::nullopt;
}

// Sets style key `key` on the cell, no command when it already has that value.
bool setStyleKey(Transaction& tx, std::size_t pageIndex, std::string_view id, std::string_view key,
                 std::string_view value)
{
    auto cell = copyOf(tx, pageIndex, id);
    if (!cell)
        return false;
    core::Style style = cell->style();
    if (style.value(key) == value)
        return true;
    style.set(key, value);
    cell->setStyle(style);
    return replaceIfChanged(tx, pageIndex, std::move(*cell));
}

// Terminal for updateEdgeParent: climbs from a relative-geometry child to its
// parent. Empty when the terminal is missing.
std::string anchoredTerminal(const Page& page, const std::string& terminalId)
{
    const Cell* t = terminalId.empty() ? nullptr : page.findCell(terminalId);
    std::size_t guard = page.cellCount() + 1;
    while (t && guard-- > 0 && !t->isEdge() && t->geometry() && t->geometry()->relative())
        t = t->parent().empty() ? nullptr : page.findCell(t->parent());
    return t ? t->id() : std::string();
}

// Subtree of `id` in post-order (children before their parent).
void collectPostOrder(const Page& page, const std::string& id, std::vector<std::string>& out)
{
    for (const Cell* child : page.children(id))
        collectPostOrder(page, child->id(), out);
    out.push_back(id);
}

// The parent `edgeId` has to move to (upstream's updateEdgeParents): the
// nearest common ancestor of its terminals, when that differs from its parent.
std::optional<std::string> edgeParentTarget(const Page& page, const std::string& edgeId)
{
    const Cell* edge = page.findCell(edgeId);
    if (!edge)
        return std::nullopt;
    const std::string s = anchoredTerminal(page, edge->source());
    const std::string t = anchoredTerminal(page, edge->target());
    if (s.empty() || t.empty())
        return std::nullopt;
    std::string nca;
    if (s == t)
        nca = page.findCell(s)->parent();
    else
        nca = nearestCommonAncestor(page, s, t);
    const Cell* ncaCell = nca.empty() ? nullptr : page.findCell(nca);
    // Not upstream: the root cell can be the common ancestor of terminals in
    // different layers; an edge directly under the root would corrupt the
    // layer structure, so it stays where it is.
    if (!ncaCell || ncaCell->parent().empty())
        return std::nullopt;
    const bool eligible = ncaCell->parent() != rootCellId(page) || isAncestor(page, nca, edgeId);
    if (!eligible || edge->parent() == nca)
        return std::nullopt;
    return nca;
}

// `edge` as a child of `parent`, its geometry kept in place.
Cell reparentedEdge(const Page& page, const Cell& edge, const std::string& parent)
{
    Cell moved = edge;
    if (core::Geometry* geo = moved.geometry()) {
        const Point d = {origin(page, parent).x - origin(page, edge.parent()).x,
                         origin(page, parent).y - origin(page, edge.parent()).y};
        translateGeometry(*geo, -d.x, -d.y, false);
    }
    moved.setParent(parent);
    return moved;
}

// One edge of updateEdgeParents; false only when a primitive failed.
bool updateEdgeParent(Transaction& tx, std::size_t pageIndex, const std::string& edgeId)
{
    const Page* page = tx.page(pageIndex);
    const std::optional<std::string> nca = page ? edgeParentTarget(*page, edgeId) : std::nullopt;
    if (!nca)
        return true;
    return setParentAndPlace(tx, pageIndex, reparentedEdge(*page, *page->findCell(edgeId), *nca), *nca,
                             childCount(*page, *nca));
}

// Grows a rectangle to cover points and rects.
class BoundsAccumulator {
public:
    void add(double x, double y, double w = 0, double h = 0)
    {
        if (!rect_) {
            rect_ = Rect{x, y, w, h};
            return;
        }
        const double right = std::max(rect_->x + rect_->width, x + w);
        const double bottom = std::max(rect_->y + rect_->height, y + h);
        rect_->x = std::min(rect_->x, x);
        rect_->y = std::min(rect_->y, y);
        rect_->width = right - rect_->x;
        rect_->height = bottom - rect_->y;
    }
    [[nodiscard]] const std::optional<Rect>& result() const { return rect_; }

private:
    std::optional<Rect> rect_;
};

// mxGraph.getBoundingBoxFromGeometry (mxGraph.js:8069) without rotation.
std::optional<Rect> geometryBounds(const Page& page, const std::vector<std::string>& ids)
{
    BoundsAccumulator acc;
    for (const std::string& id : ids) {
        const Cell* cell = page.findCell(id);
        const core::Geometry* geo = cell ? cell->geometry() : nullptr;
        if (!geo)
            continue;
        if (cell->isEdge()) {
            if (cell->source().empty() && geo->sourcePoint())
                acc.add(geo->sourcePoint()->x, geo->sourcePoint()->y);
            if (cell->target().empty() && geo->targetPoint())
                acc.add(geo->targetPoint()->x, geo->targetPoint()->y);
            for (const Point& p : geo->points())
                acc.add(p.x, p.y);
        } else if (cell->isVertex() && !geo->relative()) {
            acc.add(geo->x(), geo->y(), geo->width(), geo->height());
        }
    }
    return acc.result();
}

// Relative edge labels whose parent edge has a selected terminal move
// implicitly with it (step 1 of mxGraph.moveCells).
std::vector<std::string> withoutImplicitlyMovedLabels(const Page& page, const std::vector<std::string>& cells)
{
    const std::unordered_set<std::string> selected(cells.begin(), cells.end());
    const auto isSelected = [&](const std::string& terminalId) {
        return !terminalId.empty() && isWithin(page, selected, terminalId);
    };
    std::vector<std::string> out;
    for (const std::string& id : cells) {
        const Cell* cell = page.findCell(id);
        const Cell* parent = cell ? page.findCell(cell->parent()) : nullptr;
        const bool relative = cell && cell->geometry() && cell->geometry()->relative();
        if (!relative || !parent || !parent->isEdge()
            || (!isSelected(parent->source()) && !isSelected(parent->target())))
            out.push_back(id);
    }
    return out;
}

} // namespace

// --- z-order -------------------------------------------------------------------

Result orderCells(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& ids, ZOrder order)
{
    const Page* page = tx.page(pageIndex);
    if (!page)
        return Result::failure("no such page");
    if (!allAddressable(*page, ids))
        return Result::failure("unknown cell");
    const std::vector<std::string> cells = sortByZOrder(*page, ids);

    for (std::size_t i = 0; i < cells.size(); ++i) {
        page = tx.page(pageIndex);
        const Cell* cell = page->findCell(cells[i]);
        if (!cell || cell->parent().empty())
            continue;
        const std::string parent = cell->parent();
        const std::size_t n = childCount(*page, parent);
        const std::size_t k = childIndex(*page, *cell);
        std::size_t target = 0;
        switch (order) {
        case ZOrder::ToBack:
            target = std::min(i, n - 1);
            break;
        case ZOrder::SendBackward:
            target = k == 0 ? 0 : k - 1;
            break;
        case ZOrder::ToFront:
            target = n - 1;
            break;
        case ZOrder::BringForward:
            target = std::min(n - 1, k + 1);
            break;
        }
        if (target == k)
            continue;
        const auto anchor = beforeEntryForChildIndex(*page, parent, target, cells[i]);
        if (!anchor || !moveSubtree(tx, pageIndex, cells[i], *anchor))
            return Result::failure("cannot reorder cell " + cells[i]);
    }
    return Result::success(cells);
}

// --- edge parents --------------------------------------------------------------

namespace {

// updateEdgeParents with the skipped edges as a set, so that callers that
// run it for each of thousands of cells build the set once.
Result updateEdgeParentsSkipping(Transaction& tx, std::size_t pageIndex, std::string_view cellId,
                                 const std::unordered_set<std::string>& skipEdges)
{
    const Page* page = tx.page(pageIndex);
    if (!page)
        return Result::failure("no such page");
    if (!isAddressable(*page, cellId))
        return Result::failure("unknown cell");

    std::vector<std::string> visit;
    collectPostOrder(*page, std::string(cellId), visit);
    std::unordered_set<std::string> done; // edges handled by this call
    for (const std::string& id : visit) {
        for (const std::string& edge : connectedEdges(*tx.page(pageIndex), id)) {
            if (skipEdges.contains(edge) || !done.insert(edge).second)
                continue;
            if (!updateEdgeParent(tx, pageIndex, edge))
                return Result::failure("cannot update the parent of edge " + edge);
        }
    }
    return Result::success();
}

std::unordered_set<std::string> idSet(const std::vector<std::string>& ids) { return {ids.begin(), ids.end()}; }

// Ids of the cells in `cells` with their descendants, one block per cell in the
// order given, each block in entry order: what moving each cell with its
// subtree, one after the other, leaves behind (a cell already listed, as the
// descendant of an earlier one, is not listed again).
std::vector<std::string> subtreeBlocks(const Page& page, const std::vector<std::string>& cells)
{
    std::vector<std::string> ids;
    std::unordered_set<std::string> listed;
    std::vector<std::pair<std::size_t, std::string>> block;
    for (const std::string& id : cells) {
        if (listed.contains(id))
            continue;
        block.clear();
        const auto self = page.entryIndex(id);
        if (!self)
            continue;
        block.emplace_back(*self, id);
        for (const Cell* d : page.descendants(id)) {
            if (const auto index = page.entryIndex(d->id()))
                block.emplace_back(*index, d->id());
        }
        std::sort(block.begin(), block.end());
        for (auto& [index, name] : block) {
            if (listed.insert(name).second)
                ids.push_back(std::move(name));
        }
    }
    return ids;
}

// Entry index in front of which the subtrees of `cells`, moved in one go, are
// to land so that they become the last children of `parent` (the entry after
// the last child's subtree; after `parent` itself without children).
std::optional<std::size_t> endOfChildren(const Page& page, std::string_view parent)
{
    return beforeEntryForChildIndex(page, parent, childCount(page, parent), "");
}

// The cells `edited` (replacements with their new parent, geometry...) become
// the last children of `parent`, their subtrees moved after the others: one
// ReplaceCells and one ReorderCells, whatever the number of cells. `anchor` is
// where the block goes, found before the replacement.
bool replaceAndAppend(Transaction& tx, std::size_t pageIndex, std::vector<Cell> edited, std::size_t anchor)
{
    std::vector<std::string> ids;
    ids.reserve(edited.size());
    for (const Cell& cell : edited)
        ids.push_back(cell.id());
    if (!tx.emplace<ReplaceCells>(pageIndex, std::move(edited)))
        return false;
    return tx.emplace<ReorderCells>(pageIndex, subtreeBlocks(*tx.page(pageIndex), ids), anchor,
                                    ReorderCells::Order::KeepGivenOrder);
}

// updateEdgeParents for many cells at once: the edges attached to `cells` or
// their descendants, that are not in `skipEdges`, move to the nearest common
// ancestor of their terminals; one ReplaceCells and one ReorderCells per
// target parent. The edges are judged on the page as it is now (after `cells`
// moved), where the one-by-one version judges each on the page as the
// previous cells left it. False only when a primitive failed.
bool updateEdgeParentsBulk(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& cells,
                           const std::unordered_set<std::string>& skipEdges)
{
    const Page* page = tx.page(pageIndex);
    if (!page)
        return false;
    std::vector<std::string> targetsInOrder;
    std::unordered_map<std::string, std::vector<std::string>> edgesOf; // target parent -> edges
    std::unordered_set<std::string> done;
    std::vector<std::string> visit;
    for (const std::string& id : cells)
        collectPostOrder(*page, id, visit);
    for (const std::string& id : visit) {
        for (const std::string& edge : connectedEdges(*page, id)) {
            if (skipEdges.contains(edge) || !done.insert(edge).second)
                continue;
            if (const auto target = edgeParentTarget(*page, edge)) {
                const auto [it, added] = edgesOf.try_emplace(*target);
                if (added)
                    targetsInOrder.push_back(*target);
                it->second.push_back(edge);
            }
        }
    }
    for (const std::string& target : targetsInOrder) {
        page = tx.page(pageIndex);
        const auto anchor = endOfChildren(*page, target);
        if (!anchor)
            return false;
        std::vector<Cell> moved;
        for (const std::string& edge : edgesOf[target])
            moved.push_back(reparentedEdge(*page, *page->findCell(edge), target));
        if (!replaceAndAppend(tx, pageIndex, std::move(moved), *anchor))
            return false;
    }
    return true;
}

} // namespace

Result updateEdgeParents(Transaction& tx, std::size_t pageIndex, std::string_view cellId,
                         const std::vector<std::string>& skipEdges)
{
    return updateEdgeParentsSkipping(tx, pageIndex, cellId, idSet(skipEdges));
}

// --- reparent ------------------------------------------------------------------

Result reparentCells(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& ids,
                     const std::string& target, std::optional<std::size_t> index)
{
    const Page* page = tx.page(pageIndex);
    if (!page)
        return Result::failure("no such page");
    if (!allAddressable(*page, ids))
        return Result::failure("unknown cell");
    const Cell* targetCell = isAddressable(*page, target) ? page->findCell(target) : nullptr;
    if (!targetCell)
        return Result::failure("unknown target");
    if (!targetCell->isVertex() && targetCell->parent() != rootCellId(*page))
        return Result::failure("target must be a layer or a vertex");
    for (const std::string& id : ids) {
        if (isAncestor(*page, id, target))
            return Result::failure("cannot move a cell into itself or its descendant");
    }

    const std::vector<std::string> cells = withoutImplicitlyMovedLabels(*page, topmostCells(*page, ids));
    const std::size_t countBefore = childCount(*page, target);
    const std::size_t base = index.value_or(countBefore);
    const std::unordered_set<std::string> moving = idSet(cells);

    for (std::size_t i = 0; i < cells.size(); ++i) {
        auto cell = copyOf(tx, pageIndex, cells[i]);
        if (!cell)
            return Result::failure("cell disappeared: " + cells[i]);
        page = tx.page(pageIndex);
        const std::string previous = cell->parent();
        const bool reparented = previous != target;
        if (reparented) {
            if (core::Geometry* geo = cell->geometry()) {
                const Point from = origin(*page, previous);
                const Point to = origin(*page, target);
                translateGeometry(*geo, from.x - to.x, from.y - to.y, cell->isEdge());
            }
        }
        std::size_t k = base + i;
        if (!reparented && k > countBefore)
            --k;
        if (!setParentAndPlace(tx, pageIndex, std::move(*cell), target, k))
            return Result::failure("cannot move cell " + cells[i]);
        if (reparented) {
            if (Result r = updateEdgeParentsSkipping(tx, pageIndex, cells[i], moving); !r.ok)
                return r;
        }
    }
    return Result::success(cells);
}

Result removeFromGroup(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& ids,
                       const std::string& layer)
{
    const Page* page = tx.page(pageIndex);
    if (!page)
        return Result::failure("no such page");
    if (ids.empty())
        return Result::failure("nothing to remove");
    const std::string target = layer.empty() ? layerOf(*page, ids.front()) : layer;
    if (target.empty())
        return Result::failure("no layer to move the cells to");
    return reparentCells(tx, pageIndex, ids, target);
}

// --- group / ungroup -----------------------------------------------------------

Result groupCells(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& ids,
                  const GroupOptions& options)
{
    const Page* page = tx.page(pageIndex);
    if (!page)
        return Result::failure("no such page");
    if (!allAddressable(*page, ids))
        return Result::failure("unknown cell");
    const std::vector<std::string> sorted = sortByZOrder(*page, ids);
    if (sorted.empty())
        return Result::failure("nothing to group");
    if (sorted.size() == 1) {
        if (!setStyleKey(tx, pageIndex, sorted.front(), "container", "1"))
            return Result::failure("cannot update cell " + sorted.front());
        return Result::success(sorted);
    }

    const std::string parent = page->findCell(sorted.front())->parent();
    std::vector<std::string> cells;
    for (const std::string& id : sorted) {
        if (page->findCell(id)->parent() == parent)
            cells.push_back(id);
    }
    if (cells.size() < 2)
        return Result::failure("nothing to group");

    auto bounds = geometryBounds(*page, cells);
    if (!bounds)
        return Result::failure("the cells have no geometry");
    bounds->x -= options.border;
    bounds->y -= options.border;
    bounds->width += 2 * options.border;
    bounds->height += 2 * options.border;

    Cell group = Cell::makeVertex(tx.newCellId(pageIndex), parent, "", options.style, *bounds);
    group.setConnectable(false);
    const std::string groupId = group.id();
    const auto groupAnchor = beforeEntryForChildIndex(*page, parent, childCount(*page, parent), "");
    if (!groupAnchor || !tx.emplace<InsertCell>(pageIndex, *groupAnchor, std::move(group)))
        return Result::failure("cannot insert the group");

    // The cells become the last children of the group and their geometry
    // relative to it, then the unselected edges attached to them move to the
    // nearest common ancestor of their terminals: a fixed number of
    // primitives, whatever the number of cells (D-036).
    const std::unordered_set<std::string> grouped = idSet(cells);
    const Page* now = tx.page(pageIndex);
    std::vector<Cell> inside;
    inside.reserve(cells.size());
    for (const std::string& id : cells) {
        const Cell* cell = now->findCell(id);
        if (!cell)
            return Result::failure("cell disappeared: " + id);
        Cell moved = *cell;
        moved.setParent(groupId);
        inside.push_back(translatedCell(*now, moved, -bounds->x, -bounds->y));
    }
    const auto groupEntry = now->entryIndex(groupId);
    if (!groupEntry || !replaceAndAppend(tx, pageIndex, std::move(inside), *groupEntry + 1))
        return Result::failure("cannot move the cells into the group");
    if (!updateEdgeParentsBulk(tx, pageIndex, cells, grouped))
        return Result::failure("cannot update the edges of the group");
    return Result::success({groupId});
}

namespace {

// The children of the groups `groups` (all with the parent `parent`) become
// the last children of `parent`, keeping their absolute positions. Appends
// them to `moved`.
bool dissolveGroups(Transaction& tx, std::size_t pageIndex, const std::string& parent,
                    const std::vector<std::string>& groups, std::vector<std::string>& moved)
{
    const Page* page = tx.page(pageIndex);
    const Point parentOrigin = origin(*page, parent);
    const auto anchor = endOfChildren(*page, parent); // before the cells move: after the last child
    if (!anchor)
        return false;

    std::vector<std::string> children;
    std::vector<Cell> edited;
    for (const std::string& groupId : groups) {
        const Cell* group = page->findCell(groupId);
        if (!group)
            return false;
        const Point groupOrigin = origin(*page, groupId);
        const Point offset = {groupOrigin.x - parentOrigin.x, groupOrigin.y - parentOrigin.y};
        const double groupWidth = group->geometry() ? group->geometry()->width() : 0;
        const double groupHeight = group->geometry() ? group->geometry()->height() : 0;
        for (const std::string& id : page->childIds(groupId)) {
            Cell cell = *page->findCell(id);
            if (core::Geometry* geo = cell.geometry()) {
                if (cell.isVertex() && geo->relative()) {
                    const Point rel = geo->offset().value_or(Point{});
                    const double x = groupOrigin.x + geo->x() * groupWidth + rel.x - parentOrigin.x;
                    const double y = groupOrigin.y + geo->y() * groupHeight + rel.y - parentOrigin.y;
                    geo->setX(x);
                    geo->setY(y);
                    geo->setRelative(false);
                } else {
                    translateGeometry(*geo, offset.x, offset.y, cell.isEdge());
                }
            }
            cell.setParent(parent);
            children.push_back(id);
            edited.push_back(std::move(cell));
        }
    }
    if (edited.empty())
        return true;
    if (!replaceAndAppend(tx, pageIndex, std::move(edited), *anchor))
        return false;
    if (!updateEdgeParentsBulk(tx, pageIndex, children, idSet(children)))
        return false;
    moved.insert(moved.end(), children.begin(), children.end());
    return true;
}

// Graph.removeCellsAfterUngroup: a transparent, deletable group goes away
// with the edges connected to it.
bool removeEmptiedGroup(Transaction& tx, std::size_t pageIndex, const std::string& groupId)
{
    const Page* page = tx.page(pageIndex);
    const Cell* group = page->findCell(groupId);
    if (!group || !isTransparentCell(*group) || group->style().value("deletable") == "0")
        return true;
    for (const std::string& edge : connectedEdges(*page, groupId)) {
        if (isAddressable(*tx.page(pageIndex), edge) && !removeSubtree(tx, pageIndex, edge))
            return false;
    }
    return removeSubtree(tx, pageIndex, groupId);
}

// Dissolves the groups in rounds: each round takes the groups that are not
// inside another group still waiting, and does the groups of one parent
// together, so that a thousand groups cost a handful of passes.
bool dissolveAll(Transaction& tx, std::size_t pageIndex, std::vector<std::string> pending,
                 std::vector<std::string>& moved)
{
    while (!pending.empty()) {
        const Page* page = tx.page(pageIndex);
        const std::unordered_set<std::string> waiting = idSet(pending);
        std::vector<std::string> batch;
        for (const std::string& id : pending) {
            const Cell* group = page->findCell(id);
            if (group && !group->parent().empty() && !waiting.contains(group->parent()))
                batch.push_back(id);
        }
        if (batch.empty())
            batch.push_back(pending.front()); // groups in a loop, or without a parent: one by one
        std::vector<std::string> parents;
        std::unordered_map<std::string, std::vector<std::string>> groupsOf;
        for (const std::string& id : batch) {
            const Cell* group = page->findCell(id);
            if (!group || group->parent().empty())
                continue;
            const auto [it, added] = groupsOf.try_emplace(group->parent());
            if (added)
                parents.push_back(group->parent());
            it->second.push_back(id);
        }
        for (const std::string& parent : parents) {
            if (!dissolveGroups(tx, pageIndex, parent, groupsOf[parent], moved))
                return false;
        }
        for (const std::string& id : batch) {
            if (!removeEmptiedGroup(tx, pageIndex, id))
                return false;
        }
        const std::unordered_set<std::string> done = idSet(batch);
        std::erase_if(pending, [&](const std::string& id) { return done.contains(id); });
    }
    return true;
}

} // namespace

Result ungroupCells(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& ids)
{
    const Page* page = tx.page(pageIndex);
    if (!page)
        return Result::failure("no such page");
    if (!allAddressable(*page, ids))
        return Result::failure("unknown cell");

    std::vector<std::string> groups;
    std::unordered_set<std::string> listed;
    for (const std::string& id : ids) {
        if (page->findCell(id)->isVertex() && childCount(*page, id) > 0 && listed.insert(id).second)
            groups.push_back(id);
    }

    std::vector<std::string> moved;
    if (!dissolveAll(tx, pageIndex, std::move(groups), moved))
        return Result::failure("cannot ungroup");

    std::vector<std::string> remaining;
    std::unordered_set<std::string> seen;
    for (const std::string& id : ids) {
        page = tx.page(pageIndex);
        const Cell* cell = page->findCell(id);
        if (!cell || !seen.insert(id).second)
            continue;
        remaining.push_back(id);
        if (cell->isVertex() && childCount(*page, id) == 0 && !setStyleKey(tx, pageIndex, id, "container", "0"))
            return Result::failure("cannot update cell " + id);
    }

    // Children of a removed group (edges connected to it) are gone again.
    std::erase_if(moved, [&](const std::string& id) { return !tx.page(pageIndex)->findCell(id); });
    std::vector<std::string> result = std::move(moved);
    result.insert(result.end(), remaining.begin(), remaining.end());
    return Result::success(std::move(result));
}

// --- collapse / expand ---------------------------------------------------------

namespace {

constexpr double kPi = 3.14159265358979323846;

// mxGraph.updateAlternateBounds + mxGeometry.swap on a copy of the geometry.
void swapBounds(const Page& page, const Cell& cell, core::Geometry& geo)
{
    const core::Style style = cell.style();
    const Rect bounds = geo.bounds();
    Rect alternate = geo.alternateBounds().value_or([&] {
        // See ops_structure.h: no text metrics here, so a missing alternate
        // bounds keeps the width and shrinks to the swimlane title.
        Rect r{0, 0, bounds.width, bounds.height};
        if (isSwimlane(page, cell)) {
            const double title = swimlaneStartSize(page, cell);
            const std::string horizontal = style.value("horizontal", "1");
            if (horizontal == "0" || horizontal == "false")
                r.width = title;
            else
                r.height = title;
        }
        return r;
    }());
    alternate.x = bounds.x;
    alternate.y = bounds.y;
    if (const double alpha = core::parseNumber(style.value("rotation", "0"), 0) * kPi / 180; alpha != 0) {
        const double dx = (alternate.x + alternate.width / 2) - (bounds.x + bounds.width / 2);
        const double dy = (alternate.y + alternate.height / 2) - (bounds.y + bounds.height / 2);
        const double dx2 = std::cos(alpha) * dx - std::sin(alpha) * dy;
        const double dy2 = std::sin(alpha) * dx + std::cos(alpha) * dy;
        alternate.x += dx2 - dx;
        alternate.y += dy2 - dy;
    }
    // swap(): the geometry takes the alternate bounds, which take the old ones.
    if (geo.x() != alternate.x)
        geo.setX(alternate.x);
    if (geo.y() != alternate.y)
        geo.setY(alternate.y);
    if (geo.width() != alternate.width)
        geo.setWidth(alternate.width);
    if (geo.height() != alternate.height)
        geo.setHeight(alternate.height);
    geo.setAlternateBounds(bounds);
}

// mxGraph.cellsFolded for one cell, then its children when `recurse`.
bool foldCell(Transaction& tx, std::size_t pageIndex, const std::string& id, bool collapse, bool recurse,
              std::vector<std::string>& folded)
{
    const Page* page = tx.page(pageIndex);
    const Cell* original = page->findCell(id);
    if (original == nullptr || original->isEdge() || original->isCollapsed() == collapse)
        return true;
    Cell cell = *original;
    cell.setCollapsed(collapse);
    if (core::Geometry* geo = cell.geometry())
        swapBounds(*page, cell, *geo);
    if (!tx.emplace<ReplaceCell>(pageIndex, std::move(cell)))
        return false;
    folded.push_back(id);
    if (!extendParents(tx, pageIndex, {id}))
        return false;
    if (recurse) {
        const std::vector<std::string> children = tx.page(pageIndex)->childIds(id);
        for (const std::string& child : children) {
            if (!foldCell(tx, pageIndex, child, collapse, recurse, folded))
                return false;
        }
    }
    return true;
}

} // namespace

Result foldCells(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& ids, bool collapse,
                 const FoldOptions& options)
{
    const Page* page = tx.page(pageIndex);
    if (!page)
        return Result::failure("no such page");
    if (!allAddressable(*page, ids))
        return Result::failure("unknown cell");
    std::vector<std::string> folded;
    for (const std::string& id : ids) {
        if (!foldCell(tx, pageIndex, id, collapse, options.recurse, folded))
            return Result::failure("cannot fold cell " + id);
    }
    return Result::success(std::move(folded));
}

} // namespace qraw::commands::ops
