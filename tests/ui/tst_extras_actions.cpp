// Extras: Edit Diagram dialog, page tab reordering by dragging, measurement
// units in the length fields, and Copy as Image.
#include "support/shell_env.h"
#include "support/test_support.h"

#include "../../src/ui/format_controls.h"

#include "qraw/commands/ops_pages.h"
#include "qraw/format/codec.h"
#include "qraw/format/embed.h"
#include "qraw/ui/edit_actions.h"
#include "qraw/ui/edit_diagram_dialog.h"
#include "qraw/ui/edit_geometry_dialog.h"
#include "qraw/ui/format_panel.h"
#include "qraw/ui/main_window.h"
#include "qraw/ui/measure_units.h"
#include "qraw/ui/page_tabs.h"

#include <QAction>
#include <QActionGroup>
#include <QApplication>
#include <QClipboard>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFontDatabase>
#include <QImage>
#include <QLabel>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QSettings>
#include <QTabBar>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;
namespace id = qraw::ui::action_id;

namespace {

template <typename T>
T* control(const QWidget& parent, const char* name)
{
    return parent.findChild<T*>(QString::fromLatin1(name));
}

QString valueOf(const DocumentSession& session, const char* cell)
{
    return QString::fromStdString(session.currentPage()->findCell(cell)->value());
}

std::vector<std::string> pageNames(const DocumentSession& session)
{
    std::vector<std::string> names;
    for (const core::Page* page : session.document().pages())
        names.push_back(page->name());
    return names;
}

} // namespace

class TestExtrasActions : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { isolateProfile(); }
    void init() { QSettings().clear(); }

    // --- Edit Diagram ----------------------------------------------------------
    void editDiagramShowsThePageXml();
    void editDiagramKeepsTheDialogOpenOnAParseError();
    void editDiagramAppliesChangedTextAsOneStep();
    void editDiagramUnchangedTextIsNotParsed();
    void editDiagramCancelChangesNothing();
    void editDiagramLargeTextLoadsAndWarns();
    void extrasMenuHasEditDiagram();

    // --- page tabs -------------------------------------------------------------
    void draggingATabMovesThePageAsOneStep();
    void draggingWithTheMouseMovesThePage();
    void movePageActionsFromTheTabMenuAreOneStep();

    // --- units -----------------------------------------------------------------
    void conversionsRoundTripAtDrawioPrecision();
    void unitIsAPreferenceNotPartOfTheFile();
    void arrangeFieldsShowAndWriteInTheUnit();
    void diagramFieldsFollowTheUnit();
    void editGeometryDialogUsesTheUnit();
    void viewMenuUnitsFollowThePreference();

    // --- copy as image ---------------------------------------------------------
    void copyAsImageOfTheSelectionHasTheBoundsAtThePixelRatio();
    void copyAsImageOfThePageWhenNothingIsSelected();
    void copyAsImageIsTransparentUnlessThePageHasABackground();
    void copyAsImageCarriesTheDiagramInThePng();
    void copyAsImageActionFollowsTheDocument();
};

// --- Edit Diagram -------------------------------------------------------------

void TestExtrasActions::editDiagramShowsThePageXml()
{
    DocumentSession session(simpleDocument());
    const std::string text = format::pageModelText(*session.currentPage());
    EditDiagramDialog dialog(text);
    auto* edit = control<QPlainTextEdit>(dialog, "diagram.text");
    QVERIFY(edit != nullptr);
    QVERIFY(edit->toPlainText().startsWith(QStringLiteral("<mxGraphModel")));
    QVERIFY(edit->toPlainText().contains(QStringLiteral("<mxCell id=\"a\"")));
    QCOMPARE(edit->toPlainText().toStdString(), text);
    QVERIFY(edit->lineWrapMode() == QPlainTextEdit::NoWrap);
    QCOMPARE(edit->font(), QFontDatabase::systemFont(QFontDatabase::FixedFont));
}

void TestExtrasActions::editDiagramKeepsTheDialogOpenOnAParseError()
{
    DocumentSession session(simpleDocument());
    EditDiagramDialog dialog(format::pageModelText(*session.currentPage()));
    dialog.show();
    auto* edit = control<QPlainTextEdit>(dialog, "diagram.text");
    auto* error = control<QLabel>(dialog, "diagram.error");
    QVERIFY(!error->isVisible());

    QString text = edit->toPlainText();
    text.replace(QStringLiteral("</root>"), QStringLiteral("</rot>"));
    edit->setPlainText(text);
    dialog.accept();

    QVERIFY(dialog.isVisible());
    QVERIFY(dialog.result() != QDialog::Accepted);
    QVERIFY(!dialog.model());
    QVERIFY(error->isVisible());
    QVERIFY(error->text().contains(QStringLiteral("Line ")));
    // The cursor sits on the line of the error and the typed text is kept.
    QVERIFY(edit->toPlainText().contains(QStringLiteral("</rot>")));
    const int errorLine = edit->toPlainText().left(edit->toPlainText().indexOf(QStringLiteral("</rot>"))).count(u'\n') + 1;
    QCOMPARE(edit->textCursor().blockNumber() + 1, errorLine);

    // Fixing the text and pressing OK again accepts it.
    edit->setPlainText(edit->toPlainText()
                           .replace(QStringLiteral("</rot>"), QStringLiteral("</root>"))
                           .replace(QStringLiteral("value=\"A\""), QStringLiteral("value=\"Fixed\"")));
    dialog.accept();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(dialog.model());
}

void TestExtrasActions::editDiagramAppliesChangedTextAsOneStep()
{
    DocumentSession session(simpleDocument());
    const QByteArray before = savedBytes(session);
    EditDiagramDialog dialog(format::pageModelText(*session.currentPage()));
    auto* edit = control<QPlainTextEdit>(dialog, "diagram.text");
    edit->setPlainText(edit->toPlainText().replace(QStringLiteral("value=\"A\""), QStringLiteral("value=\"Edited\"")));
    dialog.accept();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    std::optional<core::Page> model = dialog.takeModel();
    QVERIFY(model);

    session.selection().set({"a"});
    QVERIFY(actions::replacePageModel(session, std::move(*model)));
    QCOMPARE(valueOf(session, "a"), QStringLiteral("Edited"));
    QCOMPARE(session.undoText(), QStringLiteral("Edit Diagram"));
    QVERIFY(session.selection().isEmpty() || session.selection().first() == "a");

    QVERIFY(session.undo());
    QCOMPARE(savedBytes(session), before);
    QVERIFY(!session.canUndo());
    QVERIFY(session.redo());
    QCOMPARE(valueOf(session, "a"), QStringLiteral("Edited"));
}

void TestExtrasActions::editDiagramUnchangedTextIsNotParsed()
{
    DocumentSession session(simpleDocument());
    EditDiagramDialog dialog(format::pageModelText(*session.currentPage()));
    dialog.accept();
    QCOMPARE(dialog.result(), int(QDialog::Accepted));
    QVERIFY(!dialog.model());
}

void TestExtrasActions::editDiagramCancelChangesNothing()
{
    DocumentSession session(simpleDocument());
    const QByteArray before = savedBytes(session);
    EditDiagramDialog dialog(format::pageModelText(*session.currentPage()));
    control<QPlainTextEdit>(dialog, "diagram.text")->setPlainText(QStringLiteral("garbage"));
    dialog.reject();
    QCOMPARE(dialog.result(), int(QDialog::Rejected));
    QVERIFY(!dialog.model());
    QCOMPARE(savedBytes(session), before);
    QVERIFY(!session.canUndo());
}

// A page with embedded images is tens of megabytes of text on a few lines. The dialog
// takes it as it is; the bar is that it loads, and says that it is large.
void TestExtrasActions::editDiagramLargeTextLoadsAndWarns()
{
    std::string text = "<mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>";
    const std::string payload(1200 * 1024, 'A');
    for (int i = 0; i < 8; ++i) {
        text += "<mxCell id=\"i" + std::to_string(i) + "\" style=\"shape=image;image=data:image/png,";
        text += payload;
        text += ";\" vertex=\"1\" parent=\"1\"><mxGeometry width=\"100\" height=\"60\" as=\"geometry\"/></mxCell>\n";
    }
    text += "</root></mxGraphModel>";
    QVERIFY(qsizetype(text.size()) > EditDiagramDialog::kLargeText);

    QElapsedTimer timer;
    timer.start();
    EditDiagramDialog dialog(text);
    dialog.resize(640, 460);
    dialog.show();
    QApplication::processEvents();
    QVERIFY2(timer.elapsed() < 20000, qPrintable(QString::number(timer.elapsed())));
    QVERIFY(control<QLabel>(dialog, "diagram.sizeNote") != nullptr);
    QCOMPARE(control<QPlainTextEdit>(dialog, "diagram.text")->toPlainText().size(), qsizetype(text.size()));
    // OK on the untouched text does not parse or copy it.
    dialog.accept();
    QVERIFY(!dialog.model());
}

void TestExtrasActions::extrasMenuHasEditDiagram()
{
    MainWindow window;
    QMenu* extras = window.findChild<QMenu*>(QStringLiteral("menu.extras"));
    QVERIFY(extras != nullptr);
    QStringList ids;
    for (QAction* action : extras->actions())
        ids << action->objectName();
    QVERIFY(ids.contains(QString::fromLatin1(id::kEditDiagram)));
    QCOMPARE(window.actions()[id::kEditDiagram]->text(), QStringLiteral("Edit Diagram\u2026"));
    QVERIFY(window.actions()[id::kEditDiagram]->isEnabled());

    // A page that could not be decoded has no model to show: the item is off for it.
    std::string xml = kSimpleDiagram;
    const std::string tail = "</diagram></mxfile>";
    xml.replace(xml.rfind(tail), tail.size(), "</diagram><diagram id=\"bad\" name=\"Bad\">!!!notbase64!!!</diagram></mxfile>");
    window.session().resetDocument(documentFromXml(xml));
    window.session().setCurrentPage(1);
    QVERIFY(!window.actions()[id::kEditDiagram]->isEnabled());
    window.session().setCurrentPage(0);
    QVERIFY(window.actions()[id::kEditDiagram]->isEnabled());
}

// --- page tabs ---------------------------------------------------------------------

void TestExtrasActions::draggingATabMovesThePageAsOneStep()
{
    DocumentSession session(simpleDocument());
    PageTabs tabs(session);
    QVERIFY(tabs.insertPage());
    QVERIFY(tabs.insertPage());
    QVERIFY(tabs.renamePage(0, QStringLiteral("One")));
    QVERIFY(tabs.renamePage(1, QStringLiteral("Two")));
    QVERIFY(tabs.renamePage(2, QStringLiteral("Three")));
    tabs.resize(500, 40);
    tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    const QByteArray before = savedBytes(session);
    QTabBar* bar = tabs.tabBar();
    QVERIFY(bar->isMovable());
    session.endMerge();

    // Drag the first tab over the other two: the tab bar moves it past one tab at a time (tabMoved
    // each time, as it does under a real pointer; a synthetic mouse drag moves it past one tab
    // only), and the page moves when the mouse is released.
    const QPoint from = bar->tabRect(0).center();
    QTest::mousePress(bar, Qt::LeftButton, {}, from);
    bar->moveTab(0, 1);
    QTest::qWait(150);
    bar->moveTab(1, 2);
    QTest::qWait(300);
    QVERIFY2(bar->tabText(2) == QStringLiteral("One"), qPrintable(bar->tabText(0) + bar->tabText(1) + bar->tabText(2)));
    QTest::qWait(250); // longer than the pause before the page moves; the mouse is still down
    QVERIFY(pageNames(session) == (std::vector<std::string>{"One", "Two", "Three"})); // not yet
    QCOMPARE(session.undoText(), QStringLiteral("Rename Page"));
    QTest::mouseRelease(bar, Qt::LeftButton, {}, bar->tabRect(2).center());
    QTRY_VERIFY(pageNames(session) == (std::vector<std::string>{"Two", "Three", "One"}));
    QCOMPARE(bar->tabText(2), QStringLiteral("One"));
    QCOMPARE(int(session.currentPageIndex()), 2); // the moved page stays the current one

    // The whole drag is one undo step; the step before it is the rename.
    QCOMPARE(session.undoText(), QStringLiteral("Move Page"));
    QVERIFY(session.undo());
    QCOMPARE(savedBytes(session), before);
    QCOMPARE(session.undoText(), QStringLiteral("Rename Page"));
    QCOMPARE(bar->tabText(0), QStringLiteral("One"));
    QVERIFY(session.redo());
    QVERIFY(pageNames(session) == (std::vector<std::string>{"Two", "Three", "One"}));
}

void TestExtrasActions::draggingWithTheMouseMovesThePage()
{
    DocumentSession session(simpleDocument());
    PageTabs tabs(session);
    QVERIFY(tabs.insertPage());
    QVERIFY(tabs.insertPage());
    QVERIFY(tabs.renamePage(0, QStringLiteral("One")));
    QVERIFY(tabs.renamePage(1, QStringLiteral("Two")));
    QVERIFY(tabs.renamePage(2, QStringLiteral("Three")));
    tabs.resize(500, 40);
    tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));
    QTabBar* bar = tabs.tabBar();

    // A real pointer: press on the first tab, move across the others, release.
    const QPoint from = bar->tabRect(0).center();
    const QPoint to = bar->tabRect(2).center() + QPoint(60, 0);
    QSignalSpy spy(bar, &QTabBar::tabMoved);
    QTest::mousePress(bar, Qt::LeftButton, {}, from);
    for (int i = 1; i <= 40; ++i) {
        QTest::mouseMove(bar, from + (to - from) * i / 40);
        QTest::qWait(15);
    }
    QTest::mouseRelease(bar, Qt::LeftButton, {}, to);
    QVERIFY(spy.count() >= 1);
    QTRY_VERIFY(pageNames(session) == (std::vector<std::string>{"Two", "Three", "One"}));
    QCOMPARE(session.undoText(), QStringLiteral("Move Page")); // one step for the whole drag
    QVERIFY(session.undo());
    QVERIFY(pageNames(session) == (std::vector<std::string>{"One", "Two", "Three"}));
    QCOMPARE(session.undoText(), QStringLiteral("Rename Page"));
}

void TestExtrasActions::movePageActionsFromTheTabMenuAreOneStep()
{
    DocumentSession session(simpleDocument());
    PageTabs tabs(session);
    QVERIFY(tabs.insertPage());
    QVERIFY(tabs.renamePage(0, QStringLiteral("One")));
    QVERIFY(tabs.renamePage(1, QStringLiteral("Two")));
    const QByteArray before = savedBytes(session);
    QVERIFY(tabs.movePage(1, 0)); // Move Left of the second page
    QVERIFY(pageNames(session) == (std::vector<std::string>{"Two", "One"}));
    QCOMPARE(session.undoText(), QStringLiteral("Move Page"));
    QVERIFY(session.undo());
    QCOMPARE(savedBytes(session), before);
}

// --- units ------------------------------------------------------------------------

void TestExtrasActions::conversionsRoundTripAtDrawioPrecision()
{
    // draw.io's factors: 100 px per inch, 3.937 px per millimeter.
    QCOMPARE(units::toUnit(100, MeasureUnit::Inches), 1.0);
    QCOMPARE(units::toUnit(39.37, MeasureUnit::Millimeters), 10.0);
    QCOMPARE(units::toUnit(393.7, MeasureUnit::Centimeters), 10.0);
    QCOMPARE(units::toUnit(3937, MeasureUnit::Meters), 1.0);
    QCOMPARE(units::toUnit(827, MeasureUnit::Millimeters), 210.058);
    QCOMPARE(units::toUnit(12.34, MeasureUnit::Points), 12.34);
    // A value typed in a unit is rounded to 0.1 px, so that it reads back the same.
    QCOMPARE(units::fromUnit(10, MeasureUnit::Millimeters), 39.4);
    QCOMPARE(units::fromUnit(0.125, MeasureUnit::Inches), 12.5);
    QCOMPARE(units::fromUnit(5, MeasureUnit::Millimeters, true), 19.685);
    QCOMPARE(units::fromUnit(12.345, MeasureUnit::Points), 12.345);

    // Every pixel value on the 0.1 grid comes back through any unit.
    for (const MeasureUnit unit : kAllMeasureUnits) {
        for (int tenth = -500; tenth <= 12000; tenth += 7) {
            const double pixels = tenth / 10.0;
            const double back = units::fromUnit(units::toUnit(pixels, unit), unit);
            QVERIFY2(qAbs(back - pixels) < 1e-9,
                     qPrintable(QStringLiteral("%1 px through %2 gave %3").arg(pixels).arg(units::key(unit)).arg(back)));
        }
    }
    for (const MeasureUnit unit : kAllMeasureUnits)
        QCOMPARE(units::fromKey(units::key(unit)), std::optional<MeasureUnit>(unit));
    QVERIFY(!units::fromKey(QStringLiteral("furlongs")));
}

void TestExtrasActions::unitIsAPreferenceNotPartOfTheFile()
{
    DocumentSession session(simpleDocument());
    const QByteArray before = savedBytes(session);
    QCOMPARE(MeasureUnits::instance().unit(), MeasureUnit::Points);
    QSignalSpy spy(&MeasureUnits::instance(), &MeasureUnits::unitChanged);
    MeasureUnits::instance().setUnit(MeasureUnit::Millimeters);
    QCOMPARE(spy.count(), 1);
    MeasureUnits::instance().setUnit(MeasureUnit::Millimeters); // no change, no signal
    QCOMPARE(spy.count(), 1);
    QCOMPARE(AppSettings().measureUnit(), MeasureUnit::Millimeters);
    QCOMPARE(savedBytes(session), before);
    QVERIFY(!session.canUndo());
}

void TestExtrasActions::arrangeFieldsShowAndWriteInTheUnit()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    session.selection().set({"a"}); // 40, 40, 120 x 60
    auto* x = control<LengthSpinBox>(panel, "format.x");
    auto* width = control<LengthSpinBox>(panel, "format.width");
    QVERIFY(x != nullptr && width != nullptr);
    QCOMPARE(x->value(), 40.0);
    QCOMPARE(x->suffix(), QStringLiteral(" pt"));

    MeasureUnits::instance().setUnit(MeasureUnit::Millimeters);
    QCOMPARE(x->suffix(), QStringLiteral(" mm"));
    QCOMPARE(x->decimals(), 3);
    QCOMPARE(x->value(), 10.16);
    QCOMPARE(width->value(), 30.48);
    QVERIFY(!session.canUndo()); // converting the display writes nothing

    // 20 mm is 78.74 px, written as 78.7.
    x->setValue(20);
    QCOMPARE(session.currentPage()->findCell("a")->geometry()->x(), 78.7);
    QCOMPARE(session.currentPage()->findCell("a")->geometry()->width(), 120.0); // untouched
    session.endMerge();
    QVERIFY(session.undo());
    QCOMPARE(session.currentPage()->findCell("a")->geometry()->x(), 40.0);

    MeasureUnits::instance().setUnit(MeasureUnit::Inches);
    QCOMPARE(width->value(), 1.2);
    width->setValue(2);
    QCOMPARE(session.currentPage()->findCell("a")->geometry()->width(), 200.0);

    // Several cells that differ show blank in any unit.
    session.selection().set({"a", "b"});
    QCOMPARE(x->value(), x->minimum());
    QCOMPARE(x->text().trimmed(), QString());
}

void TestExtrasActions::diagramFieldsFollowTheUnit()
{
    DocumentSession session(simpleDocument());
    FormatPanel panel(session);
    auto* units = control<QComboBox>(panel, "format.units");
    auto* grid = control<LengthSpinBox>(panel, "format.gridSize");
    auto* pageWidth = control<LengthSpinBox>(panel, "format.pageWidth");
    QVERIFY(units != nullptr && grid != nullptr && pageWidth != nullptr);
    QCOMPARE(units->count(), 5);
    QCOMPARE(units->currentText(), QStringLiteral("Points"));
    QCOMPARE(grid->value(), 10.0);
    QCOMPARE(pageWidth->value(), 827.0);

    const int millimeters = units->findData(static_cast<int>(MeasureUnit::Millimeters));
    units->setCurrentIndex(millimeters);
    emit units->activated(millimeters);
    QCOMPARE(MeasureUnits::instance().unit(), MeasureUnit::Millimeters);
    QCOMPARE(pageWidth->value(), 210.058);
    QCOMPARE(grid->value(), 2.54);
    QVERIFY(!session.canUndo());

    // A grid of 5 mm is exactly 19.685 px: the grid does not take the 0.1 px rounding.
    grid->setValue(5);
    QCOMPARE(session.currentPage()->gridSize(), 19.685);
    session.endMerge();

    session.undo();
    QCOMPARE(session.currentPage()->gridSize(), 10.0);

    // A4 is a preset: the size fields only show it. A custom size is typed in the unit and
    // written in pixels, rounded to 0.1 px; the height is untouched.
    QVERIFY(!pageWidth->isEnabled());
    QVERIFY(session.edit(QStringLiteral("Page Size"), [](commands::Transaction& tx) {
                    commands::ops::PageProperties props;
                    props.pageWidth = 500;
                    props.pageHeight = 300;
                    return commands::ops::setPageProperties(tx, 0, props);
                }).ok);
    QVERIFY(pageWidth->isEnabled());
    QCOMPARE(pageWidth->value(), 127.0);
    pageWidth->setValue(100);
    QCOMPARE(session.currentPage()->pageWidth(), 393.7);
    QCOMPARE(session.currentPage()->pageHeight(), 300.0);
    session.endMerge();

    // Changing the unit elsewhere (View > Units) updates the box.
    MeasureUnits::instance().setUnit(MeasureUnit::Centimeters);
    QCOMPARE(units->currentData().toInt(), static_cast<int>(MeasureUnit::Centimeters));
    QCOMPARE(pageWidth->value(), 10.0); // the 393.7 px typed above
}

void TestExtrasActions::editGeometryDialogUsesTheUnit()
{
    MeasureUnits::instance().setUnit(MeasureUnit::Inches);
    EditGeometryDialog dialog({core::Rect{40, 40, 120, 60}, core::Rect{40, 90, 120, 60}});
    auto* left = control<LengthSpinBox>(dialog, "geometry.left");
    auto* top = control<LengthSpinBox>(dialog, "geometry.top");
    auto* width = control<LengthSpinBox>(dialog, "geometry.width");
    QCOMPARE(left->value(), 0.4);
    QCOMPARE(top->value(), top->minimum()); // the vertices differ: blank
    QCOMPARE(width->value(), 1.2);
    QVERIFY(!dialog.edit().x && !dialog.edit().width); // nothing changed, nothing written

    width->setValue(2.5);
    left->setValue(1);
    const actions::GeometryEdit edit = dialog.edit();
    QCOMPARE(*edit.width, 250.0);
    QCOMPARE(*edit.x, 100.0);
    QVERIFY(!edit.y && !edit.height);
}

void TestExtrasActions::viewMenuUnitsFollowThePreference()
{
    MainWindow window;
    QMenu* units = window.findChild<QMenu*>(QStringLiteral("menu.units"));
    QVERIFY(units != nullptr);
    QCOMPARE(units->actions().size(), 5);
    QVERIFY(window.actions()[id::kUnitPoints]->isChecked());

    window.actions()[id::kUnitMillimeters]->trigger();
    QCOMPARE(MeasureUnits::instance().unit(), MeasureUnit::Millimeters);
    QVERIFY(window.actions()[id::kUnitMillimeters]->isChecked());
    QVERIFY(!window.actions()[id::kUnitPoints]->isChecked());

    // Another way to change it (the Format panel's box) moves the check mark.
    MeasureUnits::instance().setUnit(MeasureUnit::Meters);
    QVERIFY(window.actions()[id::kUnitMeters]->isChecked());
    QVERIFY(!window.actions()[id::kUnitMillimeters]->isChecked());
}

// --- copy as image ---------------------------------------------------------------

void TestExtrasActions::copyAsImageOfTheSelectionHasTheBoundsAtThePixelRatio()
{
    DocumentSession session(simpleDocument());
    QClipboard* clipboard = QGuiApplication::clipboard();
    session.selection().set({"a"}); // 120 x 60
    // The size of the shape plus its outline (the export renderer's bounds, as File > Export).
    QVERIFY(actions::copyAsImage(session, *clipboard, 1));
    QImage image = clipboard->image();
    QVERIFY2(image.width() >= 120 && image.width() <= 122 && image.height() >= 60 && image.height() <= 62,
             qPrintable(QStringLiteral("%1x%2").arg(image.width()).arg(image.height())));
    const QSize oneTimes = image.size();

    QVERIFY(actions::copyAsImage(session, *clipboard, 2));
    image = clipboard->image();
    QVERIFY(qAbs(image.width() - 2 * oneTimes.width()) <= 2 && qAbs(image.height() - 2 * oneTimes.height()) <= 2);
    QVERIFY(clipboard->mimeData()->hasFormat(QStringLiteral("image/png")));
    QVERIFY(!clipboard->mimeData()->data(QStringLiteral("image/png")).isEmpty());
    QCOMPARE(image.devicePixelRatio(), 2.0); // pasted elsewhere it is the size of the shape, not twice

    // A fractional ratio (125 % scaling): 1.25 pixels to each logical one, and the picture says so.
    QVERIFY(actions::copyAsImage(session, *clipboard, 1.25));
    image = clipboard->image();
    QVERIFY(qAbs(image.width() - 1.25 * oneTimes.width()) <= 2 && qAbs(image.height() - 1.25 * oneTimes.height()) <= 2);
    QCOMPARE(image.devicePixelRatio(), 1.25);
    // Copying an image does not touch the document.
    QVERIFY(!session.canUndo());
}

void TestExtrasActions::copyAsImageOfThePageWhenNothingIsSelected()
{
    DocumentSession session(simpleDocument());
    QClipboard* clipboard = QGuiApplication::clipboard();
    QVERIFY(session.selection().isEmpty());
    QVERIFY(actions::copyAsImage(session, *clipboard, 1));
    const QImage page = clipboard->image();
    // The content spans from (40, 40) to the right edge of "b" (400) and the bottom of the group (300).
    QVERIFY(page.width() >= 360 && page.width() < 420);
    QVERIFY(page.height() >= 260 && page.height() < 320);
}

void TestExtrasActions::copyAsImageIsTransparentUnlessThePageHasABackground()
{
    DocumentSession session(simpleDocument());
    QClipboard* clipboard = QGuiApplication::clipboard();
    session.selection().set({"b"}); // an ellipse: its corner is outside the shape
    QVERIFY(actions::copyAsImage(session, *clipboard, 1));
    QImage image = clipboard->image();
    QVERIFY(image.hasAlphaChannel());
    QCOMPARE(qAlpha(image.pixel(1, 1)), 0);
    QVERIFY(qAlpha(image.pixel(image.width() / 2, image.height() / 2)) > 0);

    QVERIFY(session.edit(QStringLiteral("Background"), [](commands::Transaction& tx) {
                    commands::ops::PageProperties props;
                    props.background = "#ff0000";
                    return commands::ops::setPageProperties(tx, 0, props);
                }).ok);
    QVERIFY(actions::copyAsImage(session, *clipboard, 1));
    image = clipboard->image();
    QCOMPARE(qAlpha(image.pixel(1, 1)), 255);
    QCOMPARE(QColor(image.pixel(1, 1)), QColor(255, 0, 0));
}

void TestExtrasActions::copyAsImageCarriesTheDiagramInThePng()
{
    DocumentSession session(simpleDocument());
    QClipboard* clipboard = QGuiApplication::clipboard();
    session.selection().set({"a"});
    QVERIFY(actions::copyAsImage(session, *clipboard, 1));
    const QByteArray png = clipboard->mimeData()->data(QStringLiteral("image/png"));
    const std::optional<std::string> selected = format::extractFromPng(png);
    QVERIFY(selected);
    QVERIFY(selected->find("id=\"a\"") != std::string::npos);
    QVERIFY(selected->find("id=\"b\"") == std::string::npos); // only what was copied

    session.selection().clear();
    QVERIFY(actions::copyAsImage(session, *clipboard, 1));
    const std::optional<std::string> whole =
        format::extractFromPng(clipboard->mimeData()->data(QStringLiteral("image/png")));
    QVERIFY(whole);
    QVERIFY(whole->find("id=\"b\"") != std::string::npos);
    const format::ModelTextResult parsed = format::parsePageModelText(QByteArrayView(whole->data(), qsizetype(whole->size())));
    QVERIFY(parsed.page);
    QVERIFY(parsed.page->findCell("g") != nullptr);
}

void TestExtrasActions::copyAsImageActionFollowsTheDocument()
{
    MainWindow window;
    QAction* copyAsImage = window.actions()[id::kCopyAsImage];
    QCOMPARE(copyAsImage->shortcut(), QKeySequence(Qt::CTRL | Qt::ALT | Qt::Key_X));
    QVERIFY(!copyAsImage->isEnabled()); // a blank page has nothing to draw
    window.session().resetDocument(simpleDocument());
    QVERIFY(copyAsImage->isEnabled());
    // draw.io lists it with Copy in the Edit menu and the context menu.
    QMenu* edit = window.findChild<QMenu*>(QStringLiteral("menu.edit"));
    QVERIFY(edit != nullptr && edit->actions().contains(copyAsImage));
}

SHELL_TEST_MAIN(TestExtrasActions)
#include "tst_extras_actions.moc"
