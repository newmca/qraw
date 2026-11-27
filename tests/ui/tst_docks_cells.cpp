// The Cells panel: the tree of layers and cells, its texts, the selection sync with the canvas,
// Enter editing the label, and the model's updates (dataChanged for an edit that keeps the tree, a
// rebuild for one that does not, nothing at all while the panel is hidden).

#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_layers.h"
#include "qraw/commands/ops_pages.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/ui/cells_panel.h"
#include "qraw/ui/main_window.h"

#include <QAction>
#include <QDockWidget>
#include <QMenuBar>
#include <QSignalSpy>
#include <QTest>
#include <QTreeView>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

// Layer "1" (empty name: Background): a (label A), n (no label, ellipse), r (no label), h (html label),
// the connector e from a to b, a vertex b, a group g with c1; and the locked layer "L" with x.
constexpr char kCellsDiagram[] = R"(<mxfile><diagram id="p1" name="Page-1"><mxGraphModel dx="800" dy="600" grid="1" gridSize="10" page="1" pageScale="1" pageWidth="827" pageHeight="1169"><root><mxCell id="0"/><mxCell id="1" parent="0"/><mxCell id="a" value="A" style="rounded=0;" vertex="1" parent="1"><mxGeometry x="40" y="40" width="120" height="60" as="geometry"/></mxCell><mxCell id="n" value="" style="ellipse;" vertex="1" parent="1"><mxGeometry x="40" y="120" width="60" height="60" as="geometry"/></mxCell><mxCell id="r" value="" style="" vertex="1" parent="1"><mxGeometry x="120" y="120" width="60" height="60" as="geometry"/></mxCell><mxCell id="h" value="&lt;b&gt;Hello&lt;/b&gt;&lt;br&gt;there" style="html=1;" vertex="1" parent="1"><mxGeometry x="200" y="120" width="60" height="60" as="geometry"/></mxCell><mxCell id="b" value="B" style="shape=mxgraph.flowchart.process_step;" vertex="1" parent="1"><mxGeometry x="280" y="40" width="120" height="60" as="geometry"/></mxCell><mxCell id="e" style="" edge="1" parent="1" source="a" target="b"><mxGeometry relative="1" as="geometry"/></mxCell><mxCell id="g" value="" style="group" vertex="1" connectable="0" parent="1"><mxGeometry x="40" y="200" width="200" height="100" as="geometry"/></mxCell><mxCell id="c1" value="C1" style="" vertex="1" parent="g"><mxGeometry x="10" y="20" width="80" height="40" as="geometry"/></mxCell><mxCell id="L" value="Locked" style="locked=1;" parent="0"/><mxCell id="x" value="X" style="" vertex="1" parent="L"><mxGeometry x="500" y="40" width="60" height="30" as="geometry"/></mxCell></root></mxGraphModel></diagram></mxfile>)";

struct Fixture {
    Fixture()
    {
        window.session().resetDocument(documentFromXml(kCellsDiagram));
        window.resize(1280, 800);
        window.showPanel(MainWindow::Panel::Cells, true);
        window.show();
        window.activateWindow();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        QVERIFY(model()->isActive());
    }

    CellsPanel& panel() { return *window.cellsPanel(); }
    CellTreeModel* model() { return panel().model(); }
    QTreeView* tree() { return panel().tree(); }
    DocumentSession& session() { return window.session(); }
    QString text(const char* id, int role = Qt::DisplayRole) { return model()->indexOfCell(id).data(role).toString(); }
    std::vector<std::string> selected() { return session().selection().cells(); }

    MainWindow window;
};

} // namespace

class TestDocksCells : public QObject {
    Q_OBJECT

private slots:
    void theDockIsInTheViewMenuAndFollowsItsAction();
    void rowsAreLayersThenCellsWithChildrenNested();
    void rowTextIsTheLabelOrTheKindOfShape();
    void connectorsNameTheirEnds();
    void descriptionsGiveTypeAndPosition();
    void layersAndCellsOfALockedLayerAreNotSelectable();
    void theCanvasSelectionIsShownInTheTree();
    void theTreeSelectionSelectsOnTheCanvas();
    void enterOnARowEditsTheLabel();
    void aContentEditUpdatesTheRowsAndTheConnectorsAttachedToThem();
    void anEditThatChangesTheTreeRebuildsIt();
    void aHiddenPanelDoesNoWorkAndCatchesUpWhenShown();
    void anotherPageIsShownWhenThePageChanges();
};

void TestDocksCells::theDockIsInTheViewMenuAndFollowsItsAction()
{
    MainWindow window;
    window.show();
    QAction* action = window.actions()[action_id::kCells];
    QVERIFY(action != nullptr);
    QCOMPARE(action->text(), QStringLiteral("Cells"));
    QVERIFY(action->isCheckable());
    QVERIFY(!action->isChecked());
    QVERIFY(!window.dock(MainWindow::Panel::Cells)->isVisible());
    action->trigger();
    QVERIFY(window.dock(MainWindow::Panel::Cells)->isVisible());
    QVERIFY(action->isChecked());
    QCOMPARE(window.dock(MainWindow::Panel::Cells)->windowTitle(), QStringLiteral("Cells"));
    bool inViewMenu = false;
    for (QAction* menu : window.menuBar()->actions()) {
        if (menu->text() == QLatin1String("&View"))
            inViewMenu = menu->menu()->actions().contains(action);
    }
    QVERIFY(inViewMenu);
}

void TestDocksCells::rowsAreLayersThenCellsWithChildrenNested()
{
    Fixture f;
    CellTreeModel& m = *f.model();
    QCOMPARE(m.rowCount(), 2); // Background and Locked
    const QModelIndex background = m.index(0, 0);
    QCOMPARE(m.cellId(background), std::string("1"));
    QCOMPARE(m.rowCount(background), 7); // a, n, r, h, b, e, g in document order
    QCOMPARE(m.cellId(m.index(0, 0, background)), std::string("a"));
    QCOMPARE(m.cellId(m.index(6, 0, background)), std::string("g"));
    const QModelIndex group = m.index(6, 0, background);
    QCOMPARE(m.rowCount(group), 1);
    QCOMPARE(m.cellId(m.index(0, 0, group)), std::string("c1"));
    QCOMPARE(m.parent(m.index(0, 0, group)), group);
    QCOMPARE(m.parent(group), background);
    QVERIFY(!m.parent(background).isValid());
    QCOMPARE(m.rowCount(m.index(1, 0)), 1); // the locked layer holds x
    QCOMPARE(m.indexOfCell("c1"), m.index(0, 0, group));
    QVERIFY(!m.indexOfCell("nope").isValid());
    // The layers are open, so that the cells are visible at once.
    QVERIFY(f.tree()->isExpanded(background));
}

void TestDocksCells::rowTextIsTheLabelOrTheKindOfShape()
{
    Fixture f;
    QCOMPARE(f.text("1"), QStringLiteral("Background"));
    QCOMPARE(f.text("L"), QStringLiteral("Locked"));
    QCOMPARE(f.text("a"), QStringLiteral("A"));
    QCOMPARE(f.text("n"), QStringLiteral("Ellipse (no label)"));
    QCOMPARE(f.text("r"), QStringLiteral("Rectangle (no label)"));
    QCOMPARE(f.text("g"), QStringLiteral("Group (no label)"));
    QCOMPARE(f.text("h"), QStringLiteral("Hello there")); // the markup is gone
    QCOMPARE(f.text("c1"), QStringLiteral("C1"));
    // The accessible text is the display text.
    QCOMPARE(f.text("a", Qt::AccessibleTextRole), QStringLiteral("A"));
    // A stencil is named by its last part.
    f.session().edit(QStringLiteral("Label"), [&](commands::Transaction& tx) {
        return commands::ops::setLabel(tx, 0, "b", "");
    });
    QCOMPARE(f.text("b"), QStringLiteral("Process step (no label)"));
}

void TestDocksCells::connectorsNameTheirEnds()
{
    Fixture f;
    QCOMPARE(f.text("e"), QStringLiteral("Connector from A to B"));
    // An end without a label is named by its kind; a label of the connector follows.
    f.session().edit(QStringLiteral("Label"), [&](commands::Transaction& tx) {
        return commands::ops::setLabel(tx, 0, "a", "");
    });
    QCOMPARE(f.text("e"), QStringLiteral("Connector from Rectangle to B"));
    f.session().edit(QStringLiteral("Label"), [&](commands::Transaction& tx) {
        return commands::ops::setLabel(tx, 0, "e", "uses");
    });
    QCOMPARE(f.text("e"), QStringLiteral("Connector from Rectangle to B: uses"));
    // A loose end.
    f.session().edit(QStringLiteral("Disconnect"), [&](commands::Transaction& tx) {
        return commands::ops::disconnectEdge(tx, 0, "e", false, core::Point{400, 300});
    });
    QVERIFY2(f.text("e").contains(QLatin1String("an open end")), qPrintable(f.text("e")));
}

void TestDocksCells::descriptionsGiveTypeAndPosition()
{
    Fixture f;
    QCOMPARE(f.text("a", Qt::AccessibleDescriptionRole), QStringLiteral("Rectangle, at 40, 40, size 120 by 60"));
    QCOMPARE(f.text("n", Qt::AccessibleDescriptionRole), QStringLiteral("Ellipse, at 40, 120, size 60 by 60"));
    // A child's position is the page position: the group's plus its own.
    QCOMPARE(f.text("c1", Qt::AccessibleDescriptionRole), QStringLiteral("Rectangle, at 50, 220, size 80 by 40"));
    QCOMPARE(f.text("e", Qt::AccessibleDescriptionRole), QStringLiteral("Connector, 0 waypoints"));
    QVERIFY(f.text("x", Qt::AccessibleDescriptionRole).endsWith(QLatin1String(", locked")));
    QCOMPARE(f.text("1", Qt::AccessibleDescriptionRole), QStringLiteral("Layer"));
}

void TestDocksCells::layersAndCellsOfALockedLayerAreNotSelectable()
{
    Fixture f;
    QVERIFY(!(f.model()->flags(f.model()->indexOfCell("1")) & Qt::ItemIsSelectable));
    QVERIFY(f.model()->flags(f.model()->indexOfCell("1")) & Qt::ItemIsEnabled);
    QVERIFY(f.model()->flags(f.model()->indexOfCell("a")) & Qt::ItemIsSelectable);
    QVERIFY(!(f.model()->flags(f.model()->indexOfCell("x")) & Qt::ItemIsSelectable));
}

void TestDocksCells::theCanvasSelectionIsShownInTheTree()
{
    Fixture f;
    f.session().selection().set({"a", "c1"});
    QModelIndexList rows = f.tree()->selectionModel()->selectedRows();
    QCOMPARE(rows.size(), 2);
    QVERIFY(rows.contains(f.model()->indexOfCell("a")));
    QVERIFY(rows.contains(f.model()->indexOfCell("c1")));
    QCOMPARE(f.tree()->currentIndex(), f.model()->indexOfCell("a")); // a screen reader announces the first
    f.session().selection().clear();
    QVERIFY(f.tree()->selectionModel()->selectedRows().isEmpty());
}

void TestDocksCells::theTreeSelectionSelectsOnTheCanvas()
{
    Fixture f;
    f.tree()->selectionModel()->select(f.model()->indexOfCell("b"), QItemSelectionModel::ClearAndSelect);
    QCOMPARE(f.selected(), std::vector<std::string>{"b"});
    QItemSelection both;
    both.select(f.model()->indexOfCell("a"), f.model()->indexOfCell("a"));
    both.select(f.model()->indexOfCell("n"), f.model()->indexOfCell("n"));
    f.tree()->selectionModel()->select(both, QItemSelectionModel::ClearAndSelect);
    QCOMPARE(f.selected().size(), size_t(2));
    // A layer row cannot be selected (the flag is off), so the canvas selection stays as it is.
    f.tree()->selectionModel()->select(f.model()->indexOfCell("1"), QItemSelectionModel::ClearAndSelect);
    QVERIFY(f.selected().size() == 2 || f.selected().empty());
    // The keyboard: Down selects the next row, which selects the cell.
    f.session().selection().set({"a"});
    f.tree()->setFocus();
    QTest::keyClick(f.tree(), Qt::Key_Down);
    QCOMPARE(f.selected(), std::vector<std::string>{"n"});
}

void TestDocksCells::enterOnARowEditsTheLabel()
{
    Fixture f;
    f.session().selection().set({"a"});
    f.tree()->setFocus();
    QVERIFY(!f.window.canvas().interaction().labelEditor()->isEditing());
    QTest::keyClick(f.tree(), Qt::Key_Return);
    QVERIFY(f.window.canvas().interaction().labelEditor()->isEditing());
    QCOMPARE(f.window.canvas().interaction().labelEditor()->editingCell(), std::string("a"));
    f.window.canvas().interaction().labelEditor()->stopEditing(false);
}

void TestDocksCells::aContentEditUpdatesTheRowsAndTheConnectorsAttachedToThem()
{
    Fixture f;
    const std::size_t rebuilds = f.model()->rebuildCount();
    QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
    QVERIFY(f.session().edit(QStringLiteral("Label"), [&](commands::Transaction& tx) {
        return commands::ops::setLabel(tx, 0, "a", "Alpha");
    }).ok);
    QCOMPARE(f.model()->rebuildCount(), rebuilds); // the tree did not change
    QCOMPARE(f.text("a"), QStringLiteral("Alpha"));
    QCOMPARE(f.text("e"), QStringLiteral("Connector from Alpha to B")); // an attached connector says it too
    QSet<QString> rows;
    for (const QList<QVariant>& arguments : changed)
        rows.insert(f.model()->cellId(arguments.at(0).toModelIndex()).c_str());
    QVERIFY(rows.contains(QStringLiteral("a")));
    QVERIFY(rows.contains(QStringLiteral("e")));
    QVERIFY(!rows.contains(QStringLiteral("b")));
    // A move changes no text, and still keeps the tree.
    f.session().edit(QStringLiteral("Move"), [&](commands::Transaction& tx) {
        return commands::ops::moveCells(tx, 0, {"a"}, 10, 0);
    });
    QCOMPARE(f.model()->rebuildCount(), rebuilds);
    QCOMPARE(f.text("a", Qt::AccessibleDescriptionRole), QStringLiteral("Rectangle, at 50, 40, size 120 by 60"));
}

void TestDocksCells::anEditThatChangesTheTreeRebuildsIt()
{
    Fixture f;
    const std::size_t rebuilds = f.model()->rebuildCount();
    f.session().selection().set({"a"});
    QVERIFY(f.session().edit(QStringLiteral("Delete"), [&](commands::Transaction& tx) {
        return commands::ops::removeCells(tx, 0, {"a"}, {});
    }).ok);
    QCOMPARE(f.model()->rebuildCount(), rebuilds + 1);
    QVERIFY(!f.model()->indexOfCell("a").isValid());
    QCOMPARE(f.model()->rowCount(f.model()->index(0, 0)), 6);
    QVERIFY(f.session().undo());
    QVERIFY(f.model()->indexOfCell("a").isValid());
    // Locking a layer changes which cells can be selected: the rows are made again, not patched.
    const std::size_t afterUndo = f.model()->rebuildCount();
    f.session().edit(QStringLiteral("Lock"), [&](commands::Transaction& tx) {
        return commands::ops::setLayerLocked(tx, 0, "1", true);
    });
    QCOMPARE(f.model()->rebuildCount(), afterUndo + 1);
    QVERIFY(!(f.model()->flags(f.model()->indexOfCell("a")) & Qt::ItemIsSelectable));
}

void TestDocksCells::aHiddenPanelDoesNoWorkAndCatchesUpWhenShown()
{
    Fixture f;
    f.window.showPanel(MainWindow::Panel::Cells, false);
    QVERIFY(!f.model()->isActive());
    const std::size_t rebuilds = f.model()->rebuildCount();
    QSignalSpy changed(f.model(), &QAbstractItemModel::dataChanged);
    QSignalSpy reset(f.model(), &QAbstractItemModel::modelReset);
    f.session().edit(QStringLiteral("Label"), [&](commands::Transaction& tx) {
        return commands::ops::setLabel(tx, 0, "a", "Alpha");
    });
    f.session().edit(QStringLiteral("Delete"), [&](commands::Transaction& tx) {
        return commands::ops::removeCells(tx, 0, {"n"}, {});
    });
    f.session().selection().set({"b"});
    QCOMPARE(f.model()->rebuildCount(), rebuilds);
    QCOMPARE(changed.size(), 0);
    QCOMPARE(reset.size(), 0);
    f.window.showPanel(MainWindow::Panel::Cells, true);
    QVERIFY(f.model()->isActive());
    QCOMPARE(f.model()->rebuildCount(), rebuilds + 1); // one catch-up, not one per edit
    QCOMPARE(f.text("a"), QStringLiteral("Alpha"));
    QVERIFY(!f.model()->indexOfCell("n").isValid());
    QCOMPARE(f.tree()->selectionModel()->selectedRows(), QModelIndexList{f.model()->indexOfCell("b")});
}

void TestDocksCells::anotherPageIsShownWhenThePageChanges()
{
    Fixture f;
    f.session().edit(QStringLiteral("Page"), [&](commands::Transaction& tx) {
        return commands::ops::addPage(tx, std::nullopt, std::string("Second"));
    });
    f.session().setCurrentPage(1);
    QCOMPARE(f.session().currentPageIndex(), std::size_t(1));
    QCOMPARE(f.text("1"), QStringLiteral("Background")); // the new page has its own layer
    QVERIFY(!f.model()->indexOfCell("a").isValid());
    f.session().setCurrentPage(0);
    QVERIFY(f.model()->indexOfCell("a").isValid());
}

SHELL_TEST_MAIN(TestDocksCells)
#include "tst_docks_cells.moc"
