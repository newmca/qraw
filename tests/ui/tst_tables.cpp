// Tables in the editor (docs/design/ui.md section 10): selecting a cell
// takes two clicks, the Arrange tab edits rows and columns, a dragged border
// resizes a column, and edges connect to rows. Every edit is one undo step.
// Merged cells: a range of cells is selected with Shift+click, Merge Cells and
// Unmerge Cells are in the Arrange tab, and the layout grows the merged cell.

#include "support/select_fixture.h"

#include "qraw/ui/format_panel.h"

#include "qraw/ui/action_registry.h"
#include "qraw/ui/edit_actions.h"
#include "qraw/ui/main_window.h"

#include <QMenu>
#include <QTabWidget>
#include <QToolButton>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

// A free box "src" and a 200x90 table "t" at (200,100): a 30 title and two 30 rows of two 100 cells.
constexpr char kTableDiagram[] = R"(<mxfile><diagram id="p1" name="Page-1"><mxGraphModel grid="1" gridSize="10" page="0"><root><mxCell id="0"/><mxCell id="1" parent="0"/><mxCell id="src" value="" style="rounded=0;" vertex="1" parent="1"><mxGeometry x="20" y="20" width="60" height="40" as="geometry"/></mxCell><mxCell id="t" value="Table" style="shape=table;startSize=30;container=1;collapsible=0;childLayout=tableLayout;fixedRows=1;rowLines=0;resizeLast=0;" vertex="1" parent="1"><mxGeometry x="200" y="100" width="200" height="90" as="geometry"/></mxCell><mxCell id="r1" value="" style="shape=tableRow;horizontal=0;startSize=0;swimlaneHead=0;swimlaneBody=0;fillColor=none;collapsible=0;dropTarget=0;points=[[0,0.5],[1,0.5]];portConstraint=eastwest;" vertex="1" parent="t"><mxGeometry y="30" width="200" height="30" as="geometry"/></mxCell><mxCell id="r1c1" value="a" style="shape=partialRectangle;connectable=0;fillColor=none;overflow=hidden;html=1;" vertex="1" parent="r1"><mxGeometry width="100" height="30" as="geometry"><mxRectangle width="100" height="30" as="alternateBounds"/></mxGeometry></mxCell><mxCell id="r1c2" value="b" style="shape=partialRectangle;connectable=0;fillColor=none;overflow=hidden;html=1;" vertex="1" parent="r1"><mxGeometry x="100" width="100" height="30" as="geometry"><mxRectangle width="100" height="30" as="alternateBounds"/></mxGeometry></mxCell><mxCell id="r2" value="" style="shape=tableRow;horizontal=0;startSize=0;swimlaneHead=0;swimlaneBody=0;fillColor=none;collapsible=0;dropTarget=0;points=[[0,0.5],[1,0.5]];portConstraint=eastwest;" vertex="1" parent="t"><mxGeometry y="60" width="200" height="30" as="geometry"/></mxCell><mxCell id="r2c1" value="c" style="shape=partialRectangle;connectable=0;fillColor=none;overflow=hidden;html=1;" vertex="1" parent="r2"><mxGeometry width="100" height="30" as="geometry"><mxRectangle width="100" height="30" as="alternateBounds"/></mxGeometry></mxCell><mxCell id="r2c2" value="d" style="shape=partialRectangle;connectable=0;fillColor=none;overflow=hidden;html=1;" vertex="1" parent="r2"><mxGeometry x="100" width="100" height="30" as="geometry"><mxRectangle width="100" height="30" as="alternateBounds"/></mxGeometry></mxCell></root></mxGraphModel></diagram></mxfile>)";

struct TableFixture : SelectFixture {
    TableFixture()
    {
        session.resetDocument(documentFromXml(kTableDiagram));
        original = savedBytes(session);
    }
    [[nodiscard]] std::vector<std::string> childrenOf(const std::string& id) const
    {
        std::vector<std::string> ids;
        for (const core::Cell* child : session.currentPage()->children(id))
            ids.push_back(child->id());
        return ids;
    }
};

} // namespace

class TestTables : public QObject {
    Q_OBJECT

private slots:
    void firstClickSelectsTheTableAndTheNextTheCell()
    {
        TableFixture f;
        click(f.canvas(), {250, 145}); // over r1c1
        QCOMPARE(f.selected(), std::vector<std::string>{"t"});
        click(f.canvas(), {250, 145});
        QCOMPARE(f.selected(), std::vector<std::string>{"r1c1"});
        click(f.canvas(), {350, 175}); // another cell: the selection switches to it
        QCOMPARE(f.selected(), std::vector<std::string>{"r2c2"});
        QVERIFY(!f.session.canUndo());
    }

    void arrangeTabInsertsARowBelowTheSelectedCell()
    {
        TableFixture f;
        FormatPanel panel(f.session);
        f.session.selection().set({"r1c2"});
        auto* button = panel.findChild<QToolButton*>(QStringLiteral("format.table.insertRowBelow"));
        QVERIFY(button != nullptr);
        QVERIFY(button->isEnabled());
        button->click();

        const auto rows = f.childrenOf("t");
        QCOMPARE(rows.size(), std::size_t(3));
        QCOMPARE(rows[0], std::string("r1"));
        QCOMPARE(rows[2], std::string("r2"));
        QCOMPARE(f.geometry("t").height, 120.0);
        QCOMPARE(f.geometry(rows[1]).y, 60.0);
        QCOMPARE(f.geometry("r2").y, 90.0);
        QCOMPARE(f.childrenOf(rows[1]).size(), std::size_t(2));
        QCOMPARE(f.session.selection().cells(), std::vector<std::string>{"r1c2"}); // the selection stays

        QVERIFY(f.undoRestores(f.original));
        QVERIFY(!f.session.canUndo());
    }

    void arrangeTabDeletesTheColumnOfTheSelectedCell()
    {
        TableFixture f;
        FormatPanel panel(f.session);
        f.session.selection().set({"r2c1"});
        panel.findChild<QToolButton*>(QStringLiteral("format.table.deleteColumn"))->click();
        QCOMPARE(f.childrenOf("r1"), std::vector<std::string>{"r1c2"});
        QCOMPARE(f.geometry("t").width, 100.0);
        QCOMPARE(f.geometry("r1c2").x, 0.0);
        QVERIFY(f.session.selection().isEmpty()); // the selected cell is gone
        QVERIFY(f.undoRestores(f.original));
    }

    void tableGroupOnlyForTables()
    {
        TableFixture f;
        FormatPanel panel(f.session);
        f.session.selection().set({"src"});
        QVERIFY(actions::tableTarget(f.session).empty());
        QVERIFY(!actions::editTable(f.session, actions::TableEdit::InsertRowBefore));
        f.session.selection().set({"r1c1"});
        QCOMPARE(actions::tableTarget(f.session), std::string("r1c1"));
        f.session.selection().set({"r1c1", "r1c2"});
        QCOMPARE(actions::tableTarget(f.session), std::string("t")); // lifted to the row, then to the table
        f.session.selection().set({"r1", "r2"});
        QCOMPARE(actions::tableTarget(f.session), std::string("t"));
    }

    void shiftClickSelectsTheRangeOfCells()
    {
        TableFixture f;
        click(f.canvas(), {250, 145});
        click(f.canvas(), {250, 145}); // r1c1
        QCOMPARE(f.selected(), std::vector<std::string>{"r1c1"});
        click(f.canvas(), {350, 175}, Qt::ShiftModifier); // r2c2: the 2x2 range, the first cell first
        QCOMPARE(f.selected(), (std::vector<std::string>{"r1c1", "r1c2", "r2c1", "r2c2"}));
        click(f.canvas(), {350, 145}, Qt::ShiftModifier); // r1c2: back to a row of two from the same first cell
        QCOMPARE(f.selected(), (std::vector<std::string>{"r1c1", "r1c2"}));
        // Ctrl+click still toggles.
        click(f.canvas(), {350, 175}, Qt::ControlModifier);
        QCOMPARE(f.selected(), (std::vector<std::string>{"r1c1", "r1c2", "r2c2"}));
        QVERIFY(!f.session.canUndo());
    }

    void mergeCellsGrowsTheCellAndUnmergeRestoresIt()
    {
        TableFixture f;
        f.session.selection().set({"r1c1", "r1c2", "r2c1", "r2c2"});
        QVERIFY(actions::canMergeCells(f.session));
        QVERIFY(!actions::canUnmergeCells(f.session));
        QVERIFY(actions::mergeCells(f.session));
        QCOMPARE(f.geometry("r1c1"), (core::Rect{0, 0, 200, 60}));
        for (const char* covered : {"r1c2", "r2c1", "r2c2"})
            QVERIFY(!f.cell(covered).isVisible());
        QCOMPARE(f.cell("r1c1").style().value("rowspan").value_or(""), std::string("2"));
        QCOMPARE(f.cell("r1c1").style().value("colspan").value_or(""), std::string("2"));
        const QByteArray merged = savedBytes(f.session);

        f.session.selection().set({"r1c1"});
        QVERIFY(!actions::canMergeCells(f.session));
        QVERIFY(actions::canUnmergeCells(f.session));
        QVERIFY(actions::unmergeCells(f.session));
        QCOMPARE(f.geometry("r1c1"), (core::Rect{0, 0, 100, 30}));
        QVERIFY(f.cell("r2c2").isVisible());
        QVERIFY(f.undoRestores(merged)); // one step each
        QVERIFY(f.undoRestores(f.original));
        QVERIFY(!f.session.canUndo());
    }

    void mergeNeedsARectangleOfCellsOfOneTable()
    {
        TableFixture f;
        f.session.selection().set({"r1c1"});
        QVERIFY(!actions::canMergeCells(f.session));
        f.session.selection().set({"r1c1", "r2c2"});
        QVERIFY(!actions::canMergeCells(f.session));
        QVERIFY(!actions::mergeCells(f.session));
        f.session.selection().set({"r1", "r2"});
        QVERIFY(!actions::canMergeCells(f.session));
        f.session.selection().set({"src", "r1c1"});
        QVERIFY(!actions::canMergeCells(f.session));
        QVERIFY(!f.session.canUndo());
    }

    void arrangeTabShowsMergeOrUnmerge()
    {
        TableFixture f;
        FormatPanel panel(f.session);
        const auto merge = [&] { return panel.findChild<QToolButton*>(QStringLiteral("format.table.mergeCells")); };
        const auto unmerge = [&] { return panel.findChild<QToolButton*>(QStringLiteral("format.table.unmergeCells")); };
        panel.show();

        f.session.selection().set({"r1c1"});
        QVERIFY(merge() != nullptr && unmerge() != nullptr);
        for (int i = 0; i < panel.tabs()->count(); ++i) {
            if (panel.tabs()->tabText(i) == QStringLiteral("Arrange"))
                panel.tabs()->setCurrentIndex(i);
        }
        QVERIFY(!merge()->isVisibleTo(&panel) && !unmerge()->isVisibleTo(&panel));
        f.session.selection().set({"r1c1", "r1c2"});
        QVERIFY(merge()->isVisibleTo(&panel) && !unmerge()->isVisibleTo(&panel));
        merge()->click();
        QCOMPARE(f.geometry("r1c1"), (core::Rect{0, 0, 200, 30}));
        QVERIFY(!f.cell("r1c2").isVisible());

        f.session.selection().set({"r1c1"});
        QVERIFY(!merge()->isVisibleTo(&panel) && unmerge()->isVisibleTo(&panel));
        unmerge()->click();
        QVERIFY(f.cell("r1c2").isVisible());
        QVERIFY(f.session.undo()); // the unmerge
        QVERIFY(f.undoRestores(f.original)); // the merge
    }

    void theWindowEnablesMergeAndUnmergeFromTheSelection()
    {
        MainWindow window;
        window.session().resetDocument(documentFromXml(kTableDiagram));
        QAction* merge = window.actions()[action_id::kMergeCells];
        QAction* unmerge = window.actions()[action_id::kUnmergeCells];
        QVERIFY(!merge->isEnabled() && !unmerge->isEnabled());
        window.session().selection().set({"r1c1", "r1c2"});
        QVERIFY(merge->isEnabled() && !unmerge->isEnabled());
        merge->trigger();
        QCOMPARE(window.session().currentPage()->findCell("r1c1")->geometry()->bounds(), (core::Rect{0, 0, 200, 30}));
        window.session().selection().set({"r1c1"});
        QVERIFY(!merge->isEnabled() && unmerge->isEnabled());
        unmerge->trigger();
        QVERIFY(window.session().currentPage()->findCell("r1c2")->isVisible());
        // Arrange > Table has both.
        QMenu* arrange = window.findChild<QMenu*>(QStringLiteral("menu.arrange"));
        QVERIFY(arrange != nullptr);
        QMenu* table = nullptr;
        for (QAction* entry : arrange->actions()) {
            if (entry->menu() != nullptr && entry->menu()->actions().contains(merge))
                table = entry->menu();
        }
        QVERIFY(table != nullptr && table->actions().contains(unmerge));
    }

    void aMergedCellInTheFirstRowKeepsTheColumnHandles()
    {
        TableFixture f;
        f.session.selection().set({"r1c2"});
        QVERIFY(actions::editTable(f.session, actions::TableEdit::InsertColumnAfter)); // 3 columns of 100
        f.session.selection().set({"r1c1", "r1c2"});
        QVERIFY(actions::mergeCells(f.session)); // the first row's first cell now covers the second
        QCOMPARE(f.geometry("r1c1").width, 200.0);
        f.session.selection().set({"t"});
        // The border between the first and the second column lies inside the merged cell; below it, in the
        // second row, it is a border like any other.
        drag(f.canvas(), {300, 175}, {320, 175});
        QCOMPARE(f.geometry("r2c1").width, 120.0);
        QCOMPARE(f.geometry("r2c2").width, 100.0);
        QCOMPARE(f.geometry("t").width, 320.0);
        QCOMPARE(f.geometry("r1c1").width, 220.0); // still over both columns, which are 120 and 100 wide
        QVERIFY(!f.cell("r1c2").isVisible());
    }

    void mergedCellIsDrawnOverItsNeighbours()
    {
        TableFixture f;
        f.session.selection().set({"r1c1", "r1c2"});
        QVERIFY(actions::mergeCells(f.session));
        // The covered cell is no longer under the pointer: a click on its place
        // reaches the merged cell.
        click(f.canvas(), {350, 145});
        click(f.canvas(), {350, 145});
        QCOMPARE(f.selected(), std::vector<std::string>{"r1c1"});
    }

    void draggingAColumnBorderResizesTheColumn()
    {
        TableFixture f;
        f.session.selection().set({"t"});
        // The border between the columns is at x = 300; the table grows with the first column.
        drag(f.canvas(), {300, 145}, {330, 145});
        QCOMPARE(f.geometry("t").width, 230.0);
        for (const char* row : {"r1", "r2"}) {
            const auto cells = f.childrenOf(row);
            QCOMPARE(f.geometry(cells[0]).width, 130.0);
            QCOMPARE(f.geometry(cells[1]).x, 130.0);
            QCOMPARE(f.geometry(cells[1]).width, 100.0);
        }
        QVERIFY(f.undoRestores(f.original));
        QVERIFY(!f.session.canUndo());

        f.session.selection().set({"t"}); // undo selects the changed cells
        // Shift gives the width to the next column instead of growing the table.
        drag(f.canvas(), {300, 145}, {320, 145}, Qt::ShiftModifier);
        QCOMPARE(f.geometry("t").width, 200.0);
        QCOMPARE(f.geometry("r1c1").width, 120.0);
        QCOMPARE(f.geometry("r1c2").width, 80.0);
        QVERIFY(f.undoRestores(f.original));
    }

    void draggingARowBorderResizesTheRowAndTheTable()
    {
        TableFixture f;
        f.session.selection().set({"r1"}); // a row selection shows the table's borders as well
        drag(f.canvas(), {250, 160}, {250, 175});
        QCOMPARE(f.geometry("r1").height, 45.0);
        QCOMPARE(f.geometry("r2").y, 75.0);
        QCOMPARE(f.geometry("t").height, 105.0);
        QVERIFY(f.undoRestores(f.original));
    }

    void aBorderCannotShrinkBelowTheMinimum()
    {
        TableFixture f;
        f.session.selection().set({"t"});
        drag(f.canvas(), {300, 145}, {100, 145});
        QCOMPARE(f.geometry("r1c1").width, 20.0);
        QVERIFY(f.undoRestores(f.original));
    }

    void dragArrowOntoACellConnectsToItsRow()
    {
        TableFixture f;
        const auto before = savedBytes(f.session);
        move(f.canvas(), {50, 40}, Qt::NoModifier, Qt::NoButton);
        drag(f.canvas(), {100, 40}, {350, 175}); // the east arrow of 'src' onto r2c2

        const core::Page& page = *f.session.currentPage();
        const core::Cell* edge = nullptr;
        for (const core::Cell* cell : page.cells()) {
            if (cell->isEdge())
                edge = cell;
        }
        QVERIFY(edge != nullptr);
        QCOMPARE(edge->source(), std::string("src"));
        QCOMPARE(edge->target(), std::string("r2"));
        QVERIFY(f.undoRestores(before));
    }

    void movingTheTableLeavesItsRowsAlone()
    {
        TableFixture f;
        const core::Rect rows = f.geometry("r1");
        drag(f.canvas(), {300, 110}, {330, 130}); // by the title
        QCOMPARE(f.geometry("t").x, 230.0);
        QCOMPARE(f.geometry("r1").x, rows.x);
        QCOMPARE(f.geometry("r1").y, rows.y);
        QVERIFY(f.undoRestores(f.original));
    }
};

QTEST_MAIN(TestTables)
#include "tst_tables.moc"
