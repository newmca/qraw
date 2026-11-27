// Union resize and rotate (ops_transform.h). Expected values are worked out
// by hand from Graph.scaleCellsToBounds / rotateCellsAround (Graph.js ~37423,
// ~37596). Every case checks that rolling back restores the document byte
// for byte.
#include "qraw/commands/ops_transform.h"
#include "qraw/commands/primitives.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/document.h"
#include "qraw/core/number.h"
#include "qraw/core/style.h"
#include "qraw/format/codec.h"

#include <QTest>

using namespace qraw;
using namespace qraw::commands;
using core::Cell;
using core::Page;
using core::Point;
using core::Rect;

namespace {

Cell vertex(const char* id, const char* parent, Rect bounds, const char* style = "")
{
    return Cell::makeVertex(id, parent, "", style, bounds);
}

// A free edge with a waypoint, in layer coordinates.
Cell freeEdge()
{
    Cell edge = Cell::makeEdge("e", "1", "", "", "", "");
    core::Geometry geo = core::Geometry::create();
    geo.setRelative(true);
    geo.setSourcePoint(Point{10, 50});
    geo.setTargetPoint(Point{90, 50});
    geo.setPoints({Point{50, 20}});
    edge.setGeometry(geo);
    return edge;
}

class Edit {
public:
    template <class Build>
    explicit Edit(Build&& build) : doc_(makeDocument(build)), before_(format::writeDocument(doc_)), tx_(doc_)
    {
    }

    Transaction& tx() { return tx_; }
    [[nodiscard]] const Page& page() const { return *doc_.page(0); }
    [[nodiscard]] Rect bounds(const char* id) const { return page().findCell(id)->geometry()->bounds(); }
    [[nodiscard]] double rotation(const char* id) const
    {
        return core::parseNumber(page().findCell(id)->style().value("rotation", "0"), 0);
    }
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

void twoVerticesAndEdge(Page& page)
{
    page.appendEntry(vertex("a", "1", {0, 0, 40, 40}, "fontSize=20;"));
    page.appendEntry(vertex("b", "1", {60, 60, 40, 40}));
    page.appendEntry(freeEdge());
}

} // namespace

class OpsTransformTest : public QObject {
    Q_OBJECT

private slots:
    void scaleMapsCentresSizesAndEdgePoints()
    {
        Edit edit(twoVerticesAndEdge);
        // 100x100 -> 200x100: sx = 2, sy = 1.
        QVERIFY(ops::scaleCellsToBounds(edit.tx(), 0, {"a", "b", "e"}, {0, 0, 100, 100}, {0, 0, 200, 100}, false));
        QCOMPARE(edit.bounds("a"), (Rect{0, 0, 80, 40}));
        QCOMPARE(edit.bounds("b"), (Rect{120, 60, 80, 40}));
        const auto* geo = edit.page().findCell("e")->geometry();
        QCOMPARE(*geo->sourcePoint(), (Point{20, 50}));
        QCOMPARE(*geo->targetPoint(), (Point{180, 50}));
        QCOMPARE(geo->points(), (std::vector<Point>{{100, 20}}));
        QVERIFY(edit.rollbackRestores());
    }

    void scaleStylesMultipliesBySmallerFactor()
    {
        Edit edit(twoVerticesAndEdge);
        // sx = 0.5, sy = 1: the styles scale by 0.5, fontSize 20 -> 10,
        // the default strokeWidth 1 -> 0.5, spacing 2 -> 1.
        QVERIFY(ops::scaleCellsToBounds(edit.tx(), 0, {"a", "b"}, {0, 0, 100, 100}, {0, 0, 50, 100}, true));
        const core::Style style = edit.page().findCell("a")->style();
        QCOMPARE(QString::fromStdString(style.value("fontSize", "")), QString("10"));
        QCOMPARE(QString::fromStdString(style.value("strokeWidth", "")), QString("0.5"));
        QCOMPARE(QString::fromStdString(style.value("spacing", "")), QString("1"));
        QVERIFY(edit.rollbackRestores());
    }

    void scaleKeepsNonResizableSizeAndScalesGroupChildren()
    {
        Edit edit([](Page& page) {
            page.appendEntry(vertex("fixed", "1", {0, 0, 20, 20}, "resizable=0;"));
            page.appendEntry(vertex("g", "1", {50, 0, 50, 100}));
            page.appendEntry(vertex("c", "g", {10, 10, 20, 20}));
        });
        QVERIFY(ops::scaleCellsToBounds(edit.tx(), 0, {"fixed", "g"}, {0, 0, 100, 100}, {0, 0, 200, 200}, false));
        // Only the centre moves: (10,10) -> (20,20).
        QCOMPARE(edit.bounds("fixed"), (Rect{10, 10, 20, 20}));
        QCOMPARE(edit.bounds("g"), (Rect{100, 0, 100, 200}));
        QCOMPARE(edit.bounds("c"), (Rect{20, 20, 40, 40}));
        QVERIFY(edit.rollbackRestores());
    }

    void rotateTurnsCentresEdgePointsAndRotationStyles()
    {
        Edit edit(twoVerticesAndEdge);
        QVERIFY(ops::rotateCellsAround(edit.tx(), 0, {"a", "b", "e"}, 90, {50, 50}));
        // Centres (20,20) -> (80,20) and (80,80) -> (20,80).
        QCOMPARE(edit.bounds("a"), (Rect{60, 0, 40, 40}));
        QCOMPARE(edit.bounds("b"), (Rect{0, 60, 40, 40}));
        QCOMPARE(edit.rotation("a"), 90.0);
        QCOMPARE(edit.rotation("b"), 90.0);
        const auto* geo = edit.page().findCell("e")->geometry();
        QCOMPARE(*geo->sourcePoint(), (Point{50, 10}));
        QCOMPARE(*geo->targetPoint(), (Point{50, 90}));
        QCOMPARE(geo->points(), (std::vector<Point>{{80, 50}}));
        QVERIFY(edit.rollbackRestores());
    }

    void rotateAddsToExistingRotationAndTurnsChildren()
    {
        Edit edit([](Page& page) {
            page.appendEntry(vertex("g", "1", {0, 0, 100, 100}, "rotation=300;"));
            page.appendEntry(vertex("c", "g", {10, 10, 20, 20}));
            page.appendEntry(vertex("n", "1", {200, 0, 10, 10}, "rotatable=0;"));
        });
        QVERIFY(ops::rotateCellsAround(edit.tx(), 0, {"g", "n"}, 90, {50, 50}));
        QCOMPARE(edit.rotation("g"), 30.0); // (300 + 90) mod 360
        QCOMPARE(edit.bounds("g"), (Rect{0, 0, 100, 100}));
        // The child's centre (20,20) turns around the group's centre to (80,20).
        QCOMPARE(edit.bounds("c"), (Rect{70, 10, 20, 20}));
        QCOMPARE(edit.rotation("c"), 90.0);
        // rotatable=0 moves only: centre (205,5) -> (95,205).
        QCOMPARE(edit.bounds("n"), (Rect{90, 200, 10, 10}));
        QCOMPARE(edit.rotation("n"), 0.0);
        QVERIFY(edit.rollbackRestores());
    }

    void turnVertexSwapsSizeAboutItsCentreAndAdvancesDirection()
    {
        Edit edit([](Page& page) { page.appendEntry(vertex("a", "1", {10, 20, 40, 20})); });
        // Centre (30,30) stays; 40x20 becomes 20x40 at (20,10).
        QVERIFY(ops::turnCells(edit.tx(), 0, {"a"}));
        QCOMPARE(edit.bounds("a"), (Rect{20, 10, 20, 40}));
        QCOMPARE(QString::fromStdString(edit.page().findCell("a")->style().value("direction", "")), QString("south"));
        QVERIFY(edit.rollbackRestores());
    }

    void turnBackwardsCyclesDirectionTheOtherWay()
    {
        Edit edit([](Page& page) { page.appendEntry(vertex("a", "1", {0, 0, 40, 20}, "direction=east;")); });
        QVERIFY(ops::turnCells(edit.tx(), 0, {"a"}, true));
        QCOMPARE(QString::fromStdString(edit.page().findCell("a")->style().value("direction", "")), QString("north"));
        QVERIFY(edit.rollbackRestores());
    }

    void turnReversesEdgeEndsWaypointsAndConstraints()
    {
        Edit edit([](Page& page) {
            page.appendEntry(vertex("a", "1", {0, 0, 40, 40}));
            page.appendEntry(vertex("b", "1", {60, 60, 40, 40}));
            Cell edge = Cell::makeEdge("e", "1", "", "exitX=1;exitY=0.5;entryX=0;", "a", "b");
            core::Geometry geo = core::Geometry::create();
            geo.setRelative(true);
            geo.setSourcePoint(Point{10, 50});
            geo.setTargetPoint(Point{90, 50});
            geo.setPoints({Point{50, 20}, Point{60, 30}});
            edge.setGeometry(geo);
            page.appendEntry(std::move(edge));
        });
        QVERIFY(ops::turnCells(edit.tx(), 0, {"e"}));
        const Cell* e = edit.page().findCell("e");
        QCOMPARE(QString::fromStdString(e->source()), QString("b"));
        QCOMPARE(QString::fromStdString(e->target()), QString("a"));
        QCOMPARE(QString::fromStdString(e->style().value("exitX", "")), QString("0"));
        QCOMPARE(QString::fromStdString(e->style().value("entryX", "")), QString("1"));
        // exitY=0.5 moves to the target end; the source end has no exitY any more.
        QCOMPARE(QString::fromStdString(e->style().value("exitY", "<none>")), QString("<none>"));
        QCOMPARE(QString::fromStdString(e->style().value("entryY", "<none>")), QString("0.5"));
        QCOMPARE(*e->geometry()->sourcePoint(), (Point{90, 50}));
        QCOMPARE(*e->geometry()->targetPoint(), (Point{10, 50}));
        QCOMPARE(e->geometry()->points(), (std::vector<Point>{{60, 30}, {50, 20}}));
        QVERIFY(edit.rollbackRestores());
    }

    void turnGroupRotatesItsChildrenAsABody()
    {
        Edit edit([](Page& page) {
            page.appendEntry(vertex("g", "1", {0, 0, 100, 100}));
            page.appendEntry(vertex("c", "g", {10, 10, 20, 20}));
        });
        QVERIFY(ops::turnCells(edit.tx(), 0, {"g"}));
        // The group keeps its box and gets rotation 90; its child centre (20,20) turns to (80,20).
        QCOMPARE(edit.bounds("g"), (Rect{0, 0, 100, 100}));
        QCOMPARE(edit.rotation("g"), 90.0);
        QCOMPARE(edit.bounds("c"), (Rect{70, 10, 20, 20}));
        QVERIFY(edit.rollbackRestores());
    }
};

QTEST_APPLESS_MAIN(OpsTransformTest)
#include "tst_ops_transform.moc"
