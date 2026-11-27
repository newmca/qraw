// Tests of FindEngine (matching, HTML-aware replacement) and FindPanel
// (navigation and Replace All as one undo step).

#include "support/test_support.h"

#include "qraw/ui/find_panel.h"

#include <QFile>
#include <QSignalSpy>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

// Two pages: "Intro" with an HTML label "Alpha <b>beta</b>", a wrapped cell
// o1 (owner="alice") and "Second" with a plain "Alpha" cell.
core::Document findFixture()
{
    QFile file(QFINDTESTDATA("data/m4-docks/find-pages.xml"));
    if (!file.open(QIODevice::ReadOnly))
        throw std::runtime_error("fixture missing");
    const QByteArray bytes = file.readAll();
    return documentFromXml(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
}

std::string idOf(const FindMatch& match)
{
    return match.cellId + "/" + match.attribute;
}

} // namespace

class TestDocksFind : public QObject {
    Q_OBJECT

private slots:
    void matchesLabelsAndCustomAttributes();
    void allPagesWidensTheSearch();
    void htmlLabelsAreMatchedAsText();
    void replacementKeepsMarkup();
    void findNextSelectsAndSwitchesPage();
    void replaceAllIsOneUndoStep();
    void replaceAllUpdatesCustomAttributes();
    void showsPanel();
    void findsByShapeType();
    void replaceAllSkipsShapeMatches();
    void selectAllSelectsCurrentPageMatches();
};

void TestDocksFind::matchesLabelsAndCustomAttributes()
{
    const core::Document document = simpleDocument();
    FindOptions options;
    const auto labels = FindEngine::findAll(document, 0, QStringLiteral("c"), options);
    QCOMPARE(int(labels.size()), 2); // "C1" and "C2", case-insensitive
    QCOMPARE(QString::fromStdString(idOf(labels[0])), QStringLiteral("c1/label"));

    options.matchCase = true;
    QVERIFY(FindEngine::findAll(document, 0, QStringLiteral("c"), options).empty());
    options.matchCase = false;

    options.wholeWord = true;
    QVERIFY(FindEngine::findAll(document, 0, QStringLiteral("C"), options).empty());
    options.wholeWord = false;

    options.regex = true;
    QCOMPARE(int(FindEngine::findAll(document, 0, QStringLiteral("C\\d"), options).size()), 2);
    options.regex = false;

    const core::Document fixture = findFixture();
    const auto owners = FindEngine::findAll(fixture, 0, QStringLiteral("alice"), options);
    QCOMPARE(int(owners.size()), 1);
    QCOMPARE(QString::fromStdString(idOf(owners[0])), QStringLiteral("o1/owner"));

    options.includeData = false;
    QVERIFY(FindEngine::findAll(fixture, 0, QStringLiteral("alice"), options).empty());
}

void TestDocksFind::allPagesWidensTheSearch()
{
    const core::Document fixture = findFixture();
    FindOptions options;
    QCOMPARE(int(FindEngine::findAll(fixture, 0, QStringLiteral("Alpha"), options).size()), 1);

    options.allPages = true;
    const auto matches = FindEngine::findAll(fixture, 0, QStringLiteral("Alpha"), options);
    QCOMPARE(int(matches.size()), 2);
    QCOMPARE(int(matches[0].page), 0);
    QCOMPARE(int(matches[1].page), 1);
}

void TestDocksFind::htmlLabelsAreMatchedAsText()
{
    const core::Document fixture = findFixture();
    FindOptions options;
    QCOMPARE(int(FindEngine::findAll(fixture, 0, QStringLiteral("beta"), options).size()), 1);
    QVERIFY(FindEngine::findAll(fixture, 0, QStringLiteral("<b>"), options).empty());
}

void TestDocksFind::replacementKeepsMarkup()
{
    const FindOptions options;
    QCOMPARE(QString::fromStdString(FindEngine::replaceIn("<span class=\"x\">Alpha</span> Alpha", true,
                                                          QStringLiteral("Alpha"), QStringLiteral("Omega"), options)),
             QStringLiteral("<span class=\"x\">Omega</span> Omega"));
    // A match inside an attribute value is markup, not text.
    QCOMPARE(QString::fromStdString(FindEngine::replaceIn("<span class=\"Alpha\">x</span>", true,
                                                          QStringLiteral("Alpha"), QStringLiteral("Omega"), options)),
             QStringLiteral("<span class=\"Alpha\">x</span>"));
    QCOMPARE(QString::fromStdString(FindEngine::replaceIn("Alpha & Alpha", false, QStringLiteral("Alpha"),
                                                          QStringLiteral("B"), options)),
             QStringLiteral("B & B"));
}

void TestDocksFind::findNextSelectsAndSwitchesPage()
{
    DocumentSession session(findFixture());
    FindPanel panel(session);
    QSignalSpy reveals(&panel, &FindPanel::revealCell);
    FindOptions options;
    options.allPages = true;
    panel.setOptions(options);
    panel.setSearchText(QStringLiteral("Alpha"));

    QVERIFY(panel.findNext());
    QCOMPARE(QString::fromStdString(session.selection().first()), QStringLiteral("c1"));
    QVERIFY(panel.findNext());
    QCOMPARE(int(session.currentPageIndex()), 1);
    QCOMPARE(QString::fromStdString(session.selection().first()), QStringLiteral("p"));
    QVERIFY(panel.findNext()); // wraps around to the first page
    QCOMPARE(int(session.currentPageIndex()), 0);
    QCOMPARE(reveals.count(), 3);
}

void TestDocksFind::replaceAllIsOneUndoStep()
{
    DocumentSession session(findFixture());
    FindPanel panel(session);
    FindOptions options;
    options.allPages = true;
    panel.setOptions(options);
    panel.setSearchText(QStringLiteral("Alpha"));
    panel.setReplaceText(QStringLiteral("Omega"));

    const QByteArray before = savedBytes(session);
    QCOMPARE(panel.replaceAll(), 2);
    QCOMPARE(QString::fromStdString(session.document().page(0)->findCell("c1")->value()),
             QStringLiteral("Omega <b>beta</b>"));
    QCOMPARE(QString::fromStdString(session.document().page(1)->findCell("p")->value()), QStringLiteral("Omega"));

    QVERIFY(session.undo());
    QCOMPARE(savedBytes(session), before);
    QVERIFY(!session.canUndo());
}

void TestDocksFind::replaceAllUpdatesCustomAttributes()
{
    DocumentSession session(findFixture());
    FindPanel panel(session);
    panel.setSearchText(QStringLiteral("alice"));
    panel.setReplaceText(QStringLiteral("bob"));

    QCOMPARE(panel.replaceAll(), 1);
    const core::Cell* wrapped = session.document().page(0)->findCell("o1");
    QVERIFY(wrapped != nullptr && wrapped->wrapper() != nullptr);
    QCOMPARE(QString::fromStdString(wrapped->wrapper()->attributes.value("owner")), QStringLiteral("bob"));
}

void TestDocksFind::showsPanel()
{
    DocumentSession session(findFixture());
    FindPanel panel(session);
    panel.setSearchText(QStringLiteral("Alpha"));
    panel.resize(320, 240);
    panel.show();
    QVERIFY(panel.findNext());
    grabToPng(panel, QStringLiteral("docks-find"));
}

void TestDocksFind::findsByShapeType()
{
    const core::Document document = simpleDocument();
    FindOptions options;
    QVERIFY(FindEngine::findAll(document, 0, QStringLiteral("ellipse"), options).empty()); // off by default

    options.shapes = true;
    const auto ellipses = FindEngine::findAll(document, 0, QStringLiteral("ellipse"), options);
    QCOMPARE(ellipses.size(), std::size_t(1));
    QCOMPARE(ellipses[0].cellId, std::string("b"));
    QCOMPARE(ellipses[0].attribute, std::string(kShapeMatchAttribute));

    // a and c1 have no shape token (rectangle by default), c2 has an empty style;
    // the group is a bare "group" and the edge a "connector".
    std::vector<std::string> rectangles;
    for (const FindMatch& match : FindEngine::findAll(document, 0, QStringLiteral("rectangle"), options))
        rectangles.push_back(match.cellId);
    QCOMPARE(rectangles, (std::vector<std::string>{"a", "c1", "c2"}));
}

void TestDocksFind::replaceAllSkipsShapeMatches()
{
    DocumentSession session(simpleDocument());
    FindPanel panel(session);
    const QByteArray before = savedBytes(session);
    FindOptions options;
    options.shapes = true;
    panel.setOptions(options);
    panel.setSearchText(QStringLiteral("rectangle"));
    panel.setReplaceText(QStringLiteral("box"));

    QCOMPARE(panel.replaceAll(), 0); // only shape matches: styles are never edited
    QCOMPARE(savedBytes(session), before);
    QVERIFY(!session.canUndo());
}

void TestDocksFind::selectAllSelectsCurrentPageMatches()
{
    DocumentSession session(simpleDocument());
    FindPanel panel(session);
    FindOptions options;
    options.shapes = true;
    panel.setOptions(options);
    panel.setSearchText(QStringLiteral("rectangle"));

    QCOMPARE(panel.selectAll(), 3);
    QVERIFY(session.selection().contains("a"));
    QVERIFY(session.selection().contains("c1"));
    QVERIFY(session.selection().contains("c2"));
    QVERIFY(!session.selection().contains("g"));
}

QTEST_MAIN(TestDocksFind)
#include "tst_docks_find.moc"
