// resize sizers, rotation handle, handle visibility and cursors.

#include "support/select_fixture.h"

#include "../../src/ui/select_resize.h" // private: the pure resize math is tested directly
#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_properties.h"

#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

void hover(CanvasView& view, QPointF scenePos)
{
    sendMouse(view, QEvent::MouseMove, scenePos, Qt::NoButton, Qt::NoButton);
}

void setStyle(SelectFixture& f, const char* key, const char* value)
{
    f.session.edit(QStringLiteral("Style"), [&](commands::Transaction& tx) {
        return commands::ops::setStyle(tx, 0, {"a"}, key, value);
    });
}

// Pointer position that puts a's rotation (about its centre (100, 70)) at
// `degrees`, given the handle geometry of a 120x60 vertex.
QPointF rotationPointer(double degrees)
{
    const double handleAngle = std::atan2(-(30.0 + 16), 60.0 + 16) * 180 / M_PI;
    const double t = (handleAngle + degrees) * M_PI / 180;
    return QPointF(100, 70) + 150 * QPointF(std::cos(t), std::sin(t));
}

} // namespace

class TestSelectResize : public QObject {
    Q_OBJECT

private slots:
    // --- pure math -----------------------------------------------------------
    void resizeFromSizerKeepsOppositeSideFixed()
    {
        const QRectF r(40, 40, 120, 60);
        QCOMPARE(resizeFromSizer(r, {1, 1}, {30, 10}, false), QRectF(40, 40, 150, 70));
        QCOMPARE(resizeFromSizer(r, {0, 0}, {10, 10}, false), QRectF(50, 50, 110, 50));
        QCOMPARE(resizeFromSizer(r, {0.5, 0}, {99, -20}, false), QRectF(40, 20, 120, 80)); // N: x ignored
        QCOMPARE(resizeFromSizer(r, {1, 0.5}, {20, 99}, false), QRectF(40, 40, 140, 60));  // E: y ignored
    }

    void resizeFromSizerClampsToMinimumSize()
    {
        const QRectF r(40, 40, 120, 60);
        QCOMPARE(resizeFromSizer(r, {1, 1}, {-500, -500}, false), QRectF(40, 40, 1, 1));
        QCOMPARE(resizeFromSizer(r, {0, 0}, {500, 500}, false), QRectF(159, 99, 1, 1));
    }

    void resizeFromSizerKeepsAspectRatio()
    {
        const QRectF r(40, 40, 120, 60);
        QCOMPARE(resizeFromSizer(r, {1, 1}, {60, 10}, true), QRectF(40, 40, 180, 90));
        const QRectF top = resizeFromSizer(r, {0.5, 0}, {0, -30}, true); // taller by 30 -> wider by 60
        QCOMPARE(top.height(), 90.0);
        QCOMPARE(top.width(), 180.0);
        QCOMPARE(top.center().x(), 100.0);
    }

    void keepFixedPointCompensatesRotation()
    {
        const QRectF original(40, 40, 120, 60);
        const QRectF resized(40, 40, 150, 60); // E sizer dragged by 30
        const QRectF fixed = keepFixedPoint(original, resized, {1, 0.5}, 90);
        // The W mid point, rotated by 90 degrees about the centre, is unchanged.
        const QPointF before = rotateAround(QPointF(40, 70), original.center(), 90);
        const QPointF after = rotateAround(QPointF(fixed.left(), fixed.center().y()), fixed.center(), 90);
        QVERIFY(QLineF(before, after).length() < 1e-9);
        QCOMPARE(keepFixedPoint(original, resized, {1, 0.5}, 0), resized);
    }

    void sizerCursorsFollowTheRotation()
    {
        QCOMPARE(sizerCursor({1, 1}, 0), Qt::SizeFDiagCursor);
        QCOMPARE(sizerCursor({0, 1}, 0), Qt::SizeBDiagCursor);
        QCOMPARE(sizerCursor({0.5, 0}, 0), Qt::SizeVerCursor);
        QCOMPARE(sizerCursor({1, 0.5}, 0), Qt::SizeHorCursor);
        QCOMPARE(sizerCursor({1, 0.5}, 90), Qt::SizeVerCursor);
        QCOMPARE(sizerCursor({1, 1}, 90), Qt::SizeBDiagCursor);
    }

    // --- through the canvas ----------------------------------------------------------
    void resizeFromBottomRightSnapsToGrid()
    {
        SelectFixture f;
        f.session.selection().set({"a"});
        drag(f.canvas(), {160, 100}, {193, 117});
        QCOMPARE(f.geometry("a").x, 40.0);
        QCOMPARE(f.geometry("a").y, 40.0);
        QCOMPARE(f.geometry("a").width, 150.0); // 193 -> 190
        QCOMPARE(f.geometry("a").height, 80.0); // 117 -> 120
        QVERIFY(f.undoRestores(f.original));
    }

    void altResizeDoesNotSnap()
    {
        SelectFixture f;
        f.session.selection().set({"a"});
        drag(f.canvas(), {160, 100}, {193, 117}, Qt::AltModifier);
        QCOMPARE(f.geometry("a").width, 153.0);
        QCOMPARE(f.geometry("a").height, 77.0);
    }

    void resizeFromTopLeftMovesTheOrigin()
    {
        SelectFixture f;
        f.session.selection().set({"a"});
        drag(f.canvas(), {40, 40}, {20, 10});
        QCOMPARE(f.geometry("a").x, 20.0);
        QCOMPARE(f.geometry("a").y, 10.0);
        QCOMPARE(f.geometry("a").width, 140.0);
        QCOMPARE(f.geometry("a").height, 90.0);
    }

    void shiftResizeKeepsAspectRatio()
    {
        SelectFixture f;
        f.session.selection().set({"a"});
        drag(f.canvas(), {160, 100}, {220, 110}, Qt::ShiftModifier);
        QCOMPARE(f.geometry("a").width, 180.0);
        QCOMPARE(f.geometry("a").height, 90.0);
    }

    void resizeInsideGroupWritesParentCoordinates()
    {
        SelectFixture f;
        f.session.selection().set({"c1"});
        // c1 is at (40,200) 80x40 absolute; its BR sizer is at (120,240).
        drag(f.canvas(), {120, 240}, {150, 260});
        QCOMPARE(f.geometry("c1").x, 0.0); // group coordinates
        QCOMPARE(f.geometry("c1").y, 0.0);
        QCOMPARE(f.geometry("c1").width, 110.0);
        QCOMPARE(f.geometry("c1").height, 60.0);
        QVERIFY(f.undoRestores(f.original));
    }

    void resizeOfRotatedVertexKeepsTheOppositeSideInPlace()
    {
        SelectFixture f;
        setStyle(f, "rotation", "90");
        f.session.selection().set({"a"});
        const QByteArray before = savedBytes(f.session);
        // The E sizer of the 90-degree vertex is at (100, 130); drag it down.
        drag(f.canvas(), {100, 130}, {100, 160});
        QCOMPARE(f.geometry("a").width, 150.0);
        QCOMPARE(f.geometry("a").height, 60.0);
        QCOMPARE(f.geometry("a").x, 25.0);
        QCOMPARE(f.geometry("a").y, 55.0);
        QVERIFY(f.undoRestores(before));
    }

    void escapeDuringResizeLeavesTheModelAlone()
    {
        SelectFixture f;
        f.session.selection().set({"a"});
        press(f.canvas(), {160, 100});
        move(f.canvas(), {200, 130});
        QTest::keyClick(&f.canvas(), Qt::Key_Escape);
        release(f.canvas(), {200, 130});
        QCOMPARE(savedBytes(f.session), f.original);
    }

    void rotationHandleWritesRotationStyle()
    {
        SelectFixture f;
        f.session.selection().set({"a"});
        drag(f.canvas(), {176, 24}, rotationPointer(45));
        QCOMPARE(QString::fromStdString(f.cell("a").style().value("rotation", "")), QStringLiteral("45"));
        QVERIFY(f.undoRestores(f.original));
    }

    void rotatingBackToZeroRemovesTheKey()
    {
        SelectFixture f;
        setStyle(f, "rotation", "45");
        f.session.selection().set({"a"});
        const QByteArray before = savedBytes(f.session);
        // The handle of a rotated vertex sits at the rotated offset (76, -46).
        const QPointF handle = rotateAround(QPointF(176, 24), QPointF(100, 70), 45);
        drag(f.canvas(), handle, rotationPointer(0));
        QVERIFY(!f.cell("a").style().has("rotation"));
        QVERIFY(f.undoRestores(before));
    }

    void rotateAndResizeHandlesAreHiddenByStyle()
    {
        SelectFixture f;
        setStyle(f, "rotatable", "0");
        setStyle(f, "resizable", "0");
        f.session.selection().set({"a"});
        const QByteArray before = savedBytes(f.session);
        drag(f.canvas(), {176, 24}, rotationPointer(45)); // blank canvas: rubber band, selection cleared
        QCOMPARE(savedBytes(f.session), before);
        f.session.selection().set({"a"});
        drag(f.canvas(), {160, 100}, {190, 130}, Qt::AltModifier); // no sizer: moves the vertex
        QCOMPARE(f.geometry("a").width, 120.0);
        QCOMPARE(f.geometry("a").x, 70.0);
    }

    void lockedVertexHasNoHandles()
    {
        SelectFixture f;
        setStyle(f, "locked", "1");
        f.session.selection().set({"a"});
        const QByteArray before = savedBytes(f.session);
        drag(f.canvas(), {160, 100}, {200, 140});
        QCOMPARE(savedBytes(f.session), before);
    }

    void multipleSelectionShowsTheUnionHandlesNotThoseOfTheCells()
    {
        SelectFixture f;
        f.session.selection().set({"a", "b"});
        hover(f.canvas(), {160, 100}); // a's corner: inside the union box
        QCOMPARE(f.canvas().interaction().cursor(), Qt::SizeAllCursor); // move, not resize
        hover(f.canvas(), {406, 106}); // the union's bottom-right sizer
        QCOMPARE(f.canvas().interaction().cursor(), Qt::SizeFDiagCursor);
    }

    void hoverShowsResizeAndRotateCursors()
    {
        SelectFixture f;
        f.session.selection().set({"a"});
        hover(f.canvas(), {160, 100});
        QCOMPARE(f.canvas().interaction().cursor(), Qt::SizeFDiagCursor);
        hover(f.canvas(), {160, 40});
        QCOMPARE(f.canvas().interaction().cursor(), Qt::SizeBDiagCursor);
        hover(f.canvas(), {100, 40});
        QCOMPARE(f.canvas().interaction().cursor(), Qt::SizeVerCursor);
        hover(f.canvas(), {160, 70});
        QCOMPARE(f.canvas().interaction().cursor(), Qt::SizeHorCursor);
        hover(f.canvas(), {176, 24});
        QCOMPARE(f.canvas().interaction().cursor(), Qt::CrossCursor);
        hover(f.canvas(), {100, 70});
        QCOMPARE(f.canvas().interaction().cursor(), Qt::SizeAllCursor);
    }

    void smallVertexShowsCornerSizersOnly()
    {
        SelectFixture f;
        f.session.edit(QStringLiteral("Shrink"), [](commands::Transaction& tx) {
            return commands::ops::resizeCells(tx, 0, {"a"}, {core::Rect{40, 40, 20, 20}});
        });
        f.session.selection().set({"a"});
        hover(f.canvas(), {60, 60});
        QCOMPARE(f.canvas().interaction().cursor(), Qt::SizeFDiagCursor);
        hover(f.canvas(), {50, 40}); // the N sizer is not shown: no resize cursor (the connection point shows)
        QVERIFY(f.canvas().interaction().cursor() != Qt::SizeVerCursor);
    }

    void selectionWithHandlesRendersToPng()
    {
        SelectFixture f;
        f.session.selection().set({"a"});
        const QImage image = f.canvas().grab().toImage();
        QVERIFY(!image.isNull());
        // The handle fill colour must show up around a's bottom-right corner (the handle is a few
        // logical pixels wide: the search window is in device pixels).
        const QPoint corner = devicePixel(f.canvas(), QPointF(160, 100));
        const int reach = int(std::ceil(4 * f.canvas().viewport()->devicePixelRatioF()));
        bool found = false;
        for (int dy = -reach; dy <= reach && !found; ++dy)
            for (int dx = -reach; dx <= reach && !found; ++dx)
                found = image.pixelColor(corner + QPoint(dx, dy)) == overlay::kHandleFill;
        QVERIFY(found);
        grabToPng(f.canvas(), QStringLiteral("select-handles"));
    }
};

QTEST_MAIN(TestSelectResize)
#include "tst_select_resize.moc"
