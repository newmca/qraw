// Child layouts (ops_layout.h): stackLayout results are derived by hand from
// mxStackLayout.execute (mxgraph/src/layout/mxStackLayout.js) with the
// Graph.js:12656 configuration; collapse/expand (ops::foldCells) is here too
// as it shares the swimlane helpers. Every case checks that rolling back
// restores the document byte for byte.
#include "qraw/commands/change_set.h"
#include "qraw/commands/ops_layout.h"
#include "qraw/commands/ops_structure.h"
#include "qraw/commands/primitives.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/document.h"
#include "qraw/format/codec.h"

#include <QTest>

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
    [[nodiscard]] Rect bounds(const char* id) const { return cell(id)->geometry()->bounds(); }
    [[nodiscard]] QString childrenOf(const char* parent) const
    {
        QStringList ids;
        for (const Cell* c : page().children(parent))
            ids << QString::fromStdString(c->id());
        return ids.join(',');
    }
    [[nodiscard]] bool rollbackRestores()
    {
        tx_.rollback();
        return format::writeDocument(doc_) == before_;
    }

    // The layout manager's input after an edit that touched `ids`.
    static ChangeSet changed(std::initializer_list<const char*> ids)
    {
        ChangeSet changes;
        for (const char* id : ids)
            changes.cellChanged("p1", id);
        return changes;
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

} // namespace

class OpsLayoutTest : public QObject {
    Q_OBJECT

private slots:
    void verticalStackBelowSwimlaneTitle()
    {
        // Vertical stack, title 30 on top: children start at y0 = 30, take the
        // parent width (fill), follow each other without gaps; the parent ends
        // at last.y + last.height + marginBottom (resizeParentMax=0).
        Edit e([](Page& p) {
            p.appendEntry(vertex("s", "1", {100, 100, 200, 300},
                                 "swimlane;startSize=30;childLayout=stackLayout;horizontalStack=0;resizeParentMax=0;marginBottom=10;"));
            p.appendEntry(vertex("c1", "s", {50, 60, 100, 40}));
            p.appendEntry(vertex("c2", "s", {0, 0, 80, 20}));
            p.appendEntry(vertex("c3", "s", {10, 500, 60, 30}));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, Edit::changed({"c2"})).ok);
        QCOMPARE(e.bounds("c1"), (Rect{0, 30, 200, 40}));
        QCOMPARE(e.bounds("c2"), (Rect{0, 70, 200, 20}));
        QCOMPARE(e.bounds("c3"), (Rect{0, 90, 200, 30}));
        QCOMPARE(e.bounds("s"), (Rect{100, 100, 200, 130}));
        QVERIFY(e.rollbackRestores());
    }

    void horizontalStackSpacingBorderMarginsGrowParent()
    {
        // x0 = border + marginLeft = 14, y0 = border + marginTop = 12; fill height
        // = 100 - 2 - 6 - 2*10 = 72; b.x = snap(64 + 5 - 4) + 4 = 69; the parent
        // grows to last.x + last.width + marginRight + border = 129 + 8 + 10.
        Edit e([](Page& p) {
            p.appendEntry(vertex("s", "1", {0, 0, 100, 100},
                                 "childLayout=stackLayout;stackSpacing=5;stackBorder=10;marginLeft=4;marginTop=2;"
                                 "marginBottom=6;marginRight=8;"));
            p.appendEntry(vertex("a", "s", {0, 0, 50, 20}));
            p.appendEntry(vertex("b", "s", {0, 0, 60, 30}));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, Edit::changed({"a"})).ok);
        QCOMPARE(e.bounds("a"), (Rect{14, 12, 50, 72}));
        QCOMPARE(e.bounds("b"), (Rect{69, 12, 60, 72}));
        QCOMPARE(e.bounds("s"), (Rect{0, 0, 147, 100}));
        QVERIFY(e.rollbackRestores());
    }

    void resizeParentMaxKeepsALargerParent()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("s", "1", {0, 0, 400, 100}, "childLayout=stackLayout;"));
            p.appendEntry(vertex("a", "s", {0, 0, 50, 20}));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, Edit::changed({"a"})).ok);
        QCOMPARE(e.bounds("s"), (Rect{0, 0, 400, 100}));
        QCOMPARE(e.bounds("a"), (Rect{0, 0, 50, 100}));
        QVERIFY(e.rollbackRestores());
    }

    void resizeLastTakesTheRemainingSpace()
    {
        // last.height = 200 - last.y(34) - spacing(4) = 162.
        Edit e([](Page& p) {
            p.appendEntry(vertex("s", "1", {0, 0, 100, 200},
                                 "childLayout=stackLayout;horizontalStack=0;resizeParent=0;resizeLast=1;stackSpacing=4;"));
            p.appendEntry(vertex("a", "s", {0, 0, 100, 30}));
            p.appendEntry(vertex("b", "s", {0, 0, 100, 30}));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, Edit::changed({"b"})).ok);
        QCOMPARE(e.bounds("a"), (Rect{0, 0, 100, 30}));
        QCOMPARE(e.bounds("b"), (Rect{0, 34, 100, 162}));
        QCOMPARE(e.bounds("s"), (Rect{0, 0, 100, 200}));
        QVERIFY(e.rollbackRestores());
    }

    void sideTitleShiftsAHorizontalStack()
    {
        // horizontal=0 puts the title on the left: x0 = startSize.
        Edit e([](Page& p) {
            p.appendEntry(vertex("s", "1", {0, 0, 300, 100}, "swimlane;horizontal=0;startSize=20;childLayout=stackLayout;"));
            p.appendEntry(vertex("a", "s", {0, 0, 50, 20}));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, Edit::changed({"a"})).ok);
        QCOMPARE(e.bounds("a"), (Rect{20, 0, 50, 100}));
        QVERIFY(e.rollbackRestores());
    }

    void allowGapsKeepsPositionsOnTheStackUnitGrid()
    {
        // allowGaps: ordered by x. a.x = snap(40) = 40 (> x0 = 0), a.width =
        // snap(50) = 40 (mod 10 is not above half the unit); b.x = snap(max(40 + 40,
        // 100)) = 100, b.width = 60.
        Edit e([](Page& p) {
            p.appendEntry(vertex("s", "1", {0, 0, 500, 100}, "childLayout=stackLayout;allowGaps=1;stackUnitSize=20;"));
            p.appendEntry(vertex("b", "s", {100, 0, 60, 20}));
            p.appendEntry(vertex("a", "s", {40, 0, 50, 20}));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, Edit::changed({"a"})).ok);
        QCOMPARE(e.bounds("a"), (Rect{40, 0, 40, 100}));
        QCOMPARE(e.bounds("b"), (Rect{100, 0, 60, 100}));
        QVERIFY(e.rollbackRestores());
    }

    void nothingChangesWithoutALayoutParent()
    {
        Edit e([](Page& p) { p.appendEntry(vertex("a", "1", {5, 5, 50, 20})); });
        QVERIFY(ops::applyLayouts(e.tx(), 0, Edit::changed({"a"})).ok);
        QVERIFY(e.tx().empty());
    }

    void unsupportedLayoutsAreLeftAlone()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("s", "1", {0, 0, 100, 100}, "childLayout=rack;"));
            p.appendEntry(vertex("a", "s", {7, 7, 50, 20}));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, Edit::changed({"a"})).ok);
        QVERIFY(e.tx().empty());
    }

    void removedChildrenAreFoundThroughTheirFormerParent()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("s", "1", {0, 0, 100, 100}, "childLayout=stackLayout;horizontalStack=0;resizeParentMax=0;"));
            p.appendEntry(vertex("a", "s", {0, 0, 100, 30}));
            p.appendEntry(vertex("b", "s", {0, 30, 100, 30}));
            p.appendEntry(vertex("c", "s", {0, 60, 100, 30}));
        });
        QVERIFY(e.tx().emplace<RemoveCell>(0, "b"));
        QVERIFY(ops::applyLayouts(e.tx(), 0, e.tx().changes()).ok);
        QCOMPARE(e.bounds("c"), (Rect{0, 30, 100, 30}));
        QCOMPARE(e.bounds("s"), (Rect{0, 0, 100, 60}));
        QVERIFY(e.rollbackRestores());
    }

    void nestedLayoutsRunInnerFirst()
    {
        // The inner column grows to its two children (60), the outer row then
        // gives it the row's height as fill and places the next column after it.
        Edit e([](Page& p) {
            p.appendEntry(vertex("row", "1", {0, 0, 10, 10}, "childLayout=stackLayout;resizeParent=1;"));
            p.appendEntry(vertex("col", "row", {0, 0, 80, 10}, "childLayout=stackLayout;horizontalStack=0;resizeParentMax=0;"));
            p.appendEntry(vertex("x", "col", {0, 0, 80, 20}));
            p.appendEntry(vertex("y", "col", {0, 0, 80, 40}));
            p.appendEntry(vertex("col2", "row", {0, 0, 50, 10}));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, Edit::changed({"x"})).ok);
        QCOMPARE(e.bounds("y"), (Rect{0, 20, 80, 40}));
        QCOMPARE(e.bounds("col").height, 60.0);
        QCOMPARE(e.bounds("col2").x, 80.0);
        QVERIFY(e.rollbackRestores());
    }

    void droppingBetweenSiblingsChoosesTheChildPosition()
    {
        // Parent at y=100; centres 50 (c1), 80 (c2); the point's value is 75
        // inside the parent, so c3 goes before c2.
        Edit e([](Page& p) {
            p.appendEntry(vertex("s", "1", {0, 100, 100, 300}, "childLayout=stackLayout;horizontalStack=0;"));
            p.appendEntry(vertex("c1", "s", {0, 30, 100, 40}));
            p.appendEntry(vertex("c2", "s", {0, 70, 100, 20}));
            p.appendEntry(vertex("c3", "s", {0, 90, 100, 30}));
        });
        QVERIFY(ops::moveInLayout(e.tx(), 0, {"c3"}, {50, 175}).ok);
        QCOMPARE(e.childrenOf("s"), QString("c1,c3,c2"));
        QVERIFY(e.rollbackRestores());
    }

    void foldingSwapsTheGeometryWithTheAlternateBounds()
    {
        Edit e([](Page& p) { p.appendEntry(vertex("s", "1", {10, 20, 200, 100}, "swimlane;startSize=30;")); });
        QVERIFY(ops::foldCells(e.tx(), 0, {"s"}, true).ok);
        QVERIFY(e.cell("s")->isCollapsed());
        QCOMPARE(e.bounds("s"), (Rect{10, 20, 200, 30}));
        QCOMPARE(*e.cell("s")->geometry()->alternateBounds(), (Rect{10, 20, 200, 100}));
        QVERIFY(ops::foldCells(e.tx(), 0, {"s"}, true).cells.empty()); // already collapsed
        QVERIFY(ops::foldCells(e.tx(), 0, {"s"}, false).ok);
        QVERIFY(!e.cell("s")->isCollapsed());
        QCOMPARE(e.bounds("s"), (Rect{10, 20, 200, 100}));
        QVERIFY(e.rollbackRestores());
    }

    void foldingUsesStoredAlternateBoundsAndFoldsChildrenOnRequest()
    {
        Edit e([](Page& p) {
            Cell outer = vertex("o", "1", {10, 20, 200, 100}, "swimlane;");
            outer.geometry()->setAlternateBounds(Rect{0, 0, 80, 30});
            p.appendEntry(std::move(outer));
            p.appendEntry(vertex("i", "o", {5, 40, 100, 50}, "swimlane;startSize=20;"));
        });
        ops::FoldOptions options;
        options.recurse = true;
        QVERIFY(ops::foldCells(e.tx(), 0, {"o"}, true, options).ok);
        QCOMPARE(e.bounds("o"), (Rect{10, 20, 80, 30}));
        QCOMPARE(e.bounds("i"), (Rect{5, 40, 100, 20}));
        QVERIFY(e.cell("i")->isCollapsed());
        QVERIFY(e.rollbackRestores());
    }
};

QTEST_APPLESS_MAIN(OpsLayoutTest)
#include "tst_ops_layout.moc"
