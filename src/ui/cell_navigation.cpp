#include "qraw/ui/cell_navigation.h"

#include "qraw/ui/cell_queries.h"

#include <algorithm>
#include <unordered_set>

namespace qraw::ui::navigation {

namespace {

// `id` lies below `scope` (or `scope` is empty). Bounded walk: a malformed file can have a parent cycle.
bool isInScope(const core::Page& page, std::string_view id, std::string_view scope)
{
    if (scope.empty())
        return true;
    const core::Cell* cell = page.findCell(id);
    for (std::size_t guard = 0; cell != nullptr && guard <= page.cellCount(); ++guard) {
        if (cell->parent() == scope)
            return true;
        cell = cell->parent().empty() ? nullptr : page.findCell(cell->parent());
    }
    return false;
}

} // namespace

std::vector<std::string> tabOrder(const core::Page& page, const render::PageView& view, std::string_view scope,
                                  std::string_view keep)
{
    std::vector<std::string> order;
    order.reserve(view.states().size());
    for (const render::CellState& state : view.states()) {
        if (state.id != keep && !isCellSelectable(page, state.id))
            continue;
        if (!isInScope(page, state.id, scope))
            continue;
        order.push_back(state.id);
    }
    return order;
}

std::string neighbour(const std::vector<std::string>& order, std::string_view current, bool forward)
{
    if (order.empty())
        return {};
    const auto found = std::find(order.begin(), order.end(), current);
    if (current.empty() || found == order.end())
        return forward ? order.front() : order.back();
    const std::size_t index = static_cast<std::size_t>(found - order.begin());
    const std::size_t count = order.size();
    return order[forward ? (index + 1) % count : (index + count - 1) % count];
}

std::string firstChild(const core::Page& page, const std::vector<std::string>& order, std::string_view id)
{
    const std::unordered_set<std::string_view> navigable(order.begin(), order.end());
    for (const std::string& child : page.childIds(id)) {
        if (navigable.contains(child))
            return child;
    }
    return {};
}

std::string container(const core::Page& page, std::string_view id, std::string_view scope)
{
    const core::Cell* cell = page.findCell(id);
    if (cell == nullptr)
        return {};
    const std::string& parent = cell->parent();
    if (parent.empty() || parent == scope || isLayer(page, parent))
        return {};
    const core::Cell* parentCell = page.findCell(parent);
    if (parentCell == nullptr || !(parentCell->isVertex() || parentCell->isEdge()))
        return {};
    return parent;
}

} // namespace qraw::ui::navigation
