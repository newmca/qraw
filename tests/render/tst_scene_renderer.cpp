// SceneRenderer: group nesting, cell filtering and label suppression,
// observed through the RecordingCanvas.

#include "qraw/format/codec.h"
#include "qraw/render/scene_renderer.h"

#include "support/recording_canvas.h"

#include <QFile>
#include <QTest>

#include <algorithm>

using namespace qraw;

namespace {

core::Document nestedDocument()
{
    const QByteArray xml =
        "<mxfile><diagram id=\"p\" name=\"p\"><mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
        "<mxCell id=\"g\" value=\"Group\" style=\"rounded=0\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"0\" y=\"0\" width=\"100\" height=\"100\" as=\"geometry\"/></mxCell>"
        "<mxCell id=\"c\" value=\"Child\" style=\"rounded=0\" vertex=\"1\" parent=\"g\"><mxGeometry x=\"10\" y=\"10\" width=\"30\" height=\"30\" as=\"geometry\"/></mxCell>"
        "<mxCell id=\"o\" value=\"Other\" style=\"rounded=0\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"200\" y=\"0\" width=\"50\" height=\"50\" as=\"geometry\"/></mxCell>"
        "</root></mxGraphModel></diagram></mxfile>";
    auto read = format::readDocument(xml);
    if (!read.ok())
        qFatal("fixture does not parse");
    return std::move(*read.document);
}

// golden/containers-tables.drawio: the table "tb" (page x 40..280, y 380..500,
// header 30) with two rows of 45 and two columns of 120.
core::Document tablesFixture()
{
    QFile file(QStringLiteral(QRAW_TEST_DATA_DIR "/golden/containers-tables.drawio"));
    if (!file.open(QIODevice::ReadOnly))
        qFatal("fixture is missing");
    auto read = format::readDocument(file.readAll());
    if (!read.ok())
        qFatal("fixture does not parse");
    return std::move(*read.document);
}

bool hasOp(const test::RecordingCanvas& c, const std::string& op)
{
    return std::find(c.ops.begin(), c.ops.end(), op) != c.ops.end();
}

std::vector<std::string> groupOps(const test::RecordingCanvas& c)
{
    std::vector<std::string> out;
    for (const auto& op : c.ops)
        if (op.starts_with("beginGroup") || op == "endGroup")
            out.push_back(op);
    return out;
}

} // namespace

class SceneRendererTest : public QObject {
    Q_OBJECT

private slots:
    void groupsFollowTheCellTree()
    {
        const auto doc = nestedDocument();
        const auto context = render::RenderContext::defaults();
        const auto view = render::PageView::build(*doc.page(0), context);
        test::RecordingCanvas canvas;
        render::SceneRenderer(context).paintPage(canvas, view);
        // g { c } o: the child group is nested inside its parent's group.
        QCOMPARE(groupOps(canvas), (std::vector<std::string>{"beginGroup g", "beginGroup c", "endGroup", "endGroup",
                                                              "beginGroup o", "endGroup"}));
    }

    void groupsCanBeDisabled()
    {
        const auto doc = nestedDocument();
        const auto context = render::RenderContext::defaults();
        const auto view = render::PageView::build(*doc.page(0), context);
        test::RecordingCanvas canvas;
        render::PaintOptions options;
        options.groups = false;
        render::SceneRenderer(context).paintPage(canvas, view, options);
        QVERIFY(groupOps(canvas).empty());
    }

    void cellIdsFilter()
    {
        const auto doc = nestedDocument();
        const auto context = render::RenderContext::defaults();
        const auto view = render::PageView::build(*doc.page(0), context);
        test::RecordingCanvas canvas;
        render::PaintOptions options;
        options.cellIds = {"o"};
        render::SceneRenderer(context).paintPage(canvas, view, options);
        QCOMPARE(groupOps(canvas), (std::vector<std::string>{"beginGroup o", "endGroup"}));
    }

    void labelsCanBeSuppressed()
    {
        const auto doc = nestedDocument();
        const auto context = render::RenderContext::defaults();
        const auto view = render::PageView::build(*doc.page(0), context);
        const render::SceneRenderer renderer(context);

        test::RecordingCanvas withLabels;
        renderer.paintPage(withLabels, view);
        QCOMPARE(withLabels.opsStartingWith("text ").size(), std::size_t(3));

        test::RecordingCanvas withoutLabels;
        render::PaintOptions options;
        options.labels = false;
        renderer.paintPage(withoutLabels, view, options);
        QVERIFY(withoutLabels.opsStartingWith("text ").empty());
        QVERIFY(withoutLabels.paints > 0);
    }
    void tableGridNeedsItsView()
    {
        const auto doc = tablesFixture();
        const auto context = render::RenderContext::defaults();
        const auto view = render::PageView::build(*doc.page(0), context);
        const render::CellState* table = view.find("tb");
        QVERIFY(table != nullptr);
        const render::SceneRenderer renderer(context);

        // The row line between the two rows and the column line between the two cells.
        test::RecordingCanvas withView;
        renderer.paintShape(withView, *table, &view);
        QVERIFY(hasOp(withView, "moveTo 40 455"));
        QVERIFY(hasOp(withView, "lineTo 280 455"));
        QVERIFY(hasOp(withView, "moveTo 160 410"));
        QVERIFY(hasOp(withView, "lineTo 160 500"));

        // Without the owning view a container sees no children: outer box only.
        test::RecordingCanvas withoutView;
        renderer.paintShape(withoutView, *table);
        QVERIFY(!hasOp(withoutView, "moveTo 40 455"));
        QVERIFY(!hasOp(withoutView, "moveTo 160 410"));
    }
};

QTEST_MAIN(SceneRendererTest)
#include "tst_scene_renderer.moc"
