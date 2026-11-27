// Structure operations: z-order, edge parents, reparent, group, ungroup.
// Every case also checks that rolling the transaction back restores the
// document byte for byte.
#include "qraw/commands/ops_structure.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/document.h"
#include "qraw/format/codec.h"

#include <QElapsedTimer>
#include <QTest>

#ifndef QRAW_TEST_TIMEOUT_SCALE
#define QRAW_TEST_TIMEOUT_SCALE 1 // 3 in the asan preset
#endif

using namespace qraw;
using namespace qraw::commands;
using core::Cell;
using core::Page;
using core::Rect;

namespace {

Cell vertex(const char* id, const char* parent, Rect bounds, const char* style = "")
{
    return Cell::makeVertex(id, parent, "", style, bounds);
}

// An open edit on a one-page document <root>: 0, layer 1, then `build`'s cells.
class Edit {
public:
    template <class Build>
    explicit Edit(Build&& build) : doc_(makeDocument(build)), before_(format::writeDocument(doc_)), tx_(doc_)
    {
    }

    Transaction& tx() { return tx_; }
    [[nodiscard]] const Page& page() const { return *doc_.page(0); }
    [[nodiscard]] const Cell* cell(const char* id) const { return page().findCell(id); }

    // Entry order as "a,b,#" where # is a foreign node.
    [[nodiscard]] QString order() const
    {
        QStringList ids;
        for (const auto& entry : page().rootEntries())
            ids << (std::holds_alternative<Cell>(entry) ? QString::fromStdString(std::get<Cell>(entry).id()) : "#");
        return ids.join(',');
    }
    [[nodiscard]] QString childrenOf(const char* parent) const
    {
        QStringList ids;
        for (const Cell* c : page().children(parent))
            ids << QString::fromStdString(c->id());
        return ids.join(',');
    }
    [[nodiscard]] QString parentOf(const char* id) const { return QString::fromStdString(cell(id)->parent()); }
    [[nodiscard]] QString styleOf(const char* id) const { return QString::fromStdString(cell(id)->styleText()); }
    [[nodiscard]] QString styleValue(const char* id, const char* key) const
    {
        return QString::fromStdString(cell(id)->style().value(key, ""));
    }
    [[nodiscard]] bool bounds(const char* id, Rect expected) const
    {
        const Cell* c = cell(id);
        return c && c->geometry() && c->geometry()->bounds() == expected;
    }

    // Rolls back and verifies the document is byte-identical to the start.
    [[nodiscard]] bool rollbackRestores()
    {
        tx_.rollback();
        return format::writeDocument(doc_) == before_;
    }

private:
    template <class Build>
    static core::Document makeDocument(Build&& build)
    {
        Page page;
        page.setId("p1");
        page.appendEntry(Cell("0"));
        page.appendEntry(Cell::makeLayer("1", "0"));
        build(page);
        core::Document doc;
        doc.appendEntry(std::move(page));
        return doc;
    }

    core::Document doc_;
    QByteArray before_;
    Transaction tx_;
};

void abc(Page& page)
{
    page.appendEntry(vertex("a", "1", {0, 0, 10, 10}));
    page.appendEntry(vertex("b", "1", {20, 0, 10, 10}));
    page.appendEntry(vertex("c", "1", {40, 0, 10, 10}));
}

// a (10,10,20,20) and b (50,40,10,10) joined by e1 (waypoint 30,30), a -> c
// by e2, c outside at (200,200).
void groupable(Page& page)
{
    page.appendEntry(vertex("a", "1", {10, 10, 20, 20}));
    page.appendEntry(vertex("b", "1", {50, 40, 10, 10}));
    page.appendEntry(vertex("c", "1", {200, 200, 10, 10}));
    Cell e1 = Cell::makeEdge("e1", "1", "", "", "a", "b");
    e1.ensureGeometry().setRelative(true);
    e1.geometry()->setPoints({{30, 30}});
    page.appendEntry(std::move(e1));
    Cell e2 = Cell::makeEdge("e2", "1", "", "", "a", "c");
    e2.ensureGeometry().setRelative(true);
    page.appendEntry(std::move(e2));
}

} // namespace

class TestOpsStructure : public QObject {
    Q_OBJECT

private slots:
    void orderToFront()
    {
        Edit e(abc);
        const Result r = ops::orderCells(e.tx(), 0, {"a"}, ops::ZOrder::ToFront);
        QVERIFY2(r.ok, r.error.c_str());
        QCOMPARE(e.order(), "0,1,b,c,a");
        QVERIFY(e.rollbackRestores());
    }

    void orderSendBackward()
    {
        Edit e(abc);
        QVERIFY(ops::orderCells(e.tx(), 0, {"c"}, ops::ZOrder::SendBackward).ok);
        QCOMPARE(e.order(), "0,1,a,c,b");
        QVERIFY(e.rollbackRestores());
    }

    void orderBringForward()
    {
        Edit e(abc);
        QVERIFY(ops::orderCells(e.tx(), 0, {"a"}, ops::ZOrder::BringForward).ok);
        QCOMPARE(e.order(), "0,1,b,a,c");
        QVERIFY(e.rollbackRestores());
    }

    void orderToBackKeepsRelativeOrder()
    {
        Edit e(abc);
        const Result r = ops::orderCells(e.tx(), 0, {"c", "b"}, ops::ZOrder::ToBack);
        QVERIFY(r.ok);
        QCOMPARE(e.order(), "0,1,b,c,a");
        QCOMPARE(r.cells, (std::vector<std::string>{"b", "c"}));
        QVERIFY(e.rollbackRestores());
    }

    void orderMovesSubtree()
    {
        Edit e([](Page& page) {
            page.appendEntry(vertex("a", "1", {0, 0, 50, 50}));
            page.appendEntry(vertex("a1", "a", {1, 1, 5, 5}));
            page.appendEntry(vertex("b", "1", {60, 0, 10, 10}));
        });
        QVERIFY(ops::orderCells(e.tx(), 0, {"a"}, ops::ZOrder::ToFront).ok);
        QCOMPARE(e.order(), "0,1,b,a,a1");
        QVERIFY(e.rollbackRestores());
    }

    void orderNoopEmitsNothing()
    {
        Edit e(abc);
        QVERIFY(ops::orderCells(e.tx(), 0, {"c"}, ops::ZOrder::ToFront).ok);
        QVERIFY(ops::orderCells(e.tx(), 0, {"a"}, ops::ZOrder::ToBack).ok);
        QVERIFY(e.tx().empty());
    }

    void orderLeavesForeignEntriesInPlace()
    {
        Edit e([](Page& page) {
            page.appendEntry(vertex("a", "1", {0, 0, 10, 10}));
            page.appendEntry(core::XmlNode::element("x"));
            page.appendEntry(vertex("b", "1", {20, 0, 10, 10}));
        });
        QVERIFY(ops::orderCells(e.tx(), 0, {"a"}, ops::ZOrder::ToFront).ok);
        QCOMPARE(e.childrenOf("1"), "b,a");
        QCOMPARE(e.order(), "0,1,#,b,a"); // the foreign node is not moved with a
        QVERIFY(e.rollbackRestores());
    }

    void orderUnknownCellFails()
    {
        Edit e(abc);
        QVERIFY(!ops::orderCells(e.tx(), 0, {"nope"}, ops::ZOrder::ToFront).ok);
        QVERIFY(e.tx().empty());
    }

    void groupTwoVertices()
    {
        Edit e(groupable);
        const Result r = ops::groupCells(e.tx(), 0, {"b", "a"});
        QVERIFY2(r.ok, r.error.c_str());
        QCOMPARE(r.cells.size(), std::size_t(1));
        const char* g = r.cells.front().c_str();
        QVERIFY(e.bounds(g, {10, 10, 50, 40}));
        QCOMPARE(e.styleOf(g), "group");
        QVERIFY(!e.cell(g)->isConnectable());
        QCOMPARE(e.parentOf(g), "1");
        QVERIFY(e.bounds("a", {0, 0, 20, 20}));
        QVERIFY(e.bounds("b", {40, 30, 10, 10}));
        // The group follows the former last sibling; e1 joined the group,
        // e2 (to the outside vertex c) stayed in the layer.
        QCOMPARE(e.order(), QString("0,1,c,e2,%1,a,b,e1").arg(g));
        QCOMPARE(e.parentOf("e1"), QString(g));
        QCOMPARE(e.parentOf("e2"), "1");
        const auto points = e.cell("e1")->geometry()->points();
        QCOMPARE(points.size(), std::size_t(1));
        QCOMPARE(points.front().x, 20.0);
        QCOMPARE(points.front().y, 20.0);
        QVERIFY(e.rollbackRestores());
    }

    void groupSingleCellBecomesContainer()
    {
        Edit e(abc);
        const Result r = ops::groupCells(e.tx(), 0, {"b"});
        QVERIFY(r.ok);
        QCOMPARE(r.cells, (std::vector<std::string>{"b"}));
        QCOMPARE(e.styleValue("b", "container"), "1");
        QCOMPARE(e.order(), "0,1,a,b,c");
        QVERIFY(e.rollbackRestores());
    }

    void groupNeedsTwoCellsWithTheSameParent()
    {
        Edit e([](Page& page) {
            page.appendEntry(vertex("a", "1", {0, 0, 10, 10}));
            page.appendEntry(vertex("b", "1", {20, 0, 10, 10}));
            page.appendEntry(vertex("b1", "b", {1, 1, 5, 5}));
        });
        // b1 has another parent than a: only one cell is left.
        QVERIFY(!ops::groupCells(e.tx(), 0, {"a", "b1"}).ok);
        QVERIFY(e.rollbackRestores());
    }

    void ungroupRestoresAbsolutePositions()
    {
        Edit e(groupable);
        const Result grouped = ops::groupCells(e.tx(), 0, {"a", "b"});
        QVERIFY(grouped.ok);
        const Result r = ops::ungroupCells(e.tx(), 0, grouped.cells);
        QVERIFY2(r.ok, r.error.c_str());
        QVERIFY(!e.cell(grouped.cells.front().c_str())); // transparent group removed
        QVERIFY(e.bounds("a", {10, 10, 20, 20}));
        QVERIFY(e.bounds("b", {50, 40, 10, 10}));
        QCOMPARE(e.parentOf("e1"), "1");
        QCOMPARE(e.cell("e1")->geometry()->points().front().x, 30.0);
        QCOMPARE(e.childrenOf("1"), "c,e2,a,b,e1");
        QCOMPARE(r.cells, (std::vector<std::string>{"a", "b", "e1"}));
        QVERIFY(e.rollbackRestores());
    }

    void groupAndUngroupOfThousandsOfCellsAreExactAndFast()
    {
        constexpr int kVertices = 4000;
        // A grid of vertices with an edge between neighbours (not part of the selection), some
        // foreign nodes, and one vertex outside the group that two edges lead to.
        Edit e([](Page& page) {
            for (int i = 0; i < kVertices; ++i) {
                page.appendEntry(vertex(("v" + std::to_string(i)).c_str(), "1",
                                        {double(i % 80) * 30 + 5, double(i / 80) * 30 + 5, 20, 20}));
                if (i % 500 == 0)
                    page.appendEntry(core::XmlNode::element("custom", {{"keep", "me"}}));
            }
            for (int i = 0; i + 1 < kVertices; i += 2)
                page.appendEntry(Cell::makeEdge(("e" + std::to_string(i)).c_str(), "1", "", "",
                                                "v" + std::to_string(i), "v" + std::to_string(i + 1)));
            page.appendEntry(vertex("out", "1", {5000, 5000, 20, 20}));
            page.appendEntry(Cell::makeEdge("toOut", "1", "", "", "v0", "out"));
            page.appendEntry(Cell::makeEdge("toOut2", "1", "", "", "v1", "out"));
        });
        std::vector<std::string> ids;
        for (int i = 0; i < kVertices; ++i)
            ids.push_back("v" + std::to_string(i));

        QElapsedTimer timer;
        timer.start();
        const Result grouped = ops::groupCells(e.tx(), 0, ids);
        QVERIFY2(grouped.ok, grouped.error.c_str());
        const char* g = grouped.cells.front().c_str();
        // Every vertex and every edge between two of them is in the group, in the order of
        // the selection; the edges to the outside stay in the layer.
        QCOMPARE(e.page().childIds(g).size(), std::size_t(kVertices + kVertices / 2));
        QCOMPARE(e.page().childIds(g).front(), std::string("v0"));
        QCOMPARE(e.parentOf("e100"), QString(g));
        QCOMPARE(e.parentOf("toOut"), "1");
        const Cell* v81 = e.cell("v81");
        QCOMPARE(v81->parent(), std::string(g));
        // The group is a vertex of the page: its bounds start at the corner of the vertices (border 0).
        QCOMPARE(e.cell(g)->geometry()->x(), 5.0);
        QCOMPARE(v81->geometry()->x(), double(1 % 80) * 30);
        QCOMPARE(v81->geometry()->y(), 30.0);

        const Result ungrouped = ops::ungroupCells(e.tx(), 0, {g});
        QVERIFY2(ungrouped.ok, ungrouped.error.c_str());
        QVERIFY(!e.cell(g));
        QCOMPARE(e.cell("v81")->parent(), std::string("1"));
        QCOMPARE(e.cell("v81")->geometry()->x(), double(1 % 80) * 30 + 5);
        QCOMPARE(e.cell("v81")->geometry()->y(), double(81 / 80) * 30 + 5);
        QCOMPARE(e.page().childIds("1").size(), std::size_t(kVertices + kVertices / 2 + 3));
        // The page is as it was, except that the cells now come after the foreign nodes.
        QCOMPARE(e.parentOf("e100"), "1");
        const qint64 ms = timer.elapsed();
        qInfo() << "group and ungroup of" << kVertices << "cells:" << ms << "ms";
        QVERIFY2(ms < 5000 * QRAW_TEST_TIMEOUT_SCALE, qPrintable(QStringLiteral("%1 ms").arg(ms)));
        QVERIFY(e.rollbackRestores());
    }

    void ungroupRemovesEdgeConnectedToTransparentGroup()
    {
        Edit e([](Page& page) {
            page.appendEntry(vertex("c", "1", {200, 200, 10, 10}));
            page.appendEntry(vertex("g", "1", {10, 10, 50, 40}, "group"));
            page.appendEntry(vertex("a", "g", {5, 5, 10, 10}));
            page.appendEntry(Cell::makeEdge("eg", "1", "", "", "c", "g"));
        });
        QVERIFY(ops::ungroupCells(e.tx(), 0, {"g"}).ok);
        QVERIFY(!e.cell("g"));
        QVERIFY(!e.cell("eg"));
        QVERIFY(e.bounds("a", {15, 15, 10, 10}));
        QCOMPARE(e.order(), "0,1,c,a");
        QVERIFY(e.rollbackRestores());
    }

    void ungroupKeepsOpaqueGroup()
    {
        Edit e([](Page& page) {
            page.appendEntry(vertex("g", "1", {10, 10, 50, 40}, "rounded=1"));
            page.appendEntry(vertex("a", "g", {5, 5, 10, 10}));
        });
        const Result r = ops::ungroupCells(e.tx(), 0, {"g"});
        QVERIFY(r.ok);
        QVERIFY(e.cell("g"));
        QCOMPARE(e.parentOf("a"), "1");
        QCOMPARE(e.styleValue("g", "container"), "0");
        QCOMPARE(r.cells, (std::vector<std::string>{"a", "g"}));
        QVERIFY(e.rollbackRestores());
    }

    void ungroupMakesRelativeChildAbsolute()
    {
        Edit e([](Page& page) {
            page.appendEntry(vertex("g", "1", {100, 100, 200, 100}, "rounded=1"));
            Cell r = vertex("r", "g", {0.5, 0.5, 10, 10});
            r.geometry()->setRelative(true);
            r.geometry()->setOffset(core::Point{5, 5});
            page.appendEntry(std::move(r));
        });
        QVERIFY(ops::ungroupCells(e.tx(), 0, {"g"}).ok);
        QVERIFY(!e.cell("r")->geometry()->relative());
        QVERIFY(e.bounds("r", {205, 155, 10, 10}));
        QVERIFY(e.rollbackRestores());
    }

    void reparentKeepsAbsolutePosition()
    {
        Edit e([](Page& page) {
            page.appendEntry(vertex("c", "1", {50, 50, 200, 200}, "container=1"));
            page.appendEntry(vertex("v", "1", {100, 100, 20, 20}));
        });
        QVERIFY(ops::reparentCells(e.tx(), 0, {"v"}, "c").ok);
        QCOMPARE(e.parentOf("v"), "c");
        QVERIFY(e.bounds("v", {50, 50, 20, 20}));
        QCOMPARE(e.order(), "0,1,c,v");

        QVERIFY(ops::reparentCells(e.tx(), 0, {"v"}, "1").ok);
        QVERIFY(e.bounds("v", {100, 100, 20, 20}));
        QCOMPARE(e.childrenOf("1"), "c,v");
        QVERIFY(e.rollbackRestores());
    }

    void reparentIntoDescendantFails()
    {
        Edit e([](Page& page) {
            page.appendEntry(vertex("c", "1", {50, 50, 200, 200}));
            page.appendEntry(vertex("v", "c", {10, 10, 20, 20}));
        });
        QVERIFY(!ops::reparentCells(e.tx(), 0, {"c"}, "v").ok);
        QVERIFY(!ops::reparentCells(e.tx(), 0, {"c"}, "c").ok);
        QVERIFY(e.tx().empty());
    }

    void reparentAtIndex()
    {
        Edit e([](Page& page) {
            page.appendEntry(vertex("c", "1", {50, 50, 200, 200}));
            page.appendEntry(vertex("w1", "c", {10, 10, 20, 20}));
            page.appendEntry(vertex("w2", "c", {40, 10, 20, 20}));
            page.appendEntry(vertex("v", "1", {100, 100, 20, 20}));
        });
        QVERIFY(ops::reparentCells(e.tx(), 0, {"v"}, "c", 0).ok);
        QCOMPARE(e.childrenOf("c"), "v,w1,w2");
        QCOMPARE(e.order(), "0,1,c,v,w1,w2");
        QVERIFY(e.rollbackRestores());
    }

    void reparentMovesConnectedEdgeToCommonAncestor()
    {
        Edit e([](Page& page) {
            page.appendEntry(vertex("c", "1", {50, 50, 200, 200}));
            page.appendEntry(vertex("w", "c", {10, 10, 20, 20}));
            page.appendEntry(vertex("v", "1", {100, 100, 20, 20}));
            Cell edge = Cell::makeEdge("e", "1", "", "", "v", "w");
            edge.ensureGeometry().setRelative(true);
            edge.geometry()->setPoints({{120, 120}});
            page.appendEntry(std::move(edge));
        });
        QVERIFY(ops::reparentCells(e.tx(), 0, {"v"}, "c").ok);
        QCOMPARE(e.parentOf("e"), "c");
        const auto points = e.cell("e")->geometry()->points();
        QCOMPARE(points.front().x, 70.0);
        QCOMPARE(points.front().y, 70.0);
        QCOMPARE(e.childrenOf("c"), "w,v,e");
        QVERIFY(e.rollbackRestores());
    }

    void removeFromGroupMovesToLayer()
    {
        Edit e([](Page& page) {
            page.appendEntry(vertex("g", "1", {50, 50, 200, 200}));
            page.appendEntry(vertex("v", "g", {10, 10, 20, 20}));
        });
        QVERIFY(ops::removeFromGroup(e.tx(), 0, {"v"}).ok);
        QCOMPARE(e.parentOf("v"), "1");
        QVERIFY(e.bounds("v", {60, 60, 20, 20}));
        QVERIFY(e.rollbackRestores());
    }
};

QTEST_GUILESS_MAIN(TestOpsStructure)
#include "tst_ops_structure.moc"
