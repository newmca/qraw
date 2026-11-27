// moving with grid snapping, alignment guides, Alt, Shift,
// Escape, Ctrl+drag duplication.

#include "support/select_fixture.h"

#include "qraw/ui/interaction.h"

#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

// Paints the overlay of the canvas' controller into an image and reports
// whether anything non-transparent was drawn (the preview of a gesture).
bool overlayDraws(CanvasView& view)
{
    QImage image(800, 600, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    OverlayContext context;
    context.scale = 1;
    view.interaction().paintOverlay(painter, context);
    painter.end();
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            if (qAlpha(image.pixel(x, y)) != 0)
                return true;
    return false;
}

} // namespace

class TestSelectMove : public QObject {
    Q_OBJECT

private slots:
    void dragSnapsToGridAndIsOneUndoEntry()
    {
        SelectFixture f;
        drag(f.canvas(), {100, 70}, {133, 77});
        QCOMPARE(f.selected(), std::vector<std::string>{"a"});
        QCOMPARE(f.geometry("a").x, 70.0);
        QCOMPARE(f.geometry("a").y, 50.0);
        QVERIFY(f.undoRestores(f.original));
        QVERIFY(!f.session.canUndo());
    }

    void altDragMovesUnsnapped()
    {
        SelectFixture f;
        drag(f.canvas(), {100, 70}, {133, 77}, Qt::AltModifier);
        QCOMPARE(f.geometry("a").x, 73.0);
        QCOMPARE(f.geometry("a").y, 47.0);
        QVERIFY(f.undoRestores(f.original));
    }

    void shiftDragConstrainsToDominantAxis()
    {
        SelectFixture f;
        f.setGrid(false);
        drag(f.canvas(), {100, 70}, {130, 78}, Qt::ShiftModifier | Qt::AltModifier);
        // Shift also toggles: a is selected and stays so; only x moved.
        QCOMPARE(f.geometry("a").x, 70.0);
        QCOMPARE(f.geometry("a").y, 40.0);
    }

    void dragBelowToleranceIsAClick()
    {
        SelectFixture f;
        press(f.canvas(), {100, 70});
        move(f.canvas(), {102, 71});
        release(f.canvas(), {102, 71});
        QCOMPARE(savedBytes(f.session), f.original);
        QCOMPARE(f.selected(), std::vector<std::string>{"a"});
    }

    void guidesAlignTopWithNeighbourWhenGridIsOff()
    {
        SelectFixture f;
        f.setGrid(false);
        const QByteArray before = savedBytes(f.session);
        // a's top would land on y=42; b's top is 40 -> snaps within 4 px.
        drag(f.canvas(), {100, 70}, {110, 72});
        QCOMPARE(f.geometry("a").y, 40.0);
        QCOMPARE(f.geometry("a").x, 50.0); // x has no guide and no grid
        QVERIFY(f.undoRestores(before));
    }

    void guidesAreOffWhenDisabledOnTheView()
    {
        SelectFixture f;
        f.setGrid(false);
        f.canvas().setGuidesEnabled(false);
        drag(f.canvas(), {100, 70}, {110, 72});
        QCOMPARE(f.geometry("a").y, 42.0);
    }

    void movingGroupDoesNotSnapToItsOwnChildren()
    {
        SelectFixture f;
        f.setGrid(false);
        // g's top is 200, like its child c1's. Dragged up by 4 px it must go
        // to 196 instead of snapping back onto c1's (unmoved) top line.
        drag(f.canvas(), {120, 280}, {120, 276});
        QCOMPARE(f.selected(), std::vector<std::string>{"g"});
        QCOMPARE(f.geometry("g").y, 196.0);
    }

    void escapeDuringDragCancelsWithoutEditing()
    {
        SelectFixture f;
        press(f.canvas(), {100, 70});
        move(f.canvas(), {140, 100});
        move(f.canvas(), {160, 120});
        QVERIFY(f.canvas().interaction().isGestureActive());
        QVERIFY(overlayDraws(f.canvas()));
        QTest::keyClick(&f.canvas(), Qt::Key_Escape);
        QVERIFY(!f.canvas().interaction().isGestureActive());
        release(f.canvas(), {160, 120});
        QCOMPARE(savedBytes(f.session), f.original);
        QVERIFY(!f.session.canUndo());
    }

    void previewDoesNotTouchTheModelUntilRelease()
    {
        SelectFixture f;
        press(f.canvas(), {100, 70});
        move(f.canvas(), {150, 120});
        QCOMPARE(savedBytes(f.session), f.original);
        QCOMPARE(f.canvas().interaction().cursor(), Qt::SizeAllCursor);
        release(f.canvas(), {150, 120});
        QVERIFY(f.session.canUndo());
    }

    void movingTwoCellsMovesBothByTheSameDelta()
    {
        SelectFixture f;
        f.session.selection().set({"a", "b"});
        drag(f.canvas(), {100, 70}, {120, 90});
        QCOMPARE(f.geometry("a").x, 60.0);
        QCOMPARE(f.geometry("b").x, 300.0);
        QCOMPARE(f.geometry("a").y, 60.0);
        QCOMPARE(f.geometry("b").y, 60.0);
        QVERIFY(f.undoRestores(f.original));
    }

    void movingAGroupMovesItsChildrenWithoutSelectingThem()
    {
        SelectFixture f;
        drag(f.canvas(), {80, 220}, {100, 240});
        QCOMPARE(f.selected(), std::vector<std::string>{"g"});
        QCOMPARE(f.bounds("c1").topLeft(), QPointF(60, 220));
        QCOMPARE(f.geometry("c1").x, 0.0); // children keep their group coordinates
        QVERIFY(f.undoRestores(f.original));
    }

    void ctrlDragDuplicatesTheSelection()
    {
        if (!transferAvailable())
            QSKIP("needs cell_transfer");
        SelectFixture f;
        // Ctrl is also the toggle modifier: select first, then Ctrl+drag it.
        f.session.selection().set({"a"});
        const std::size_t cellsBefore = f.session.currentPage()->cellCount();
        drag(f.canvas(), {100, 70}, {100, 150}, Qt::ControlModifier);
        QCOMPARE(f.session.currentPage()->cellCount(), cellsBefore + 1);
        QCOMPARE(f.geometry("a").y, 40.0); // the original stays
        QCOMPARE(f.session.selection().size(), std::size_t(1));
        QVERIFY(f.session.selection().first() != "a");
        QVERIFY(f.undoRestores(f.original));
    }
};

QTEST_MAIN(TestSelectMove)
#include "tst_select_move.moc"
