// CanvasView zoom, fitting, panning, context menu, keys and the
// scene rectangle (docs/design/ui.md section 3).

#include "support/test_support.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_pages.h"

#include <QScrollBar>
#include <QSignalSpy>
#include <QWheelEvent>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;
namespace ops = qraw::commands::ops;

namespace {

// Claims every press and counts what reaches the handler layer.
class CountingHandler final : public InteractionHandler {
public:
    explicit CountingHandler(int* presses, int* keys) : presses_(presses), keys_(keys) {}
    std::unique_ptr<Gesture> press(const CanvasEvent&) override
    {
        ++*presses_;
        return nullptr;
    }
    bool keyPress(QKeyEvent& event) override
    {
        if (event.key() != Qt::Key_Escape)
            return false;
        ++*keys_;
        return true;
    }

private:
    int* presses_;
    int* keys_;
};

void wheel(CanvasView& view, QPoint viewportPos, int angleDelta, Qt::KeyboardModifiers modifiers)
{
    const QPoint global = view.viewport()->mapToGlobal(viewportPos);
    QWheelEvent event(QPointF(viewportPos), QPointF(global), QPoint(), QPoint(0, angleDelta), Qt::NoButton,
                      modifiers, Qt::NoScrollPhase, false);
    QApplication::sendEvent(view.viewport(), &event);
}

QPoint scrollValues(const CanvasView& view)
{
    return {view.horizontalScrollBar()->value(), view.verticalScrollBar()->value()};
}

QRectF viewportInScene(const CanvasView& view)
{
    return view.mapToScene(view.viewport()->rect()).boundingRect();
}

} // namespace

class TestCanvasView : public QObject {
    Q_OBJECT

private slots:
    void zoomStepsFollowDrawio()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        QSignalSpy zoomed(canvas.get(), &CanvasView::zoomChanged);
        canvas->zoomIn();
        QCOMPARE(canvas->zoom(), 1.2);
        QCOMPARE(zoomed.count(), 1);
        canvas->zoomIn();
        QCOMPARE(canvas->zoom(), 29.0 / 20); // round(24 * 1.2) = 29
        canvas->zoomOut();
        canvas->zoomOut();
        QCOMPARE(canvas->zoom(), 1.0);
        canvas->zoomOut();
        QCOMPARE(canvas->zoom(), 0.85);

        // From between stops the next stop in the direction is taken; 100 % is always reachable exactly.
        canvas->setZoom(0.5);
        std::vector<double> up;
        while (canvas->zoom() < 1) {
            canvas->zoomIn();
            up.push_back(canvas->zoom());
        }
        QCOMPARE(up, (std::vector<double>{0.6, 0.7, 0.85, 1.0}));
        canvas->setZoom(0.52);
        canvas->zoomIn();
        QCOMPARE(canvas->zoom(), 0.6);

        // Range ends.
        canvas->setZoom(CanvasView::kMaxZoom);
        canvas->zoomIn();
        QCOMPARE(canvas->zoom(), CanvasView::kMaxZoom);
        canvas->setZoom(0.05);
        canvas->zoomOut();
        QCOMPARE(canvas->zoom(), 0.05);
    }

    void ctrlWheelZoomsAroundTheCursor()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        const QPoint cursor(300, 200);
        const QPointF sceneUnder = canvas->mapToScene(cursor);
        wheel(*canvas, cursor, 120, Qt::ControlModifier);
        QCOMPARE(canvas->zoom(), 1.2);
        const QPointF after = canvas->mapFromScene(sceneUnder);
        QVERIFY2(std::abs(after.x() - cursor.x()) <= 1 && std::abs(after.y() - cursor.y()) <= 1,
                 qPrintable(QStringLiteral("%1,%2").arg(after.x()).arg(after.y())));
        wheel(*canvas, cursor, -120, Qt::ControlModifier);
        QCOMPARE(canvas->zoom(), 1.0);
    }

    void plainAndShiftWheelScroll()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        const QPoint before = scrollValues(*canvas);
        wheel(*canvas, {200, 200}, -120, Qt::NoModifier);
        QVERIFY(canvas->verticalScrollBar()->value() > before.y());
        QCOMPARE(canvas->horizontalScrollBar()->value(), before.x());
        wheel(*canvas, {200, 200}, -120, Qt::ShiftModifier);
        QVERIFY(canvas->horizontalScrollBar()->value() > before.x());
        QCOMPARE(canvas->zoom(), 1.0);
    }

    void fitWindowCentersAndOnlyZoomsOut()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        canvas->setZoom(0.5);
        canvas->fitWindow();
        QCOMPARE(canvas->zoom(), 1.0); // the content (360 x 260) fits at 100 %, capped there
        const auto b = canvas->diagramScene().pageView().bounds();
        const QRectF content(b.x, b.y, b.width, b.height);
        QVERIFY((canvas->mapFromScene(content.center()) - canvas->viewport()->rect().center()).manhattanLength() <= 2);

        canvas->resize(300, 200);
        canvas->fitWindow();
        QVERIFY(canvas->zoom() < 1);
        const QRectF shown = viewportInScene(*canvas);
        QVERIFY(shown.contains(content));
        QCOMPARE(std::fmod(canvas->zoom() * 20, 1.0), 0.0); // on the 5 % grid
    }

    void fitPageShowsTheFirstTile()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        canvas->fitPage();
        const QRectF shown = viewportInScene(*canvas);
        QVERIFY(shown.contains(QRectF(0, 0, 827, 1169)));
        QVERIFY(canvas->zoom() < 1);
    }

    void fitSelectionIsCappedAtTwo()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        session.selection().set({"a"});
        canvas->fitSelection();
        QCOMPARE(canvas->zoom(), 2.0);
        const QRectF a = canvas->diagramScene().boundsOf({"a"});
        QVERIFY((canvas->mapFromScene(a.center()) - canvas->viewport()->rect().center()).manhattanLength() <= 2);
        session.selection().clear();
        canvas->fitSelection(); // nothing selected: fit all
        QCOMPARE(canvas->zoom(), 1.0);
    }

    void resetZoomShowsThePageOrigin()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        canvas->setZoom(2.4);
        canvas->resetZoom();
        QCOMPARE(canvas->zoom(), 1.0);
        const QPointF origin = canvas->mapFromScene(QPointF(0, 0));
        QVERIFY(std::abs(origin.x() - 20) <= 1 && std::abs(origin.y() - 20) <= 1);
    }

    void centerInsertPointSnapsToTheGrid()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        canvas->centerOn(QRectF(123, 457, 0, 0));
        QCOMPARE(canvas->centerInsertPoint(), QPointF(120, 460));
        QVERIFY(session.edit(QStringLiteral("Grid off"), [](commands::Transaction& tx) {
                    commands::ops::PageProperties p;
                    p.grid = false;
                    return ops::setPageProperties(tx, 0, p);
                }).ok);
        const QPointF free = canvas->centerInsertPoint();
        QCOMPARE(free, QPointF(std::round(free.x()), std::round(free.y())));
        QVERIFY(std::abs(free.x() - 123) <= 1 && std::abs(free.y() - 457) <= 1);
    }

    void rightClickRequestsAContextMenuAndSelects()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        QSignalSpy menu(canvas.get(), &CanvasView::contextMenuRequested);
        press(*canvas, {100, 70}, Qt::NoModifier, Qt::RightButton);
        release(*canvas, {100, 70}, Qt::NoModifier, Qt::RightButton);
        QCOMPARE(menu.count(), 1);
        QCOMPARE(menu.first().at(1).toPointF(), QPointF(100, 70));
        QCOMPARE(session.selection().cells(), (std::vector<std::string>{"a"}));

        // A selected cell stays as is; blank canvas keeps the selection.
        session.selection().set({"a", "b"});
        press(*canvas, {100, 70}, Qt::NoModifier, Qt::RightButton);
        release(*canvas, {100, 70}, Qt::NoModifier, Qt::RightButton);
        QCOMPARE(session.selection().size(), std::size_t(2));
        press(*canvas, {600, 500}, Qt::NoModifier, Qt::RightButton);
        release(*canvas, {600, 500}, Qt::NoModifier, Qt::RightButton);
        QCOMPARE(menu.count(), 3);
        QCOMPARE(session.selection().size(), std::size_t(2));
    }

    void rightDragPansWithoutAMenu()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        int presses = 0;
        int keys = 0;
        canvas->interaction().addHandler(std::make_unique<CountingHandler>(&presses, &keys), 0);
        QSignalSpy menu(canvas.get(), &CanvasView::contextMenuRequested);
        const QPoint before = scrollValues(*canvas);
        press(*canvas, {400, 300}, Qt::NoModifier, Qt::RightButton);
        for (int i = 1; i <= 5; ++i)
            move(*canvas, QPointF(400 - 20 * i, 300 - 10 * i), Qt::NoModifier, Qt::RightButton);
        release(*canvas, {300, 250}, Qt::NoModifier, Qt::RightButton);
        QCOMPARE(menu.count(), 0);
        QCOMPARE(presses, 0);
        QVERIFY(scrollValues(*canvas).x() > before.x());
        QVERIFY(scrollValues(*canvas).y() > before.y());
    }

    void middleDragPans()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        int presses = 0;
        int keys = 0;
        canvas->interaction().addHandler(std::make_unique<CountingHandler>(&presses, &keys), 0);
        const QPoint before = scrollValues(*canvas);
        const QPointF start(300, 300);
        press(*canvas, start, Qt::NoModifier, Qt::MiddleButton);
        QCOMPARE(canvas->viewport()->cursor().shape(), Qt::ClosedHandCursor);
        move(*canvas, start + QPointF(-50, 0), Qt::NoModifier, Qt::MiddleButton);
        release(*canvas, start + QPointF(-50, 0), Qt::NoModifier, Qt::MiddleButton);
        QCOMPARE(presses, 0);
        QVERIFY(scrollValues(*canvas).x() > before.x());
    }

    void spaceDragPansAndNeverReachesHandlers()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        int presses = 0;
        int keys = 0;
        canvas->interaction().addHandler(std::make_unique<CountingHandler>(&presses, &keys), 0);

        QTest::keyPress(canvas.get(), Qt::Key_Space);
        QCOMPARE(canvas->viewport()->cursor().shape(), Qt::OpenHandCursor);
        const QPoint before = scrollValues(*canvas);
        drag(*canvas, {300, 300}, {200, 250});
        QVERIFY(scrollValues(*canvas).x() > before.x());
        QVERIFY(scrollValues(*canvas).y() > before.y());
        QCOMPARE(presses, 0);
        QTest::keyRelease(canvas.get(), Qt::Key_Space);
        QCOMPARE(canvas->viewport()->cursor().shape(), Qt::ArrowCursor);

        click(*canvas, {300, 300});
        QCOMPARE(presses, 1); // without Space the handlers see the press again
    }

    void keysGoToHandlersFirstAndArrowsNeverScroll()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        int presses = 0;
        int keys = 0;
        canvas->interaction().addHandler(std::make_unique<CountingHandler>(&presses, &keys), 0);
        QTest::keyClick(canvas.get(), Qt::Key_Escape);
        QCOMPARE(keys, 1);
        const QPoint before = scrollValues(*canvas);
        for (Qt::Key key : {Qt::Key_Left, Qt::Key_Right, Qt::Key_Up, Qt::Key_Down})
            QTest::keyClick(canvas.get(), key);
        QCOMPARE(scrollValues(*canvas), before);
    }

    void sceneRectGrowsByOneViewportAndKeepsTheVisibleArea()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        canvas->setZoom(0.5);
        QRectF area = canvas->diagramScene().pageArea();
        QRectF shown = viewportInScene(*canvas);
        QRectF rect = canvas->sceneRect();
        QVERIFY(rect.left() <= area.left() - shown.width() + 1);
        QVERIFY(rect.right() >= area.right() + shown.width() - 1);
        QVERIFY(rect.top() <= area.top() - shown.height() + 1);
        QVERIFY(rect.bottom() >= area.bottom() + shown.height() - 1);

        // Content far to the right grows the page area; the view must not jump.
        const QPointF topLeft = canvas->mapToScene(QPoint(0, 0));
        QVERIFY(session.edit(QStringLiteral("Add"), [](commands::Transaction& tx) {
                    return ops::addCells(tx, 0, {core::Cell::makeVertex("far", "1", "F", "", core::Rect{4000, 100, 50, 50})});
                }).ok);
        QVERIFY(canvas->diagramScene().pageArea().right() > area.right());
        QVERIFY(canvas->sceneRect().right() > rect.right());
        const QPointF after = canvas->mapToScene(QPoint(0, 0));
        QVERIFY(std::abs(after.x() - topLeft.x()) <= 2 / canvas->zoom() && std::abs(after.y() - topLeft.y()) <= 2 / canvas->zoom());
    }

    void focusLossCancelsPanAndSpace()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        QTest::keyPress(canvas.get(), Qt::Key_Space);
        QFocusEvent out(QEvent::FocusOut, Qt::ActiveWindowFocusReason);
        QApplication::sendEvent(canvas.get(), &out);
        QCOMPARE(canvas->viewport()->cursor().shape(), Qt::ArrowCursor);
    }
};

QTEST_MAIN(TestCanvasView)
#include "tst_canvas_view.moc"
