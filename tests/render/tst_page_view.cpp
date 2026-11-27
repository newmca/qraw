// PageView: visibility, absolute geometry, labels and lookups.

#include "qraw/format/codec.h"
#include "qraw/render/page_view.h"
#include "qraw/render/shape.h"


#include <QDate>
#include <QDateTime>
#include <QLocale>
#include <QTest>

using namespace qraw;

namespace {

core::Document documentWith(const QByteArray& cells)
{
    const QByteArray xml = "<mxfile><diagram id=\"p\" name=\"p\"><mxGraphModel><root><mxCell id=\"0\"/>"
                           "<mxCell id=\"1\" parent=\"0\"/>" +
                           cells + "</root></mxGraphModel></diagram></mxfile>";
    auto read = format::readDocument(xml);
    if (!read.ok())
        qFatal("fixture does not parse");
    return std::move(*read.document);
}

QByteArray vertex(const char* id, const char* parent, double x, double y, double w, double h, const char* style = "",
                  const char* value = "", const char* extra = "")
{
    return QByteArray("<mxCell id=\"") + id + "\" value=\"" + value + "\" style=\"" + style + "\" vertex=\"1\" parent=\"" +
           parent + "\" " + extra + "><mxGeometry x=\"" + QByteArray::number(x) + "\" y=\"" + QByteArray::number(y) +
           "\" width=\"" + QByteArray::number(w) + "\" height=\"" + QByteArray::number(h) +
           "\" as=\"geometry\"/></mxCell>";
}

render::PageView build(const core::Document& doc)
{
    return render::PageView::build(*doc.page(0), render::RenderContext::defaults());
}

std::vector<std::string> ids(const render::PageView& view)
{
    std::vector<std::string> out;
    for (const auto& s : view.states())
        out.push_back(s.id);
    return out;
}

} // namespace

class PageViewTest : public QObject {
    Q_OBJECT

private slots:
    void paintOrderAndDepth()
    {
        const auto doc = documentWith(vertex("g", "1", 0, 0, 100, 100) + vertex("c1", "g", 10, 10, 20, 20) +
                                      vertex("gc", "c1", 1, 1, 5, 5) + vertex("c2", "g", 40, 40, 20, 20) +
                                      vertex("top", "1", 200, 0, 10, 10));
        const auto view = build(doc);
        QCOMPARE(ids(view), (std::vector<std::string>{"g", "c1", "gc", "c2", "top"}));
        QCOMPARE(view.find("g")->depth, 0);
        QCOMPARE(view.find("gc")->depth, 2);
        QCOMPARE(view.find("g")->parent, render::CellState::npos);
        QCOMPARE(view.states()[view.find("gc")->parent].id, std::string("c1"));
        QVERIFY(view.find("missing") == nullptr);
    }

    void invisibleAndCollapsedAreSkipped()
    {
        const auto doc = documentWith(
            "<mxCell id=\"L2\" parent=\"0\" visible=\"0\"/>" + vertex("inL2", "L2", 0, 0, 10, 10) +
            vertex("hidden", "1", 0, 0, 10, 10, "", "", "visible=\"0\"") + vertex("kid", "hidden", 0, 0, 5, 5) +
            vertex("box", "1", 0, 0, 50, 50, "swimlane", "", "collapsed=\"1\"") + vertex("inBox", "box", 0, 0, 5, 5) +
            vertex("shown", "1", 60, 0, 10, 10));
        const auto view = build(doc);
        QCOMPARE(ids(view), (std::vector<std::string>{"box", "shown"}));
        QVERIFY(view.find("box")->collapsed);
    }

    void absoluteBounds()
    {
        const auto doc = documentWith(vertex("g", "1", 100, 50, 200, 100) + vertex("c", "g", 10, 20, 30, 40) +
                                      // relative child: parent origin + 0.5 * parent size + offset
                                      "<mxCell id=\"rel\" value=\"\" style=\"\" vertex=\"1\" parent=\"g\"><mxGeometry x=\"0.5\" "
                                      "y=\"0.5\" width=\"10\" height=\"10\" relative=\"1\" as=\"geometry\"><mxPoint x=\"4\" "
                                      "y=\"6\" as=\"offset\"/></mxGeometry></mxCell>");
        const auto view = build(doc);
        const auto child = view.find("c")->bounds;
        QCOMPARE(child.x, 110.0);
        QCOMPARE(child.y, 70.0);
        QCOMPARE(child.width, 30.0);
        const auto rel = view.find("rel")->bounds;
        QCOMPARE(rel.x, 100 + 100 + 4.0);
        QCOMPARE(rel.y, 50 + 50 + 6.0);
    }

    void boundsAndSelection()
    {
        const auto doc = documentWith(vertex("a", "1", 0, 0, 10, 10) + vertex("b", "1", 100, 100, 10, 10));
        const auto view = build(doc);
        const std::vector<std::string> both{"a", "b"};
        const std::vector<std::string> onlyB{"b", "nope"};
        QCOMPARE(view.selection(both).size(), std::size_t(2));
        QCOMPARE(view.selection(onlyB).size(), std::size_t(1));
        // paint bounds add the stroke growth, so test containment
        const auto only = view.boundsOf(onlyB);
        QVERIFY(only.x > 99 && only.x <= 100 && only.x + only.width >= 110);
        const auto all = view.bounds();
        QVERIFY(all.x <= 0 && all.x + all.width >= 110 && all.y <= 0 && all.y + all.height >= 110);
    }

    void danglingTerminalWarns()
    {
        const auto doc = documentWith(
            vertex("a", "1", 0, 0, 40, 40) +
            "<mxCell id=\"e\" edge=\"1\" parent=\"1\" source=\"a\" target=\"gone\"><mxGeometry relative=\"1\" as=\"geometry\">"
            "<mxPoint x=\"200\" y=\"20\" as=\"targetPoint\"/></mxGeometry></mxCell>");
        const auto view = build(doc);
        QVERIFY(view.find("e") != nullptr);
        bool warned = false;
        for (const auto& d : view.diagnostics())
            warned = warned || (d.severity == core::Diagnostic::Severity::Warning && d.cellId == "e");
        QVERIFY(warned);
    }

    // mxCellRenderer.getShapeConstructor: a shape name without a painter is the default
    // rectangle (FIDELITY class 2). A shape draw.io has and we lack keeps the
    // placeholder and a diagnostic.
    void unregisteredShapeNameIsPlainRectangle()
    {
        const auto doc = documentWith(vertex("r", "1", 0, 0, 40, 40, "shape=rect;fillColor=#fff2cc;") +
                                      vertex("s", "1", 50, 0, 40, 40, "shape=star;") +
                                      vertex("x", "1", 100, 0, 40, 40, "shape=xor;") +
                                      vertex("m", "1", 150, 0, 40, 40, "shape=mxgraph.nothing.here;"));
        const auto view = build(doc);
        const auto& builtin = render::ShapeRegistry::builtin();
        QCOMPARE(view.find("r")->shape, builtin.find("rectangle"));
        QCOMPARE(view.find("s")->shape, builtin.find("rectangle"));
        QCOMPARE(view.find("x")->shape, &builtin.fallback());
        QCOMPARE(view.find("m")->shape, &builtin.fallback());
        std::vector<std::string> reported;
        for (const auto& d : view.diagnostics())
            reported.push_back(d.cellId);
        QCOMPARE(reported.size(), std::size_t(2)); // x and m only
    }

    void edgeToInvisibleTerminalIsSkipped()
    {
        const auto doc = documentWith(vertex("a", "1", 0, 0, 40, 40) +
                                      vertex("h", "1", 100, 0, 40, 40, "", "", "visible=\"0\"") +
                                      "<mxCell id=\"e\" edge=\"1\" parent=\"1\" source=\"a\" target=\"h\"><mxGeometry "
                                      "relative=\"1\" as=\"geometry\"/></mxCell>");
        const auto view = build(doc);
        QVERIFY(view.find("e") == nullptr);
    }

    void edgeRoutesBetweenTerminals()
    {
        const auto doc = documentWith(vertex("a", "1", 0, 0, 40, 40, "rounded=0") +
                                      vertex("b", "1", 200, 0, 40, 40, "rounded=0") +
                                      "<mxCell id=\"e\" edge=\"1\" parent=\"1\" source=\"a\" target=\"b\"><mxGeometry "
                                      "relative=\"1\" as=\"geometry\"/></mxCell>");
        const auto view = build(doc);
        const auto* e = view.find("e");
        QVERIFY(e && e->points.size() >= 2);
        QVERIFY(e->points.front().x <= 40 && e->points.back().x >= 200);
        QVERIFY(e->paintBounds.width >= e->bounds.width);
    }

    void wrapperLabelAndPlaceholders()
    {
        const auto doc = documentWith(
            "<object id=\"w\" placeholders=\"1\" label=\"Hello %name% %id% %unknown% 100%%\" name=\"World\"><mxCell style=\"\" "
            "vertex=\"1\" parent=\"1\"><mxGeometry x=\"0\" y=\"0\" width=\"100\" height=\"40\" as=\"geometry\"/></mxCell></object>");
        const auto view = build(doc);
        const auto* w = view.find("w");
        QVERIFY(w && w->label.has_value());
        QCOMPARE(QString::fromStdString(w->labelText.content), QStringLiteral("Hello World w %unknown% 100%%"));
    }

    // The Kanban pattern: the card's value is resolved from the ancestor object.
    void placeholdersResolveFromAncestors()
    {
        const auto doc = documentWith(
            "<object id=\"lane\" status=\"New\" label=\"\"><mxCell style=\"swimlane\" vertex=\"1\" parent=\"1\">"
            "<mxGeometry x=\"0\" y=\"0\" width=\"200\" height=\"200\" as=\"geometry\"/></mxCell></object>"
            "<object id=\"card\" placeholders=\"1\" label=\"%status%\"><mxCell style=\"rounded=1;\" vertex=\"1\" "
            "parent=\"lane\"><mxGeometry x=\"10\" y=\"10\" width=\"50\" height=\"20\" as=\"geometry\"/></mxCell></object>" +
            // A style flag alone does not turn placeholders on.
            vertex("style", "lane", 10, 40, 50, 20, "placeholders=1", "%status%"));
        const auto view = build(doc);
        QCOMPARE(QString::fromStdString(view.find("card")->labelText.content), QStringLiteral("New"));
        QCOMPARE(QString::fromStdString(view.find("style")->labelText.content), QStringLiteral("%status%"));
    }

    void globalVariablesFromRenderContext()
    {
        const auto doc = documentWith(
            "<object id=\"g\" placeholders=\"1\" label=\"%page% %pagenumber%/%pagecount% %filename% %date%\">"
            "<mxCell style=\"\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"0\" y=\"0\" width=\"100\" height=\"40\" "
            "as=\"geometry\"/></mxCell></object>");
        const QDate day(2024, 3, 5);
        const QLocale locale = QLocale::system();

        render::RenderContext context = render::RenderContext::defaults();
        context.placeholders.page = "Page-1";
        context.placeholders.fileName = "plan.drawio";
        context.placeholders.pageNumber = 2;
        context.placeholders.pageCount = 3;
        context.placeholders.now = QDateTime(day, QTime(14, 7));
        const auto view = render::PageView::build(*doc.page(0), context);
        QCOMPARE(QString::fromStdString(view.find("g")->labelText.content),
                 QStringLiteral("Page-1 2/3 plan.drawio ") + locale.toString(day, QLocale::ShortFormat));

        // Unset values leave their tokens literal.
        const auto bare = build(doc);
        QCOMPARE(QString::fromStdString(bare.find("g")->labelText.content),
                 QStringLiteral("%page% %pagenumber%/%pagecount% %filename% ") + locale.toString(QDate::currentDate(), QLocale::ShortFormat));
    }

    void noLabelAndEmptyValue()
    {
        const auto doc = documentWith(vertex("n", "1", 0, 0, 10, 10, "noLabel=1", "text") +
                                      vertex("e", "1", 20, 0, 10, 10, "", "") + vertex("t", "1", 40, 0, 10, 10, "", "text"));
        const auto view = build(doc);
        QVERIFY(!view.find("n")->label.has_value());
        QVERIFY(!view.find("e")->label.has_value());
        QVERIFY(view.find("t")->label.has_value());
    }
};

QTEST_MAIN(PageViewTest)
#include "tst_page_view.moc"
