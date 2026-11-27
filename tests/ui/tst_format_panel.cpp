// The Format panel driven through its controls (found by objectName): tab
// set per selection, writes with one undo step each, mixed display and the
// tab renders.

#include "support/test_support.h"

#include "../../src/ui/font_families.h"
#include "qraw/ui/format_panel.h"

#include <QApplication>
#include <QCheckBox>
#include <QComboBox>
#include <QCompleter>
#include <QFontDatabase>
#include <QDoubleSpinBox>
#include <QSettings>
#include <QLineEdit>
#include <QSpinBox>
#include <QTabWidget>
#include <QToolButton>
#include <QtTest>

#include <algorithm>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

// A control of the panel by objectName (the tabs keep their widgets alive
// while they are not the current tab).
template <typename T>
T* control(const FormatPanel& panel, const char* name)
{
    return panel.findChild<T*>(QString::fromLatin1(name));
}

QString styleValue(const DocumentSession& session, const char* id, const char* key)
{
    const core::Cell* cell = session.document().page(0)->findCell(id);
    return cell ? QString::fromStdString(cell->style().value(key).value_or("<absent>")) : QString();
}

} // namespace

class TestFormatPanel : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { isolateSettings(); }
    void init() { QSettings().clear(); }

    void diagramTabWhenNothingSelected();
    void cellTabsWhenSelected();
    void gridCheckboxWritesAndUndoes();
    void fillColorWritesBothCellsInOneUndoStep();
    void strokeWidthStepsAreOneUndoStep();
    void mixedFormattedTextShowsPartialState();
    void boldToggleAppliesToBothCells();
    void widthSpinResizesSelectedVertex();
    void tabsRenderToPng();
    void typedFieldCommitsOnPendingEdits();
    void fontListHasDefaultsRecentsAndInstalledFamilies();
    void usedFontsComeFirst();
    void fontFamilyListIsWrittenVerbatim();
    void fontSearchMatchesAnywhereInTheName();
};

void TestFormatPanel::diagramTabWhenNothingSelected()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);

    QCOMPARE(panel.tabs()->count(), 1);
    QCOMPARE(panel.tabs()->tabText(0), QStringLiteral("Diagram"));
    QVERIFY(control<QCheckBox>(panel, "format.grid") != nullptr);
}

void TestFormatPanel::cellTabsWhenSelected()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    const QByteArray before = savedBytes(session);

    session.selection().set({"a"});

    QCOMPARE(panel.tabs()->count(), 3);
    QCOMPARE(panel.tabs()->tabText(0), QStringLiteral("Style"));
    QCOMPARE(panel.tabs()->tabText(1), QStringLiteral("Text"));
    QCOMPARE(panel.tabs()->tabText(2), QStringLiteral("Arrange"));
    // Showing values never writes.
    QVERIFY(!session.canUndo());
    QCOMPARE(savedBytes(session), before);
}

void TestFormatPanel::gridCheckboxWritesAndUndoes()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    const QByteArray before = savedBytes(session);

    QCheckBox* grid = control<QCheckBox>(panel, "format.grid");
    QVERIFY(grid != nullptr);
    QVERIFY(grid->isChecked());
    grid->click();

    QVERIFY(!session.currentPage()->gridEnabled());
    QVERIFY(session.canUndo());
    session.undo();
    QCOMPARE(savedBytes(session), before);
    QVERIFY(control<QCheckBox>(panel, "format.grid")->isChecked());
}

void TestFormatPanel::fillColorWritesBothCellsInOneUndoStep()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    session.selection().set({"a", "b"});
    const QByteArray before = savedBytes(session);

    QWidget* fill = control<QWidget>(panel, "format.fillColor");
    QVERIFY(fill != nullptr);
    QLineEdit* text = fill->findChild<QLineEdit*>();
    QVERIFY(text != nullptr);
    text->setText(QStringLiteral("#ff0000"));
    QTest::keyClick(text, Qt::Key_Return);

    QCOMPARE(styleValue(session, "a", "fillColor"), QStringLiteral("#ff0000"));
    QCOMPARE(styleValue(session, "b", "fillColor"), QStringLiteral("#ff0000"));

    // One step: a single undo restores the document.
    session.undo();
    QCOMPARE(savedBytes(session), before);
    QVERIFY(!session.canUndo());
}

void TestFormatPanel::typedFieldCommitsOnPendingEdits()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    panel.show();
    QVERIFY(QTest::qWaitForWindowActive(&panel));
    session.selection().set({"a"});
    const QByteArray before = savedBytes(session);

    QLineEdit* text = control<QWidget>(panel, "format.fillColor")->findChild<QLineEdit*>();
    QVERIFY(text != nullptr);
    text->setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(text));
    text->selectAll(); // the field shows the current fill (#ffffff)
    QTest::keyClicks(text, QStringLiteral("#ff0000")); // typed, not committed

    QCOMPARE(styleValue(session, "a", "fillColor"), QStringLiteral("<absent>"));
    session.flushPendingEdits(); // what Ctrl+S does before saving
    QCOMPARE(styleValue(session, "a", "fillColor"), QStringLiteral("#ff0000"));

    // The focus-out that follows must not add an undo entry for the same value.
    text->editingFinished();
    session.undo();
    QCOMPARE(savedBytes(session), before);
    QVERIFY(!session.canUndo());
}

void TestFormatPanel::strokeWidthStepsAreOneUndoStep()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    session.selection().set({"a"});
    const QByteArray before = savedBytes(session);

    QSpinBox* width = control<QSpinBox>(panel, "format.strokeWidth");
    QVERIFY(width != nullptr);
    const int start = width->value();
    for (int step = 1; step <= 3; ++step)
        width->setValue(start + step); // each step is a user change
    QTest::keyClick(width, Qt::Key_Return); // editingFinished ends the merge

    QCOMPARE(styleValue(session, "a", "strokeWidth"), QString::number(start + 3));

    session.undo();
    QCOMPARE(savedBytes(session), before);
    QVERIFY(!session.canUndo());
}

void TestFormatPanel::mixedFormattedTextShowsPartialState()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    session.selection().set({"a", "b"}); // only "b" is html=1

    QCheckBox* formatted = control<QCheckBox>(panel, "format.html");
    QVERIFY(formatted != nullptr);
    QCOMPARE(formatted->checkState(), Qt::PartiallyChecked);

    session.selection().set({"b"});
    QCOMPARE(control<QCheckBox>(panel, "format.html")->checkState(), Qt::Checked);
}

void TestFormatPanel::boldToggleAppliesToBothCells()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    session.selection().set({"a", "b"});
    const QByteArray before = savedBytes(session);

    QToolButton* bold = control<QToolButton>(panel, "format.bold");
    QVERIFY(bold != nullptr);
    bold->click();

    QCOMPARE(styleValue(session, "a", "fontStyle"), QStringLiteral("1"));
    QCOMPARE(styleValue(session, "b", "fontStyle"), QStringLiteral("1"));

    session.undo();
    QCOMPARE(savedBytes(session), before);
}

void TestFormatPanel::widthSpinResizesSelectedVertex()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    session.selection().set({"a"});

    QDoubleSpinBox* width = control<QDoubleSpinBox>(panel, "format.width");
    QVERIFY(width != nullptr);
    QCOMPARE(width->value(), 120.0);
    width->setValue(200);

    const core::Cell* cell = session.document().page(0)->findCell("a");
    QVERIFY(cell != nullptr && cell->geometry() != nullptr);
    QCOMPARE(cell->geometry()->width(), 200.0);
}

void TestFormatPanel::tabsRenderToPng()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    panel.resize(360, 720);
    panel.show();
    grabToPng(panel, QStringLiteral("format-diagram"));

    session.selection().set({"a"});
    for (int i = 0; i < panel.tabs()->count(); ++i) {
        panel.tabs()->setCurrentIndex(i);
        grabToPng(panel, QStringLiteral("format-tab-%1").arg(i)); // writes only when QRAW_SCREENSHOT_DIR is set
    }
}

// --- font family combo -----------------------------------------------------------

namespace {

QComboBox* fontCombo(const FormatPanel& panel)
{
    return control<QComboBox>(panel, "format.fontFamily");
}

QStringList itemTexts(const QComboBox& combo)
{
    QStringList texts;
    for (int i = 0; i < combo.count(); ++i)
        texts << combo.itemText(i);
    return texts;
}

// Chooses an entry as a click in the popup would.
void chooseFont(QComboBox& combo, const QString& family)
{
    const int index = combo.findText(family);
    QVERIFY(index >= 0);
    combo.setCurrentIndex(index);
    emit combo.activated(index);
}

} // namespace

void TestFormatPanel::fontListHasDefaultsRecentsAndInstalledFamilies()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    session.selection().set({"a"});
    const QComboBox* combo = fontCombo(panel);
    QVERIFY(combo != nullptr);

    const QStringList texts = itemTexts(*combo);
    QCOMPARE(texts.first(), QStringLiteral("Helvetica")); // draw.io's default fonts come first
    QVERIFY(texts.contains(QStringLiteral("Times New Roman")));
    // Private families (macOS lists ".Apple Color Emoji UI" and other system UI
    // faces first) are not offered to the user, so they are not expected.
    QStringList installed = QFontDatabase::families();
    installed.erase(std::remove_if(installed.begin(), installed.end(),
                                   [](const QString& f) { return QFontDatabase::isPrivateFamily(f); }),
                    installed.end());
    for (const QString& family : installed.mid(0, 5)) // case-insensitive: a case variant of a family is listed once
        QVERIFY2(texts.contains(family, Qt::CaseInsensitive), qPrintable(family));
    for (const QString& family : QFontDatabase::families()) {
        if (QFontDatabase::isPrivateFamily(family))
            QVERIFY2(!texts.contains(family), qPrintable(family + " is private and must not be listed"));
    }
    // The installed families are listed once each.
    QStringList sorted = texts.mid(texts.lastIndexOf(QString()) + 1);
    QCOMPARE(sorted.removeDuplicates(), 0);
}

void TestFormatPanel::usedFontsComeFirst()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    session.selection().set({"a"});
    QComboBox* combo = fontCombo(panel);
    QVERIFY(combo != nullptr);
    QVERIFY(AppSettings().recentFonts().isEmpty());

    chooseFont(*combo, QStringLiteral("Georgia"));
    QCOMPARE(styleValue(session, "a", "fontFamily"), QStringLiteral("Georgia"));
    chooseFont(*combo, QStringLiteral("Tahoma"));
    QCOMPARE(AppSettings().recentFonts(), (QStringList{QStringLiteral("Tahoma"), QStringLiteral("Georgia")}));

    // After the defaults and a separator: the recent fonts, most recent first.
    const int firstRecent = static_cast<int>(defaultFontFamilies().size()) + 1;
    QCOMPARE(combo->itemText(firstRecent), QStringLiteral("Tahoma"));
    QCOMPARE(combo->itemText(firstRecent + 1), QStringLiteral("Georgia"));

    // At most ten, and a font used again moves up.
    for (int i = 0; i < 12; ++i) {
        const QString family = QStringLiteral("Family %1").arg(i);
        combo->setEditText(family);
        emit combo->lineEdit()->editingFinished();
    }
    QCOMPARE(AppSettings().recentFonts().size(), AppSettings::kMaxRecentFonts);
    QCOMPARE(AppSettings().recentFonts().first(), QStringLiteral("Family 11"));
}

void TestFormatPanel::fontFamilyListIsWrittenVerbatim()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    session.selection().set({"a"});
    QComboBox* combo = fontCombo(panel);
    QVERIFY(combo != nullptr);

    combo->setEditText(QStringLiteral("Arial, Helvetica,sans-serif"));
    emit combo->lineEdit()->editingFinished();
    QCOMPARE(styleValue(session, "a", "fontFamily"), QStringLiteral("Arial, Helvetica,sans-serif"));
    QCOMPARE(AppSettings().recentFonts().first(), QStringLiteral("Arial, Helvetica,sans-serif"));

    // Shown as it is, also after another selection.
    session.selection().set({"b"});
    QCOMPARE(fontCombo(panel)->currentText(), QStringLiteral("Helvetica")); // the resolved default
    session.selection().set({"a"});
    QCOMPARE(fontCombo(panel)->currentText(), QStringLiteral("Arial, Helvetica,sans-serif"));

    // Mixed values show nothing.
    session.selection().set({"a", "b"});
    QCOMPARE(fontCombo(panel)->currentText(), QString());
}

void TestFormatPanel::fontSearchMatchesAnywhereInTheName()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    session.selection().set({"a"});
    const QComboBox* combo = fontCombo(panel);
    QVERIFY(combo != nullptr && combo->completer() != nullptr);
    QCompleter* completer = combo->completer();

    completer->setCompletionPrefix(QStringLiteral("new rom")); // the middle of "Times New Roman", any case
    QVERIFY(completer->completionCount() >= 1);
    QStringList found;
    for (int i = 0; i < completer->completionCount(); ++i) {
        completer->setCurrentRow(i);
        found << completer->currentCompletion();
    }
    QVERIFY(found.contains(QStringLiteral("Times New Roman")));
    for (const QString& family : found)
        QVERIFY2(family.contains(QStringLiteral("new rom"), Qt::CaseInsensitive), qPrintable(family));

    completer->setCompletionPrefix(QStringLiteral("no such family xyz"));
    QCOMPARE(completer->completionCount(), 0);
}

QTEST_MAIN(TestFormatPanel)
#include "tst_format_panel.moc"
