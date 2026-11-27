// Keyboard navigation between cells: Tab, Shift+Tab, Alt+Tab and Alt+Shift+Tab on the focused
// canvas select cells instead of moving the focus (Graph.selectCell, COMPATIBILITY.md "Keyboard
// navigation"); Ctrl+Tab leaves the canvas; the arrows move the selection.

#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/ui/cell_navigation.h"
#include "qraw/ui/main_window.h"

#include <QScrollBar>
#include <QTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

// Layer "1" with a, b and a group g (children c1, c2) and the connector e; layer "L" is locked and holds x;
// the cell "h" on layer 1 is hidden; "far" lies 6000 px right and 5000 px down.
constexpr char kNavigationDiagram[] = R"(<mxfile><diagram id="p1" name="Page-1"><mxGraphModel dx="800" dy="600" grid="1" gridSize="10" guides="1" tooltips="1" connect="1" arrows="1" fold="1" page="1" pageScale="1" pageWidth="827" pageHeight="1169" math="0" shadow="0"><root><mxCell id="0"/><mxCell id="1" parent="0"/><mxCell id="a" value="A" style="rounded=0;" vertex="1" parent="1"><mxGeometry x="40" y="40" width="120" height="60" as="geometry"/></mxCell><mxCell id="h" value="H" style="" vertex="1" visible="0" parent="1"><mxGeometry x="40" y="120" width="60" height="30" as="geometry"/></mxCell><mxCell id="b" value="B" style="ellipse;" vertex="1" parent="1"><mxGeometry x="280" y="40" width="120" height="60" as="geometry"/></mxCell><mxCell id="e" style="" edge="1" parent="1" source="a" target="b"><mxGeometry relative="1" as="geometry"/></mxCell><mxCell id="g" value="" style="group" vertex="1" connectable="0" parent="1"><mxGeometry x="40" y="200" width="200" height="100" as="geometry"/></mxCell><mxCell id="c1" value="C1" style="" vertex="1" parent="g"><mxGeometry width="80" height="40" as="geometry"/></mxCell><mxCell id="c2" value="C2" style="" vertex="1" parent="g"><mxGeometry x="120" y="60" width="80" height="40" as="geometry"/></mxCell><mxCell id="far" value="Far" style="" vertex="1" parent="1"><mxGeometry x="6000" y="5000" width="100" height="50" as="geometry"/></mxCell><mxCell id="L" value="Locked" style="locked=1;" parent="0"/><mxCell id="x" value="X" style="" vertex="1" parent="L"><mxGeometry x="500" y="40" width="60" height="30" as="geometry"/></mxCell></root></mxGraphModel></diagram></mxfile>)";

// What Tab visits, in order: the visible, selectable vertices and edges, parents before their children.
const std::vector<std::string> kTabOrder{"a", "b", "e", "g", "c1", "c2", "far"};

struct Fixture {
    explicit Fixture(std::string_view xml = kNavigationDiagram)
    {
        window.session().resetDocument(documentFromXml(xml));
        window.resize(1100, 800);
        window.show();
        window.activateWindow();
        QVERIFY2(QTest::qWaitForWindowActive(&window), "window not active");
        canvas().setFocus();
        QVERIFY(canvas().hasFocus());
    }

    CanvasView& canvas() { return window.canvas(); }
    DocumentSession& session() { return window.session(); }
    std::vector<std::string> selected() { return session().selection().cells(); }
    void key(Qt::Key key, Qt::KeyboardModifiers modifiers = Qt::NoModifier) { QTest::keyClick(&canvas(), key, modifiers); }
    // Shift+Tab as a keyboard sends it.
    void backtab() { key(Qt::Key_Backtab, Qt::ShiftModifier); }

    MainWindow window;
};

} // namespace

class TestKeyboardNavigation : public QObject {
    Q_OBJECT

private slots:
    void tabOrderSkipsHiddenAndLockedCells();
    void tabVisitsTheCellsInOrderAndWraps();
    void shiftTabGoesBackwardsFromTheLastCell();
    void tabKeepsTheFocusOnTheCanvas();
    void ctrlTabLeavesTheCanvas();
    void tabOnAnEmptyPageMovesTheFocusOn();
    void altTabEntersAContainerAndAltShiftTabLeavesIt();
    void anEnteredGroupKeepsTabInsideIt();
    void tabScrollsTheCellIntoView();
    void arrowsMoveTheSelectedCellAsOneUndoStepPerSequence();
    void shiftArrowMovesByTheGridSize();
    void arrowsMoveTheCellTabSelected();
    void tabDoesNothingToTheDocument();
};

void TestKeyboardNavigation::tabOrderSkipsHiddenAndLockedCells()
{
    Fixture f;
    const std::vector<std::string> order = navigation::tabOrder(*f.session().currentPage(), f.canvas().diagramScene().pageView(), "");
    QCOMPARE(order, kTabOrder);
    // A cell that is selected stays in the order, so that Tab can leave it (draw.io: temp == cell).
    const std::vector<std::string> withLocked =
        navigation::tabOrder(*f.session().currentPage(), f.canvas().diagramScene().pageView(), "", "x");
    QVERIFY(std::find(withLocked.begin(), withLocked.end(), "x") != withLocked.end());
    // The neighbour function is plain arithmetic on the order.
    QCOMPARE(navigation::neighbour({"p", "q", "r"}, "r", true), std::string("p"));
    QCOMPARE(navigation::neighbour({"p", "q", "r"}, "p", false), std::string("r"));
    QCOMPARE(navigation::neighbour({"p", "q", "r"}, "", true), std::string("p"));
    QCOMPARE(navigation::neighbour({"p", "q", "r"}, "", false), std::string("r"));
    QCOMPARE(navigation::neighbour({"p", "q", "r"}, "gone", true), std::string("p"));
    QCOMPARE(navigation::neighbour({}, "p", true), std::string());
}

void TestKeyboardNavigation::tabVisitsTheCellsInOrderAndWraps()
{
    Fixture f;
    QVERIFY(f.selected().empty());
    for (const std::string& id : kTabOrder) {
        f.key(Qt::Key_Tab);
        QCOMPARE(f.selected(), std::vector<std::string>{id});
    }
    f.key(Qt::Key_Tab);
    QCOMPARE(f.selected(), std::vector<std::string>{"a"});
}

void TestKeyboardNavigation::shiftTabGoesBackwardsFromTheLastCell()
{
    Fixture f;
    f.backtab();
    QCOMPARE(f.selected(), std::vector<std::string>{"far"});
    f.backtab();
    QCOMPARE(f.selected(), std::vector<std::string>{"c2"});
    // Qt also delivers Shift+Tab as Key_Tab with the Shift modifier on some platforms.
    f.key(Qt::Key_Tab, Qt::ShiftModifier);
    QCOMPARE(f.selected(), std::vector<std::string>{"c1"});
    f.session().selection().set({"a"});
    f.backtab(); // wraps from the first cell to the last
    QCOMPARE(f.selected(), std::vector<std::string>{"far"});
}

void TestKeyboardNavigation::tabKeepsTheFocusOnTheCanvas()
{
    Fixture f;
    for (int i = 0; i < 12; ++i) {
        f.key(Qt::Key_Tab);
        QVERIFY2(f.canvas().hasFocus(), "Tab moved the keyboard focus out of the canvas");
    }
    f.backtab();
    QVERIFY(f.canvas().hasFocus());
}

void TestKeyboardNavigation::ctrlTabLeavesTheCanvas()
{
    Fixture f;
    f.key(Qt::Key_Tab);
    f.key(Qt::Key_Tab, Qt::ControlModifier);
    QVERIFY2(!f.canvas().hasFocus(), "Ctrl+Tab did not leave the canvas");
    QCOMPARE(f.selected(), std::vector<std::string>{"a"}); // the selection is untouched
    f.canvas().setFocus();
    f.key(Qt::Key_Backtab, Qt::ControlModifier | Qt::ShiftModifier);
    QVERIFY(!f.canvas().hasFocus());
    QCOMPARE(f.selected(), std::vector<std::string>{"a"});
}

void TestKeyboardNavigation::tabOnAnEmptyPageMovesTheFocusOn()
{
    Fixture f(R"(<mxfile><diagram id="p" name="Empty"><mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/></root></mxGraphModel></diagram></mxfile>)");
    f.key(Qt::Key_Tab);
    QVERIFY2(!f.canvas().hasFocus(), "Tab on an empty page trapped the keyboard on the canvas");
}

void TestKeyboardNavigation::altTabEntersAContainerAndAltShiftTabLeavesIt()
{
    Fixture f;
    f.session().selection().set({"g"});
    f.key(Qt::Key_Tab, Qt::AltModifier);
    QCOMPARE(f.selected(), std::vector<std::string>{"c1"}); // the first child
    f.key(Qt::Key_Backtab, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(f.selected(), std::vector<std::string>{"g"}); // its container
    f.key(Qt::Key_Backtab, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(f.selected(), std::vector<std::string>{"g"}); // a cell on a layer has no container: nothing changes
    // A cell without children stays selected.
    f.session().selection().set({"a"});
    f.key(Qt::Key_Tab, Qt::AltModifier);
    QCOMPARE(f.selected(), std::vector<std::string>{"a"});
    // Alt+Tab with nothing selected does nothing.
    f.session().selection().clear();
    f.key(Qt::Key_Tab, Qt::AltModifier);
    QVERIFY(f.selected().empty());
    QVERIFY(f.canvas().hasFocus());
}

void TestKeyboardNavigation::anEnteredGroupKeepsTabInsideIt()
{
    Fixture f;
    f.session().enterGroup("g");
    f.key(Qt::Key_Tab);
    QCOMPARE(f.selected(), std::vector<std::string>{"c1"});
    f.key(Qt::Key_Tab);
    QCOMPARE(f.selected(), std::vector<std::string>{"c2"});
    f.key(Qt::Key_Tab);
    QCOMPARE(f.selected(), std::vector<std::string>{"c1"});
    // The entered group is the top of the view: its children have no container to go to.
    f.key(Qt::Key_Backtab, Qt::AltModifier | Qt::ShiftModifier);
    QCOMPARE(f.selected(), std::vector<std::string>{"c1"});
}

void TestKeyboardNavigation::tabScrollsTheCellIntoView()
{
    Fixture f;
    f.canvas().setZoom(1);
    f.canvas().resetZoom();
    f.session().selection().set({"c2"});
    f.key(Qt::Key_Tab); // far
    QCOMPARE(f.selected(), std::vector<std::string>{"far"});
    const QRectF visible = f.canvas().mapToScene(f.canvas().viewport()->rect()).boundingRect();
    const QRectF cell = f.canvas().diagramScene().boundsOf({"far"});
    QVERIFY2(visible.contains(cell), qPrintable(QStringLiteral("cell %1,%2 outside the view %3,%4 %5x%6")
                                                    .arg(cell.x()).arg(cell.y()).arg(visible.x()).arg(visible.y())
                                                    .arg(visible.width()).arg(visible.height())));
    f.key(Qt::Key_Tab); // wraps to a, which is far away now
    QCOMPARE(f.selected(), std::vector<std::string>{"a"});
    const QRectF visibleAgain = f.canvas().mapToScene(f.canvas().viewport()->rect()).boundingRect();
    QVERIFY(visibleAgain.contains(f.canvas().diagramScene().boundsOf({"a"})));
}

void TestKeyboardNavigation::arrowsMoveTheSelectedCellAsOneUndoStepPerSequence()
{
    Fixture f;
    const QByteArray original = savedBytes(f.session());
    f.session().selection().set({"a"});
    f.key(Qt::Key_Right);
    f.key(Qt::Key_Right);
    f.key(Qt::Key_Down);
    const core::Rect moved = f.session().currentPage()->findCell("a")->geometry()->bounds();
    QCOMPARE(moved.x, 42.0); // draw.io's nudge: one page unit a key press
    QCOMPARE(moved.y, 41.0);
    QVERIFY(f.session().undo()); // the sequence is one undo step
    QCOMPARE(savedBytes(f.session()), original);
}

void TestKeyboardNavigation::shiftArrowMovesByTheGridSize()
{
    Fixture f;
    f.session().selection().set({"a"});
    f.key(Qt::Key_Left, Qt::ShiftModifier);
    QCOMPARE(f.session().currentPage()->findCell("a")->geometry()->x(), 30.0); // gridSize is 10
}

void TestKeyboardNavigation::arrowsMoveTheCellTabSelected()
{
    Fixture f;
    f.key(Qt::Key_Tab); // a
    f.key(Qt::Key_Tab); // b
    f.key(Qt::Key_Up);
    QCOMPARE(f.session().currentPage()->findCell("b")->geometry()->y(), 39.0);
    QCOMPARE(f.session().currentPage()->findCell("a")->geometry()->y(), 40.0);
}

void TestKeyboardNavigation::tabDoesNothingToTheDocument()
{
    Fixture f;
    const QByteArray original = savedBytes(f.session());
    const bool modified = f.session().isModified();
    for (int i = 0; i < 9; ++i)
        f.key(Qt::Key_Tab);
    QCOMPARE(savedBytes(f.session()), original);
    QCOMPARE(f.session().isModified(), modified);
    QVERIFY(!f.session().canUndo());
}

SHELL_TEST_MAIN(TestKeyboardNavigation)
#include "tst_keyboard_navigation.moc"
