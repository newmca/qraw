// Radial tree layout and mxCircleLayout port (layout_graph.h), without a document.
#include "layout_test_support.h"

#include <numbers>

using namespace layout_test;

namespace {

double distance(const Point& a, const Point& b) { return std::hypot(a.x - b.x, a.y - b.y); }

// A tree of `count` nodes where node i > 0 is the child of (i - 1) / 3: 1 + 3 + 9 + 17 nodes.
layout::Graph ternaryTree(std::size_t count)
{
    layout::Graph graph = makeGraph(count, {}, 80, 30);
    for (std::size_t i = 1; i < count; ++i)
        graph.edges.push_back({"e" + std::to_string(i), static_cast<int>((i - 1) / 3), static_cast<int>(i)});
    return graph;
}

} // namespace

class LayoutRadialCircleTest : public QObject {
    Q_OBJECT

private slots:
    void radialRootStaysInTheCentreAndLevelsShareARadius()
    {
        layout::Graph graph = ternaryTree(30);
        graph.nodes[0].bounds = {300, 200, 80, 30};
        const layout::Placement p = layout::radialLayout(graph, {}, {});
        const Point root = centre(at(p, 0));
        QCOMPARE(at(p, 0), (Rect{300, 200, 80, 30}));

        // Depth of node i in the ternary tree: 1 for 1..3, 2 for 4..12, 3 for the rest.
        std::vector<double> ring(4, -1);
        for (std::size_t i = 1; i < 30; ++i) {
            const std::size_t depth = i <= 3 ? 1 : (i <= 12 ? 2 : 3);
            const double d = distance(centre(at(p, i)), root);
            if (ring[depth] < 0)
                ring[depth] = d;
            QVERIFY2(std::abs(d - ring[depth]) < 1e-6, qPrintable(QStringLiteral("node %1").arg(i)));
        }
        QVERIFY(ring[1] > 0 && ring[2] > ring[1] && ring[3] > ring[2]);
        QVERIFY(noOverlaps(p, 20));
    }

    void radialFirstChildIsOnTopAndChildrenGoClockwise()
    {
        const layout::Graph graph = makeGraph(5, {{0, 1}, {0, 2}, {0, 3}, {0, 4}}, 40, 40);
        const layout::Placement p = layout::radialLayout(graph, {}, {});
        const Point root = centre(at(p, 0));
        const Point first = centre(at(p, 1));
        QVERIFY(std::abs(first.x - root.x) < 1e-6 && first.y < root.y); // straight up
        const Point second = centre(at(p, 2));
        QVERIFY(second.x > root.x && std::abs(second.y - root.y) < 1e-6); // then to the right
        QVERIFY(centre(at(p, 3)).y > root.y);
    }

    void radialSectorsFollowTheLeafCount()
    {
        // Node 1 has three leaves, node 2 one: node 1's sector is three times as wide, so its
        // children spread over three quarters of the circle.
        const layout::Graph graph = makeGraph(7, {{0, 1}, {0, 2}, {1, 3}, {1, 4}, {1, 5}}, 20, 20);
        const layout::Placement p = layout::radialLayout(graph, {}, {});
        const Point root = centre(at(p, 0));
        const auto angle = [&](std::size_t i) {
            const Point c = centre(at(p, i));
            return std::atan2(c.y - root.y, c.x - root.x);
        };
        // Leaves 3, 4, 5 sit at the centres of three equal thirds of 3/4 of the circle: pi / 2 apart.
        const double quarter = std::numbers::pi / 2;
        QVERIFY(std::abs(std::remainder(angle(4) - angle(3), 2 * std::numbers::pi) - quarter) < 1e-6);
        QVERIFY(std::abs(std::remainder(angle(5) - angle(4), 2 * std::numbers::pi) - quarter) < 1e-6);
    }

    void radialSeveralTreesAreSideBySide()
    {
        const layout::Graph graph = makeGraph(4, {{0, 1}, {2, 3}}, 40, 40);
        const layout::Placement p = layout::radialLayout(graph, {}, {});
        QVERIFY(noOverlaps(p, 20));
        QVERIFY(centre(at(p, 2)).x > centre(at(p, 1)).x || centre(at(p, 2)).x > centre(at(p, 0)).x);
    }

    void circleMatchesMxCircleLayoutForFourVertices()
    {
        // radius = max(4 * 40 / pi, 100) = 100, left = top = 0: top, right, bottom, left.
        const layout::Graph graph = makeGraph(4, {{0, 1}}, 40, 40);
        const layout::Placement p = layout::circleLayout(graph, {});
        QCOMPARE(at(p, 0), (Rect{100, 0, 40, 40}));
        QCOMPARE(at(p, 1), (Rect{200, 100, 40, 40}));
        QCOMPARE(at(p, 2), (Rect{100, 200, 40, 40}));
        QCOMPARE(at(p, 3), (Rect{0, 100, 40, 40}));
    }

    void circleUsesTheTopLeftOfTheVerticesOrTheGivenOrigin()
    {
        layout::Graph graph = makeGraph(4, {}, 40, 40);
        graph.nodes[2].bounds = {50, 70, 40, 40};
        layout::Placement p;
        graph.nodes[0].bounds = {10, 20, 40, 40};
        graph.nodes[1].bounds = {30, 40, 40, 40};
        graph.nodes[3].bounds = {50, 60, 40, 40};
        graph.nodes[2].bounds = {70, 80, 40, 40};
        p = layout::circleLayout(graph, {}); // left 10, top 20
        QCOMPARE(at(p, 0), (Rect{110, 20, 40, 40}));
        layout::CircleOptions moved;
        moved.moveCircle = true;
        moved.x0 = 5;
        moved.y0 = 6;
        p = layout::circleLayout(graph, moved);
        QCOMPARE(at(p, 0), (Rect{105, 6, 40, 40}));
    }

    void circleGrowsWithTheVertexSizeAndSpacesThemEvenly()
    {
        layout::Graph graph = makeGraph(20, {}, 100, 60);
        const layout::Placement p = layout::circleLayout(graph, {});
        // radius = 20 * 100 / pi
        const double r = 20 * 100 / std::numbers::pi;
        const Point middle{centre(at(p, 0)).x, centre(at(p, 0)).y + r}; // node 0 is at the top
        for (std::size_t i = 0; i < 20; ++i) {
            QVERIFY(std::abs(distance(centre(at(p, i)), middle) - r) < 1.5); // rounded to whole pixels
            const double angle = std::atan2(centre(at(p, i)).y - middle.y, centre(at(p, i)).x - middle.x);
            const double expected = static_cast<double>(i) * 2 * std::numbers::pi / 20 - std::numbers::pi / 2;
            QVERIFY(std::abs(std::remainder(angle - expected, 2 * std::numbers::pi)) < 0.02);
        }
        QVERIFY(noOverlaps(p));
    }

    void circleLeavesLockedVerticesAlone()
    {
        layout::Graph graph = makeGraph(3, {}, 40, 40);
        graph.nodes[1].movable = false;
        const layout::Placement p = layout::circleLayout(graph, {});
        QVERIFY(p.bounds[0] && !p.bounds[1] && p.bounds[2]);
    }
};

QTEST_APPLESS_MAIN(LayoutRadialCircleTest)
#include "tst_layout_radial_circle.moc"
