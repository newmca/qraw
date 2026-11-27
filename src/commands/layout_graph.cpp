#include "layout_internal.h"

#include <algorithm>
#include <limits>

namespace qraw::commands::layout {

std::vector<std::size_t> findTreeRoots(const Graph& graph, bool invert)
{
    const std::size_t count = graph.nodes.size();
    std::vector<int> fanIn(count, 0);
    std::vector<int> fanOut(count, 0);
    for (const Edge& edge : graph.edges) {
        if (edge.source < 0 || edge.target < 0 || edge.source == edge.target)
            continue;
        ++fanOut[static_cast<std::size_t>(edge.source)];
        ++fanIn[static_cast<std::size_t>(edge.target)];
    }

    std::vector<std::size_t> roots;
    std::optional<std::size_t> best;
    int bestDifference = std::numeric_limits<int>::min();
    for (std::size_t i = 0; i < count; ++i) {
        const int start = invert ? fanIn[i] : fanOut[i];
        const int end = invert ? fanOut[i] : fanIn[i];
        if (end == 0 && start > 0)
            roots.push_back(i);
        // mxGraph keeps the first vertex with the greatest difference.
        const int difference = start - end;
        if (start > 0 && difference > bestDifference) {
            bestDifference = difference;
            best = i;
        }
    }
    if (roots.empty() && best)
        roots.push_back(*best);
    return roots;
}

std::optional<core::Rect> placedBounds(const Placement& placement)
{
    std::optional<core::Rect> total;
    for (const auto& rect : placement.bounds) {
        if (!rect)
            continue;
        if (!total) {
            total = *rect;
            continue;
        }
        const double right = std::max(total->x + total->width, rect->x + rect->width);
        const double bottom = std::max(total->y + total->height, rect->y + rect->height);
        total->x = std::min(total->x, rect->x);
        total->y = std::min(total->y, rect->y);
        total->width = right - total->x;
        total->height = bottom - total->y;
    }
    return total;
}

namespace detail {

Forest buildForest(const Graph& graph, std::vector<std::size_t> roots, bool invert, bool includeUnreached)
{
    const std::size_t count = graph.nodes.size();
    if (roots.empty())
        roots = findTreeRoots(graph, invert);

    // Targets of each node, in node (z-) order, without repeats and self loops.
    std::vector<std::vector<std::size_t>> next(count);
    for (const Edge& edge : graph.edges) {
        if (edge.source < 0 || edge.target < 0 || edge.source == edge.target)
            continue;
        const auto from = static_cast<std::size_t>(invert ? edge.target : edge.source);
        const auto to = static_cast<std::size_t>(invert ? edge.source : edge.target);
        next[from].push_back(to);
    }
    for (auto& targets : next) {
        std::sort(targets.begin(), targets.end());
        targets.erase(std::unique(targets.begin(), targets.end()), targets.end());
    }

    Forest forest;
    std::vector<bool> visited(count, false);
    const auto grow = [&](std::size_t root) {
        Tree tree;
        const auto add = [&](std::size_t node, int parent) {
            visited[node] = true;
            const int index = static_cast<int>(tree.nodes.size());
            tree.nodes.push_back({node, parent, {}});
            if (parent >= 0)
                tree.nodes[static_cast<std::size_t>(parent)].children.push_back(index);
            return index;
        };
        std::vector<std::pair<int, std::size_t>> stack; // tree node, next target to look at
        stack.emplace_back(add(root, -1), 0);
        while (!stack.empty()) {
            auto& [index, cursor] = stack.back();
            const std::size_t node = tree.nodes[static_cast<std::size_t>(index)].node;
            if (cursor >= next[node].size()) {
                stack.pop_back();
                continue;
            }
            const std::size_t target = next[node][cursor++];
            if (!visited[target]) {
                const int child = add(target, index); // emplace_back below invalidates `index` and `cursor`
                stack.emplace_back(child, 0);
            }
        }
        forest.push_back(std::move(tree));
    };
    for (const std::size_t root : roots) {
        if (root < count && !visited[root])
            grow(root);
    }
    if (includeUnreached) {
        for (std::size_t i = 0; i < count; ++i) {
            if (!visited[i])
                grow(i);
        }
    }
    return forest;
}

} // namespace detail

} // namespace qraw::commands::layout
