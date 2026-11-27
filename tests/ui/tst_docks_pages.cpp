// Tests of PageTabs: the tabs mirror the pages through every page edit, a
// tab click switches the page, and undo restores the document.

#include "support/test_support.h"

#include "qraw/ui/page_tabs.h"

#include <QTabBar>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

class TestDocksPages : public QObject {
    Q_OBJECT

private slots:
    void mirrorsPagesAfterEdits();
    void insertsAfterTheCurrentPage();
    void undoRestoresTheDocument();
    void clickingATabSwitchesThePage();
    void lastPageCannotBeDeleted();
};

void TestDocksPages::mirrorsPagesAfterEdits()
{
    DocumentSession session(simpleDocument());
    PageTabs tabs(session);
    QCOMPARE(tabs.tabBar()->count(), 1);
    QCOMPARE(tabs.tabBar()->tabText(0), QStringLiteral("Page-1"));

    QVERIFY(tabs.insertPage());
    QCOMPARE(tabs.tabBar()->count(), 2);
    QCOMPARE(tabs.tabBar()->tabText(1), QStringLiteral("Page-2"));
    QCOMPARE(tabs.tabBar()->currentIndex(), 1);

    QVERIFY(tabs.renamePage(0, QStringLiteral("Intro")));
    QCOMPARE(tabs.tabBar()->tabText(0), QStringLiteral("Intro"));

    QVERIFY(tabs.duplicateCurrentPage());
    QCOMPARE(tabs.tabBar()->count(), 3);
    QCOMPARE(tabs.tabBar()->tabText(2), QStringLiteral("Copy of Page-2"));

    QVERIFY(tabs.movePage(0, 2));
    QCOMPARE(tabs.tabBar()->tabText(2), QStringLiteral("Intro"));
    QCOMPARE(tabs.tabBar()->tabText(0), QStringLiteral("Page-2"));

    QVERIFY(tabs.removeCurrentPage());
    QCOMPARE(tabs.tabBar()->count(), 2);
    QCOMPARE(tabs.tabBar()->currentIndex(), int(session.currentPageIndex()));
}

void TestDocksPages::insertsAfterTheCurrentPage()
{
    DocumentSession session(simpleDocument());
    PageTabs tabs(session);
    QVERIFY(tabs.insertPage());
    QVERIFY(tabs.insertPage());
    session.setCurrentPage(0);
    QVERIFY(tabs.insertPage());
    QCOMPARE(int(session.currentPageIndex()), 1);
    QCOMPARE(int(session.document().pageCount()), 4);
}

void TestDocksPages::undoRestoresTheDocument()
{
    DocumentSession session(simpleDocument());
    PageTabs tabs(session);
    const QByteArray before = savedBytes(session);

    QVERIFY(tabs.insertPage());
    QVERIFY(tabs.renamePage(0, QStringLiteral("Intro")));
    QCOMPARE(tabs.tabBar()->count(), 2);

    QVERIFY(session.undo());
    QVERIFY(session.undo());
    QCOMPARE(savedBytes(session), before);
    QCOMPARE(tabs.tabBar()->count(), 1);
    QCOMPARE(tabs.tabBar()->tabText(0), QStringLiteral("Page-1"));
}

void TestDocksPages::clickingATabSwitchesThePage()
{
    DocumentSession session(simpleDocument());
    PageTabs tabs(session);
    QVERIFY(tabs.insertPage());
    tabs.resize(400, 40);
    tabs.show();
    QVERIFY(QTest::qWaitForWindowExposed(&tabs));

    session.setCurrentPage(0);
    QCOMPARE(tabs.tabBar()->currentIndex(), 0);
    QTest::mouseClick(tabs.tabBar(), Qt::LeftButton, {}, tabs.tabBar()->tabRect(1).center());
    QCOMPARE(int(session.currentPageIndex()), 1);
    QCOMPARE(tabs.tabBar()->currentIndex(), 1);
    grabToPng(tabs, QStringLiteral("docks-pages"));
}

void TestDocksPages::lastPageCannotBeDeleted()
{
    DocumentSession session(simpleDocument());
    PageTabs tabs(session);
    QVERIFY(!tabs.removeCurrentPage());
    QCOMPARE(tabs.tabBar()->count(), 1);
}

QTEST_MAIN(TestDocksPages)
#include "tst_docks_pages.moc"
