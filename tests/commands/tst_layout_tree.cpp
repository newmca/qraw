// Tree layouts (layout_graph.h treeLayout, findTreeRoots): the pure algorithm,
// without a document.
#include "layout_test_support.h"

#include <QElapsedTimer>

using namespace layout_test;

class LayoutTreeTest : public QObject {
    Q_OBJECT

private slots:
    void rootsHaveOutgoingAndNoIncomingEdges()
    {
        // 0 -> 1 -> 2, 3 -> 2, 4 alone: roots 0 and 3; reversed, the only sink 2.
        const layout::Graph graph = makeGraph(5, {{0, 1}, {1, 2}, {3, 2}});
        QCOMPARE(layout::findTreeRoots(graph), (std::vector<std::size_t>{0, 3}));
        QCOMPARE(layout::findTreeRoots(graph, true), (std::vector<std::size_t>{2}));
    }

    void withoutRootsTheVertexWithTheMostOutgoingEdgesIsTheRoot()
    {
        // A cycle 0 -> 1 -> 2 -> 0 with a branch 1 -> 3: node 1 has two out and one in.
        const layout::Graph graph = makeGraph(4, {{0, 1}, {1, 2}, {2, 0}, {1, 3}});
        QCOMPARE(layout::findTreeRoots(graph), (std::vector<std::size_t>{1}));
    }

    void verticalTreeCentresTheParentAndKeepsTheRootInPlace()
    {
        // R(0) with A(1) B(2) C(3); A has A1(4) A2(5). 100 x 40 each.
        layout::Graph graph = makeGraph(6, {{0, 1}, {0, 2}, {0, 3}, {1, 4}, {1, 5}});
        graph.nodes[0].bounds = {500, 300, 100, 40};
        const layout::Placement p = layout::treeLayout(graph, {}, {});

        QCOMPARE(at(p, 0), (Rect{500, 300, 100, 40})); // the root does not move
        // Levels are levelDistance (30) below the previous one.
        for (std::size_t i : {1, 2, 3})
            QCOMPARE(at(p, i).y, 370.0);
        for (std::size_t i : {4, 5})
            QCOMPARE(at(p, i).y, 440.0);
        // Siblings left to right in z-order, nodeDistance (20) apart at least.
        QVERIFY(at(p, 1).x < at(p, 2).x && at(p, 2).x < at(p, 3).x);
        QVERIFY(at(p, 4).x < at(p, 5).x);
        QVERIFY(at(p, 2).x - (at(p, 1).x + 100) >= 20 - 1e-9);
        // A parent is centred between its first and last child.
        QCOMPARE(centre(at(p, 0)).x, (centre(at(p, 1)).x + centre(at(p, 3)).x) / 2);
        QCOMPARE(centre(at(p, 1)).x, (centre(at(p, 4)).x + centre(at(p, 5)).x) / 2);
        QVERIFY(noOverlaps(p, 20));
    }

    void horizontalTreeSwapsTheAxes()
    {
        layout::Graph graph = makeGraph(4, {{0, 1}, {0, 2}, {0, 3}});
        layout::TreeOptions options;
        options.horizontal = true;
        options.levelDistance = 50;
        const layout::Placement p = layout::treeLayout(graph, {}, options);
        for (std::size_t i : {1, 2, 3})
            QCOMPARE(at(p, i).x, 150.0); // 100 wide root + 50
        QVERIFY(at(p, 1).y < at(p, 2).y && at(p, 2).y < at(p, 3).y);
        QCOMPARE(at(p, 2).y - (at(p, 1).y + 40), 20.0); // nodeDistance
        QCOMPARE(centre(at(p, 0)).y, centre(at(p, 2)).y);
        QVERIFY(noOverlaps(p));
    }

    void invertGrowsTheTreeTheOtherWay()
    {
        layout::Graph graph = makeGraph(3, {{0, 1}, {0, 2}});
        graph.nodes[0].bounds = {0, 200, 100, 40};
        layout::TreeOptions options;
        options.invert = false;
        const layout::Placement down = layout::treeLayout(graph, {}, options);
        // An inverted tree follows the edges backwards: the root is the sink, here node 0.
        layout::Graph reversed = makeGraph(3, {{1, 0}, {2, 0}});
        reversed.nodes[0].bounds = {0, 200, 100, 40};
        options.invert = true;
        const layout::Placement up = layout::treeLayout(reversed, {}, options);
        QCOMPARE(at(down, 1).y, 270.0);
        QCOMPARE(at(up, 1).y, 130.0); // 200 - 30 - 40
        QCOMPARE(at(up, 1).x, at(down, 1).x);
    }

    void levelsAreAsThickAsTheirTallestNodeAndAlignedAcrossSubtrees()
    {
        layout::Graph graph = makeGraph(5, {{0, 1}, {0, 2}, {1, 3}, {2, 4}});
        graph.nodes[1].bounds.height = 100; // taller than its sibling
        const layout::Placement p = layout::treeLayout(graph, {}, {});
        QCOMPARE(at(p, 1).y, at(p, 2).y);
        // The level below starts levelDistance under the tallest node of level 1.
        QCOMPARE(at(p, 3).y, at(p, 1).y + 100 + 30);
        QCOMPARE(at(p, 4).y, at(p, 3).y);
    }

    void aWideSubtreePushesItsNeighbourAway()
    {
        // 0 -> 1, 2; node 1 has four children, node 2 one: the grandchildren must not touch.
        layout::Graph graph = makeGraph(8, {{0, 1}, {0, 2}, {1, 3}, {1, 4}, {1, 5}, {1, 6}, {2, 7}});
        const layout::Placement p = layout::treeLayout(graph, {}, {});
        QVERIFY(noOverlaps(p, 20));
        QVERIFY(at(p, 7).x > at(p, 6).x);
    }

    void severalRootsAreSideBySideAndAnUnreachedNodeIsAddedOnRequest()
    {
        layout::Graph graph = makeGraph(5, {{0, 1}, {2, 3}});
        graph.nodes[0].bounds = {10, 10, 100, 40};
        layout::TreeOptions options;
        layout::Placement p = layout::treeLayout(graph, {}, options);
        QVERIFY(p.bounds[0] && p.bounds[1] && p.bounds[2] && p.bounds[3]);
        QVERIFY(!p.bounds[4]); // not in a tree
        QCOMPARE(at(p, 0).x, 10.0);
        QVERIFY(at(p, 2).x >= at(p, 0).x + 100 + 20 - 1e-9); // the second tree is to the right of the first
        QCOMPARE(at(p, 2).y, 10.0);                          // and on the same level
        QVERIFY(noOverlaps(p, 20));

        options.includeUnreached = true;
        p = layout::treeLayout(graph, {}, options);
        QVERIFY(p.bounds[4]);
        QVERIFY(noOverlaps(p, 20));
    }

    void anOriginPlacesTheTopLeftOfTheForest()
    {
        layout::Graph graph = makeGraph(3, {{0, 1}, {0, 2}});
        graph.nodes[0].bounds = {400, 400, 100, 40};
        layout::TreeOptions options;
        options.origin = Point{20, 20};
        const layout::Placement p = layout::treeLayout(graph, {}, options);
        const auto box = layout::placedBounds(p);
        QVERIFY(box);
        QCOMPARE(box->x, 20.0);
        QCOMPARE(box->y, 20.0);
    }

    void lockedNodesKeepTheirPositionAndAreStillPartOfTheTree()
    {
        layout::Graph graph = makeGraph(3, {{0, 1}, {0, 2}});
        graph.nodes[2].movable = false;
        const layout::Placement p = layout::treeLayout(graph, {}, {});
        QVERIFY(p.bounds[1]);
        QVERIFY(!p.bounds[2]);
    }

    void cyclesAndSharedChildrenBelongToTheFirstTreeOnly()
    {
        // 0 -> 1 -> 2 -> 0 and 0 -> 2: every node is placed once.
        const layout::Graph graph = makeGraph(3, {{0, 1}, {1, 2}, {2, 0}, {0, 2}});
        const layout::Placement p = layout::treeLayout(graph, {0}, {});
        QVERIFY(p.bounds[0] && p.bounds[1] && p.bounds[2]);
        QVERIFY(noOverlaps(p, 20));
    }

    void theResultDependsOnlyOnTheGraph()
    {
        const layout::Graph graph = makeGraph(7, {{0, 1}, {0, 2}, {1, 3}, {1, 4}, {2, 5}, {2, 6}});
        const layout::Placement a = layout::treeLayout(graph, {}, {});
        const layout::Placement b = layout::treeLayout(graph, {}, {});
        QVERIFY(a.bounds == b.bounds);
    }

    void aThousandNodeTreeIsFast()
    {
        // A tree where node i (i > 0) is the child of (i - 1) / 3, with sizes that vary.
        layout::Graph graph = makeGraph(1000, {});
        for (int i = 1; i < 1000; ++i)
            graph.edges.push_back({"e" + std::to_string(i), (i - 1) / 3, i});
        for (std::size_t i = 0; i < graph.nodes.size(); ++i)
            graph.nodes[i].bounds = {0, 0, 60.0 + static_cast<double>(i % 7) * 10, 30.0 + static_cast<double>(i % 3) * 10};
        QElapsedTimer timer;
        timer.start();
        const layout::Placement p = layout::treeLayout(graph, {}, {});
        const qint64 elapsed = timer.elapsed();
        QVERIFY(noOverlaps(p, 20));
        const double scale = qEnvironmentVariable("QRAW_TEST_TIMEOUT_SCALE", "1").toDouble();
        QVERIFY2(elapsed < 200 * std::max(1.0, scale), qPrintable(QStringLiteral("took %1 ms").arg(elapsed)));
    }
};

QTEST_APPLESS_MAIN(LayoutTreeTest)
#include "tst_layout_tree.moc"
