// Arranging the cells of an import before they are inserted: the same layout
// functions as Arrange > Layout (ops_layout_graph.cpp), on detached cells.
#include "import_support.h"
#include "layout_support.h"

#include <algorithm>
#include <optional>
#include <unordered_map>

namespace qraw::commands::imports::detail {

namespace {

using core::Cell;
using core::Point;
using core::Rect;
using ops::detail::applyRouting;
using ops::detail::EdgeRouting;

bool isTopLevelVertex(const Cell& cell)
{
    return cell.isVertex() && cell.parent().empty() && cell.geometry() != nullptr && !cell.geometry()->relative();
}

EdgeRouting routingFor(Arrangement arrangement)
{
    switch (arrangement) {
    case Arrangement::OrgChart: return EdgeRouting::Elbow;
    case Arrangement::RadialTree:
    case Arrangement::Circle: return EdgeRouting::Straight;
    case Arrangement::VerticalFlow:
    case Arrangement::HorizontalFlow: return EdgeRouting::Orthogonal;
    default: return EdgeRouting::Keep;
    }
}

layout::Placement place(const layout::Graph& graph, Arrangement arrangement, const Spacing& spacing)
{
    const Point origin{0, 0};
    switch (arrangement) {
    case Arrangement::VerticalTree:
    case Arrangement::HorizontalTree:
    case Arrangement::OrgChart: {
        layout::TreeOptions options;
        options.horizontal = arrangement == Arrangement::HorizontalTree;
        options.levelDistance = spacing.level;
        options.nodeDistance = spacing.node;
        options.includeUnreached = true;
        options.origin = origin;
        return layout::treeLayout(graph, {}, options);
    }
    case Arrangement::RadialTree: {
        layout::RadialOptions options;
        options.levelDistance = spacing.level;
        options.nodeDistance = spacing.node;
        options.includeUnreached = true;
        options.origin = origin;
        return layout::radialLayout(graph, {}, options);
    }
    case Arrangement::VerticalFlow:
    case Arrangement::HorizontalFlow: {
        layout::FlowOptions options;
        options.direction = arrangement == Arrangement::HorizontalFlow ? layout::FlowDirection::Right : layout::FlowDirection::Down;
        options.intraCellSpacing = spacing.node;
        options.interRankCellSpacing = spacing.level;
        options.parallelEdgeSpacing = spacing.edge;
        options.origin = origin;
        return layout::flowLayout(graph, options);
    }
    case Arrangement::Organic: {
        layout::OrganicOptions options;
        options.forceConstant = spacing.node;
        options.origin = origin;
        return layout::organicLayout(graph, options);
    }
    case Arrangement::Circle: {
        layout::CircleOptions options;
        options.moveCircle = true;
        return layout::circleLayout(graph, options);
    }
    case Arrangement::None: break;
    }
    return {};
}

} // namespace

void arrange(std::vector<Cell>& cells, Arrangement arrangement, const Spacing& spacing)
{
    if (arrangement == Arrangement::None) {
        moveToOrigin(cells);
        return;
    }
    layout::Graph graph;
    std::vector<std::size_t> nodeCell; // node -> index in `cells`
    std::unordered_map<std::string, int> nodeOf;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (!isTopLevelVertex(cells[i]))
            continue;
        nodeOf.emplace(cells[i].id(), static_cast<int>(graph.nodes.size()));
        graph.nodes.push_back({cells[i].id(), cells[i].geometry()->bounds(), true});
        nodeCell.push_back(i);
    }
    if (graph.nodes.empty())
        return;
    std::vector<std::size_t> edgeCell; // layout edge -> index in `cells`
    const auto endOf = [&](const std::string& id) {
        const auto it = nodeOf.find(id);
        return it == nodeOf.end() ? -1 : it->second;
    };
    for (std::size_t i = 0; i < cells.size(); ++i) {
        if (cells[i].isEdge() && cells[i].parent().empty()) {
            graph.edges.push_back({cells[i].id(), endOf(cells[i].source()), endOf(cells[i].target())});
            edgeCell.push_back(i);
        }
    }

    const layout::Placement placement = place(graph, arrangement, spacing);
    for (std::size_t n = 0; n < nodeCell.size(); ++n) {
        if (n < placement.bounds.size() && placement.bounds[n]) {
            cells[nodeCell[n]].geometry()->setX(placement.bounds[n]->x);
            cells[nodeCell[n]].geometry()->setY(placement.bounds[n]->y);
        }
    }
    const EdgeRouting routing = routingFor(arrangement);
    for (std::size_t e = 0; e < edgeCell.size(); ++e) {
        Cell& edge = cells[edgeCell[e]];
        if (graph.edges[e].source < 0 && graph.edges[e].target < 0)
            continue;
        if (edge.geometry() != nullptr) {
            const bool routed = e < placement.edgePoints.size() && placement.edgePoints[e];
            edge.geometry()->setPoints(routed ? *placement.edgePoints[e] : std::vector<Point>{});
        }
        core::Style style = edge.style();
        applyRouting(style, routing);
        if (style != edge.style())
            edge.setStyle(style);
    }
    moveToOrigin(cells);
}

void moveToOrigin(std::vector<Cell>& cells)
{
    double left = 0;
    double top = 0;
    bool any = false;
    const auto extend = [&](double x, double y) {
        left = any ? std::min(left, x) : x;
        top = any ? std::min(top, y) : y;
        any = true;
    };
    for (const Cell& cell : cells) {
        const core::Geometry* geometry = cell.geometry();
        if (!cell.parent().empty() || geometry == nullptr)
            continue;
        if (cell.isEdge()) {
            for (const Point& p : geometry->points())
                extend(p.x, p.y);
            if (const auto p = geometry->sourcePoint())
                extend(p->x, p->y);
            if (const auto p = geometry->targetPoint())
                extend(p->x, p->y);
        } else if (cell.isVertex() && !geometry->relative()) {
            extend(geometry->x(), geometry->y());
        }
    }
    if (!any || (left == 0 && top == 0))
        return;
    for (Cell& cell : cells) {
        core::Geometry* geometry = cell.geometry();
        if (!cell.parent().empty() || geometry == nullptr)
            continue;
        if (cell.isEdge()) {
            std::vector<Point> points = geometry->points();
            for (Point& p : points) {
                p.x -= left;
                p.y -= top;
            }
            if (!points.empty())
                geometry->setPoints(points);
            if (auto p = geometry->sourcePoint())
                geometry->setSourcePoint(Point{p->x - left, p->y - top});
            if (auto p = geometry->targetPoint())
                geometry->setTargetPoint(Point{p->x - left, p->y - top});
        } else if (cell.isVertex() && !geometry->relative()) {
            geometry->setX(geometry->x() - left);
            geometry->setY(geometry->y() - top);
        }
    }
}

} // namespace qraw::commands::imports::detail
