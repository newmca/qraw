// removeCells and removeLayers (ops_cells.h, ops_layers.h).
#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_layers.h"
#include "qraw/commands/ops_tables.h"

#include "qraw/commands/primitives.h"
#include "qraw/commands/queries.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/page.h"

#include <algorithm>
#include <functional>
#include <string>
#include <unordered_set>
#include <utility>

namespace qraw::commands {

namespace detail {
// Defined in ops_cells.cpp: turns one end of `edge` into a terminal point (when
// the edge has a geometry) and clears its source/target attribute.
void detachTerminal(const core::Page& page, core::Cell& edge, bool source);
} // namespace detail

namespace ops {

namespace {

using core::Cell;
using core::Page;

// Ordered set of ids: insertion order plus O(1) membership.
class IdSet {
public:
    bool add(const std::string& id)
    {
        if (!lookup_.insert(id).second)
            return false;
        order_.push_back(id);
        return true;
    }
    [[nodiscard]] bool contains(const std::string& id) const { return lookup_.contains(id); }
    [[nodiscard]] const std::vector<std::string>& ids() const { return order_; }

private:
    std::vector<std::string> order_;
    std::unordered_set<std::string> lookup_;
};

// `ids` ordered by descending position in the page's entry vector.
std::vector<std::string> backToFront(const Page& page, std::vector<std::string> ids)
{
    std::vector<std::pair<std::size_t, std::string>> keyed;
    keyed.reserve(ids.size());
    for (auto& id : ids) {
        const std::size_t entry = page.entryIndex(id).value_or(0);
        keyed.emplace_back(entry, std::move(id));
    }
    std::ranges::sort(keyed, std::greater<>{}, &std::pair<std::size_t, std::string>::first);
    ids.clear();
    for (auto& [entry, id] : keyed)
        ids.push_back(std::move(id));
    return ids;
}

bool isDeletable(const Cell& cell, bool respectDeletable)
{
    return !respectDeletable || cell.style().value("deletable", "") != "0";
}

} // namespace

Result removeCells(Transaction& tx, std::size_t page, const std::vector<std::string>& ids,
                   const RemoveOptions& options)
{
    const Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");
    for (const auto& id : ids) {
        if (!isAddressable(*p, id))
            return Result::failure("unknown cell '" + id + "'");
    }

    // Step 1: the set of cells to delete.
    IdSet set;
    for (const auto& id : ids) {
        if (isDeletable(*p->findCell(id), options.respectDeletable))
            set.add(id);
    }
    if (options.includeEdges) {
        const std::vector<std::string> base = set.ids();
        for (const auto& edge : allEdges(*p, base)) {
            if (isDeletable(*p->findCell(edge), options.respectDeletable))
                set.add(edge);
        }
    }

    // Step 1b: Graph.deleteCells shrinks a table by each deleted row.
    if (options.shrinkTables) {
        for (const auto& id : set.ids()) {
            p = tx.page(page);
            const Cell& cell = *p->findCell(id);
            const Cell* table = p->findCell(cell.parent());
            if (table == nullptr || !isTable(*table) || set.contains(table->id()) || !cell.geometry() ||
                !table->geometry())
                continue;
            Cell shrunk = *table;
            shrunk.geometry()->setHeight(shrunk.geometry()->height() - cell.geometry()->height());
            if (!tx.emplace<ReplaceCell>(page, std::move(shrunk)))
                return Result::failure("could not shrink table '" + table->id() + "'");
        }
        p = tx.page(page);
    }

    // Step 2: invisible parents that lose all their children go too (one level).
    const std::vector<std::string> direct = set.ids();
    std::unordered_set<std::string> checked;
    for (const auto& id : direct) {
        const Cell* parent = p->findCell(p->findCell(id)->parent());
        if (!parent || set.contains(parent->id()) || !checked.insert(parent->id()).second)
            continue;
        if (!(parent->isVertex() || parent->isEdge()) || !isDeletable(*parent, options.respectDeletable) ||
            !isTransparentCell(*parent))
            continue;
        const auto children = p->children(parent->id());
        if (std::ranges::all_of(children, [&](const Cell* c) { return set.contains(c->id()); }))
            set.add(parent->id());
    }
    if (set.ids().empty())
        return Result::success();

    const std::unordered_set<std::string> removed(set.ids().begin(), set.ids().end());
    auto inRemoved = [&](const Page& current, const std::string& id) { return isWithin(current, removed, id); };

    // Step 3: free the surviving edges, then remove each subtree. Later
    // entries go first: erasing near the end of the entry vector moves few
    // entries (and, when everything goes, none), and the undo that re-inserts
    // them in the opposite order appends. In selection order every removal
    // shifted the whole tail, O(n^2) on a 50k-cell page.
    for (const auto& id : backToFront(*p, topmostCells(*p, set.ids()))) {
        p = tx.page(page);
        for (const auto& edgeId : allEdges(*p, {id})) {
            if (inRemoved(*p, edgeId))
                continue;
            const Cell& original = *p->findCell(edgeId);
            Cell edge = original;
            if (!edge.source().empty() && isAncestor(*p, id, edge.source()))
                detail::detachTerminal(*p, edge, true);
            if (!edge.target().empty() && isAncestor(*p, id, edge.target()))
                detail::detachTerminal(*p, edge, false);
            if (edge != original && !tx.emplace<ReplaceCell>(page, std::move(edge)))
                return Result::failure("could not disconnect edge '" + edgeId + "'");
        }
        if (!removeSubtree(tx, page, id))
            return Result::failure("could not remove cell '" + id + "'");
    }
    return Result::success();
}

Result removeLayers(Transaction& tx, std::size_t page, const std::vector<std::string>& ids)
{
    const Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");
    const Cell* root = p->rootCell();
    if (!root)
        return Result::failure("page has no root cell");
    const std::string rootId = root->id();
    for (const auto& id : ids) {
        const Cell* layer = p->findCell(id);
        if (!layer || layer->parent() != rootId)
            return Result::failure("'" + id + "' is not a layer");
    }

    std::vector<std::string> before;
    for (const Cell* layer : p->children(rootId))
        before.push_back(layer->id());
    std::size_t firstRemoved = before.size();
    for (std::size_t i = 0; i < before.size(); ++i) {
        if (std::ranges::find(ids, before[i]) != ids.end()) {
            firstRemoved = i;
            break;
        }
    }

    if (Result removed = removeCells(tx, page, ids, {.includeEdges = false, .respectDeletable = false}); !removed.ok)
        return removed;

    p = tx.page(page);
    const auto remaining = p->children(rootId);
    std::string current;
    if (remaining.empty()) {
        Cell layer = Cell::makeLayer(tx.newCellId(page), rootId);
        current = layer.id();
        if (!tx.emplace<InsertCell>(page, p->rootEntries().size(), std::move(layer)))
            return Result::failure("could not add a replacement layer");
    } else if (firstRemoved > 0 && p->findCell(before[firstRemoved - 1])) {
        current = before[firstRemoved - 1];
    } else {
        current = remaining.front()->id();
    }
    return Result::success({current});
}

} // namespace ops
} // namespace qraw::commands
