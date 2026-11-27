// click selection, drill-down, toggle, rubber band, enter/exit
// group, keyboard (Escape, nudge).

#include "support/select_fixture.h"

#include "qraw/commands/ops_properties.h"

#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

class TestSelectSelection : public QObject {
    Q_OBJECT

private slots:
    void clickSelectsVertexAndBlankClears()
    {
        SelectFixture f;
        click(f.canvas(), {100, 70});
        QCOMPARE(f.selected(), std::vector<std::string>{"a"});
        click(f.canvas(), {600, 450});
        QVERIFY(f.selected().empty());
        QCOMPARE(savedBytes(f.session), f.original); // selecting never edits
    }

    void clickOnGroupChildSelectsGroupThenChild()
    {
        SelectFixture f;
        click(f.canvas(), {80, 220}); // inside c1
        QCOMPARE(f.selected(), std::vector<std::string>{"g"});
        click(f.canvas(), {80, 220});
        QCOMPARE(f.selected(), std::vector<std::string>{"c1"});
    }

    void dragOfSelectedGroupByItsChildMovesTheGroup()
    {
        SelectFixture f;
        f.setGrid(false);
        click(f.canvas(), {80, 220});
        drag(f.canvas(), {80, 220}, {95, 235}, Qt::AltModifier); // Alt: unsnapped
        QCOMPARE(f.selected(), std::vector<std::string>{"g"});
        QCOMPARE(f.bounds("g").topLeft(), QPointF(55, 215));
    }

    void shiftClickTogglesAndKeepsOtherCells()
    {
        SelectFixture f;
        click(f.canvas(), {100, 70});
        click(f.canvas(), {340, 70}, Qt::ShiftModifier);
        QVERIFY(sameIds(f.selected(), {"a", "b"}));
        click(f.canvas(), {100, 70}, Qt::ShiftModifier);
        QCOMPARE(f.selected(), std::vector<std::string>{"b"});
        click(f.canvas(), {100, 70}, Qt::ControlModifier);
        QVERIFY(sameIds(f.selected(), {"a", "b"}));
    }

    void clickOnOneOfSeveralSelectedCellsSelectsOnlyIt()
    {
        SelectFixture f;
        f.session.selection().set({"a", "b"});
        click(f.canvas(), {100, 70});
        QCOMPARE(f.selected(), std::vector<std::string>{"a"});
    }

    void rubberBandSelectsCellsInsideOnly()
    {
        SelectFixture f;
        drag(f.canvas(), {20, 20}, {430, 120});
        QVERIFY(f.session.selection().contains("a"));
        QVERIFY(f.session.selection().contains("b"));
        for (const char* inGroup : {"c1", "c2", "g"})
            QVERIFY2(!f.session.selection().contains(inGroup), inGroup);
        QCOMPARE(savedBytes(f.session), f.original);
    }

    void rubberBandWithShiftAddsToSelection()
    {
        SelectFixture f;
        f.session.selection().set({"g"});
        drag(f.canvas(), {20, 20}, {200, 120}, Qt::ShiftModifier); // only a lies inside
        QVERIFY(sameIds(f.selected(), {"g", "a"}));
    }

    void rubberBandInsideEnteredGroupSelectsItsChildren()
    {
        SelectFixture f;
        f.session.enterGroup("g");
        drag(f.canvas(), {20, 190}, {300, 320});
        QVERIFY(sameIds(f.selected(), {"c1", "c2"}));
    }

    void doubleClickEntersGroupAndBlankExitsIt()
    {
        SelectFixture f;
        doubleClick(f.canvas(), {80, 220});
        QCOMPARE(f.session.enteredGroup(), std::string("g"));
        QCOMPARE(f.selected(), std::vector<std::string>{"c1"});
        // Inside the entered group a click selects the child itself.
        click(f.canvas(), {200, 280});
        QCOMPARE(f.selected(), std::vector<std::string>{"c2"});
        doubleClick(f.canvas(), {600, 450});
        QVERIFY(f.session.enteredGroup().empty());
    }

    void doubleClickOnPlainVertexIsNotHandled()
    {
        SelectFixture f;
        doubleClick(f.canvas(), {100, 70});
        QVERIFY(f.session.enteredGroup().empty());
    }

    void escapeClearsSelectionThenLeavesGroup()
    {
        SelectFixture f;
        f.session.enterGroup("g");
        f.session.selection().set({"c1"});
        QTest::keyClick(&f.canvas(), Qt::Key_Escape);
        QVERIFY(f.selected().empty());
        QCOMPARE(f.session.enteredGroup(), std::string("g"));
        QTest::keyClick(&f.canvas(), Qt::Key_Escape);
        QVERIFY(f.session.enteredGroup().empty());
    }

    void lockedCellSelectsButDoesNotMove()
    {
        SelectFixture f;
        f.session.edit(QStringLiteral("Lock"), [](commands::Transaction& tx) {
            return commands::ops::setStyle(tx, 0, {"a"}, "locked", "1");
        });
        const QByteArray before = savedBytes(f.session);
        drag(f.canvas(), {100, 70}, {160, 130});
        QCOMPARE(f.selected(), std::vector<std::string>{"a"});
        QCOMPARE(savedBytes(f.session), before);
        QTest::keyClick(&f.canvas(), Qt::Key_Right);
        QCOMPARE(savedBytes(f.session), before);
    }

    void movableZeroCellDoesNotMove()
    {
        SelectFixture f;
        f.session.edit(QStringLiteral("Pin"), [](commands::Transaction& tx) {
            return commands::ops::setStyle(tx, 0, {"a"}, "movable", "0");
        });
        const QByteArray before = savedBytes(f.session);
        drag(f.canvas(), {100, 70}, {160, 130});
        QCOMPARE(savedBytes(f.session), before);
    }

    void arrowKeysNudgeByOneAndShiftByGridStep()
    {
        SelectFixture f;
        f.session.selection().set({"a"});
        QTest::keyClick(&f.canvas(), Qt::Key_Right);
        QTest::keyClick(&f.canvas(), Qt::Key_Right);
        QTest::keyClick(&f.canvas(), Qt::Key_Down);
        QCOMPARE(f.geometry("a").x, 42.0);
        QCOMPARE(f.geometry("a").y, 41.0);
        QTest::keyClick(&f.canvas(), Qt::Key_Down, Qt::ShiftModifier); // gridSize = 10
        QCOMPARE(f.geometry("a").y, 51.0);
        // The whole sequence is one undo entry.
        QVERIFY(f.undoRestores(f.original));
    }

    void anotherKeyEndsTheNudgeSequence()
    {
        SelectFixture f;
        f.session.selection().set({"a"});
        QTest::keyClick(&f.canvas(), Qt::Key_Left);
        QTest::keyClick(&f.canvas(), Qt::Key_Left);
        QTest::keyClick(&f.canvas(), Qt::Key_A); // not handled, but ends the sequence
        QTest::keyClick(&f.canvas(), Qt::Key_Left);
        QCOMPARE(f.geometry("a").x, 37.0);
        QVERIFY(f.session.undo());
        QCOMPARE(f.geometry("a").x, 38.0);
        QVERIFY(f.session.undo());
        QCOMPARE(savedBytes(f.session), f.original);
    }

    void nudgeSequenceEndsOnMousePress()
    {
        SelectFixture f;
        f.session.selection().set({"a"});
        QTest::keyClick(&f.canvas(), Qt::Key_Left);
        click(f.canvas(), {100, 70}); // selects nothing new, ends the sequence
        QTest::keyClick(&f.canvas(), Qt::Key_Left);
        QVERIFY(f.session.undo());
        QCOMPARE(f.geometry("a").x, 39.0);
    }

    void arrowWithoutSelectionIsNotConsumed()
    {
        SelectFixture f;
        QKeyEvent event(QEvent::KeyPress, Qt::Key_Left, Qt::NoModifier);
        QVERIFY(!f.canvas().interaction().keyPress(event));
    }

    void ctrlArrowIsLeftToWindowShortcuts()
    {
        SelectFixture f;
        f.session.selection().set({"a"});
        QKeyEvent event(QEvent::KeyPress, Qt::Key_Left, Qt::ControlModifier);
        QVERIFY(!f.canvas().interaction().keyPress(event));
        QCOMPARE(savedBytes(f.session), f.original);
    }

    void hoverOverSelectedCellShowsMoveCursor()
    {
        SelectFixture f;
        f.session.selection().set({"a"});
        sendMouse(f.canvas(), QEvent::MouseMove, {100, 70}, Qt::NoButton, Qt::NoButton);
        QCOMPARE(f.canvas().interaction().cursor(), Qt::SizeAllCursor);
        sendMouse(f.canvas(), QEvent::MouseMove, {340, 70}, Qt::NoButton, Qt::NoButton);
        QCOMPARE(f.canvas().interaction().cursor(), Qt::ArrowCursor);
    }

    void handlersBuildWithEmptyContext()
    {
        ToolContext empty;
        auto handlers = createSelectionHandlers(empty);
        QCOMPARE(handlers.size(), std::size_t(5)); // link icon, fold control, selection, table handles, vertex handles
        CanvasEvent event;
        event.button = Qt::LeftButton;
        for (auto& [handler, priority] : handlers) {
            QVERIFY(!handler->press(event));
            QVERIFY(!handler->hover(event));
            QVERIFY(!handler->doubleClick(event));
        }
    }
};

QTEST_MAIN(TestSelectSelection)
#include "tst_select_selection.moc"
