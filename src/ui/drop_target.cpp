#include "drop_target.h"

#include "select_support.h"

#include "qraw/commands/ops_layout.h"
#include "qraw/commands/queries.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"

#include <algorithm>
#include <unordered_set>

namespace qraw::ui {

namespace {

// The pointer inside the (possibly rotated) bounds of a state.
bool containsPoint(const render::CellState& state, QPointF point)
{
    const QRectF bounds = toQRect(state.bounds);
    const double rotation = state.style.rotation();
    if (rotation != 0)
        point = rotateAround(point, bounds.center(), -rotation);
    return bounds.contains(point);
}

// Graph.isContainer on the resolved style (stylesheet names included).
bool isContainerState(const render::CellState& state)
{
    if (state.style.shape() == "swimlane")
        return state.style.string("container", "1") != "0";
    return state.style.string("container", "0") == "1";
}

bool isTableState(const render::CellState& state)
{
    const std::string_view shape = state.style.shape();
    return shape == "table" || shape == "tableRow" || state.style.string("childLayout") == "tableLayout";
}

// Graph.isLane: a swimlane-like container that is a child of a stack layout.
bool isLane(const core::Page& page, const render::CellState& state)
{
    const core::Cell* parent = state.cell != nullptr ? page.findCell(state.cell->parent()) : nullptr;
    return parent != nullptr && isContainerState(state) && commands::ops::hasChildLayout(*parent);
}

bool isLaneId(const core::Page& page, const render::PageView& view, const std::string& id)
{
    const render::CellState* state = view.find(id);
    return state != nullptr && isLane(page, *state);
}

// Graph.isValidDropTarget (+ mxGraph.isValidDropTarget).
bool isValidTarget(const core::Page& page, const render::PageView& view, const render::CellState& state,
                   const std::vector<std::string>& cells)
{
    if (state.kind != render::CellState::Kind::Vertex || state.collapsed || state.cell == nullptr)
        return false;
    if (!isCellSelectable(page, state.id) || isCellLocked(page, state.id) || isTableState(state))
        return false;
    const bool container = isContainerState(state);
    if (state.style.string("dropTarget", "1") == "0")
        return false;
    if (state.style.string("part", "0") == "1" && !container)
        return false;
    if (isLane(page, state) &&
        std::ranges::any_of(cells, [&](const std::string& id) { return isLaneId(page, view, id); }))
        return false; // lanes are not dropped into lanes
    return container || page.childCount(state.id) > 0;
}

} // namespace

namespace {

// dropTarget() on `view`; `candidates` are the paint indices to look at, in
// ascending order (the states under the pointer from the scene's hit grid), or
// null for every state.
std::string findDropTarget(const DocumentSession& session, const render::PageView& view,
                           const std::vector<std::string>& cells, QPointF scenePos,
                           const std::vector<std::uint32_t>* candidates)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return {};
    // Edge labels cannot leave their edge.
    const std::unordered_set<std::string> moving(cells.begin(), cells.end());
    for (const std::string& id : cells) {
        const core::Cell* cell = page->findCell(id);
        const core::Cell* parent = cell != nullptr ? page->findCell(cell->parent()) : nullptr;
        if (parent != nullptr && parent->isEdge() && !moving.contains(parent->id()))
            return {};
    }

    // Topmost vertex under the pointer that is not (inside) a moving cell.
    const render::CellState* hit = nullptr;
    const auto& states = view.states();
    const auto consider = [&](const render::CellState& state) {
        if (state.kind != render::CellState::Kind::Vertex || !containsPoint(state, scenePos))
            return;
        if (moving.empty() || !commands::isWithin(*page, moving, state.id))
            hit = &state;
    };
    if (candidates != nullptr) {
        for (auto it = candidates->rbegin(); it != candidates->rend() && hit == nullptr; ++it)
            consider(states[*it]);
    } else {
        for (auto it = states.rbegin(); it != states.rend() && hit == nullptr; ++it)
            consider(*it);
    }
    // Climb to the first valid target (mxGraph.getDropTarget).
    for (const render::CellState* state = hit; state != nullptr;
         state = state->parent == render::CellState::npos ? nullptr : &states[state->parent]) {
        if (isValidTarget(*page, view, *state, cells))
            return state->id;
    }
    return {};
}

// moveDestination() with the drop target already found.
MoveDestination destinationFor(const DocumentSession& session, const render::PageView& view,
                               const std::vector<std::string>& cells, QPointF scenePos, std::string target)
{
    MoveDestination destination;
    destination.target = std::move(target);
    if (!destination.target.empty()) {
        destination.parent = destination.target;
        return destination;
    }
    // mxGraphHandler.shouldRemoveCellsFromParent: dragged out of the parent.
    const core::Page* page = session.currentPage();
    const core::Cell* first = page != nullptr && !cells.empty() ? page->findCell(cells.front()) : nullptr;
    const core::Cell* parent = first != nullptr ? page->findCell(first->parent()) : nullptr;
    if (parent != nullptr && parent->isVertex()) {
        const render::CellState* state = view.find(parent->id());
        if (state != nullptr && !containsPoint(*state, scenePos))
            destination.parent = session.insertionParent();
    }
    return destination;
}

} // namespace

std::string dropTarget(const DocumentSession& session, const render::PageView& view,
                       const std::vector<std::string>& cells, QPointF scenePos)
{
    return findDropTarget(session, view, cells, scenePos, nullptr);
}

std::string dropTarget(const DocumentSession& session, const DiagramScene& scene,
                       const std::vector<std::string>& cells, QPointF scenePos)
{
    // A point inside a state is inside its box in the scene's grid, which a
    // probe of no size meets.
    const std::vector<std::uint32_t> candidates = scene.statesIn(QRectF(scenePos, QSizeF(0, 0)));
    return findDropTarget(session, scene.pageView(), cells, scenePos, &candidates);
}

MoveDestination moveDestination(const DocumentSession& session, const render::PageView& view,
                                const std::vector<std::string>& cells, QPointF scenePos, bool allowDrop)
{
    return destinationFor(session, view, cells, scenePos,
                          allowDrop ? dropTarget(session, view, cells, scenePos) : std::string());
}

MoveDestination moveDestination(const DocumentSession& session, const DiagramScene& scene,
                                const std::vector<std::string>& cells, QPointF scenePos, bool allowDrop)
{
    return destinationFor(session, scene.pageView(), cells, scenePos,
                          allowDrop ? dropTarget(session, scene, cells, scenePos) : std::string());
}

} // namespace qraw::ui
