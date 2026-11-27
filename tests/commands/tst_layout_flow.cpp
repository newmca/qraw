// Flow (layered) and organic layouts (layout_graph.h), without a document.
#include "layout_test_support.h"

#include <chrono>
#include <cmath>
#include <set>

using namespace layout_test;

namespace {

// The same numbers on every platform.
struct Lcg {
    unsigned state = 12345;
    unsigned next(unsigned bound)
    {
        state = state * 1664525u + 1013904223u;
        return (state >> 8) % bound;
    }
};

// `count` nodes, each with up to three edges to later nodes within `reach`: a DAG with long edges.
layout::Graph randomDag(std::size_t count, unsigned reach)
{
    Lcg random;
    layout::Graph graph = makeGraph(count, {}, 100, 40);
    int number = 0;
    for (std::size_t i = 0; i + 1 < count; ++i) {
        const unsigned edges = 1 + random.next(3);
        for (unsigned k = 0; k < edges; ++k) {
            const auto to = static_cast<int>(std::min<std::size_t>(count - 1, i + 1 + random.next(reach)));
            graph.edges.push_back({"e" + std::to_string(number++), static_cast<int>(i), to});
        }
    }
    return graph;
}

// A connected graph: a spanning tree plus `extra` random edges.
layout::Graph randomConnected(std::size_t count, std::size_t extra)
{
    Lcg random;
    layout::Graph graph = makeGraph(count, {}, 120, 60);
    int number = 0;
    for (std::size_t i = 1; i < count; ++i)
        graph.edges.push_back({"e" + std::to_string(number++), static_cast<int>(random.next(static_cast<unsigned>(i))),
                               static_cast<int>(i)});
    for (std::size_t k = 0; k < extra; ++k) {
        const auto a = static_cast<int>(random.next(static_cast<unsigned>(count)));
        const auto b = static_cast<int>(random.next(static_cast<unsigned>(count)));
        if (a != b)
            graph.edges.push_back({"x" + std::to_string(number++), a, b});
    }
    return graph;
}

// Edge crossings of a drawing in which every edge joins two neighbouring rows (equal y per row).
int crossings(const layout::Graph& graph, const layout::Placement& p)
{
    int count = 0;
    for (std::size_t i = 0; i < graph.edges.size(); ++i) {
        for (std::size_t j = i + 1; j < graph.edges.size(); ++j) {
            const auto& a = graph.edges[i];
            const auto& b = graph.edges[j];
            if (a.source == b.source || a.target == b.target)
                continue;
            const double sa = centre(at(p, static_cast<std::size_t>(a.source))).x;
            const double ta = centre(at(p, static_cast<std::size_t>(a.target))).x;
            const double sb = centre(at(p, static_cast<std::size_t>(b.source))).x;
            const double tb = centre(at(p, static_cast<std::size_t>(b.target))).x;
            if ((sa - sb) * (ta - tb) < 0)
                ++count;
        }
    }
    return count;
}

// The edge as a polyline: centre of the source, its waypoints, centre of the target.
std::vector<Point> polyline(const layout::Graph& graph, const layout::Placement& p, std::size_t edge)
{
    std::vector<Point> line{centre(at(p, static_cast<std::size_t>(graph.edges[edge].source)))};
    for (const Point& point : *p.edgePoints[edge])
        line.push_back(point);
    line.push_back(centre(at(p, static_cast<std::size_t>(graph.edges[edge].target))));
    return line;
}

bool isOrthogonal(const std::vector<Point>& line)
{
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        if (std::abs(line[i].x - line[i + 1].x) > 1e-9 && std::abs(line[i].y - line[i + 1].y) > 1e-9)
            return false;
    }
    return true;
}

// True when no segment of the (orthogonal) polyline passes through the inside of `r`.
bool avoids(const std::vector<Point>& line, const Rect& r)
{
    for (std::size_t i = 0; i + 1 < line.size(); ++i) {
        const double x0 = std::min(line[i].x, line[i + 1].x), x1 = std::max(line[i].x, line[i + 1].x);
        const double y0 = std::min(line[i].y, line[i + 1].y), y1 = std::max(line[i].y, line[i + 1].y);
        if (x1 > r.x && x0 < r.x + r.width && y1 > r.y && y0 < r.y + r.height)
            return false;
    }
    return true;
}

double meanDistance(const layout::Graph& graph, const layout::Placement& p, bool connected)
{
    std::set<std::pair<int, int>> linked;
    for (const auto& e : graph.edges)
        linked.insert({std::min(e.source, e.target), std::max(e.source, e.target)});
    double sum = 0;
    int n = 0;
    for (int i = 0; i < static_cast<int>(graph.nodes.size()); ++i) {
        for (int j = i + 1; j < static_cast<int>(graph.nodes.size()); ++j) {
            if (linked.contains({i, j}) != connected)
                continue;
            const Point a = centre(at(p, static_cast<std::size_t>(i)));
            const Point b = centre(at(p, static_cast<std::size_t>(j)));
            sum += std::hypot(a.x - b.x, a.y - b.y);
            ++n;
        }
    }
    return n ? sum / n : 0;
}

#ifdef NDEBUG
constexpr double kBudgetMs = 1000;
#else
constexpr double kBudgetMs = 20000; // Debug builds are an order of magnitude slower
#endif

// The wall-clock budget is a statement about one machine. Hosted CI runners and package builders are
// shared and too noisy for it (the organic layout of 300 nodes took 1070 ms against a budget of 1000 in
// the Debian package build): QRAW_BENCH_TIME_BUDGETS=off, which ci.yml and the package builds set for
// bench_paint as well, skips it. What the layout computed is still checked.
bool timeBudgetsOn()
{
    const QString value = qEnvironmentVariable("QRAW_BENCH_TIME_BUDGETS").trimmed().toLower();
    return value != QLatin1String("off") && value != QLatin1String("0") && value != QLatin1String("false") &&
           value != QLatin1String("no");
}

template <class F>
double milliseconds(F&& f)
{
    const auto start = std::chrono::steady_clock::now();
    f();
    return std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - start).count();
}

} // namespace

class LayoutFlowTest : public QObject {
    Q_OBJECT

private slots:
    void everyEdgePointsDownAndNothingOverlaps()
    {
        const layout::Graph graph = randomDag(60, 8);
        const layout::Placement p = layout::flowLayout(graph, {});
        for (const auto& e : graph.edges) {
            QVERIFY2(centre(at(p, static_cast<std::size_t>(e.target))).y > centre(at(p, static_cast<std::size_t>(e.source))).y,
                     qPrintable(QString::fromStdString(e.id)));
        }
        QVERIFY(noOverlaps(p));
        QVERIFY(noOverlaps(p, 29)); // intraCellSpacing 30 inside a rank, more between ranks
    }

    void horizontalFlowPointsRightAndTheOtherDirectionsFollow()
    {
        const layout::Graph graph = randomDag(40, 6);
        const struct {
            layout::FlowDirection direction;
            int axis; // 0: x, 1: y
            int sign;
        } cases[] = {{layout::FlowDirection::Right, 0, 1},
                     {layout::FlowDirection::Left, 0, -1},
                     {layout::FlowDirection::Down, 1, 1},
                     {layout::FlowDirection::Up, 1, -1}};
        for (const auto& c : cases) {
            layout::FlowOptions options;
            options.direction = c.direction;
            const layout::Placement p = layout::flowLayout(graph, options);
            for (const auto& e : graph.edges) {
                const Point s = centre(at(p, static_cast<std::size_t>(e.source)));
                const Point t = centre(at(p, static_cast<std::size_t>(e.target)));
                QVERIFY((c.axis == 0 ? t.x - s.x : t.y - s.y) * c.sign > 0);
            }
            QVERIFY(noOverlaps(p));
        }
    }

    void aCycleHasOneReversedEdgeAndTheRestPointDown()
    {
        const layout::Graph graph = makeGraph(4, {{0, 1}, {1, 2}, {2, 0}, {2, 3}});
        const layout::Placement p = layout::flowLayout(graph, {});
        int up = 0;
        for (const auto& e : graph.edges) {
            if (centre(at(p, static_cast<std::size_t>(e.target))).y < centre(at(p, static_cast<std::size_t>(e.source))).y)
                ++up;
        }
        QCOMPARE(up, 1);
        QVERIFY(noOverlaps(p));
    }

    void ranksAreTheLongestPathAndSourcesMoveNextToTheirTargets()
    {
        // 0 -> 1 -> 2 -> 3 and 4 -> 3: 4 is a source with one edge, so it sits next to 3, not on top.
        const layout::Graph graph = makeGraph(5, {{0, 1}, {1, 2}, {2, 3}, {4, 3}});
        const layout::Placement p = layout::flowLayout(graph, {});
        QCOMPARE(at(p, 4).y, at(p, 2).y);
        QCOMPARE(at(p, 3).y - at(p, 2).y, 40.0 + 100.0); // height + interRankCellSpacing
    }

    void aPlanarBipartiteGraphIsDrawnWithoutCrossings()
    {
        // The given order of the nodes would cross every pair of edges.
        const layout::Graph graph = makeGraph(6, {{0, 5}, {1, 4}, {2, 3}});
        const layout::Placement p = layout::flowLayout(graph, {});
        QCOMPARE(crossings(graph, p), 0);

        const layout::Graph ladder = makeGraph(8, {{0, 7}, {0, 6}, {1, 6}, {1, 5}, {2, 5}, {2, 4}, {3, 4}});
        QCOMPARE(crossings(ladder, layout::flowLayout(ladder, {})), 0);
    }

    void completeBipartiteGraphsReachTheirMinimumCrossings()
    {
        // In two ranks K(m,n) has C(m,2) * C(n,2) crossings whatever the order: K2,2 one, K2,3 three.
        const layout::Graph k22 = makeGraph(4, {{0, 2}, {0, 3}, {1, 2}, {1, 3}});
        QCOMPARE(crossings(k22, layout::flowLayout(k22, {})), 1);
        const layout::Graph k23 = makeGraph(5, {{0, 2}, {0, 3}, {0, 4}, {1, 2}, {1, 3}, {1, 4}});
        QCOMPARE(crossings(k23, layout::flowLayout(k23, {})), 3);

        // Three ranks, edges between neighbouring ranks only: crossing free once the pairs are in order.
        const layout::Graph chain = makeGraph(6, {{0, 3}, {1, 2}, {2, 5}, {3, 4}});
        QCOMPARE(crossings(chain, layout::flowLayout(chain, {})), 0);
    }

    void routesAreOrthogonalAndLongEdgesRunAroundTheNodesInBetween()
    {
        // 0 -> 1 -> 2 and 0 -> 2: the long edge passes rank 1 beside node 1.
        const layout::Graph graph = makeGraph(3, {{0, 1}, {1, 2}, {0, 2}});
        for (const auto direction : {layout::FlowDirection::Down, layout::FlowDirection::Right, layout::FlowDirection::Up,
                                     layout::FlowDirection::Left}) {
            layout::FlowOptions options;
            options.direction = direction;
            const layout::Placement p = layout::flowLayout(graph, options);
            for (std::size_t e = 0; e < graph.edges.size(); ++e) {
                QVERIFY(p.edgePoints[e]);
                QVERIFY(isOrthogonal(polyline(graph, p, e)));
            }
            QVERIFY(!p.edgePoints[2]->empty());
            QVERIFY(avoids(polyline(graph, p, 2), at(p, 1)));
        }
        // Edges between aligned nodes need no waypoints.
        const layout::Placement p = layout::flowLayout(graph, {});
        QVERIFY(p.edgePoints[0]->empty() || isOrthogonal(polyline(graph, p, 0)));
    }

    void aReversedEdgeRunsFromItsSourceToItsTarget()
    {
        // 0 -> 1 -> 2 -> 0: the last edge is reversed and spans two ranks. Its route starts at
        // node 2, at the bottom, and ends at node 0, at the top, and goes around node 1.
        const layout::Graph graph = makeGraph(3, {{0, 1}, {1, 2}, {2, 0}});
        const layout::Placement p = layout::flowLayout(graph, {});
        const std::vector<Point> line = polyline(graph, p, 2);
        QVERIFY(isOrthogonal(line));
        QCOMPARE(line.front(), centre(at(p, 2)));
        QCOMPARE(line.back(), centre(at(p, 0)));
        QVERIFY(avoids(line, at(p, 1)));
    }

    void parallelEdgesAreSpreadApart()
    {
        const layout::Graph graph = makeGraph(2, {{0, 1}, {0, 1}, {0, 1}});
        const layout::Placement p = layout::flowLayout(graph, {});
        QVERIFY(p.edgePoints[1] && p.edgePoints[1]->empty()); // the middle one stays straight
        for (const std::size_t e : {std::size_t{0}, std::size_t{2}}) {
            QVERIFY(p.edgePoints[e] && p.edgePoints[e]->size() == 4);
            QVERIFY(isOrthogonal(polyline(graph, p, e)));
        }
        // The two outer edges run 2 * parallelEdgeSpacing apart in the middle.
        QCOMPARE((*p.edgePoints[2])[1].x - (*p.edgePoints[0])[1].x, 20.0);
    }

    void componentsStandSideBySide()
    {
        // Two trees and an isolated node.
        const layout::Graph graph = makeGraph(7, {{0, 1}, {0, 2}, {3, 4}, {3, 5}});
        layout::FlowOptions options;
        options.origin = Point{10, 20};
        const layout::Placement p = layout::flowLayout(graph, options);
        QCOMPARE(at(p, 0).y, 20.0);
        QCOMPARE(at(p, 3).y, 20.0);
        QVERIFY(at(p, 0).x >= 10);
        const double firstRight = std::max({at(p, 0).x + 100, at(p, 1).x + 100, at(p, 2).x + 100});
        QVERIFY(std::min({at(p, 3).x, at(p, 4).x, at(p, 5).x}) >= firstRight + 60 - 1e-9); // interHierarchySpacing
        QVERIFY(at(p, 6).x >= std::max({at(p, 3).x, at(p, 4).x, at(p, 5).x}) + 100 + 60 - 1e-9);
        QVERIFY(noOverlaps(p));
    }

    void theResultDoesNotDependOnThePositionsOfTheNodes()
    {
        layout::Graph graph = randomDag(80, 10);
        const layout::Placement first = layout::flowLayout(graph, {});
        const layout::Placement second = layout::flowLayout(graph, {});
        QVERIFY(first.bounds == second.bounds);
        QVERIFY(first.edgePoints == second.edgePoints);
        for (auto& node : graph.nodes) {
            node.bounds.x = 500;
            node.bounds.y = 500;
        }
        layout::FlowOptions options;
        options.origin = Point{0, 0};
        graph.nodes.front().bounds.x = 0;
        graph.nodes.front().bounds.y = 0;
        QVERIFY(layout::flowLayout(graph, options).bounds == layout::flowLayout(randomDag(80, 10), options).bounds);
    }

    void lockedNodesAndTheirEdgesAreLeftOut()
    {
        layout::Graph graph = makeGraph(3, {{0, 1}, {1, 2}});
        graph.nodes[1].movable = false;
        const layout::Placement p = layout::flowLayout(graph, {});
        QVERIFY(p.bounds[0] && !p.bounds[1] && p.bounds[2]);
        QVERIFY(!p.edgePoints[0] && !p.edgePoints[1]);
    }

    void spacingOptionsAreHonoured()
    {
        const layout::Graph graph = makeGraph(3, {{0, 1}, {0, 2}});
        layout::FlowOptions options;
        options.intraCellSpacing = 50;
        options.interRankCellSpacing = 70;
        const layout::Placement p = layout::flowLayout(graph, options);
        QCOMPARE(at(p, 1).y - at(p, 0).y, 40.0 + 70.0);
        QCOMPARE(std::abs(at(p, 2).x - at(p, 1).x), 150.0); // 100 wide + 50
    }

    void aLargeDagIsLaidOutQuickly()
    {
        const layout::Graph graph = randomDag(500, 30);
        layout::Placement p;
        const double ms = milliseconds([&] { p = layout::flowLayout(graph, {}); });
        qInfo("flow layout of a 500 node DAG (%zu edges): %.1f ms", graph.edges.size(), ms);
        QVERIFY2(!timeBudgetsOn() || ms < kBudgetMs, qPrintable(QString::number(ms)));
        QVERIFY(noOverlaps(p));
    }

    // --- organic -----------------------------------------------------------------------

    void organicLeavesNoOverlaps()
    {
        const layout::Graph graph = randomConnected(40, 20);
        const layout::Placement p = layout::organicLayout(graph, {});
        QVERIFY(noOverlaps(p, 8)); // the final pass keeps 10
    }

    void organicIsDeterministicAndKeepsTheOrigin()
    {
        const layout::Graph graph = randomConnected(40, 20);
        layout::OrganicOptions options;
        options.origin = Point{100, 50};
        const layout::Placement first = layout::organicLayout(graph, options);
        QVERIFY(first.bounds == layout::organicLayout(graph, options).bounds);
        const auto box = layout::placedBounds(first);
        QVERIFY(box);
        QCOMPARE(box->x, 100.0);
        QCOMPARE(box->y, 50.0);

        options.seed = 7;
        QVERIFY(first.bounds != layout::organicLayout(graph, options).bounds);
    }

    void organicPutsConnectedNodesCloserThanUnconnectedOnes()
    {
        const layout::Graph graph = randomConnected(40, 20);
        const layout::Placement p = layout::organicLayout(graph, {});
        QVERIFY(meanDistance(graph, p, true) < 0.8 * meanDistance(graph, p, false));
    }

    void organicHandlesTinyAndLockedGraphs()
    {
        layout::Graph one = makeGraph(1, {});
        QVERIFY(layout::organicLayout(one, {}).bounds[0]);
        QVERIFY(layout::organicLayout(makeGraph(0, {}), {}).bounds.empty());

        layout::Graph graph = makeGraph(3, {{0, 1}, {1, 2}});
        graph.nodes[1].movable = false;
        const layout::Placement p = layout::organicLayout(graph, {});
        QVERIFY(p.bounds[0] && !p.bounds[1] && p.bounds[2]);
    }

    void aLargeGraphIsLaidOutQuickly()
    {
        const layout::Graph graph = randomConnected(300, 150);
        layout::Placement p;
        const double ms = milliseconds([&] { p = layout::organicLayout(graph, {}); });
        qInfo("organic layout of 300 nodes (%zu edges): %.1f ms", graph.edges.size(), ms);
        QVERIFY2(!timeBudgetsOn() || ms < kBudgetMs, qPrintable(QString::number(ms)));
        QVERIFY(noOverlaps(p, 8));
    }
};

QTEST_APPLESS_MAIN(LayoutFlowTest)
#include "tst_layout_flow.moc"
