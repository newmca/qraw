#include "connect_edit.h"

#include "qraw/commands/default_style.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/commands/queries.h"
#include "qraw/ui/app_settings.h"

namespace qraw::ui::connect {

namespace {

using commands::Result;

std::string edgeParentFor(const core::Page& page, const std::string& source, const std::string& otherCell)
{
    std::string parent = commands::nearestCommonAncestor(page, source, otherCell);
    const core::Cell* root = page.rootCell();
    if (parent.empty() || (root != nullptr && parent == root->id()))
        parent = commands::layerOf(page, source);
    return parent;
}

std::vector<commands::ops::StyleEdit> constraintKeys(bool source)
{
    const std::string prefix = source ? "exit" : "entry";
    std::vector<commands::ops::StyleEdit> edits;
    for (const char* suffix : {"X", "Y", "Dx", "Dy", "Perimeter"})
        edits.push_back({prefix + suffix, std::nullopt});
    return edits;
}

} // namespace

Result addConnection(commands::Transaction& tx, std::size_t pageIndex, const NewConnection& request)
{
    const core::Page* page = tx.page(pageIndex);
    const core::Cell* source = page != nullptr ? page->findCell(request.source) : nullptr;
    if (source == nullptr)
        return Result::failure("unknown source terminal");

    std::vector<core::Cell> cells;
    std::string target = request.target;
    std::string cloneId;
    std::string edgeParent;

    if (target.empty() && request.cloneBounds) {
        // HoverIcons / createTargetVertex: a copy of the source (same style,
        // size and label: Graph.connectVertex duplicates the cell, mxConnectionHandler
        // .createTargetVertex clones it) whose geometry is relative to its parent.
        cloneId = tx.newCellId(pageIndex);
        const core::Point origin = commands::origin(*page, request.insertionParent);
        core::Rect local = *request.cloneBounds;
        local.x -= origin.x;
        local.y -= origin.y;
        cells.push_back(
            core::Cell::makeVertex(cloneId, request.insertionParent, source->value(), source->styleText(), local));
        target = cloneId;
        edgeParent = edgeParentFor(*page, request.source, request.insertionParent);
    } else if (!target.empty()) {
        if (page->findCell(target) == nullptr)
            return Result::failure("unknown target terminal");
        edgeParent = edgeParentFor(*page, request.source, target);
    } else {
        edgeParent = request.insertionParent;
    }

    const std::string edgeId = tx.newCellId(pageIndex);
    // A new connector takes the default edge style (Graph.createCurrentEdgeStyle).
    const std::string edgeStyle = commands::applyDefaultStyle(core::Style(kDefaultEdgeStyle), AppSettings().defaultStyles(),
                                                              commands::StyleTarget::Edge, commands::InsertKind::Connector)
                                      .toString();
    core::Cell edge = core::Cell::makeEdge(edgeId, edgeParent, "", edgeStyle, "", "");
    if (target.empty() && request.freePoint) {
        const core::Point origin = commands::origin(*page, edgeParent);
        edge.ensureGeometry().setTargetPoint(core::Point{request.freePoint->x - origin.x, request.freePoint->y - origin.y});
    }
    cells.push_back(std::move(edge));

    if (Result added = commands::ops::addCells(tx, pageIndex, std::move(cells)); !added)
        return added;
    if (Result connected = commands::ops::connectEdge(tx, pageIndex, edgeId, request.source, true, request.sourceConstraint);
        !connected)
        return connected;
    if (!target.empty()) {
        if (Result connected =
                commands::ops::connectEdge(tx, pageIndex, edgeId, target, false, request.targetConstraint);
            !connected)
            return connected;
    }
    return Result::success({cloneId.empty() ? edgeId : cloneId});
}

Result clearEndConstraint(commands::Transaction& tx, std::size_t page, const std::string& edge, bool source)
{
    const core::Page* p = tx.page(page);
    const core::Cell* cell = p != nullptr ? p->findCell(edge) : nullptr;
    if (cell == nullptr)
        return Result::failure("unknown edge");
    const core::Style style = cell->style();
    const std::string prefix = source ? "exit" : "entry";
    if (!style.has(prefix + "X") && !style.has(prefix + "Y") && !style.has(prefix + "Dx") &&
        !style.has(prefix + "Dy") && !style.has(prefix + "Perimeter"))
        return Result::success();
    return commands::ops::setStyles(tx, page, {edge}, constraintKeys(source));
}

Result reconnectEnd(commands::Transaction& tx, std::size_t page, const std::string& edge, const std::string& terminal,
                    bool source, const std::optional<commands::ops::ConnectionConstraint>& constraint)
{
    if (Result r = commands::ops::connectEdge(tx, page, edge, terminal, source, constraint); !r)
        return r;
    return constraint ? Result::success() : clearEndConstraint(tx, page, edge, source);
}

Result detachEnd(commands::Transaction& tx, std::size_t page, const std::string& edge, bool source,
                 const core::Point& point)
{
    const core::Page* p = tx.page(page);
    const core::Cell* cell = p != nullptr ? p->findCell(edge) : nullptr;
    if (cell == nullptr)
        return Result::failure("unknown edge");
    const core::Point origin = commands::origin(*p, cell->parent());
    if (Result r = commands::ops::disconnectEdge(tx, page, edge, source, {point.x - origin.x, point.y - origin.y}); !r)
        return r;
    return clearEndConstraint(tx, page, edge, source);
}

} // namespace qraw::ui::connect
