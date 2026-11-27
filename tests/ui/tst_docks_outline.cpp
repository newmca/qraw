// Tests of OutlineView: the viewport rectangle follows zoom and scrolling,
// and a press/drag in the outline moves the canvas.

#include "support/test_support.h"

#include "qraw/ui/outline_view.h"

#include <QApplication>
#include <QLineF>
#include <QMouseEvent>
#include <QScrollBar>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

class TestDocksOutline : public QObject {
    Q_OBJECT

private slots:
    void viewportFollowsZoom();
    void pressCentresAndDragPans();
};

void TestDocksOutline::viewportFollowsZoom()
{
    DocumentSession session(simpleDocument());
    auto canvas = makeCanvas(session);
    OutlineView outline(*canvas);
    outline.resize(200, 150);
    outline.show();
    QVERIFY(QTest::qWaitForWindowExposed(&outline));

    const QRectF whole = outline.viewportRect();
    QVERIFY(!whole.isEmpty());
    canvas->setZoom(2);
    const QRectF zoomed = outline.viewportRect();
    QVERIFY(zoomed.width() < whole.width());
    QVERIFY(zoomed.height() < whole.height());
    grabToPng(outline, QStringLiteral("docks-outline"));
}

void TestDocksOutline::pressCentresAndDragPans()
{
    DocumentSession session(simpleDocument());
    auto canvas = makeCanvas(session);
    OutlineView outline(*canvas);
    outline.resize(200, 150);
    outline.show();
    QVERIFY(QTest::qWaitForWindowExposed(&outline));
    canvas->setZoom(2);
    QVERIFY(canvas->horizontalScrollBar()->maximum() > 0);

    // Press on the outline away from the viewport rectangle: the canvas centres there.
    QPointF press(5, 5);
    if (outline.viewportRect().contains(press))
        press = QPointF(outline.width() - 5, outline.height() - 5);
    QVERIFY(!outline.viewportRect().contains(press));
    const QPointF rectCentreBefore = outline.viewportRect().center();
    QTest::mousePress(&outline, Qt::LeftButton, {}, press.toPoint());
    QTest::mouseRelease(&outline, Qt::LeftButton, {}, press.toPoint());
    // The scroll range can stop the canvas short of the press point, so only
    // the direction is asserted.
    QVERIFY(QLineF(outline.viewportRect().center(), press).length()
            < QLineF(rectCentreBefore, press).length());

    // Pressing inside the rectangle and dragging pans the canvas in the drag direction.
    // The drag goes towards the side where the scroll range still has room.
    const QPointF grab = outline.viewportRect().center();
    const QRectF before = outline.viewportRect();
    const QScrollBar* scroll = canvas->horizontalScrollBar();
    const qreal step = scroll->value() < scroll->maximum() ? 20 : -20;
    const QPointF target = grab + QPointF(step, 0);
    QTest::mousePress(&outline, Qt::LeftButton, {}, grab.toPoint());
    QMouseEvent move(QEvent::MouseMove, target, outline.mapToGlobal(target.toPoint()), Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(&outline, &move);
    QTest::mouseRelease(&outline, Qt::LeftButton, {}, target.toPoint());
    QVERIFY((outline.viewportRect().center().x() - before.center().x()) * step > 0);
}

QTEST_MAIN(TestDocksOutline)
#include "tst_docks_outline.moc"
