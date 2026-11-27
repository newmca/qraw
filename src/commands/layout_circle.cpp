// Port of mxCircleLayout (mxgraph/src/layout/mxCircleLayout.js, draw.io,
// Apache-2.0), modified: a pure function over a LayoutGraph. The model
// updates (edge reset, noEdgeStyle) are the caller's (ops_layout_graph.cpp).
#include "qraw/commands/layout_graph.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace qraw::commands::layout {

namespace {

// JavaScript Math.round: halves go up.
double jsRound(double value) { return std::floor(value + 0.5); }

} // namespace

Placement circleLayout(const Graph& graph, const CircleOptions& options)
{
    Placement placement;
    placement.bounds.assign(graph.nodes.size(), std::nullopt);
    const std::size_t count = graph.nodes.size();
    if (count == 0)
        return placement;

    double largest = 0;
    double top = graph.nodes.front().bounds.y;
    double left = graph.nodes.front().bounds.x;
    for (const Node& node : graph.nodes) {
        top = std::min(top, node.bounds.y);
        left = std::min(left, node.bounds.x);
        largest = std::max(largest, std::max(node.bounds.width, node.bounds.height));
    }
    // getRadius: large enough for the vertices not to overlap.
    const double radius = std::max(static_cast<double>(count) * largest / std::numbers::pi, options.radius);
    if (options.moveCircle) {
        left = options.x0;
        top = options.y0;
    }

    const double phi = 2 * std::numbers::pi / static_cast<double>(count);
    for (std::size_t i = 0; i < count; ++i) {
        if (!graph.nodes[i].movable)
            continue;
        const double angle = static_cast<double>(i) * phi - std::numbers::pi / 2;
        core::Rect bounds = graph.nodes[i].bounds;
        bounds.x = jsRound(left + radius + radius * std::cos(angle));
        bounds.y = jsRound(top + radius + radius * std::sin(angle));
        placement.bounds[i] = bounds;
    }
    return placement;
}

} // namespace qraw::commands::layout
