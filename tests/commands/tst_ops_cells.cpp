// Cell operations (ops_cells.h, removeLayers): behavior on small documents.
// Every case also checks that rolling the transaction back restores the
// saved bytes exactly.
#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_layers.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/document.h"
#include "qraw/core/number.h"
#include "qraw/format/codec.h"

#include <QTest>

using namespace qraw;
using namespace qraw::commands;
using core::Cell;
using core::Page;
using core::Point;

namespace {

Cell vertex(const char* id, const char* parent, core::Rect bounds, const char* style = "")
{
    return Cell::makeVertex(id, parent, id, style, bounds);
}

// An edge parented to the layer, with a relative geometry unless `withGeometry` is false.
Cell edge(const char* id, const char* source, const char* target, bool withGeometry = true)
{
    Cell e = Cell::makeEdge(id, "1", "", "", source, target);
    if (withGeometry)
        e.ensureGeometry().setRelative(true);
    else
        e.setGeometry(std::nullopt); // makeEdge adds one by default
    return e;
}

Page pageOf(std::initializer_list<Cell> cells)
{
    Page page;
    page.setId("p");
    page.appendEntry(Cell("0"));
    page.appendEntry(Cell::makeLayer("1", "0"));
    for (const Cell& c : cells)
        page.appendEntry(c);
    return page;
}

// A document with `page`, an open transaction on it, and the bytes before.
struct Scene {
    explicit Scene(Page page) : doc(make(std::move(page))), before(format::writeDocument(doc)), tx(doc) {}

    static core::Document make(Page page)
    {
        core::Document d;
        d.appendEntry(std::move(page));
        return d;
    }
    [[nodiscard]] const Page& page() const { return *doc.page(0); }
    [[nodiscard]] const Cell& cell(const char* id) const { return *page().findCell(id); }
    [[nodiscard]] std::vector<std::string> order() const
    {
        std::vector<std::string> out;
        for (const Cell* c : page().cells())
            out.push_back(c->id());
        return out;
    }
    [[nodiscard]] bool rolledBack()
    {
        tx.rollback();
        return format::writeDocument(doc) == before;
    }

    core::Document doc;
    QByteArray before;
    Transaction tx;
};

using Ids = std::vector<std::string>;

} // namespace

class TestOpsCells : public QObject {
    Q_OBJECT

private slots:
    // --- addCells -----------------------------------------------------------

    void addAppendsAsLastChild()
    {
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10}), vertex("a1", "A", {1, 1, 2, 2}), vertex("B", "1", {20, 0, 10, 10})}));
        const auto r = ops::addCells(s.tx, 0, {vertex("C", "1", {40, 0, 10, 10})});
        QVERIFY(r.ok);
        QCOMPARE(r.cells, (Ids{"C"}));
        QCOMPARE(s.order(), (Ids{"0", "1", "A", "a1", "B", "C"}));
        QVERIFY(s.rolledBack());
    }

    void addAtIndexLandsBeforeSubtree()
    {
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10}), vertex("a1", "A", {1, 1, 2, 2}), vertex("B", "1", {20, 0, 10, 10})}));
        QVERIFY(ops::addCells(s.tx, 0, {vertex("X", "1", {}), vertex("Y", "1", {})}, 1).ok);
        QCOMPARE(s.order(), (Ids{"0", "1", "A", "a1", "X", "Y", "B"}));
        QVERIFY(ops::addCells(s.tx, 0, {vertex("Z", "1", {})}, 0).ok);
        QCOMPARE(s.order(), (Ids{"0", "1", "Z", "A", "a1", "X", "Y", "B"}));
        QVERIFY(ops::addCells(s.tx, 0, {vertex("W", "1", {})}, 99).ok); // beyond the end: last child
        QCOMPARE(s.order().back(), std::string("W"));
        QVERIFY(s.rolledBack());
    }

    void addChildOfNewParentAndGeneratedId()
    {
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10})}));
        Cell group = vertex("", "1", {0, 0, 50, 50}, "group");
        const auto r = ops::addCells(s.tx, 0, {group, vertex("K", "G", {}), vertex("L", "G", {})});
        QVERIFY(!r.ok); // "G" does not exist yet
        QVERIFY(s.tx.empty());

        group.setId("G");
        const auto ok = ops::addCells(s.tx, 0, {group, vertex("K", "G", {}), vertex("L", "G", {})}, 0);
        QVERIFY(ok.ok);
        QCOMPARE(s.order(), (Ids{"0", "1", "G", "K", "L", "A"}));

        const auto generated = ops::addCells(s.tx, 0, {vertex("", "1", {})});
        QVERIFY(generated.ok);
        QCOMPARE(generated.cells.size(), std::size_t(1));
        QVERIFY(!generated.cells[0].empty());
        QVERIFY(s.page().findCell(generated.cells[0]));
        QVERIFY(s.rolledBack());
    }

    void addRejectsBadInput()
    {
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10})}));
        QVERIFY(!ops::addCells(s.tx, 0, {vertex("A", "1", {})}).ok);                            // id in use
        QVERIFY(!ops::addCells(s.tx, 0, {vertex("N", "1", {}), vertex("N", "1", {})}).ok);      // repeated in batch
        QVERIFY(!ops::addCells(s.tx, 0, {vertex("N", "missing", {})}).ok);                      // unknown parent
        QVERIFY(!ops::addCells(s.tx, 0, {Cell::makeEdge("E", "1", "", "", "nope", "A")}).ok);   // unknown source
        QVERIFY(!ops::addCells(s.tx, 5, {vertex("N", "1", {})}).ok);                            // unknown page
        QVERIFY(s.tx.empty());
    }

    // --- removeCells ----------------------------------------------------------

    void removeKeepsEdgeAndDisconnectsAtCenter()
    {
        // A sits inside group G at (100,100): the point must use G's origin.
        Scene s(pageOf({vertex("G", "1", {100, 100, 300, 300}, "group"), vertex("A", "G", {10, 20, 40, 60}),
                        vertex("B", "1", {400, 0, 50, 50}), edge("E", "A", "B")}));
        const auto r = ops::removeCells(s.tx, 0, {"A"});
        QVERIFY(r.ok);
        QVERIFY(r.cells.empty());
        QVERIFY(!s.page().findCell("A"));
        const Cell& e = s.cell("E");
        QCOMPARE(e.source(), std::string());
        QCOMPARE(e.target(), std::string("B"));
        QCOMPARE(*e.geometry()->sourcePoint(), (Point{130, 150}));
        QVERIFY(!s.page().findCell("G")); // transparent group that lost its only child goes too
        QVERIFY(s.rolledBack());
    }

    void removeIncludeEdgesDeletesEdge()
    {
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10}), vertex("B", "1", {20, 0, 10, 10}), edge("E", "A", "B")}));
        QVERIFY(ops::removeCells(s.tx, 0, {"A"}, {.includeEdges = true}).ok);
        QCOMPARE(s.order(), (Ids{"0", "1", "B"}));
        QVERIFY(s.rolledBack());
    }

    void removeTakesDescendantsAndFreesTheirEdges()
    {
        Scene s(pageOf({vertex("P", "1", {10, 10, 100, 100}, "rounded=1;"), vertex("c", "P", {5, 5, 20, 20}),
                        vertex("B", "1", {300, 0, 10, 10}), edge("E", "B", "c")}));
        QVERIFY(ops::removeCells(s.tx, 0, {"P"}).ok);
        QCOMPARE(s.order(), (Ids{"0", "1", "B", "E"}));
        QCOMPARE(s.cell("E").target(), std::string());
        QCOMPARE(*s.cell("E").geometry()->targetPoint(), (Point{25, 25})); // 10 + 5 + 20/2
        QVERIFY(s.rolledBack());
    }

    void removeSeveralCellsInAnyOrderRollsBackToTheSameBytes()
    {
        // The cells are removed back to front whatever order the ids come in;
        // the result and the rollback must not depend on it.
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10}), vertex("P", "1", {20, 0, 100, 100}, "rounded=1;"),
                        vertex("c", "P", {5, 5, 20, 20}), vertex("B", "1", {200, 0, 10, 10}), vertex("C", "1", {300, 0, 10, 10}),
                        edge("E", "A", "B"), edge("F", "C", "c"), edge("G", "C", "B")}));
        QVERIFY(ops::removeCells(s.tx, 0, {"B", "A", "P"}, {.includeEdges = true}).ok);
        QCOMPARE(s.order(), (Ids{"0", "1", "C"})); // E, F and G touch removed cells or their children
        QVERIFY(s.rolledBack());
    }

    void removeSkipsUndeletable()
    {
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10}, "deletable=0;")}));
        QVERIFY(ops::removeCells(s.tx, 0, {"A"}).ok);
        QVERIFY(s.tx.empty());
        QVERIFY(ops::removeCells(s.tx, 0, {"A"}, {.respectDeletable = false}).ok);
        QVERIFY(!s.page().findCell("A"));
        QVERIFY(!ops::removeCells(s.tx, 0, {"missing"}).ok);
        QVERIFY(s.rolledBack());
    }

    void removeTransparentParentOnlyWhenEmpty()
    {
        Scene s(pageOf({vertex("G", "1", {0, 0, 100, 100}, "group"), vertex("a", "G", {}), vertex("b", "G", {}),
                        vertex("O", "1", {0, 0, 100, 100}, "rounded=1;"), vertex("c", "O", {})}));
        QVERIFY(ops::removeCells(s.tx, 0, {"a"}).ok); // b is left: G stays
        QVERIFY(s.page().findCell("G"));
        QVERIFY(ops::removeCells(s.tx, 0, {"b"}).ok); // G is transparent and now empty
        QVERIFY(!s.page().findCell("G"));
        QVERIFY(ops::removeCells(s.tx, 0, {"c"}).ok); // O is opaque: stays
        QVERIFY(s.page().findCell("O"));
        QVERIFY(s.rolledBack());
    }

    void removeEdgeWithoutGeometryLosesAttribute()
    {
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10}), vertex("B", "1", {20, 0, 10, 10}), edge("E", "A", "B", false)}));
        QVERIFY(ops::removeCells(s.tx, 0, {"A", "B"}).ok);
        const Cell& e = s.cell("E");
        QVERIFY(e.source().empty() && e.target().empty());
        QVERIFY(!e.geometry());
        QVERIFY(s.rolledBack());
    }

    // --- removeLayers ---------------------------------------------------------

    void removeOnlyLayerCreatesNewOne()
    {
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10})}));
        const auto r = ops::removeLayers(s.tx, 0, {"1"});
        QVERIFY(r.ok);
        QCOMPARE(r.cells.size(), std::size_t(1));
        QVERIFY(r.cells[0] != "1");
        QCOMPARE(s.page().layers().size(), std::size_t(1));
        QCOMPARE(s.page().layers()[0]->id(), r.cells[0]);
        QVERIFY(!s.page().findCell("A"));
        QVERIFY(!ops::removeLayers(s.tx, 0, {"0"}).ok); // the root is not a layer
        QVERIFY(s.rolledBack());
    }

    void removeLayerPicksNeighbourAndKeepsForeignEdges()
    {
        Cell layer2 = Cell::makeLayer("2", "0");
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10}), layer2, vertex("B", "2", {40, 40, 20, 20}), edge("E", "A", "B")}));
        const auto r = ops::removeLayers(s.tx, 0, {"2"});
        QVERIFY(r.ok);
        QCOMPARE(r.cells, (Ids{"1"}));
        QVERIFY(!s.page().findCell("B"));
        QCOMPARE(s.cell("E").target(), std::string()); // disconnected, not deleted
        QCOMPARE(*s.cell("E").geometry()->targetPoint(), (Point{50, 50}));
        QVERIFY(s.rolledBack());

        Scene t(pageOf({vertex("A", "1", {0, 0, 10, 10}), layer2}));
        const auto first = ops::removeLayers(t.tx, 0, {"1"});
        QVERIFY(first.ok);
        QCOMPARE(first.cells, (Ids{"2"}));
        QVERIFY(t.rolledBack());
    }

    // --- moveCells ------------------------------------------------------------

    void moveVertex()
    {
        Scene s(pageOf({vertex("A", "1", {10, 20, 40, 60})}));
        const auto r = ops::moveCells(s.tx, 0, {"A"}, 5, -3);
        QVERIFY(r.ok);
        QCOMPARE(r.cells, (Ids{"A"}));
        QCOMPARE(s.cell("A").geometry()->bounds(), (core::Rect{15, 17, 40, 60}));
        QVERIFY(s.rolledBack());
    }

    void moveZeroIsNoOp()
    {
        Scene s(pageOf({vertex("A", "1", {10, 20, 40, 60})}));
        const auto r = ops::moveCells(s.tx, 0, {"A"}, 0, 0);
        QVERIFY(r.ok);
        QVERIFY(s.tx.empty());
        QVERIFY(!ops::moveCells(s.tx, 0, {"missing"}, 1, 1).ok);
    }

    void moveEdgeDisconnectsFreeTerminalAndTranslatesPoints()
    {
        Cell e = edge("E", "A", "B");
        e.geometry()->setPoints({{50, 50}});
        e.geometry()->setTargetPoint(Point{1, 1}); // replaced by B's center
        Scene s(pageOf({vertex("A", "1", {0, 0, 40, 40}), vertex("B", "1", {200, 0, 40, 40}), e}));
        QVERIFY(ops::moveCells(s.tx, 0, {"A", "E"}, 10, 10).ok);
        const Cell& moved = s.cell("E");
        QCOMPARE(moved.source(), std::string("A"));
        QCOMPARE(moved.target(), std::string());
        QCOMPARE(*moved.geometry()->targetPoint(), (Point{230, 30})); // center (220,20) moved along
        QCOMPARE(moved.geometry()->points(), (std::vector<Point>{{60, 60}}));
        QCOMPARE(s.cell("A").geometry()->x(), 10.0);
        QVERIFY(s.rolledBack());

        Scene keep(pageOf({vertex("A", "1", {0, 0, 40, 40}), vertex("B", "1", {200, 0, 40, 40}), e}));
        QVERIFY(ops::moveCells(keep.tx, 0, {"E"}, 10, 10, {.disconnect = false}).ok);
        QCOMPARE(keep.cell("E").target(), std::string("B"));
    }

    void moveDropsEdgeLabelOfMovedTerminal()
    {
        Cell label = Cell::makeVertex("L", "E", "label", "edgeLabel;", {0.5, 0, 0, 0});
        label.geometry()->setRelative(true);
        Scene s(pageOf({vertex("A", "1", {0, 0, 40, 40}), vertex("B", "1", {200, 0, 40, 40}), edge("E", "A", "B"), label}));
        const auto r = ops::moveCells(s.tx, 0, {"A", "L"}, 10, 10);
        QVERIFY(r.ok);
        QCOMPARE(r.cells, (Ids{"A"}));
        QVERIFY(!s.cell("L").geometry()->offset());
        QVERIFY(s.rolledBack());
    }

    void moveRelativeChildOfRotatedParentRoundsOffset()
    {
        Cell child = Cell::makeVertex("C", "P", "", "", {0.5, 0, 0, 0});
        child.geometry()->setRelative(true);
        Scene s(pageOf({vertex("P", "1", {0, 0, 100, 100}, "rotation=90;"), child}));
        QVERIFY(ops::moveCells(s.tx, 0, {"C"}, 10, 0).ok);
        QCOMPARE(*s.cell("C").geometry()->offset(), (Point{0, -10}));
        QVERIFY(s.rolledBack());
    }

    // --- resizeCells ----------------------------------------------------------

    void resizeSetsBoundsAndOffset()
    {
        Cell label = Cell::makeVertex("L", "E", "", "", {0.5, 0, 0, 0});
        label.geometry()->setRelative(true);
        label.geometry()->setOffset(Point{1, 2});
        Scene s(pageOf({vertex("A", "1", {10, 20, 40, 60}), edge("E", "A", "A"), label}));
        QVERIFY(!ops::resizeCells(s.tx, 0, {"A"}, {}).ok); // size mismatch
        QVERIFY(ops::resizeCells(s.tx, 0, {"A", "L"}, {{15, 20, 80, 90}, {3, 4, 20, 10}}).ok);
        QCOMPARE(s.cell("A").geometry()->bounds(), (core::Rect{15, 20, 80, 90}));
        const auto& lg = *s.cell("L").geometry();
        QCOMPARE(lg.x(), 0.5);
        QCOMPARE(*lg.offset(), (Point{3.5, 6}));
        QCOMPARE(lg.width(), 20.0);
        QVERIFY(s.rolledBack());
    }

    void resizeExtendsParentsRecursively()
    {
        Scene s(pageOf({vertex("G", "1", {0, 0, 100, 100}), vertex("P", "G", {10, 10, 50, 50}), vertex("C", "P", {10, 10, 20, 20})}));
        QVERIFY(ops::resizeCells(s.tx, 0, {"C"}, {{60, 60, 100, 100}}).ok);
        QCOMPARE(s.cell("P").geometry()->bounds(), (core::Rect{10, 10, 160, 160}));
        QCOMPARE(s.cell("G").geometry()->bounds(), (core::Rect{0, 0, 170, 170}));
        QVERIFY(s.rolledBack());
    }

    // --- connections ----------------------------------------------------------

    void connectWritesAndClearsConstraint()
    {
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10}), vertex("B", "1", {20, 0, 10, 10}), edge("E", "", "")}));
        QVERIFY(ops::connectEdge(s.tx, 0, "E", "A", true, ops::ConnectionConstraint{Point{1, 0.5}, 0, 5, false}).ok);
        const Cell& e = s.cell("E");
        QCOMPARE(e.source(), std::string("A"));
        const core::Style style = e.style();
        QCOMPARE(style.value("exitX", ""), std::string("1"));
        QCOMPARE(style.value("exitY", ""), std::string("0.5"));
        QCOMPARE(style.value("exitDx", ""), std::string("0"));
        QCOMPARE(style.value("exitDy", ""), std::string("5"));
        QCOMPARE(style.value("exitPerimeter", ""), std::string("0"));

        QVERIFY(ops::connectEdge(s.tx, 0, "E", "A", true, ops::ConnectionConstraint{}).ok); // floating
        QVERIFY(s.cell("E").styleText().empty());
        QVERIFY(ops::connectEdge(s.tx, 0, "E", "B", false, ops::ConnectionConstraint{Point{0, 0}, 0, 0, true}).ok);
        QCOMPARE(s.cell("E").style().value("entryX", "?"), std::string("0"));
        QVERIFY(!s.cell("E").style().has("entryPerimeter"));
        QVERIFY(s.rolledBack());
    }

    void connectRejectsInvalidTerminals()
    {
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10}), edge("E", "", "")}));
        QVERIFY(!ops::connectEdge(s.tx, 0, "E", "missing", true).ok);
        QVERIFY(!ops::connectEdge(s.tx, 0, "E", "E", false).ok);
        QVERIFY(!ops::connectEdge(s.tx, 0, "E", "1", true).ok); // a layer
        QVERIFY(!ops::connectEdge(s.tx, 0, "A", "A", true).ok); // not an edge
        QVERIFY(s.tx.empty());
    }

    void disconnectStoresPoint()
    {
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10}), edge("E", "A", "A", false)}));
        QVERIFY(ops::disconnectEdge(s.tx, 0, "E", true, {7, 8}).ok);
        const Cell& e = s.cell("E");
        QCOMPARE(e.source(), std::string());
        QCOMPARE(e.target(), std::string("A"));
        QVERIFY(e.geometry()->relative());
        QCOMPARE(*e.geometry()->sourcePoint(), (Point{7, 8}));
        QVERIFY(!ops::disconnectEdge(s.tx, 0, "A", true, {0, 0}).ok);
        QVERIFY(s.rolledBack());
    }

    void waypoints()
    {
        Scene s(pageOf({vertex("A", "1", {0, 0, 10, 10}), edge("E", "A", "A", false)}));
        QVERIFY(ops::setWaypoints(s.tx, 0, "E", {}).ok); // nothing to remove, nothing to create
        QVERIFY(s.tx.empty());
        QVERIFY(ops::setWaypoints(s.tx, 0, "E", {{1, 2}, {3, 4}}).ok);
        QCOMPARE(s.cell("E").geometry()->points(), (std::vector<Point>{{1, 2}, {3, 4}}));
        const auto commands = s.tx.savepoint();
        QVERIFY(ops::setWaypoints(s.tx, 0, "E", {{1, 2}, {3, 4}}).ok);
        QCOMPARE(s.tx.savepoint(), commands); // same points: no command
        QVERIFY(ops::setWaypoints(s.tx, 0, "E", {}).ok);
        QVERIFY(s.cell("E").geometry()->points().empty());
        QVERIFY(!ops::setWaypoints(s.tx, 0, "A", {{1, 1}}).ok);
        QVERIFY(s.rolledBack());
    }
};

QTEST_GUILESS_MAIN(TestOpsCells)
#include "tst_ops_cells.moc"
