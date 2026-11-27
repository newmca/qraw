// The KDE HIG menu rules, over the action registry and over the menus of the window (D-050):
//  * Title Case (small words a, an, and, as, at, but, by, for, from, in, into, of, on, or, the, to, with in
//    lower case unless first or last);
//  * the ellipsis (U+2026, never three dots) only on actions that always open a dialog that asks for more
//    input, and on every one of them;
//  * no two actions share a shortcut; bare function keys only where KDE has a standard shortcut for them;
//    no Meta modifier.

#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/ui/main_window.h"

#include <QAction>
#include <QKeySequence>
#include <QMenu>
#include <QMenuBar>
#include <QSet>
#include <QTest>
#include <QTimer>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

const QSet<QString>& smallWords()
{
    static const QSet<QString> words{"a", "an", "and", "as", "at", "but", "by", "for", "from", "in", "into", "of", "on", "or", "the", "to", "with"};
    return words;
}

// Words that are written the way their owner writes them, whatever Title Case says. None of the texts needs
// one today besides "Qraw" (which starts with a capital anyway); a new proper name is added here on purpose.
const QSet<QString>& properNames()
{
    static const QSet<QString> names{"Qraw", "draw.io", "macOS", "iOS"};
    return names;
}

QString withoutMnemonicAndEllipsis(QString text)
{
    text.remove(QLatin1Char('&'));
    if (text.endsWith(QChar(kEllipsis)))
        text.chop(1);
    return text;
}

// An empty string when `raw` is in Title Case, else what is wrong with it.
QString titleCaseProblem(const QString& raw)
{
    QStringList words;
    for (const QString& part : withoutMnemonicAndEllipsis(raw).split(QLatin1Char(' '), Qt::SkipEmptyParts)) {
        for (const QString& word : part.split(QLatin1Char('/'), Qt::SkipEmptyParts))
            words << word;
    }
    for (int i = 0; i < words.size(); ++i) {
        const QString& word = words.at(i);
        if (properNames().contains(word))
            continue;
        const bool edge = i == 0 || i == words.size() - 1;
        if (!edge && smallWords().contains(word.toLower())) {
            if (word != word.toLower())
                return QStringLiteral("\"%1\" is a small word inside the text: lower case").arg(word);
        } else if (word.at(0).isLetter() && !word.at(0).isUpper()) {
            return QStringLiteral("\"%1\" must start with a capital").arg(word);
        }
    }
    return {};
}

QStringList textProblems(const QString& text, const QString& where)
{
    QStringList problems;
    if (text.contains(QLatin1String("...")))
        problems << QStringLiteral("%1 \"%2\": three dots, the ellipsis is U+2026").arg(where, text);
    if (const QString problem = titleCaseProblem(text); !problem.isEmpty())
        problems << QStringLiteral("%1 \"%2\": %3").arg(where, text, problem);
    const QString body = text.chopped(text.endsWith(QChar(kEllipsis)) ? 1 : 0);
    if (body.contains(QChar(kEllipsis)))
        problems << QStringLiteral("%1 \"%2\": the ellipsis belongs at the end").arg(where, text);
    return problems;
}

// The texts of a menu and of its submenus, for the problems above. Recent files carry file names.
void collectMenuProblems(const QMenu& menu, const QString& path, QStringList& problems)
{
    if (menu.objectName() == QLatin1String("menu.recent"))
        return;
    for (const QAction* action : menu.actions()) {
        if (action->isSeparator() || action->text().isEmpty())
            continue; // the table picker is a widget
        const QString where = path + QLatin1String(" > ") + withoutMnemonicAndEllipsis(action->text());
        problems << textProblems(action->text(), path);
        if (action->menu() != nullptr)
            collectMenuProblems(*action->menu(), where, problems);
    }
}

bool isFunctionKey(Qt::Key key)
{
    return key >= Qt::Key_F1 && key <= Qt::Key_F35;
}

} // namespace

class TestMenuConventions : public QObject {
    Q_OBJECT

private slots:
    void theRulesRecogniseTitleCase();
    void everyActionTextIsInTitleCase();
    void theEllipsisIsOnTheActionsThatOpenADialogAndOnlyOnThose();
    void everyMenuOfTheWindowFollowsTheRules();
    void theContextMenusFollowTheRules();
    void noTwoActionsShareAShortcut();
    void bareFunctionKeysAreKdeStandardShortcuts();
    void otherBareKeysAreTheKnownOnes();
    void noShortcutUsesTheMetaKey();
    void standardKeysAreUsedWhereOneExists();
};

void TestMenuConventions::theRulesRecogniseTitleCase()
{
    QVERIFY(titleCaseProblem("Copy as Image").isEmpty());
    QVERIFY(titleCaseProblem("&Open…").isEmpty());
    QVERIFY(titleCaseProblem("Set as Default Style").isEmpty());
    QVERIFY(titleCaseProblem("Remove from Group").isEmpty());
    QVERIFY(titleCaseProblem("Lock/Unlock").isEmpty());
    QVERIFY(titleCaseProblem("Rotate 90 Degrees").isEmpty());
    QVERIFY(titleCaseProblem("To Front").isEmpty());      // a small word first is capitalised
    QVERIFY(titleCaseProblem("Move Selection To").isEmpty()); // and last
    QVERIFY(!titleCaseProblem("Copy As Image").isEmpty());
    QVERIFY(!titleCaseProblem("Open recent").isEmpty());
    QVERIFY(!titleCaseProblem("Edit style…").isEmpty());
    QVERIFY(!titleCaseProblem("find/replace").isEmpty());
    QVERIFY(!textProblems("Open...", "x").isEmpty()); // three dots
    QVERIFY(!textProblems("Op…en", "x").isEmpty());
}

void TestMenuConventions::everyActionTextIsInTitleCase()
{
    ActionRegistry registry;
    createStandardActions(registry);
    QStringList problems;
    for (const ActionInfo& info : registry.actions())
        problems << textProblems(info.action->text(), info.id);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
}

void TestMenuConventions::theEllipsisIsOnTheActionsThatOpenADialogAndOnlyOnThose()
{
    ActionRegistry registry;
    createStandardActions(registry);
    QStringList problems;
    int dialogs = 0;
    for (const ActionInfo& info : registry.actions()) {
        const bool hasEllipsis = info.action->text().endsWith(QChar(kEllipsis));
        dialogs += info.opensDialog ? 1 : 0;
        if (hasEllipsis != info.opensDialog)
            problems << QStringLiteral("%1 \"%2\": %3").arg(info.id, info.action->text(),
                                                              info.opensDialog ? "opens a dialog, no ellipsis" : "ellipsis without a dialog");
    }
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    QVERIFY(dialogs >= 15); // the list is not empty by accident
    // Spot checks of both kinds.
    QCOMPARE(registry[action_id::kOpen]->text(), QStringLiteral("&Open…"));
    QCOMPARE(registry[action_id::kSaveAs]->text(), QStringLiteral("Save &As…"));
    QCOMPARE(registry[action_id::kNew]->text(), QStringLiteral("&New"));
    QCOMPARE(registry[action_id::kPrintPreview]->text(), QStringLiteral("Print Pre&view"));
    QCOMPARE(registry[action_id::kFind]->text(), QStringLiteral("&Find/Replace")); // it shows a panel
    QCOMPARE(registry[action_id::kFullscreen]->text(), QStringLiteral("Full Screen"));
    QCOMPARE(withEllipsis(QStringLiteral("Open…")), QStringLiteral("Open…")); // once
}

void TestMenuConventions::everyMenuOfTheWindowFollowsTheRules()
{
    MainWindow window;
    QStringList problems;
    for (const QAction* top : window.menuBar()->actions()) {
        problems << textProblems(top->text(), QStringLiteral("menu bar"));
        collectMenuProblems(*top->menu(), withoutMnemonicAndEllipsis(top->text()), problems);
    }
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
}

void TestMenuConventions::theContextMenusFollowTheRules()
{
    MainWindow window;
    window.session().resetDocument(simpleDocument());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QStringList problems;
    int menus = 0;
    for (const bool onSelection : {true, false}) {
        window.session().selection().set(onSelection ? std::vector<std::string>{"a"} : std::vector<std::string>{});
        QTimer::singleShot(100, [&] {
            if (auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget())) {
                ++menus;
                collectMenuProblems(*menu, onSelection ? QStringLiteral("selection menu") : QStringLiteral("canvas menu"), problems);
                menu->close();
            }
        });
        emit window.canvas().contextMenuRequested(window.canvas().mapToGlobal(QPoint(300, 300)), QPointF(300, 300));
    }
    QCOMPARE(menus, 2);
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
}

void TestMenuConventions::noTwoActionsShareAShortcut()
{
    ActionRegistry registry;
    createStandardActions(registry);
    QMap<QString, QString> owner;
    QStringList problems;
    for (const ActionInfo& info : registry.actions()) {
        for (const QKeySequence& sequence : info.action->shortcuts()) {
            const QString key = sequence.toString(QKeySequence::PortableText);
            if (owner.contains(key))
                problems << QStringLiteral("%1 is on both %2 and %3").arg(key, owner.value(key), info.id);
            owner.insert(key, info.id);
        }
    }
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    // The canvas keys do not take an action's shortcut either: the arrows (alone, with Shift), Tab, Enter, Escape.
    for (const char* canvasKey : {"Left", "Right", "Up", "Down", "Shift+Left", "Shift+Right", "Shift+Up", "Shift+Down", "Tab", "Shift+Tab",
                                  "Alt+Tab", "Alt+Shift+Tab", "Ctrl+Tab", "Return", "Enter", "Escape", "Space"})
        QVERIFY2(!owner.contains(QString::fromLatin1(canvasKey)), canvasKey);
}

// KDE's standard function-key shortcuts (KStandardShortcut): F1 Help, F2 Rename (edit the name in place), F3 Find
// Next, F5 Reload, F11 Full Screen. HIG: no other bare function key.
void TestMenuConventions::bareFunctionKeysAreKdeStandardShortcuts()
{
    static const QSet<int> kdeStandard{Qt::Key_F1, Qt::Key_F2, Qt::Key_F3, Qt::Key_F5, Qt::Key_F11};
    ActionRegistry registry;
    createStandardActions(registry);
    QStringList problems;
    QSet<int> used;
    for (const ActionInfo& info : registry.actions()) {
        for (const QKeySequence& sequence : info.action->shortcuts()) {
            if (sequence.count() != 1 || sequence[0].keyboardModifiers() != Qt::NoModifier || !isFunctionKey(sequence[0].key()))
                continue;
            used.insert(sequence[0].key());
            if (!kdeStandard.contains(sequence[0].key()))
                problems << QStringLiteral("%1: %2 is a bare function key that KDE does not use for it").arg(info.id, sequence.toString());
        }
    }
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
    QVERIFY(used.contains(Qt::Key_F1));
    QVERIFY(used.contains(Qt::Key_F2));
    QCOMPARE(registry[action_id::kKeyboardShortcuts]->shortcut(), QKeySequence(Qt::Key_F1));
    QCOMPARE(registry[action_id::kEditLabel]->shortcut(), QKeySequence(Qt::Key_F2));
}

// Bare keys that are not function keys: draw.io's (Delete and Backspace delete, Home resets the view, X draws
// freehand) and the dedicated keys of some keyboards that the platform lists next to the standard shortcuts
// (Key_Open, Key_Save, ... of QKeySequence::keyBindings). A new bare key has to be added here on purpose.
void TestMenuConventions::otherBareKeysAreTheKnownOnes()
{
    QSet<int> known{Qt::Key_Delete, Qt::Key_Backspace, Qt::Key_Home, Qt::Key_X};
    for (int standard = 1; standard < 100; ++standard) {
        for (const QKeySequence& binding : QKeySequence::keyBindings(static_cast<QKeySequence::StandardKey>(standard))) {
            if (binding.count() == 1 && binding[0].keyboardModifiers() == Qt::NoModifier)
                known.insert(binding[0].key());
        }
    }
    ActionRegistry registry;
    createStandardActions(registry);
    QStringList problems;
    for (const ActionInfo& info : registry.actions()) {
        for (const QKeySequence& sequence : info.action->shortcuts()) {
            if (sequence.count() != 1 || sequence[0].keyboardModifiers() != Qt::NoModifier || isFunctionKey(sequence[0].key()))
                continue;
            if (!known.contains(sequence[0].key()))
                problems << QStringLiteral("%1: %2 is an unexpected bare key").arg(info.id, sequence.toString());
        }
    }
    QVERIFY2(problems.isEmpty(), qPrintable(problems.join(QLatin1Char('\n'))));
}

void TestMenuConventions::noShortcutUsesTheMetaKey()
{
#ifdef Q_OS_MACOS
    QSKIP("macOS standard shortcuts use Control, which Qt reports as Meta"); // Qt maps Cmd to Ctrl and Ctrl to Meta
#endif
    ActionRegistry registry;
    createStandardActions(registry);
    for (const ActionInfo& info : registry.actions()) {
        for (const QKeySequence& sequence : info.action->shortcuts()) {
            for (int i = 0; i < sequence.count(); ++i)
                QVERIFY2(!(sequence[i].keyboardModifiers() & Qt::MetaModifier), qPrintable(info.id + QLatin1String(": ") + sequence.toString()));
        }
    }
}

void TestMenuConventions::standardKeysAreUsedWhereOneExists()
{
    ActionRegistry registry;
    createStandardActions(registry);
    const struct {
        const char* id;
        QKeySequence::StandardKey key;
    } standard[] = {{action_id::kNew, QKeySequence::New},       {action_id::kOpen, QKeySequence::Open},
                    {action_id::kSave, QKeySequence::Save},     {action_id::kSaveAs, QKeySequence::SaveAs},
                    {action_id::kPrint, QKeySequence::Print},   {action_id::kClose, QKeySequence::Close},
                    {action_id::kQuit, QKeySequence::Quit},     {action_id::kUndo, QKeySequence::Undo},
                    {action_id::kRedo, QKeySequence::Redo},     {action_id::kCut, QKeySequence::Cut},
                    {action_id::kCopy, QKeySequence::Copy},     {action_id::kPaste, QKeySequence::Paste},
                    {action_id::kSelectAll, QKeySequence::SelectAll}, {action_id::kFind, QKeySequence::Find},
                    {action_id::kZoomIn, QKeySequence::ZoomIn}, {action_id::kZoomOut, QKeySequence::ZoomOut},
                    {action_id::kFullscreen, QKeySequence::FullScreen}};
    for (const auto& entry : standard) {
        const QList<QKeySequence> shortcuts = registry[entry.id]->shortcuts();
        for (const QKeySequence& binding : QKeySequence::keyBindings(entry.key)) {
            // A platform binding that draw.io uses for something else (Ctrl+Shift+F is To Front) is not taken.
            if (binding == QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F))
                continue;
            QVERIFY2(shortcuts.contains(binding), qPrintable(QStringLiteral("%1 lacks %2").arg(entry.id, binding.toString())));
        }
    }
}

SHELL_TEST_MAIN(TestMenuConventions)
#include "tst_menu_conventions.moc"
