// Arrange > Layout (ops::arrangeLayout) and the childLayout values treeLayout and
// circleLayout and flowLayout (ops::applyLayouts) on documents: what moves, what happens to the
// edges, undo, and that opening and saving never runs a layout.
#include "qraw/commands/change_set.h"
#include "qraw/commands/ops_layout.h"
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

// An edge with a waypoint, so that a reset is visible.
Cell edge(const char* id, const char* source, const char* target, const char* style = "")
{
    Cell cell = Cell::makeEdge(id, "1", "", style, source, target);
    core::Geometry geometry = core::Geometry::create();
    geometry.setRelative(true);
    geometry.setPoints({{5, 5}});
    cell.setGeometry(geometry);
    return cell;
}

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
    [[nodiscard]] QString style(const char* id) const { return QString::fromStdString(cell(id)->styleText()); }
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

// r -> a, b; a -> c on layer 1, all at the origin of the page.
void tree(Page& p)
{
    p.appendEntry(vertex("r", "1", {300, 200, 100, 40}));
    p.appendEntry(vertex("a", "1", {0, 0, 100, 40}));
    p.appendEntry(vertex("b", "1", {0, 0, 100, 40}));
    p.appendEntry(vertex("c", "1", {0, 0, 100, 40}));
    p.appendEntry(vertex("lonely", "1", {900, 900, 50, 50}));
    p.appendEntry(edge("e1", "r", "a"));
    p.appendEntry(edge("e2", "r", "b"));
    p.appendEntry(edge("e3", "a", "c"));
}

ChangeSet changedCells(std::initializer_list<const char*> ids)
{
    ChangeSet changes;
    for (const char* id : ids)
        changes.cellChanged("p1", id);
    return changes;
}

} // namespace

class LayoutOpsTest : public QObject {
    Q_OBJECT

private slots:
    void verticalTreeMovesTheTreeResetsItsEdgesAndLeavesOtherCellsAlone()
    {
        Edit e(tree);
        const ops::LayoutScope scope{"1", {}, ""};
        QVERIFY(ops::canArrangeLayout(e.page(), ops::LayoutKind::VerticalTree, scope));
        QVERIFY(ops::arrangeLayout(e.tx(), 0, ops::LayoutKind::VerticalTree, scope).ok);
        QCOMPARE(e.bounds("r"), (Rect{300, 200, 100, 40}));
        QCOMPARE(e.bounds("a").y, 270.0);
        QCOMPARE(e.bounds("b").y, 270.0);
        QCOMPARE(e.bounds("c").y, 340.0);
        QVERIFY(e.bounds("a").x < e.bounds("b").x);
        QCOMPARE(e.bounds("lonely"), (Rect{900, 900, 50, 50}));
        QVERIFY(e.cell("e1")->geometry()->points().empty()); // waypoints reset
        QVERIFY(e.cell("e1")->styleText().find("noEdgeStyle") == std::string::npos);
        QVERIFY(e.rollbackRestores());
    }

    void horizontalTreePutsLevelsLeftToRight()
    {
        Edit e(tree);
        QVERIFY(ops::arrangeLayout(e.tx(), 0, ops::LayoutKind::HorizontalTree, {"1", {}, ""}).ok);
        QCOMPARE(e.bounds("a").x, 430.0); // 300 + 100 + 30
        QCOMPARE(e.bounds("c").x, 560.0);
        QVERIFY(e.bounds("a").y < e.bounds("b").y);
        QVERIFY(e.rollbackRestores());
    }

    void orgChartRoutesEdgesWithElbows()
    {
        Edit e(tree);
        QVERIFY(ops::arrangeLayout(e.tx(), 0, ops::LayoutKind::OrgChart, {"1", {}, ""}).ok);
        QCOMPARE(e.bounds("a").y, 280.0); // level distance 40
        QVERIFY(e.cell("e1")->style().value("edgeStyle", "") == "elbowEdgeStyle");
        QCOMPARE(QString::fromStdString(e.cell("e1")->style().value("elbow", "")), QStringLiteral("vertical"));
        QVERIFY(e.rollbackRestores());
    }

    void radialTreePutsTheRootInTheMiddleAndDisablesTheEdgeStyle()
    {
        Edit e(tree);
        QVERIFY(ops::arrangeLayout(e.tx(), 0, ops::LayoutKind::RadialTree, {"1", {}, ""}).ok);
        QCOMPARE(e.bounds("r"), (Rect{300, 200, 100, 40}));
        QVERIFY(e.bounds("a").y < 200); // the first child is straight above
        QCOMPARE(QString::fromStdString(e.cell("e1")->style().value("noEdgeStyle", "")), QStringLiteral("1"));
        QVERIFY(e.cell("e3")->geometry()->points().empty());
        QVERIFY(e.rollbackRestores());
    }

    void circleLaysOutTheVerticesOfTheParentAndResetsAllEdges()
    {
        Edit e(tree);
        QVERIFY(ops::arrangeLayout(e.tx(), 0, ops::LayoutKind::Circle, {"1", {}, ""}).ok);
        // Five vertices: radius = max(5 * 100 / pi, 100); first vertex at the top.
        const Rect first = e.bounds("r");
        QVERIFY(first.y <= e.bounds("a").y && first.y <= e.bounds("b").y);
        QCOMPARE(QString::fromStdString(e.cell("e2")->style().value("noEdgeStyle", "")), QStringLiteral("1"));
        QVERIFY(e.cell("e2")->geometry()->points().empty());
        QVERIFY(e.rollbackRestores());
    }

    void aSelectedRootLaysOutOnlyItsTree()
    {
        Edit e(tree);
        // Starting at a: only a and c are arranged, r and b stay.
        const ops::LayoutScope scope = ops::layoutScopeForSelection(e.page(), {"a"}, "1");
        QCOMPARE(QString::fromStdString(scope.root), QStringLiteral("a"));
        QVERIFY(ops::arrangeLayout(e.tx(), 0, ops::LayoutKind::VerticalTree, scope).ok);
        QCOMPARE(e.bounds("a"), (Rect{0, 0, 100, 40}));
        QCOMPARE(e.bounds("c").y, 70.0);
        QCOMPARE(e.bounds("r"), (Rect{300, 200, 100, 40}));
        QVERIFY(e.rollbackRestores());
    }

    void severalSelectedVerticesAreLaidOutAmongThemselves()
    {
        Edit e(tree);
        const ops::LayoutScope scope = ops::layoutScopeForSelection(e.page(), {"a", "b", "c"}, "1");
        QCOMPARE(scope.vertices.size(), std::size_t{3});
        QVERIFY(ops::arrangeLayout(e.tx(), 0, ops::LayoutKind::Circle, scope).ok);
        QCOMPARE(e.bounds("r"), (Rect{300, 200, 100, 40})); // not selected
        QVERIFY(e.rollbackRestores());
    }

    void aContainerSelectionLaysOutItsChildren()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("box", "1", {0, 0, 400, 400}, "container=1;"));
            p.appendEntry(vertex("x", "box", {10, 10, 50, 50}));
            p.appendEntry(vertex("y", "box", {10, 10, 50, 50}));
        });
        const ops::LayoutScope scope = ops::layoutScopeForSelection(e.page(), {"box"}, "1");
        QCOMPARE(QString::fromStdString(scope.parent), QStringLiteral("box"));
        QVERIFY(ops::arrangeLayout(e.tx(), 0, ops::LayoutKind::Circle, scope).ok);
        QVERIFY(e.bounds("x") != e.bounds("y"));
        QVERIFY(e.rollbackRestores());
    }

    void nothingToArrangeFailsWithoutChangingAnything()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("a", "1", {0, 0, 10, 10}));
            p.appendEntry(vertex("b", "1", {50, 0, 10, 10}));
        });
        const ops::LayoutScope scope{"1", {}, ""};
        QVERIFY(!ops::canArrangeLayout(e.page(), ops::LayoutKind::VerticalTree, scope)); // no edge: no root
        QVERIFY(!ops::arrangeLayout(e.tx(), 0, ops::LayoutKind::VerticalTree, scope).ok);
        QVERIFY(e.tx().empty());
        QVERIFY(ops::canArrangeLayout(e.page(), ops::LayoutKind::Circle, scope));
        QVERIFY(!ops::canArrangeLayout(e.page(), ops::LayoutKind::Circle, {"1", {"a"}, ""}));
    }

    void lockedVerticesStay()
    {
        Edit e([](Page& p) {
            tree(p);
            p.findCell("b")->setStyleText("locked=1;");
        });
        const Rect before = e.bounds("b");
        QVERIFY(ops::arrangeLayout(e.tx(), 0, ops::LayoutKind::VerticalTree, {"1", {}, ""}).ok);
        QCOMPARE(e.bounds("b"), before);
        QVERIFY(e.rollbackRestores());
    }

    void verticalFlowStacksTheRanksRoutesLongEdgesAndStylesThem()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("a", "1", {0, 0, 100, 40}));
            p.appendEntry(vertex("b", "1", {0, 0, 100, 40}));
            p.appendEntry(vertex("c", "1", {0, 0, 100, 40}));
            p.appendEntry(edge("e1", "a", "b"));
            p.appendEntry(edge("e2", "b", "c"));
            p.appendEntry(edge("e3", "a", "c"));
        });
        const ops::LayoutScope scope{"1", {}, ""};
        QVERIFY(ops::arrangeLayout(e.tx(), 0, ops::LayoutKind::VerticalFlow, scope).ok);
        QCOMPARE(e.bounds("b").y - e.bounds("a").y, 140.0);
        QCOMPARE(e.bounds("c").y - e.bounds("b").y, 140.0);
        for (const core::Point& point : e.cell("e1")->geometry()->points())
            QVERIFY(point != (core::Point{5, 5})); // the stale waypoint is gone
        QVERIFY(!e.cell("e3")->geometry()->points().empty()); // the long edge goes around b
        QVERIFY(e.style("e1").contains("edgeStyle=orthogonalEdgeStyle"));
        QVERIFY(e.rollbackRestores());
    }

    void horizontalFlowPointsRightAndAContainerSelectionLaysOutItsChildren()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("box", "1", {0, 0, 400, 400}));
            p.appendEntry(vertex("a", "box", {200, 200, 100, 40}));
            p.appendEntry(vertex("b", "box", {0, 0, 100, 40}));
            Cell link = edge("e1", "a", "b");
            link.setParent("box");
            p.appendEntry(std::move(link));
        });
        const ops::LayoutScope scope = ops::layoutScopeForSelection(e.page(), {"box"}, "1");
        QCOMPARE(scope.parent, std::string("box"));
        QVERIFY(ops::arrangeLayout(e.tx(), 0, ops::LayoutKind::HorizontalFlow, scope).ok);
        QCOMPARE(e.bounds("b").x - e.bounds("a").x, 200.0); // 100 wide + interRankCellSpacing 100
        QCOMPARE(e.bounds("box"), (Rect{0, 0, 400, 400}));  // the menu never resizes the container
        QVERIFY(e.rollbackRestores());
    }

    void organicKeepsTheEdgeStyleAndResetsWaypoints()
    {
        Edit e([](Page& p) {
            for (const char* id : {"a", "b", "c"})
                p.appendEntry(vertex(id, "1", {0, 0, 80, 40}));
            p.appendEntry(edge("e1", "a", "b", "endArrow=none;"));
            p.appendEntry(edge("e2", "b", "c"));
        });
        const ops::LayoutScope scope{"1", {}, ""};
        QVERIFY(ops::canArrangeLayout(e.page(), ops::LayoutKind::Organic, scope));
        QVERIFY(ops::arrangeLayout(e.tx(), 0, ops::LayoutKind::Organic, scope).ok);
        QCOMPARE(e.style("e1"), QString("endArrow=none;"));
        QVERIFY(e.cell("e1")->geometry()->points().empty());
        const Rect a = e.bounds("a");
        const Rect b = e.bounds("b");
        QVERIFY(a.x + a.width <= b.x || b.x + b.width <= a.x || a.y + a.height <= b.y || b.y + b.height <= a.y);
        QVERIFY(e.rollbackRestores());
    }

    void flowAndOrganicNeedTwoVertices()
    {
        Edit e([](Page& p) { p.appendEntry(vertex("a", "1", {0, 0, 80, 40})); });
        const ops::LayoutScope scope{"1", {}, ""};
        for (const auto kind : {ops::LayoutKind::HorizontalFlow, ops::LayoutKind::VerticalFlow, ops::LayoutKind::Organic}) {
            QVERIFY(!ops::canArrangeLayout(e.page(), kind, scope));
            QVERIFY(!ops::arrangeLayout(e.tx(), 0, kind, scope).ok);
        }
    }

    // --- childLayout -------------------------------------------------------------------

    void treeLayoutContainerLaysOutItsChildrenAndGrows()
    {
        // Vertical tree (horizontalTree=0), padding 20, level distance 30; not a swimlane, so no title strip.
        Edit e([](Page& p) {
            p.appendEntry(vertex("box", "1", {100, 100, 50, 50}, "childLayout=treeLayout;horizontalTree=0;container=1;"));
            p.appendEntry(vertex("r", "box", {5, 5, 100, 40}));
            p.appendEntry(vertex("a", "box", {0, 0, 100, 40}));
            p.appendEntry(vertex("b", "box", {0, 0, 100, 40}));
            Cell e1 = edge("e1", "r", "a");
            e1.setParent("box");
            Cell e2 = edge("e2", "r", "b");
            e2.setParent("box");
            p.appendEntry(std::move(e1));
            p.appendEntry(std::move(e2));
        });
        QVERIFY(ops::hasChildLayout(*e.cell("box")));
        QVERIFY(ops::applyLayouts(e.tx(), 0, changedCells({"a"})).ok);
        QCOMPARE(e.bounds("a").y, 90.0);  // 20 + 40 + 30
        QCOMPARE(e.bounds("r").y, 20.0);
        QCOMPARE(e.bounds("a").x, 20.0);
        QCOMPARE(e.bounds("b").x, 140.0); // 100 + nodeDistance 20
        QCOMPARE(e.bounds("r").x, 80.0);  // centred over a and b
        QVERIFY(e.cell("e1")->geometry()->points().empty());
        QCOMPARE(e.bounds("box"), (Rect{100, 100, 260, 150})); // grown to the children plus 20
        QVERIFY(e.rollbackRestores());
    }

    void treeLayoutContainerGoesHorizontallyByDefault()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("box", "1", {0, 0, 50, 50}, "childLayout=treeLayout;container=1;"));
            p.appendEntry(vertex("r", "box", {0, 0, 100, 40}));
            p.appendEntry(vertex("a", "box", {0, 0, 100, 40}));
            Cell e1 = edge("e1", "r", "a");
            e1.setParent("box");
            p.appendEntry(std::move(e1));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, changedCells({"r"})).ok);
        QCOMPARE(e.bounds("a").x, 150.0); // 20 + 100 + 30
        QCOMPARE(e.bounds("a").y, e.bounds("r").y);
        QVERIFY(e.rollbackRestores());
    }

    void aChildAddedToATreeContainerIsPlaced()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("box", "1", {0, 0, 400, 400}, "childLayout=treeLayout;horizontalTree=0;container=1;"));
            p.appendEntry(vertex("r", "box", {20, 20, 100, 40}));
        });
        // The new cell is dropped on top of the root; the layout puts it next to it.
        QVERIFY(e.tx().emplace<InsertCell>(0, e.page().rootEntries().size(), vertex("n", "box", {20, 20, 100, 40})));
        ChangeSet changes;
        changes.cellAdded("p1", "n");
        QVERIFY(ops::applyLayouts(e.tx(), 0, changes).ok);
        const Rect root = e.bounds("r");
        const Rect added = e.bounds("n");
        QVERIFY(root.x + root.width <= added.x || added.x + added.width <= root.x);
        QVERIFY(e.rollbackRestores());
    }

    void treeLayoutKeepsAPinnedContainerWhenResizeParentIsOff()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("box", "1", {0, 0, 50, 50}, "childLayout=treeLayout;resizeParent=0;container=1;"));
            p.appendEntry(vertex("r", "box", {0, 0, 100, 40}));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, changedCells({"r"})).ok);
        QCOMPARE(e.bounds("box"), (Rect{0, 0, 50, 50}));
        QCOMPARE(e.bounds("r").x, 20.0);
        QVERIFY(e.rollbackRestores());
    }

    void circleLayoutContainerPlacesTheCircleBelowTheTitleAndFitsTheContainer()
    {
        // x0 = 20, y0 = startSize 40 + 20; radius = max(4 * 40 / pi, 100) = 100.
        Edit e([](Page& p) {
            p.appendEntry(vertex("box", "1", {10, 10, 50, 50}, "childLayout=circleLayout;container=1;"));
            for (const char* id : {"a", "b", "c", "d"})
                p.appendEntry(vertex(id, "box", {0, 0, 40, 40}));
            Cell e1 = edge("e1", "a", "b");
            e1.setParent("box");
            p.appendEntry(std::move(e1));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, changedCells({"a"})).ok);
        QCOMPARE(e.bounds("a"), (Rect{120, 60, 40, 40}));
        QCOMPARE(e.bounds("b"), (Rect{220, 160, 40, 40}));
        QCOMPARE(e.bounds("c"), (Rect{120, 260, 40, 40}));
        QCOMPARE(e.bounds("d"), (Rect{20, 160, 40, 40}));
        QCOMPARE(e.bounds("box"), (Rect{10, 10, 280, 320})); // 260 + 20, 300 + 20
        QVERIFY(e.cell("e1")->geometry()->points().empty());
        QCOMPARE(QString::fromStdString(e.cell("e1")->style().value("noEdgeStyle", "")), QStringLiteral("1"));
        QVERIFY(e.rollbackRestores());
    }

    void circleLayoutContainerWithASideTitleStartsRightOfIt()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("box", "1", {0, 0, 50, 50}, "swimlane;horizontal=0;startSize=30;childLayout=circleLayout;"));
            p.appendEntry(vertex("a", "box", {0, 0, 40, 40}));
            p.appendEntry(vertex("b", "box", {0, 0, 40, 40}));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, changedCells({"a"})).ok);
        QCOMPARE(e.bounds("a"), (Rect{50 + 100, 20, 40, 40})); // x0 = 30 + 20, top of the circle
        QVERIFY(e.rollbackRestores());
    }

    void layoutsConvergeAndLockedChildrenStay()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("box", "1", {0, 0, 50, 50}, "childLayout=circleLayout;container=1;"));
            p.appendEntry(vertex("a", "box", {0, 0, 40, 40}));
            p.appendEntry(vertex("b", "box", {7, 7, 40, 40}, "locked=1;"));
            p.appendEntry(vertex("c", "box", {0, 0, 40, 40}));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, changedCells({"a"})).ok);
        QCOMPARE(e.bounds("b"), (Rect{7, 7, 40, 40}));
        const Rect a = e.bounds("a");
        const Rect box = e.bounds("box");
        const std::size_t commands = e.tx().savepoint();
        QVERIFY(ops::applyLayouts(e.tx(), 0, changedCells({"a"})).ok); // a second run changes nothing
        QCOMPARE(e.tx().savepoint(), commands);
        QCOMPARE(e.bounds("a"), a);
        QCOMPARE(e.bounds("box"), box);
        QVERIFY(e.rollbackRestores());
    }

    void flowLayoutContainerLaysOutItsChildrenLeftwardsByDefaultAndGrows()
    {
        // flowOrientation defaults to east, which Graph.getElkDirection turns into LEFT: edges point left.
        Edit e([](Page& p) {
            p.appendEntry(vertex("box", "1", {100, 100, 50, 50}, "childLayout=flowLayout;container=1;"));
            p.appendEntry(vertex("a", "box", {5, 5, 100, 40}));
            p.appendEntry(vertex("b", "box", {0, 0, 100, 40}));
            Cell link = edge("e1", "a", "b");
            link.setParent("box");
            p.appendEntry(std::move(link));
        });
        QVERIFY(ops::hasChildLayout(*e.cell("box")));
        QVERIFY(ops::applyLayouts(e.tx(), 0, changedCells({"a"})).ok);
        QCOMPARE(e.bounds("a").x, 220.0); // 20 + 100 + interRankCellSpacing 100: a is on the far side
        QCOMPARE(e.bounds("b").x, 20.0);
        QCOMPARE(e.bounds("a").y, e.bounds("b").y);
        QCOMPARE(e.bounds("box"), (Rect{100, 100, 340, 80})); // children plus parentPadding 20
        QVERIFY(e.style("e1").contains("edgeStyle=orthogonalEdgeStyle"));
        QVERIFY(e.rollbackRestores());
    }

    void flowLayoutOrientationAndSpacingComeFromTheStyle()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("box", "1", {0, 0, 50, 50},
                                 "childLayout=flowLayout;container=1;flowOrientation=north;interRankCellSpacing=60;"
                                 "intraCellSpacing=10;parentPadding=0;"));
            p.appendEntry(vertex("a", "box", {0, 0, 100, 40}));
            p.appendEntry(vertex("b", "box", {0, 0, 100, 40}));
            p.appendEntry(vertex("c", "box", {0, 0, 100, 40}));
            for (const char* t : {"b", "c"}) {
                Cell link = edge((std::string("e") + t).c_str(), "a", t);
                link.setParent("box");
                p.appendEntry(std::move(link));
            }
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, changedCells({"a"})).ok);
        QCOMPARE(e.bounds("a").y, 0.0);
        QCOMPARE(e.bounds("b").y, 100.0); // 40 + 60: north is the flow down
        QCOMPARE(e.bounds("c").x - e.bounds("b").x, 110.0);
        QCOMPARE(e.bounds("box"), (Rect{0, 0, 210, 140}));
        // A second run changes nothing.
        const std::size_t commands = e.tx().savepoint();
        QVERIFY(ops::applyLayouts(e.tx(), 0, changedCells({"a"})).ok);
        QCOMPARE(e.tx().savepoint(), commands);
        QVERIFY(e.rollbackRestores());
    }

    void aChildAddedToAFlowContainerIsPlaced()
    {
        Edit e([](Page& p) {
            p.appendEntry(vertex("box", "1", {0, 0, 400, 400}, "childLayout=flowLayout;flowOrientation=north;container=1;"));
            p.appendEntry(vertex("a", "box", {300, 300, 100, 40}));
            p.appendEntry(vertex("b", "box", {300, 300, 100, 40}));
        });
        QVERIFY(ops::applyLayouts(e.tx(), 0, changedCells({"b"})).ok);
        QVERIFY(e.bounds("a").x != e.bounds("b").x); // isolated children are part of the layout: side by side
        QCOMPARE(e.bounds("a").y, e.bounds("b").y);
        QVERIFY(e.rollbackRestores());
    }

    void openingAndSavingNeverRunsAChildLayout()
    {
        const char* xml =
            R"(<mxfile><diagram id="p" name="P"><mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/>)"
            R"(<mxCell id="t" style="childLayout=treeLayout;container=1;" vertex="1" parent="1"><mxGeometry x="10" y="10" width="50" height="50" as="geometry"/></mxCell>)"
            R"(<mxCell id="t1" style="" vertex="1" parent="t"><mxGeometry x="1" y="2" width="30" height="20" as="geometry"/></mxCell>)"
            R"(<mxCell id="t2" style="" vertex="1" parent="t"><mxGeometry x="1" y="2" width="30" height="20" as="geometry"/></mxCell>)"
            R"(<mxCell id="c" style="childLayout=circleLayout;container=1;" vertex="1" parent="1"><mxGeometry x="100" y="10" width="50" height="50" as="geometry"/></mxCell>)"
            R"(<mxCell id="c1" style="" vertex="1" parent="c"><mxGeometry x="1" y="2" width="30" height="20" as="geometry"/></mxCell>)"
            R"(<mxCell id="c2" style="" vertex="1" parent="c"><mxGeometry x="1" y="2" width="30" height="20" as="geometry"/></mxCell>)"
            R"(</root></mxGraphModel></diagram></mxfile>)";
        const format::ReadResult read = format::readDocument(xml);
        QVERIFY(read.ok());
        QVERIFY(ops::hasChildLayout(*read.document->page(0)->findCell("t")));
        QVERIFY(ops::hasChildLayout(*read.document->page(0)->findCell("c")));
        const QByteArray saved = format::writeDocument(*read.document);
        const format::ReadResult again = format::readDocument(saved);
        QVERIFY(again.ok());
        QCOMPARE(format::writeDocument(*again.document), saved);
        QCOMPARE(read.document->page(0)->findCell("t1")->geometry()->bounds(), (Rect{1, 2, 30, 20}));
    }
};

QTEST_APPLESS_MAIN(LayoutOpsTest)
#include "tst_layout_ops.moc"
