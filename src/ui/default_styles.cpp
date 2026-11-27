// Set as Default Style on the UI side; see default_styles.h.
#include "default_styles.h"

#include "qraw/core/page.h"
#include "qraw/ui/app_settings.h"

#include <string>
#include <unordered_map>

namespace qraw::ui {

namespace {

bool isEdgeLabelStyle(const core::Cell& cell)
{
    const core::Style style = cell.style();
    return style.hasName("text") || style.hasName("edgeLabel");
}

} // namespace

commands::StyleTarget defaultStyleTarget(const core::Page& page, const core::Cell& cell)
{
    if (cell.isEdge())
        return commands::StyleTarget::Edge;
    const core::Cell* parent = page.findCell(cell.parent());
    if (parent != nullptr && parent->isEdge() && isEdgeLabelStyle(cell))
        return commands::StyleTarget::EdgeLabel;
    return commands::StyleTarget::Vertex;
}

void applyStoredDefaults(CellBundle& bundle)
{
    const commands::DefaultStyles defaults = AppSettings().defaultStyles();
    if (defaults.empty())
        return;
    std::unordered_map<std::string, bool> isEdge; // id -> edge, for the labels of edges in the bundle
    for (const core::Cell& cell : bundle.cells)
        isEdge.emplace(cell.id(), cell.isEdge());
    for (core::Cell& cell : bundle.cells) {
        commands::StyleTarget target = commands::StyleTarget::Vertex;
        if (cell.isEdge()) {
            target = commands::StyleTarget::Edge;
        } else if (const auto parent = isEdge.find(cell.parent()); parent != isEdge.end() && parent->second &&
                                                                   isEdgeLabelStyle(cell)) {
            target = commands::StyleTarget::EdgeLabel;
        }
        const core::Style styled = commands::applyDefaultStyle(cell.style(), defaults, target);
        if (styled != cell.style())
            cell.setStyle(styled);
    }
}

} // namespace qraw::ui
