// Accessibility: every control of the main window and of every dialog the screenshot harness
// can open has an accessible name; the canvas has an accessible interface that says what the diagram
// holds; icon-only buttons show their tooltip when the keyboard focus reaches them.

#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/ui/accessibility.h"
#include "qraw/ui/app_setup.h"
#include "qraw/ui/cells_panel.h"
#include "qraw/ui/main_window.h"
#include "qraw/ui/page_tabs.h"

#include <QAbstractButton>
#include <QAbstractItemView>
#include <QAbstractSlider>
#include <QAbstractSpinBox>
#include <QAccessible>
#include <QComboBox>
#include <QDialog>
#include <QHeaderView>
#include <QLineEdit>
#include <QTabBar>
#include <QTabWidget>
#include <QTest>
#include <QToolBar>
#include <QToolButton>
#include <QToolTip>
#include <QtTest/QTestAccessibility>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

QString nameOf(QObject* object)
{
    const QAccessibleInterface* interface = QAccessible::queryAccessibleInterface(object);
    return interface != nullptr ? interface->text(QAccessible::Name).trimmed() : QString();
}

// "MainWindow/dock.format/FormatPanel/QToolButton 'objectName'" for a failure message.
QString describe(const QWidget* widget)
{
    QStringList path;
    for (const QWidget* w = widget; w != nullptr; w = w->parentWidget())
        path.prepend(w->objectName().isEmpty() ? QString::fromLatin1(w->metaObject()->className()) : w->objectName());
    return path.join(QLatin1Char('/'));
}

// The lines that edit inside a spin box or an editable combo box and the header of a table are part of that
// control: assistive technology sees one control, named once.
bool isPartOfAnotherControl(const QWidget* widget)
{
    const QWidget* parent = widget->parentWidget();
    if (parent == nullptr)
        return false;
    return qobject_cast<const QAbstractSpinBox*>(parent) != nullptr || qobject_cast<const QComboBox*>(parent) != nullptr
        || (qobject_cast<const QHeaderView*>(widget) != nullptr && qobject_cast<const QAbstractItemView*>(parent) != nullptr);
}

bool isInteractive(const QWidget* widget)
{
    return qobject_cast<const QAbstractButton*>(widget) != nullptr || qobject_cast<const QComboBox*>(widget) != nullptr
        || qobject_cast<const QLineEdit*>(widget) != nullptr || qobject_cast<const QAbstractSpinBox*>(widget) != nullptr
        || qobject_cast<const QAbstractSlider*>(widget) != nullptr
        || qobject_cast<const QAbstractItemView*>(widget) != nullptr || qobject_cast<const CanvasView*>(widget) != nullptr;
}

// The names missing under `root`: the visible interactive widgets without a name and the tabs without a text.
QStringList unnamedControls(QWidget& root)
{
    QStringList missing;
    for (const QWidget* widget : root.findChildren<QWidget*>()) {
        if (!widget->isVisible())
            continue;
        if (const auto* tabs = qobject_cast<const QTabBar*>(widget)) {
            for (int i = 0; i < tabs->count(); ++i) {
                if (tabs->tabText(i).trimmed().isEmpty() && tabs->accessibleTabName(i).trimmed().isEmpty())
                    missing << QStringLiteral("%1 (tab %2)").arg(describe(widget)).arg(i);
            }
            continue;
        }
        if (!isInteractive(widget) || isPartOfAnotherControl(widget))
            continue;
        if (nameOf(const_cast<QWidget*>(widget)).isEmpty())
            missing << describe(widget);
    }
    return missing;
}

// QTestAccessibility keeps a copy of every event it is given and never frees them (LeakSanitizer says so),
// and its handler stays installed until cleanup(): the events are deleted and the handler removed here.
struct AccessibilityRecording {
    AccessibilityRecording()
    {
        QTestAccessibility::initialize();
        QAccessible::setActive(true);
    }
    ~AccessibilityRecording()
    {
        QAccessible::setActive(false);
        QTestAccessibility::cleanup(); // uninstalls the handler: it would keep copying events of later tests
        discard();
    }
    static void discard()
    {
        qDeleteAll(QTestAccessibility::events());
        QTestAccessibility::clearEvents();
    }
};

void showAllDocks(MainWindow& window)
{
    for (const auto panel : {MainWindow::Panel::Palette, MainWindow::Panel::Format, MainWindow::Panel::Layers,
                             MainWindow::Panel::Outline, MainWindow::Panel::Find, MainWindow::Panel::Cells})
        window.showPanel(panel, true);
}

} // namespace

class TestAccessibility : public QObject {
    Q_OBJECT

private slots:
    void everyControlOfTheMainWindowHasAnAccessibleName();
    void everyControlOfEveryDialogHasAnAccessibleName();
    void everyTabOfTheFormatPanelIsNamed();
    void theCanvasIsADiagramThatDescribesItself();
    void theCanvasTellsAssistiveTechnologyWhenItChanges();
    void iconOnlyButtonsShowTheirTooltipOnKeyboardFocus();
};

void TestAccessibility::everyControlOfTheMainWindowHasAnAccessibleName()
{
    MainWindow window;
    window.session().resetDocument(simpleDocument());
    window.resize(1280, 800);
    showAllDocks(window);
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    // Every Format panel tab, so that every control is built and shown once.
    window.session().selection().set({"a"});
    QCoreApplication::processEvents();

    QWidget* central = &window;
    const QStringList missing = unnamedControls(*central);
    QVERIFY2(missing.isEmpty(), qPrintable(QStringLiteral("no accessible name:\n  ") + missing.join(QStringLiteral("\n  "))));
}

void TestAccessibility::everyControlOfEveryDialogHasAnAccessibleName()
{
    MainWindow window;
    window.session().resetDocument(simpleDocument());
    window.session().selection().set({"a"});
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    AppSettings settings;
    QStringList missing;
    for (const QString& name : screenshotDialogNames()) {
        const std::unique_ptr<QDialog> dialog = createScreenshotDialog(window, settings, name);
        QVERIFY2(dialog != nullptr, qPrintable(name));
        dialog->show();
        QVERIFY(QTest::qWaitForWindowExposed(dialog.get()));
        QCoreApplication::processEvents();
        for (const QString& control : unnamedControls(*dialog))
            missing << QStringLiteral("[%1] %2").arg(name, control);
    }
    QVERIFY2(missing.isEmpty(), qPrintable(QStringLiteral("no accessible name:\n  ") + missing.join(QStringLiteral("\n  "))));
}

void TestAccessibility::everyTabOfTheFormatPanelIsNamed()
{
    MainWindow window;
    window.session().resetDocument(simpleDocument());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.session().selection().set({"a"});
    const auto tabWidgets = window.findChildren<QTabWidget*>();
    QVERIFY(!tabWidgets.isEmpty());
    for (QTabWidget* tabs : tabWidgets) {
        for (int i = 0; i < tabs->count(); ++i)
            QVERIFY2(!tabs->tabText(i).isEmpty(), qPrintable(describe(tabs)));
    }
}

void TestAccessibility::theCanvasIsADiagramThatDescribesItself()
{
    MainWindow window;
    window.session().resetDocument(simpleDocument());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));

    QAccessibleInterface* canvas = QAccessible::queryAccessibleInterface(&window.canvas());
    QVERIFY(canvas != nullptr);
    QCOMPARE(canvas->role(), QAccessible::Canvas);
    QCOMPARE(canvas->text(QAccessible::Name), QStringLiteral("Diagram"));
    // Six shapes (a, b, g, c1, c2 and the connector e), nothing selected.
    QCOMPARE(canvas->text(QAccessible::Description), QStringLiteral("Page-1, 6 shapes, 0 selected"));
    window.session().selection().set({"a", "b"});
    QCOMPARE(canvas->text(QAccessible::Description), QStringLiteral("Page-1, 6 shapes, 2 selected"));
    QVERIFY(canvas->state().focusable);
}

void TestAccessibility::theCanvasTellsAssistiveTechnologyWhenItChanges()
{
    AccessibilityRecording recording; // declared first: it ends after the window's own events
    MainWindow window;
    window.session().resetDocument(simpleDocument());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    QAccessible::queryAccessibleInterface(&window.canvas()); // makes the object known

    AccessibilityRecording::discard();
    window.session().selection().set({"a"});
    bool seen = false;
    for (const QAccessibleEvent* event : QTestAccessibility::events())
        seen = seen || (event->object() == &window.canvas() && event->type() == QAccessible::DescriptionChanged);
    QVERIFY2(seen, "no DescriptionChanged event for a selection change");
}

void TestAccessibility::iconOnlyButtonsShowTheirTooltipOnKeyboardFocus()
{
    MainWindow window;
    window.session().resetDocument(simpleDocument());
    window.show();
    QVERIFY(QTest::qWaitForWindowExposed(&window));
    window.activateWindow();
    QVERIFY(QTest::qWaitForWindowActive(&window));

    QToolButton* button = nullptr;
    for (QToolButton* candidate : window.findChildren<QToolButton*>()) {
        if (candidate->isVisible() && candidate->toolButtonStyle() == Qt::ToolButtonIconOnly && !candidate->toolTip().isEmpty()
            && candidate->focusPolicy() != Qt::NoFocus) {
            button = candidate;
            break;
        }
    }
    QVERIFY2(button != nullptr, "no icon-only tool button that can take the focus");
    QToolTip::hideText();
    QVERIFY(!QToolTip::isVisible());

    // A click focuses the button too, but only the keyboard shows the tip.
    button->setFocus(Qt::MouseFocusReason);
    QTest::qWait(20);
    QVERIFY(!QToolTip::isVisible());
    window.canvas().setFocus();
    button->setFocus(Qt::TabFocusReason);
    QTRY_VERIFY(QToolTip::isVisible());
    QCOMPARE(QToolTip::text(), button->toolTip());
    window.canvas().setFocus();
    QTRY_VERIFY(!QToolTip::isVisible());
}

SHELL_TEST_MAIN(TestAccessibility)
#include "tst_accessibility.moc"
