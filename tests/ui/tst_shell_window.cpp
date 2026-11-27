// MainWindow: structure, action state, M4 acceptance (open, edit, save, reload).
#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/format/codec.h"
#include "qraw/ui/main_window.h"

#include <QAction>
#include <QComboBox>
#include <QDockWidget>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QTemporaryDir>

using namespace qraw;
using namespace qraw::ui;
namespace id = qraw::ui::action_id;

namespace {

// Scripted prompts: Save As answers `savePath`, unsaved changes `choice`.
class FakePrompts final : public DocumentPrompts {
public:
    SaveChoice askSaveChanges(const QString&) override { return choice; }
    std::optional<QString> askOpenPath(const QString&) override { return std::nullopt; }
    std::optional<QString> askSavePath(const QString&, const QString&) override
    {
        if (savePath.isEmpty())
            return std::nullopt;
        return savePath;
    }
    void showError(const QString& title, const QString& message) override { errors << title + ": " + message; }

    QString savePath;
    SaveChoice choice = SaveChoice::Discard;
    QStringList errors;
};

QAction* menuAction(QMenuBar* bar, int index)
{
    return bar->actions().at(index);
}

QString firstActionId(QMenuBar* bar, int menuIndex)
{
    return menuAction(bar, menuIndex)->menu()->actions().first()->objectName();
}

const core::Cell* firstVertex(const core::Document& document)
{
    for (const core::Cell* cell : document.page(0)->cells()) {
        if (cell->isVertex() && cell->geometry() != nullptr)
            return cell;
    }
    return nullptr;
}

bool isEnabled(MainWindow& window, const char* actionId)
{
    return window.actions()[actionId]->isEnabled();
}

} // namespace

class TestShellWindow : public QObject {
    Q_OBJECT

private slots:
    void menuBarHasTheMenusInOrder()
    {
        MainWindow window;
        QMenuBar* bar = window.menuBar();
        QStringList titles;
        for (QAction* action : bar->actions())
            titles << action->text();
        QCOMPARE(titles, (QStringList{"&File", "&Edit", "&View", "&Arrange", "&Insert", "E&xtras", "&Help"}));
        QCOMPARE(firstActionId(bar, 0), QString::fromLatin1(id::kNew));
        QCOMPARE(firstActionId(bar, 1), QString::fromLatin1(id::kUndo));
        QCOMPARE(firstActionId(bar, 2), QString::fromLatin1(id::kFormatPanel));
        QCOMPARE(firstActionId(bar, 3), QString::fromLatin1(id::kToFront));
        QCOMPARE(firstActionId(bar, 4), QString::fromLatin1(id::kInsertText));
        QCOMPARE(firstActionId(bar, 5), QString::fromLatin1(id::kEditDiagram));
        QCOMPARE(firstActionId(bar, 6), QString::fromLatin1(id::kKeyboardShortcuts));
        // Open Recent is the holder of a submenu.
        QVERIFY(window.actions()[id::kOpenRecent]->menu() != nullptr);
    }

    void everyActionIsReachableFromAMenuOrToolbar()
    {
        MainWindow window;
        for (const ActionInfo& info : window.actions().actions())
            QVERIFY2(!info.action->associatedObjects().isEmpty(), qPrintable(info.id));
    }

    void docksHaveTheDocumentedObjectNames()
    {
        MainWindow window;
        window.show();
        for (const char* name : {"dock.palette", "dock.format", "dock.layers", "dock.outline", "dock.find"})
            QVERIFY2(window.findChild<QDockWidget*>(QString::fromLatin1(name)) != nullptr, name);
        QVERIFY(window.dock(MainWindow::Panel::Palette)->isVisible());
        QVERIFY(window.dock(MainWindow::Panel::Format)->isVisible());
        QVERIFY(!window.dock(MainWindow::Panel::Layers)->isVisible());
        QVERIFY(!window.dock(MainWindow::Panel::Find)->isVisible());
        QVERIFY(window.actions()[id::kFormatPanel]->isChecked());
        QVERIFY(!window.actions()[id::kLayers]->isChecked());
    }

    void panelToggleActionsFollowTheDocks()
    {
        MainWindow window;
        window.show();
        window.actions()[id::kLayers]->trigger();
        QVERIFY(window.dock(MainWindow::Panel::Layers)->isVisible());
        window.showPanel(MainWindow::Panel::Layers, false);
        QVERIFY(!window.actions()[id::kLayers]->isChecked());
        window.dock(MainWindow::Panel::Format)->close();
        QVERIFY(!window.actions()[id::kFormatPanel]->isChecked());
    }

    void actionStateFollowsTheSelection()
    {
        MainWindow window;
        window.session().resetDocument(test::simpleDocument());
        QVERIFY(!isEnabled(window, id::kDelete));
        QVERIFY(!isEnabled(window, id::kGroup));
        QVERIFY(isEnabled(window, id::kPaste));
        QVERIFY(!isEnabled(window, id::kUndo));

        window.session().selection().set({"a"});
        QVERIFY(isEnabled(window, id::kDelete));
        QVERIFY(isEnabled(window, id::kToFront));
        QVERIFY(isEnabled(window, id::kGroup)); // one vertex becomes a container
        QVERIFY(!isEnabled(window, id::kUngroup));
        QVERIFY(!isEnabled(window, id::kEnterGroup));

        window.session().selection().set({"g"});
        QVERIFY(isEnabled(window, id::kUngroup));
        QVERIFY(isEnabled(window, id::kEnterGroup));

        window.session().selection().clear();
        QVERIFY(!isEnabled(window, id::kDelete));
        QVERIFY(!isEnabled(window, id::kRevert)); // untitled: nothing to revert to
    }

    void undoActionShowsTheStepName()
    {
        MainWindow window;
        window.session().resetDocument(test::simpleDocument());
        const QByteArray before = test::savedBytes(window.session());
        window.session().edit(QStringLiteral("Move"), [](commands::Transaction& tx) {
            return commands::ops::moveCells(tx, 0, {"a"}, 10, 0);
        });
        QCOMPARE(window.actions()[id::kUndo]->text(), QStringLiteral("&Undo Move"));
        QVERIFY(isEnabled(window, id::kUndo));
        window.actions()[id::kUndo]->trigger();
        QCOMPARE(test::savedBytes(window.session()), before);
        QVERIFY(isEnabled(window, id::kRedo));
        QCOMPARE(window.actions()[id::kUndo]->text(), QStringLiteral("&Undo"));
    }

    void gridToggleIsOneUndoableEdit()
    {
        MainWindow window;
        window.session().resetDocument(test::simpleDocument());
        const QByteArray before = test::savedBytes(window.session());
        QAction* grid = window.actions()[id::kGrid];
        QVERIFY(grid->isChecked()); // the sample page has grid="1"
        grid->trigger();
        QVERIFY(!window.session().currentPage()->gridEnabled());
        QVERIFY(!grid->isChecked());
        QVERIFY(test::savedBytes(window.session()) != before);
        window.session().undo();
        QCOMPARE(test::savedBytes(window.session()), before);
        QVERIFY(grid->isChecked());
    }

    void pageActionsMoveBetweenPages()
    {
        MainWindow window;
        window.session().resetDocument(test::simpleDocument());
        QVERIFY(!isEnabled(window, id::kNextPage));
        QVERIFY(!isEnabled(window, id::kDeletePage));
        QCOMPARE(window.findChild<QLabel*>(QStringLiteral("status.page"))->text(), QStringLiteral("Page 1 of 1"));
    }

    void statusBarShowsTheSelection()
    {
        MainWindow window;
        window.session().resetDocument(test::simpleDocument());
        auto* label = window.findChild<QLabel*>(QStringLiteral("status.selection"));
        QCOMPARE(label->text(), QStringLiteral("No selection"));
        window.session().selection().set({"a", "b"});
        QCOMPARE(label->text(), QStringLiteral("2 shapes selected"));
        window.session().selection().set({"a"});
        QCOMPARE(label->text(), QStringLiteral("1 shape selected"));
        window.session().selection().set({"e"});
        QCOMPARE(label->text(), QStringLiteral("1 edge selected"));
    }

    void zoomComboAndLabelFollowTheCanvas()
    {
        MainWindow window;
        window.canvas().setZoom(1.5);
        QCOMPARE(window.findChild<QLabel*>(QStringLiteral("status.zoom"))->text(), QStringLiteral("150 %"));
        QCOMPARE(window.findChild<QComboBox*>(QStringLiteral("zoomCombo"))->currentText(), QStringLiteral("150 %"));
    }

    void titleMarksUnsavedChanges()
    {
        if (!test::sessionFilesAvailable())
            QSKIP("DocumentController not available");
        MainWindow window;
        QVERIFY(window.windowTitle().contains(QStringLiteral("[*]")));
        QVERIFY(window.windowTitle().endsWith(QStringLiteral("- Qraw")));
        QVERIFY(!window.isWindowModified());
        window.session().resetDocument(test::simpleDocument());
        window.session().edit(QStringLiteral("Move"), [](commands::Transaction& tx) {
            return commands::ops::moveCells(tx, 0, {"a"}, 10, 0);
        });
        QVERIFY(window.isWindowModified());
    }

    void closeAsksBeforeDiscardingChanges()
    {
        if (!test::sessionFilesAvailable())
            QSKIP("DocumentController not available");
        MainWindow window;
        FakePrompts prompts;
        window.setPrompts(&prompts);
        window.show();
        window.session().resetDocument(test::simpleDocument());
        window.session().edit(QStringLiteral("Move"), [](commands::Transaction& tx) {
            return commands::ops::moveCells(tx, 0, {"a"}, 10, 0);
        });
        prompts.choice = DocumentPrompts::SaveChoice::Cancel;
        QVERIFY(!window.close());
        QVERIFY(window.isVisible());
        prompts.choice = DocumentPrompts::SaveChoice::Discard;
        QVERIFY(window.close());
    }

    // M4 acceptance: open a corpus file, select a vertex, move it, save, reload.
    void opensEditsAndSavesACorpusFile()
    {
        if (!test::sessionFilesAvailable())
            QSKIP("FileService/DocumentController not available");
        const QString source = QStringLiteral(QRAW_SOURCE_DIR "/tests/corpus/upstream-templates/basic/flowchart.xml");
        QTemporaryDir directory;
        QVERIFY(directory.isValid());

        MainWindow window;
        FakePrompts prompts;
        prompts.savePath = directory.path() + QStringLiteral("/moved.drawio");
        window.setPrompts(&prompts);
        window.show();
        QVERIFY(window.controller().openPath(source));
        test::grabToPng(window, QStringLiteral("shell-window-flowchart"));

        const core::Cell* vertex = firstVertex(window.session().document());
        QVERIFY(vertex != nullptr);
        const std::string vertexId = vertex->id();
        const double x = vertex->geometry()->x();
        const double y = vertex->geometry()->y();

        window.session().selection().set({vertexId});
        const QByteArray original = test::savedBytes(window.session());
        QVERIFY(window.session()
                    .edit(QStringLiteral("Move"),
                          [&](commands::Transaction& tx) {
                              return commands::ops::moveCells(tx, 0, {vertexId}, 30, 20);
                          })
                    .ok);
        QVERIFY(window.isWindowModified());

        QVERIFY(window.controller().saveAs());
        QVERIFY(!window.isWindowModified());
        const format::ReadResult reloaded = format::readFile(prompts.savePath);
        QVERIFY(reloaded.ok());
        const core::Cell* moved = reloaded.document->page(0)->findCell(vertexId);
        QVERIFY(moved != nullptr && moved->geometry() != nullptr);
        QCOMPARE(moved->geometry()->x(), x + 30);
        QCOMPARE(moved->geometry()->y(), y + 20);

        // Undo restores the opened document byte for byte.
        window.session().undo();
        QCOMPARE(test::savedBytes(window.session()), original);
        QVERIFY(prompts.errors.isEmpty());
    }
};

SHELL_TEST_MAIN(TestShellWindow)
#include "tst_shell_window.moc"
