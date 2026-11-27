// Snapper unit tests (grid, guides, Alt, resize).

#include "support/select_fixture.h"

#include "qraw/ui/snapping.h"

#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

class TestSelectSnapper : public QObject {
    Q_OBJECT

private slots:
    void snapRoundsToGridOnlyWhenGridIsOn()
    {
        SelectFixture f;
        const Snapper snapper(f.canvas().diagramScene());
        QCOMPARE(snapper.snap(13.0), 10.0);
        QCOMPARE(snapper.snap(15.0), 20.0);
        QCOMPARE(snapper.snap(QPointF(24, -6)), QPointF(20, -10));
        f.setGrid(false);
        QCOMPARE(snapper.snap(13.0), 13.0); // reads the page on every call
    }

    void snapMoveSnapsTopLeftToGrid()
    {
        SelectFixture f;
        const Snapper snapper(f.canvas().diagramScene(), false);
        const SnapResult r = snapper.snapMove(QRectF(40, 40, 120, 60), {33, 7}, {"a"}, Qt::NoModifier, 1);
        QCOMPARE(r.delta, QPointF(30, 10)); // 73 -> 70, 47 -> 50
        QVERIFY(r.guides.empty());
    }

    void snapMoveWithGuidesAlignsWithNeighbours()
    {
        SelectFixture f;
        f.setGrid(false);
        const Snapper snapper(f.canvas().diagramScene(), true);
        // a moved right by 30 and down by 2: its top (42) is within 4 px of
        // b's top (40); no vertical line of the moved a is near another cell's.
        const SnapResult r = snapper.snapMove(QRectF(40, 40, 120, 60), {30, 2}, {"a"}, Qt::NoModifier, 1);
        QCOMPARE(r.delta, QPointF(30, 0));
        QCOMPARE(r.guides.size(), std::size_t(1));
        const QLineF guide = r.guides.front();
        QCOMPARE(guide.y1(), 40.0);
        QCOMPARE(guide.y2(), 40.0);
        QCOMPARE(guide.x1(), 70.0);  // from the moved a's left ...
        QCOMPARE(guide.x2(), 400.0); // ... to b's right
    }

    void snapMoveGuidesInBothAxes()
    {
        SelectFixture f;
        f.setGrid(false);
        const Snapper snapper(f.canvas().diagramScene(), true);
        // a moved so that its centre x equals b's centre x (340) +2 and top equals b's top +1.
        const SnapResult r = snapper.snapMove(QRectF(40, 40, 120, 60), {242, 1}, {"a"}, Qt::NoModifier, 1);
        QCOMPARE(r.delta, QPointF(240, 0));
        QCOMPARE(r.guides.size(), std::size_t(2));
    }

    void guideToleranceIsHalfAGridStepWhenTheGridIsOn()
    {
        SelectFixture f; // grid 10 -> tolerance 5
        const Snapper snapper(f.canvas().diagramScene(), true);
        // Top at 44: aligned to b's top (40) by the guide, not to the grid (40 as well);
        // use the left edge instead: 285 vs b's left 280 -> guide wins (5 <= 5).
        const SnapResult near = snapper.snapMove(QRectF(40, 120, 120, 60), {245, 0}, {"a"}, Qt::NoModifier, 1);
        QCOMPARE(near.delta.x(), 240.0);
        QCOMPARE(near.guides.size(), std::size_t(1));
        // 6 px away: no guide; falls back to the grid.
        const SnapResult far = snapper.snapMove(QRectF(40, 120, 120, 60), {246, 0}, {"a"}, Qt::NoModifier, 1);
        QCOMPARE(far.delta.x(), 250.0);
        QVERIFY(far.guides.empty());
    }

    void movingCellsAndTheirDescendantsAreNotGuideTargets()
    {
        SelectFixture f;
        f.setGrid(false);
        const Snapper snapper(f.canvas().diagramScene(), true);
        // g moved by (7, -3): c1's top (200) and c2's bottom (300) are within
        // reach of g's moved top / bottom but belong to g: no guide.
        const SnapResult r = snapper.snapMove(QRectF(40, 200, 200, 100), {7, -3}, {"g"}, Qt::NoModifier, 1);
        QCOMPARE(r.delta, QPointF(7, -3));
        QVERIFY(r.guides.empty());
    }

    void altDisablesGridAndGuides()
    {
        SelectFixture f;
        const Snapper snapper(f.canvas().diagramScene(), true);
        const SnapResult r = snapper.snapMove(QRectF(40, 40, 120, 60), {33, 7}, {"a"}, Qt::AltModifier, 1);
        QCOMPARE(r.delta, QPointF(33, 7));
        QVERIFY(r.guides.empty());
    }

    void guideToleranceWithoutGridScalesWithZoom()
    {
        SelectFixture f;
        f.setGrid(false);
        const Snapper snapper(f.canvas().diagramScene(), true);
        // 3 page units away from b's top: 3 px at 100 % (snaps), 6 px at 200 % (4 px = 2 units: no).
        QCOMPARE(snapper.snapMove(QRectF(40, 40, 120, 60), {0, 3}, {"a"}, Qt::NoModifier, 1).delta.y(), 0.0);
        QCOMPARE(snapper.snapMove(QRectF(40, 40, 120, 60), {0, 3}, {"a"}, Qt::NoModifier, 0.5).delta.y(), 3.0);
    }

    void snapResizeSnapsEdgesAndHonoursAlt()
    {
        SelectFixture f;
        const Snapper snapper(f.canvas().diagramScene());
        QCOMPARE(snapper.snapResize(QRectF(41, 42, 112, 58), Qt::NoModifier), QRectF(40, 40, 110, 60));
        QCOMPARE(snapper.snapResize(QRectF(41, 42, 112, 58), Qt::AltModifier), QRectF(41, 42, 112, 58));
    }
};

QTEST_MAIN(TestSelectSnapper)
#include "tst_select_snapper.moc"
