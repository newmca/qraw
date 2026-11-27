// Arrange > Layout and the childLayout values treeLayout, circleLayout and flowLayout:
// builds a layout::Graph from a page, runs a layout_graph.h function and writes
// the result back with ReplaceCell commands.
#include "layout_support.h"
#include "qraw/commands/layout_graph.h"
#include "qraw/commands/ops_layout.h"
#include "qraw/commands/primitives.h"
#include "qraw/commands/transaction.h"

#include <algorithm>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace qraw::commands::ops {

namespace {

using core::Cell;
using core::Page;
using core::Point;
using core::Rect;
using detail::applyRouting;
using detail::EdgeRouting;
using detail::flag;
using detail::number;

// A layout::Graph of some children of a parent, and which cell is which node.
struct PageGraph {
    layout::Graph graph;
    std::unordered_map<std::string, std::size_t> index; // vertex id -> node
};

// mxGraphLayout.isVertexIgnored: invisible cells and edges are skipped; so are
// vertices without a position of their own (labels with a relative geometry).
bool isLaidOut(const Cell& cell)
{
    return cell.isVertex() && cell.isVisible() && cell.geometry() != nullptr && !cell.geometry()->relative();
}

// The laid out children of `parent` (those named in `only` when it is not empty)
// and the edges that end at one of them.
PageGraph buildGraph(const Page& page, const std::string& parent, const std::vector<std::string>& only)
{
    PageGraph result;
    const std::unordered_set<std::string> wanted(only.begin(), only.end());
    for (const std::string& id : page.childIds(parent)) {
        const Cell* cell = page.findCell(id);
        if (cell == nullptr || !isLaidOut(*cell) || (!wanted.empty() && !wanted.contains(id)))
            continue;
        result.index.emplace(id, result.graph.nodes.size());
        result.graph.nodes.push_back({id, cell->geometry()->bounds(), detail::isMovable(page, *cell)});
    }

    std::unordered_set<std::string> seen;
    std::vector<const Cell*> edges;
    for (const layout::Node& node : result.graph.nodes) {
        for (const Cell* edge : page.connectedEdges(node.id)) {
            if (edge->isVisible() && seen.insert(edge->id()).second)
                edges.push_back(edge);
        }
    }
    const auto endOf = [&](const std::string& terminal) {
        const auto it = result.index.find(terminal);
        return it == result.index.end() ? -1 : static_cast<int>(it->second);
    };
    std::sort(edges.begin(), edges.end(),
              [&](const Cell* a, const Cell* b) { return page.zIndex(a->id()) < page.zIndex(b->id()); });
    for (const Cell* edge : edges)
        result.graph.edges.push_back({edge->id(), endOf(edge->source()), endOf(edge->target())});
    return result;
}

// --- writing back --------------------------------------------------------------------

bool moveVertices(Transaction& tx, std::size_t pageIndex, const PageGraph& source, const layout::Placement& placement)
{
    for (std::size_t i = 0; i < source.graph.nodes.size(); ++i) {
        const auto& target = placement.bounds[i];
        if (!target)
            continue;
        const Rect& current = source.graph.nodes[i].bounds;
        if (target->x == current.x && target->y == current.y)
            continue;
        Cell changed = *tx.page(pageIndex)->findCell(source.graph.nodes[i].id);
        changed.geometry()->setX(target->x);
        changed.geometry()->setY(target->y);
        if (!tx.emplace<ReplaceCell>(pageIndex, std::move(changed)))
            return false;
    }
    return true;
}

// mxGraph.resetEdge on the edges that end at a moved vertex: their waypoints go
// (or become the route the layout computed), and the edge style follows `routing`.
bool resetEdges(Transaction& tx, std::size_t pageIndex, const PageGraph& source, const layout::Placement& placement,
                EdgeRouting routing)
{
    const auto moved = [&](int node) { return node >= 0 && placement.bounds[static_cast<std::size_t>(node)]; };
    for (std::size_t i = 0; i < source.graph.edges.size(); ++i) {
        const layout::Edge& edge = source.graph.edges[i];
        const std::vector<Point>* route =
            i < placement.edgePoints.size() && placement.edgePoints[i] ? &*placement.edgePoints[i] : nullptr;
        if (!moved(edge.source) && !moved(edge.target) && route == nullptr)
            continue;
        Cell changed = *tx.page(pageIndex)->findCell(edge.id);
        if (changed.geometry() != nullptr)
            changed.geometry()->setPoints(route != nullptr ? *route : std::vector<Point>{});
        core::Style style = changed.style();
        applyRouting(style, routing);
        if (style != changed.style())
            changed.setStyle(style);
        if (changed != *tx.page(pageIndex)->findCell(edge.id) && !tx.emplace<ReplaceCell>(pageIndex, std::move(changed)))
            return false;
    }
    return true;
}

// The Graph.js circleLayout and treeLayout containers: the parent fits the
// laid out children plus `padding` (mxStackLayout's resizeParent counterpart).
struct ParentFit {
    double padding = 20;
    bool grow = false; // only ever make the parent larger
};

bool fitParent(Transaction& tx, std::size_t pageIndex, const std::string& parentId, const ParentFit& fit)
{
    const Page& page = *tx.page(pageIndex);
    const Cell* parent = page.findCell(parentId);
    if (parent == nullptr || parent->geometry() == nullptr)
        return true;
    double width = 0;
    double height = 0;
    for (const std::string& id : page.childIds(parentId)) {
        const core::Geometry* geometry = page.findCell(id)->geometry();
        if (geometry != nullptr && !geometry->relative()) {
            width = std::max(width, geometry->x() + geometry->width());
            height = std::max(height, geometry->y() + geometry->height());
        }
    }
    if (width <= 0 || height <= 0)
        return true;
    width += fit.padding;
    height += fit.padding;
    if (fit.grow) {
        width = std::max(width, parent->geometry()->width());
        height = std::max(height, parent->geometry()->height());
    }
    if (width == parent->geometry()->width() && height == parent->geometry()->height())
        return true;
    Cell changed = *parent;
    changed.geometry()->setWidth(width);
    changed.geometry()->setHeight(height);
    return tx.emplace<ReplaceCell>(pageIndex, std::move(changed));
}

// The top-left corner of the bounds of the nodes.
Point topLeft(const layout::Graph& graph)
{
    Point result{graph.nodes.front().bounds.x, graph.nodes.front().bounds.y};
    for (const layout::Node& node : graph.nodes) {
        result.x = std::min(result.x, node.bounds.x);
        result.y = std::min(result.y, node.bounds.y);
    }
    return result;
}

// A transparentBounds parent keeps its own geometry and its children carry
// the position the layout is anchored at (Graph.js circleLayout: "anchor the
// ring at the pre-run top-left of the cells it places"): shifts the placement
// so that the top-left corner of the placed cells stays where it was.
void keepTopLeft(const PageGraph& source, layout::Placement& placement)
{
    const auto placed = layout::placedBounds(placement);
    if (!placed)
        return;
    layout::Graph moved;
    for (std::size_t i = 0; i < source.graph.nodes.size(); ++i) {
        if (placement.bounds[i])
            moved.nodes.push_back(source.graph.nodes[i]);
    }
    const Point before = topLeft(moved);
    for (auto& rect : placement.bounds) {
        if (rect) {
            rect->x += before.x - placed->x;
            rect->y += before.y - placed->y;
        }
    }
}

struct ContainerSettings {
    bool transparent = false;
    bool resizeParent = true;
    double padding = 20;
};

ContainerSettings containerSettings(const Cell& parent)
{
    const core::Style style = parent.style();
    ContainerSettings settings;
    settings.transparent = style.value("transparentBounds", "") == "1";
    settings.resizeParent = !settings.transparent && flag(style, "resizeParent", true);
    settings.padding = number(style, "parentPadding", 20);
    return settings;
}

// --- the menu ----------------------------------------------------------------------------

struct KindSettings {
    layout::TreeOptions tree;
    layout::RadialOptions radial;
    layout::CircleOptions circle;
    layout::FlowOptions flow;
    layout::OrganicOptions organic;
    EdgeRouting routing = EdgeRouting::Keep;
};

KindSettings settingsFor(LayoutKind kind)
{
    KindSettings s;
    switch (kind) {
    case LayoutKind::VerticalTree: break;
    case LayoutKind::HorizontalTree: s.tree.horizontal = true; break;
    case LayoutKind::OrgChart:
        s.tree.levelDistance = 40;
        s.routing = EdgeRouting::Elbow;
        break;
    case LayoutKind::RadialTree: s.routing = EdgeRouting::Straight; break;
    case LayoutKind::Circle: s.routing = EdgeRouting::Straight; break;
    case LayoutKind::HorizontalFlow:
        s.flow.direction = layout::FlowDirection::Right;
        s.routing = EdgeRouting::Orthogonal;
        break;
    case LayoutKind::VerticalFlow:
        s.flow.direction = layout::FlowDirection::Down;
        s.routing = EdgeRouting::Orthogonal;
        break;
    case LayoutKind::Organic: break; // edges keep their style (EditorUi.CSV_ELK_LAYOUTS 'organic')
    }
    return s;
}

bool isTreeKind(LayoutKind kind)
{
    return kind == LayoutKind::VerticalTree || kind == LayoutKind::HorizontalTree || kind == LayoutKind::OrgChart ||
           kind == LayoutKind::RadialTree;
}

// The roots a tree layout starts from: the scope's root, or graph.findTreeRoots.
std::vector<std::size_t> treeRoots(const PageGraph& source, const LayoutScope& scope)
{
    if (scope.root.empty())
        return layout::findTreeRoots(source.graph);
    const auto it = source.index.find(scope.root);
    if (it == source.index.end())
        return {};
    const bool hasChild = std::any_of(source.graph.edges.begin(), source.graph.edges.end(), [&](const layout::Edge& e) {
        return e.source == static_cast<int>(it->second) && e.target >= 0 && e.target != e.source;
    });
    return hasChild ? std::vector<std::size_t>{it->second} : std::vector<std::size_t>{};
}

} // namespace

LayoutScope layoutScopeForSelection(const Page& page, const std::vector<std::string>& selection,
                                    const std::string& defaultParent)
{
    std::vector<const Cell*> vertices;
    for (const std::string& id : selection) {
        const Cell* cell = page.findCell(id);
        if (cell != nullptr && isLaidOut(*cell))
            vertices.push_back(cell);
    }
    LayoutScope scope;
    scope.parent = defaultParent;
    if (vertices.size() == 1) {
        const Cell& cell = *vertices.front();
        const bool container = std::any_of(page.childIds(cell.id()).begin(), page.childIds(cell.id()).end(),
                                           [&](const std::string& id) { return isLaidOut(*page.findCell(id)); });
        if (container)
            scope.parent = cell.id();
        else {
            scope.parent = cell.parent();
            scope.root = cell.id();
        }
    } else if (vertices.size() > 1) {
        scope.parent = vertices.front()->parent();
        for (const Cell* cell : vertices) {
            if (cell->parent() == scope.parent)
                scope.vertices.push_back(cell->id());
        }
    }
    return scope;
}

bool canArrangeLayout(const Page& page, LayoutKind kind, const LayoutScope& scope)
{
    if (page.findCell(scope.parent) == nullptr)
        return false;
    const PageGraph source = buildGraph(page, scope.parent, scope.vertices);
    if (!isTreeKind(kind))
        return source.graph.nodes.size() >= 2; // flows, organic and circle take every vertex of the parent
    return !treeRoots(source, scope).empty();
}

Result arrangeLayout(Transaction& tx, std::size_t pageIndex, LayoutKind kind, const LayoutScope& scope)
{
    const Page* page = tx.page(pageIndex);
    if (page == nullptr)
        return Result::failure("unknown page");
    if (!canArrangeLayout(*page, kind, scope))
        return Result::failure("nothing to arrange");
    const PageGraph source = buildGraph(*page, scope.parent, scope.vertices);
    const KindSettings settings = settingsFor(kind);

    layout::Placement placement;
    switch (kind) {
    case LayoutKind::VerticalTree:
    case LayoutKind::HorizontalTree:
    case LayoutKind::OrgChart: placement = layout::treeLayout(source.graph, treeRoots(source, scope), settings.tree); break;
    case LayoutKind::RadialTree:
        placement = layout::radialLayout(source.graph, treeRoots(source, scope), settings.radial);
        break;
    case LayoutKind::Circle: placement = layout::circleLayout(source.graph, settings.circle); break;
    case LayoutKind::HorizontalFlow:
    case LayoutKind::VerticalFlow: placement = layout::flowLayout(source.graph, settings.flow); break;
    case LayoutKind::Organic: placement = layout::organicLayout(source.graph, settings.organic); break;
    }
    if (!moveVertices(tx, pageIndex, source, placement) ||
        !resetEdges(tx, pageIndex, source, placement, settings.routing))
        return Result::failure("could not arrange the cells");
    return Result::success();
}

// --- childLayout ---------------------------------------------------------------------------

namespace detail {

// Graph.getLegacyChildLayouts (js/grapheditor/Graph.js:2308): mxCompactTreeLayout with
// horizontal = horizontalTree (default 1), levelDistance = treeLevelDistance
// (default 30), edges kept, nodes not resized, groupPadding = parentPadding
// (default 20), and the container grown to fit unless resizeParent=0.
// Every child is part of a tree, so a new unconnected child is placed too.
Result executeTreeChildLayout(Transaction& tx, std::size_t pageIndex, const std::string& parentId)
{
    const Page& page = *tx.page(pageIndex);
    const Cell* parent = page.findCell(parentId);
    if (parent == nullptr)
        return Result::success();
    const ContainerSettings container = containerSettings(*parent);
    const PageGraph source = buildGraph(page, parentId, {});
    if (source.graph.nodes.empty())
        return Result::success();

    layout::TreeOptions options;
    options.horizontal = flag(parent->style(), "horizontalTree", true);
    options.levelDistance = number(parent->style(), "treeLevelDistance", 30);
    options.includeUnreached = true;
    if (container.transparent) {
        options.origin = topLeft(source.graph);
    } else {
        const detail::Insets start = detail::startInsets(page, *parent);
        options.origin = Point{container.padding + start.left, container.padding + start.top};
    }
    const layout::Placement placement = layout::treeLayout(source.graph, {}, options);
    if (!moveVertices(tx, pageIndex, source, placement) ||
        !resetEdges(tx, pageIndex, source, placement, EdgeRouting::Keep))
        return Result::failure("could not arrange the children of '" + parentId + "'");
    if (container.resizeParent && !fitParent(tx, pageIndex, parentId, {container.padding, true}))
        return Result::failure("could not resize '" + parentId + "'");
    return Result::success();
}

// The Graph.js circleLayout wrapper (Graph.js:12704): mxCircleLayout with the
// circle moved to (x0, y0) = 20 right of and below the title strip (a swimlane's
// startSize, 40 when absent, whatever the container is); the container is fitted
// to the circle plus 20 unless it is transparentBounds or resizeParent=0.
Result executeCircleChildLayout(Transaction& tx, std::size_t pageIndex, const std::string& parentId)
{
    const Page& page = *tx.page(pageIndex);
    const Cell* parent = page.findCell(parentId);
    if (parent == nullptr)
        return Result::success();
    const ContainerSettings container = containerSettings(*parent);
    const PageGraph source = buildGraph(page, parentId, {});
    if (source.graph.nodes.empty())
        return Result::success();

    const core::Style style = parent->style();
    const double startSize = number(style, "startSize", 40);
    const bool sideTitle = style.value("horizontal", "1") == "0";
    layout::CircleOptions options;
    options.moveCircle = true;
    options.x0 = sideTitle ? startSize + 20 : 20;
    options.y0 = sideTitle ? 20 : startSize + 20;
    layout::Placement placement = layout::circleLayout(source.graph, options);
    if (container.transparent)
        keepTopLeft(source, placement);
    if (!moveVertices(tx, pageIndex, source, placement) ||
        !resetEdges(tx, pageIndex, source, placement, EdgeRouting::Straight))
        return Result::failure("could not arrange the children of '" + parentId + "'");
    if (container.resizeParent && !fitParent(tx, pageIndex, parentId, {20, false}))
        return Result::failure("could not resize '" + parentId + "'");
    return Result::success();
}

// The Graph.js flowLayout (Graph.getLegacyChildLayouts, Graph.js:2308):
// mxHierarchicalLayout, run by draw.io as its ELK replacement, with the
// orientation `flowOrientation` (default east), intraCellSpacing (30),
// interRankCellSpacing (100), interHierarchySpacing (60) and
// parallelEdgeSpacing (10), orthogonal edges, isolated children kept in the
// layout, the container grown to fit its children plus parentPadding unless
// resizeParent=0. The orientation names the side of the roots, so the edges
// point the other way (Graph.getElkDirection): north down, south up, west
// right and east, the default, left.
Result executeFlowChildLayout(Transaction& tx, std::size_t pageIndex, const std::string& parentId)
{
    const Page& page = *tx.page(pageIndex);
    const Cell* parent = page.findCell(parentId);
    if (parent == nullptr)
        return Result::success();
    const ContainerSettings container = containerSettings(*parent);
    const PageGraph source = buildGraph(page, parentId, {});
    if (source.graph.nodes.empty())
        return Result::success();

    const core::Style style = parent->style();
    layout::FlowOptions options;
    const std::string orientation = style.value("flowOrientation", "east");
    if (orientation == "north")
        options.direction = layout::FlowDirection::Down;
    else if (orientation == "south")
        options.direction = layout::FlowDirection::Up;
    else if (orientation == "west")
        options.direction = layout::FlowDirection::Right;
    else
        options.direction = layout::FlowDirection::Left;
    options.intraCellSpacing = number(style, "intraCellSpacing", options.intraCellSpacing);
    options.interRankCellSpacing = number(style, "interRankCellSpacing", options.interRankCellSpacing);
    options.interHierarchySpacing = number(style, "interHierarchySpacing", options.interHierarchySpacing);
    options.parallelEdgeSpacing = number(style, "parallelEdgeSpacing", options.parallelEdgeSpacing);
    if (container.transparent) {
        options.origin = topLeft(source.graph);
    } else {
        const detail::Insets start = detail::startInsets(page, *parent);
        options.origin = Point{container.padding + start.left, container.padding + start.top};
    }
    const layout::Placement placement = layout::flowLayout(source.graph, options);
    if (!moveVertices(tx, pageIndex, source, placement) ||
        !resetEdges(tx, pageIndex, source, placement, EdgeRouting::Orthogonal))
        return Result::failure("could not arrange the children of '" + parentId + "'");
    if (container.resizeParent && !fitParent(tx, pageIndex, parentId, {container.padding, true}))
        return Result::failure("could not resize '" + parentId + "'");
    return Result::success();
}

} // namespace detail

} // namespace qraw::commands::ops
