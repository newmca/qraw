#include "qraw/ui/cell_queries.h"

#include "qraw/core/style.h"

namespace qraw::ui {

namespace {

bool styleFlag(const core::Cell& cell, std::string_view key)
{
    return cell.style().value(key, "0") == "1";
}

} // namespace

bool isLayer(const core::Page& page, std::string_view id)
{
    const core::Cell* cell = page.findCell(id);
    const core::Cell* root = page.rootCell();
    return cell != nullptr && root != nullptr && cell != root && cell->parent() == root->id();
}

bool isLayerHidden(const core::Page& page, std::string_view layerId)
{
    const core::Cell* layer = page.findCell(layerId);
    return layer != nullptr && !layer->isVisible();
}

bool isLayerLocked(const core::Page& page, std::string_view layerId)
{
    const core::Cell* layer = page.findCell(layerId);
    return layer != nullptr && styleFlag(*layer, "locked");
}

bool isCellLocked(const core::Page& page, std::string_view id)
{
    // Bounded walk: a malformed file may contain a parent cycle.
    const core::Cell* cell = page.findCell(id);
    for (std::size_t guard = 0; cell != nullptr && guard <= page.cellCount(); ++guard) {
        if (styleFlag(*cell, "locked"))
            return true;
        if (cell->parent().empty())
            break;
        cell = page.findCell(cell->parent());
    }
    return false;
}

bool isCellSelectable(const core::Page& page, std::string_view id)
{
    const core::Cell* cell = page.findCell(id);
    if (cell == nullptr || cell == page.rootCell() || isLayer(page, id))
        return false;
    const core::Cell* current = cell;
    for (std::size_t guard = 0; current != nullptr && guard <= page.cellCount(); ++guard) {
        if (isLayer(page, current->id()))
            return !isLayerHidden(page, current->id()) && !isLayerLocked(page, current->id());
        if (!current->isVisible())
            return false;
        current = current->parent().empty() ? nullptr : page.findCell(current->parent());
    }
    return false; // not below a layer
}

bool hasChildren(const core::Page& page, std::string_view id)
{
    return page.childCount(id) > 0;
}

std::string ancestorInScope(const core::Page& page, std::string_view id, std::string_view scope)
{
    const core::Cell* cell = page.findCell(id);
    for (std::size_t guard = 0; cell != nullptr && guard <= page.cellCount(); ++guard) {
        const std::string& parent = cell->parent();
        if (parent.empty())
            return {};
        const bool inScope = scope.empty() ? isLayer(page, parent) : parent == scope;
        if (inScope)
            return cell->id();
        cell = page.findCell(parent);
    }
    return {};
}

std::string labelOf(const core::Page& page, std::string_view id)
{
    const core::Cell* cell = page.findCell(id);
    return cell != nullptr ? cell->value() : std::string();
}

bool isHtmlLabel(const core::Page& page, std::string_view id)
{
    const core::Cell* cell = page.findCell(id);
    return cell != nullptr && cell->style().value("html", "0") == "1";
}

} // namespace qraw::ui
