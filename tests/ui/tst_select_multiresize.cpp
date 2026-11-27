// resize and rotate handles on the union of several cells
// (Graph.MultiSelectionHandler). simpleDocument: a (40,40,120,60) and
// b (280,40,120,60) give the union (40,40)-(400,100); the box is padded by
// 6 px, so the bottom-right sizer is at (406,106) and the rotation handle
// 16 px beyond the top-right corner, at (422,18). The grid is 10.

#include "support/select_fixture.h"

#include "../../src/ui/select_resize.h" // private: the pure box math is tested directly

#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

class TestSelectMultiResize : public QObject {
    Q_OBJECT

private slots:
    void unionBoxMathKeepsAspectAndCentre()
    {
        const QRectF box(40, 40, 360, 60);
        UnionResizeOptions plain;
        QCOMPARE(resizeUnionBounds(box, {1, 1}, {40, 20}, plain), QRectF(40, 40, 400, 80));
        QCOMPARE(resizeUnionBounds(box, {0, 0}, {10, 10}, plain), QRectF(50, 50, 350, 50));
        QCOMPARE(resizeUnionBounds(box, {1, 1}, {-500, -500}, plain), QRectF(40, 40, 1, 1));
        UnionResizeOptions aspect;
        aspect.keepAspect = true;
        QCOMPARE(resizeUnionBounds(box, {1, 1}, {0, 30}, aspect), QRectF(40, 40, 540, 90)); // follows the height
        UnionResizeOptions centred;
        centred.centered = true;
        QCOMPARE(resizeUnionBounds(box, {1, 0.5}, {20, 0}, centred), QRectF(20, 40, 400, 60));
    }

    void draggingTheBottomRightSizerScalesBothCells()
    {
        SelectFixture f;
        f.session.selection().set({"a", "b"});
        // 360x60 -> 720x120: both axes double (the pointer moves by 360, 60).
        drag(f.canvas(), {406, 106}, {766, 166});
        QCOMPARE(f.geometry("a"), (core::Rect{40, 40, 240, 120}));
        QCOMPARE(f.geometry("b"), (core::Rect{520, 40, 240, 120}));
        QVERIFY(f.undoRestores(f.original)); // one undo step reverts the whole gesture
    }

    void controlScalesStrokeWidthAndFont()
    {
        SelectFixture f;
        f.session.selection().set({"a", "b"});
        drag(f.canvas(), {406, 106}, {766, 166}, Qt::ControlModifier);
        QCOMPARE(QString::fromStdString(f.cell("a").style().value("fontSize", "")), QStringLiteral("24"));
        QCOMPARE(QString::fromStdString(f.cell("a").style().value("strokeWidth", "")), QStringLiteral("2"));
        QVERIFY(f.undoRestores(f.original));
    }

    void rotationHandleTurnsBothCellsAroundTheUnionCentre()
    {
        SelectFixture f;
        f.session.selection().set({"a", "b"});
        // The union's centre is (220,70). A pointer straight below the centre
        // is 90 degrees clockwise from the handle's start direction minus its
        // own offset angle; drag to the angle that gives exactly 90 degrees.
        const double handleAngle = std::atan2(-(30.0 + 16 + 6), 180.0 + 16 + 6) * 180 / M_PI;
        const double t = (handleAngle + 90) * M_PI / 180;
        const QPointF pointer = QPointF(220, 70) + 400 * QPointF(std::cos(t), std::sin(t));
        drag(f.canvas(), {422, 18}, pointer);
        QCOMPARE(QString::fromStdString(f.cell("a").style().value("rotation", "")), QStringLiteral("90"));
        QCOMPARE(QString::fromStdString(f.cell("b").style().value("rotation", "")), QStringLiteral("90"));
        // a's centre (100,70) turns to (220 - 0, 70 - 120) = (220,-50).
        QCOMPARE(f.geometry("a"), (core::Rect{160, -80, 120, 60}));
        QVERIFY(f.undoRestores(f.original));
    }

    void clickingTheRotationHandleTurnsByAQuarter()
    {
        SelectFixture f;
        f.session.selection().set({"a", "b"});
        click(f.canvas(), {422, 18});
        QCOMPARE(QString::fromStdString(f.cell("a").style().value("rotation", "")), QStringLiteral("90"));
        QVERIFY(f.undoRestores(f.original));
    }

    void childrenOfAPlainGroupGetTheUnionBoxToo()
    {
        SelectFixture f;
        f.session.selection().set({"c1", "c2"}); // (40,200)-(240,300) in the group g
        sendMouse(f.canvas(), QEvent::MouseMove, {246, 306}, Qt::NoButton, Qt::NoButton);
        QCOMPARE(f.canvas().interaction().cursor(), Qt::SizeFDiagCursor);
    }
};

QTEST_MAIN(TestSelectMultiResize)
#include "tst_select_multiresize.moc"
