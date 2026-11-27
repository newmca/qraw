// Text-field shortcut guard: while a text input of the window has the focus,
// document shortcuts (Ctrl+D duplicate) do not fire; the canvas keeps them.
#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/ui/format_panel.h"
#include "qraw/ui/main_window.h"

#include <QSpinBox>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;

class TestShellTextGuard : public QObject {
    Q_OBJECT

private slots:
    void textInputKeepsDocumentShortcutsOut();
    void canvasKeepsDocumentShortcuts();
};

void TestShellTextGuard::textInputKeepsDocumentShortcutsOut()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    window.session().resetDocument(test::simpleDocument());
    window.session().selection().set({"a"});
    const QByteArray before = test::savedBytes(window.session());

    // A standard spin box in the Format panel.
    QSpinBox* spin = window.formatPanel()->findChild<QSpinBox*>(QStringLiteral("format.strokeWidth"));
    QVERIFY(spin != nullptr);
    spin->setFocus();
    QTRY_VERIFY(QApplication::focusWidget() == spin || spin->isAncestorOf(QApplication::focusWidget()));
    QTest::keyClick(QApplication::focusWidget(), Qt::Key_D, Qt::ControlModifier);
    QCOMPARE(test::savedBytes(window.session()), before);
    QVERIFY(!window.session().canUndo());

    // A custom widget that opted in with the qrawTextInput property.
    QWidget custom;
    custom.setProperty("qrawTextInput", true);
    custom.setParent(&window);
    custom.show();
    custom.setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), &custom);
    QTest::keyClick(&custom, Qt::Key_D, Qt::ControlModifier);
    QCOMPARE(test::savedBytes(window.session()), before);
    QVERIFY(!window.session().canUndo());
}

void TestShellTextGuard::canvasKeepsDocumentShortcuts()
{
    MainWindow window;
    window.show();
    QVERIFY(QTest::qWaitForWindowActive(&window));
    window.session().resetDocument(test::simpleDocument());
    window.session().selection().set({"a"});
    const QByteArray before = test::savedBytes(window.session());

    window.canvas().setFocus();
    QTRY_COMPARE(QApplication::focusWidget(), static_cast<QWidget*>(&window.canvas()));
    QTest::keyClick(&window.canvas(), Qt::Key_D, Qt::ControlModifier);
    QVERIFY(window.session().canUndo());
    QVERIFY(test::savedBytes(window.session()) != before);
    window.session().undo();
    QCOMPARE(test::savedBytes(window.session()), before);
}

SHELL_TEST_MAIN(TestShellTextGuard)
#include "tst_shell_textguard.moc"
