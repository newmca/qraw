#pragma once
// Graph builders and geometric checks for the layout tests (tst_layout_*.cpp).

#include "qraw/commands/layout_graph.h"

#include <QString>
#include <QtTest>

#include <cmath>
#include <initializer_list>
#include <string>
#include <utility>

namespace layout_test {

namespace layout = qraw::commands::layout;
using qraw::core::Point;
using qraw::core::Rect;

// Nodes n0..n(count-1), each w x h at the origin, and edges (from, to) in the given order.
inline layout::Graph makeGraph(std::size_t count, std::initializer_list<std::pair<int, int>> edges, double w = 100,
                               double h = 40)
{
    layout::Graph graph;
    for (std::size_t i = 0; i < count; ++i)
        graph.nodes.push_back({"n" + std::to_string(i), {0, 0, w, h}, true});
    int number = 0;
    for (const auto& [from, to] : edges)
        graph.edges.push_back({"e" + std::to_string(number++), from, to});
    return graph;
}

// The new bounds of node i (the test fails when the layout left it alone).
inline Rect at(const layout::Placement& placement, std::size_t i)
{
    if (!placement.bounds[i]) {
        QTest::qFail(qPrintable(QStringLiteral("node %1 was not placed").arg(i)), __FILE__, __LINE__);
        return {};
    }
    return *placement.bounds[i];
}

inline Point centre(const Rect& r) { return {r.x + r.width / 2, r.y + r.height / 2}; }

// True when the rectangles are at least `gap` apart on one axis.
inline bool apart(const Rect& a, const Rect& b, double gap = 0)
{
    return a.x + a.width + gap <= b.x + 1e-9 || b.x + b.width + gap <= a.x + 1e-9 ||
           a.y + a.height + gap <= b.y + 1e-9 || b.y + b.height + gap <= a.y + 1e-9;
}

inline bool noOverlaps(const layout::Placement& placement, double gap = 0)
{
    for (std::size_t i = 0; i < placement.bounds.size(); ++i) {
        for (std::size_t j = i + 1; j < placement.bounds.size(); ++j) {
            if (placement.bounds[i] && placement.bounds[j] && !apart(*placement.bounds[i], *placement.bounds[j], gap))
                return false;
        }
    }
    return true;
}

} // namespace layout_test
