// Set as Default Style: new cells from the palette, the connect gestures and
// blank-canvas text take the default style; paste and duplicate do not; the
// setting survives a reload; the action, its shortcut, the context menu and the
// Style tab buttons.

#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/commands/ops_properties.h"
#include "qraw/ui/format_panel.h"
#include "qraw/ui/main_window.h"
#include "qraw/ui/palette_dock.h"

#include <QAction>
#include <QMenu>
#include <QPushButton>
#include <QTextEdit>

#include <set>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;
namespace id = qraw::ui::action_id;

namespace {

void setStyle(DocumentSession& session, const char* cell, const char* style)
{
    QVERIFY(session.edit(QStringLiteral("Style"), [&](commands::Transaction& tx) {
        return commands::ops::setStyleText(tx, 0, {cell}, style);
    }).ok);
}

std::set<std::string> cellIds(const DocumentSession& session)
{
    std::set<std::string> ids;
    for (const core::Cell* cell : session.currentPage()->cells())
        ids.insert(cell->id());
    return ids;
}

// The one cell added since `before`, or null.
const core::Cell* addedCell(const DocumentSession& session, const std::set<std::string>& before)
{
    const core::Cell* found = nullptr;
    for (const core::Cell* cell : session.currentPage()->cells()) {
        if (before.count(cell->id()) == 0) {
            if (found != nullptr)
                return nullptr;
            found = cell;
        }
    }
    return found;
}

QString value(const core::Cell& cell, const char* key)
{
    return QString::fromStdString(cell.style().value(key).value_or("<absent>"));
}

QTextEdit* editorWidget(CanvasView& view)
{
    for (QTextEdit* edit : view.viewport()->findChildren<QTextEdit*>()) {
        if (edit->isVisible())
            return edit;
    }
    return nullptr;
}

} // namespace

class TestStyleDefaults : public QObject {
    Q_OBJECT

private slots:
    void init() { clearDefaultStyle(); }
    void cleanup() { clearDefaultStyle(); }

    void paletteInsertTakesTheVertexDefaultAndClearRemovesIt()
    {
        DocumentSession session(simpleDocument());
        setStyle(session, "a", "rounded=0;whiteSpace=wrap;fillColor=#FF0000;fontSize=18;");
        session.selection().set({"a"});
        QVERIFY(actions::canSetDefaultStyle(session));
        QVERIFY(actions::setAsDefaultStyle(session));

        auto canvas = makeCanvas(session);
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        std::set<std::string> ids = cellIds(session);
        const QByteArray before = savedBytes(session);
        QVERIFY(dock.insertEntry(QStringLiteral("general/rectangle")));
        const core::Cell* inserted = addedCell(session, ids);
        QVERIFY(inserted != nullptr);
        QCOMPARE(value(*inserted, "fillColor"), QStringLiteral("#FF0000"));
        QCOMPARE(value(*inserted, "fontSize"), QStringLiteral("18"));
        QCOMPARE(session.undoText(), QStringLiteral("Insert"));
        QVERIFY(session.undo()); // one undo step for the whole insertion
        QCOMPARE(savedBytes(session), before);

        actions::clearDefaultStyle();
        ids = cellIds(session);
        QVERIFY(dock.insertEntry(QStringLiteral("general/rectangle")));
        inserted = addedCell(session, ids);
        QVERIFY(inserted != nullptr);
        QCOMPARE(value(*inserted, "fillColor"), QStringLiteral("<absent>"));
        QCOMPARE(value(*inserted, "fontSize"), QStringLiteral("<absent>"));
    }

    void defaultsNeedExactlyOneSelectedCell()
    {
        DocumentSession session(simpleDocument());
        QVERIFY(!actions::canSetDefaultStyle(session));
        session.selection().set({"a", "b"});
        QVERIFY(!actions::canSetDefaultStyle(session));
        QVERIFY(!actions::setAsDefaultStyle(session));
        QVERIFY(AppSettings().defaultStyles().empty());
    }

    void edgeDefaultAppliesToDragConnectedEdges()
    {
        DocumentSession session(simpleDocument());
        setStyle(session, "e", "edgeStyle=elbowEdgeStyle;strokeColor=#00FF00;endArrow=block;");
        session.selection().set({"e"});
        QVERIFY(actions::setAsDefaultStyle(session));
        const commands::DefaultStyles stored = AppSettings().defaultStyles();
        QVERIFY(stored.vertex.empty());
        QVERIFY(stored.edge.has("strokeColor"));

        session.selection().clear();
        auto view = makeCanvas(session);
        const std::set<std::string> ids = cellIds(session);
        move(*view, {100, 70}, Qt::NoModifier, Qt::NoButton);
        drag(*view, {180, 70}, {340, 70}); // east arrow of 'a' onto 'b'
        const core::Cell* edge = addedCell(session, ids);
        QVERIFY(edge != nullptr && edge->isEdge());
        QCOMPARE(value(*edge, "edgeStyle"), QStringLiteral("elbowEdgeStyle"));
        QCOMPARE(value(*edge, "strokeColor"), QStringLiteral("#00FF00"));
        QCOMPARE(value(*edge, "endArrow"), QStringLiteral("block"));
        QCOMPARE(value(*edge, "html"), QStringLiteral("1")); // the rest of a new connector stays
    }

    void danglingEdgeAndTextTakeTheirDefaults()
    {
        DocumentSession session(simpleDocument());
        setStyle(session, "e", "strokeColor=#00FF00;");
        setStyle(session, "a", "fillColor=#FF0000;fontSize=18;fontColor=#0000FF;");
        session.selection().set({"e"});
        QVERIFY(actions::setAsDefaultStyle(session));
        session.selection().set({"a"});
        QVERIFY(actions::setAsDefaultStyle(session));
        session.selection().clear();

        auto view = makeCanvas(session);
        std::set<std::string> ids = cellIds(session);
        drag(*view, {160, 70}, {300, 300}); // the east connection point of 'a' to blank canvas
        const core::Cell* edge = addedCell(session, ids);
        QVERIFY(edge != nullptr && edge->isEdge());
        QCOMPARE(value(*edge, "strokeColor"), QStringLiteral("#00FF00"));
        QCOMPARE(value(*edge, "edgeStyle"), QStringLiteral("none")); // the default edge is straight

        ids = cellIds(session);
        doubleClick(*view, {600, 400});
        QTextEdit* edit = editorWidget(*view);
        QVERIFY(edit != nullptr);
        QTest::keyClicks(edit, "Hi");
        QTest::keyClick(edit, Qt::Key_Return, Qt::ControlModifier);
        const core::Cell* text = addedCell(session, ids);
        QVERIFY(text != nullptr && text->style().hasName("text"));
        // Only the text styles of the vertex default reach a text cell.
        QCOMPARE(value(*text, "fontSize"), QStringLiteral("18"));
        QCOMPARE(value(*text, "fontColor"), QStringLiteral("#0000FF"));
        QCOMPARE(value(*text, "fillColor"), QStringLiteral("none"));
    }

    void duplicateAndPasteKeepTheirStyle()
    {
        DocumentSession session(simpleDocument());
        setStyle(session, "a", "rounded=0;fillColor=#FF0000;");
        session.selection().set({"a"});
        QVERIFY(actions::setAsDefaultStyle(session));
        setStyle(session, "a", "rounded=0;");

        const std::set<std::string> ids = cellIds(session);
        QVERIFY(actions::duplicateSelection(session));
        const core::Cell* clone = addedCell(session, ids);
        QVERIFY(clone != nullptr);
        QCOMPARE(QString::fromStdString(clone->styleText()), QStringLiteral("rounded=0;"));
    }

    void settingSurvivesAReload()
    {
        DocumentSession session(simpleDocument());
        setStyle(session, "a", "rounded=0;fillColor=#FF0000;fontSize=18;text;");
        session.selection().set({"a"});
        QVERIFY(actions::setAsDefaultStyle(session));
        const commands::DefaultStyles stored = AppSettings().defaultStyles();
        QSettings().sync();

        const AppSettings reloaded; // a new instance reads the stored strings
        const commands::DefaultStyles again = reloaded.defaultStyles();
        QCOMPARE(again, stored);
        QCOMPARE(QString::fromStdString(again.vertex.value("fillColor").value_or("")), QStringLiteral("#FF0000"));
        QVERIFY(again.vertexFromText);
        QCOMPARE(QSettings().value(QStringLiteral("style/defaultVertex")).toString(), QStringLiteral("rounded=0;fillColor=#FF0000;fontSize=18;"));

        actions::clearDefaultStyle();
        QVERIFY(AppSettings().defaultStyles().empty());
        QVERIFY(!QSettings().contains(QStringLiteral("style/defaultVertex")));
    }

    void shortcutsTriggerTheActionsAndButtonsFollowThem()
    {
        MainWindow window;
        window.resize(1200, 800);
        window.show();
        QVERIFY(QTest::qWaitForWindowActive(&window));
        DocumentSession& session = window.session();
        session.resetDocument(simpleDocument());
        setStyle(session, "a", "rounded=0;fillColor=#FF0000;");
        QAction* setDefault = window.actions()[id::kSetDefaultStyle];
        QAction* clear = window.actions()[id::kClearDefaultStyle];
        QAction* paste = window.actions()[id::kPasteStyle];
        QCOMPARE(setDefault->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D));
        QCOMPARE(clear->shortcut(), QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R));
        session.selection().clear();
        QVERIFY(!setDefault->isEnabled());
        QVERIFY(!clear->isEnabled());

        session.selection().set({"a"});
        QVERIFY(setDefault->isEnabled());
        QTest::keyClick(window.canvas().viewport(), Qt::Key_D, Qt::ControlModifier | Qt::ShiftModifier);
        QVERIFY(!AppSettings().defaultStyles().empty());
        QVERIFY(clear->isEnabled());

        // The Style tab has the buttons; they follow the actions.
        FormatPanel* panel = window.formatPanel();
        auto* copyButton = panel->findChild<QPushButton*>(QStringLiteral("format.copyStyle"));
        auto* pasteButton = panel->findChild<QPushButton*>(QStringLiteral("format.pasteStyle"));
        auto* defaultButton = panel->findChild<QPushButton*>(QStringLiteral("format.setAsDefaultStyle"));
        QVERIFY(copyButton && pasteButton && defaultButton);
        QVERIFY(copyButton->isEnabled());
        QVERIFY(!pasteButton->isEnabled()); // nothing copied yet
        QCOMPARE(pasteButton->isEnabled(), paste->isEnabled());
        copyButton->click();
        QVERIFY(paste->isEnabled());
        QVERIFY(pasteButton->isEnabled());
        session.selection().set({"b"});
        pasteButton->click();
        QCOMPARE(value(*session.currentPage()->findCell("b"), "fillColor"), QStringLiteral("#FF0000"));
        session.selection().set({"a", "b"});
        QVERIFY(!defaultButton->isEnabled());

        clear->trigger();
        QVERIFY(AppSettings().defaultStyles().empty());
        QVERIFY(!clear->isEnabled());
    }

    void contextMenuListsTheStyleActions()
    {
        MainWindow window;
        window.show();
        DocumentSession& session = window.session();
        session.resetDocument(simpleDocument());
        setStyle(session, "a", "rounded=0;fillColor=#FF0000;");

        // The popup menu runs its own event loop: it is inspected from a timer.
        QStringList listed;
        auto showMenu = [&] {
            listed.clear();
            QTimer::singleShot(0, [&] {
                if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                    for (const QAction* action : menu->actions())
                        listed << action->objectName();
                    menu->close();
                }
            });
            emit window.canvas().contextMenuRequested(window.canvas().mapToGlobal(QPoint(100, 100)), QPointF(100, 100));
        };

        session.selection().set({"a"});
        showMenu();
        for (const char* actionId : {id::kCopyStyle, id::kPasteStyle, id::kSetDefaultStyle})
            QVERIFY2(listed.contains(QString::fromLatin1(actionId)), actionId);

        QVERIFY(actions::setAsDefaultStyle(session));
        session.selection().clear();
        showMenu();
        QVERIFY(listed.contains(QString::fromLatin1(id::kClearDefaultStyle)));
        QVERIFY(!listed.contains(QString::fromLatin1(id::kSetDefaultStyle)));
    }

private:
    static void clearDefaultStyle() { actions::clearDefaultStyle(); }
};

SHELL_TEST_MAIN(TestStyleDefaults)
#include "tst_style_defaults.moc"
