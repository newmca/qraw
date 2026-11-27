// sketch=1 and comic=1: determinism, hachure fills, edges, comic jiggle,
// labels untouched (src/render/sketch_canvas.cpp). Observed through the
// RecordingCanvas.

#include "qraw/format/codec.h"
#include "qraw/render/scene_renderer.h"
#include "qraw/render/svg_canvas.h"

#include "support/recording_canvas.h"

#include <QTest>

#include <algorithm>

using namespace qraw;

namespace {

core::Document documentWith(const QByteArray& cells)
{
    const QByteArray xml = "<mxfile><diagram id=\"p\" name=\"p\"><mxGraphModel><root><mxCell id=\"0\"/>"
                           "<mxCell id=\"1\" parent=\"0\"/>" + cells + "</root></mxGraphModel></diagram></mxfile>";
    auto read = format::readDocument(xml);
    if (!read.ok())
        qFatal("fixture does not parse");
    return std::move(*read.document);
}

QByteArray vertex(const char* id, const char* style)
{
    return QByteArray("<mxCell id=\"") + id + "\" value=\"Hi\" style=\"" + style +
           "\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"10\" y=\"10\" width=\"120\" height=\"60\" as=\"geometry\"/></mxCell>";
}

test::RecordingCanvas paint(const core::Document& doc)
{
    const auto context = render::RenderContext::defaults();
    const auto view = render::PageView::build(*doc.page(0), context);
    test::RecordingCanvas canvas;
    render::SceneRenderer(context).paintPage(canvas, view);
    return canvas;
}

test::RecordingCanvas paintVertex(const char* id, const char* style)
{
    return paint(documentWith(vertex(id, style)));
}

int count(const test::RecordingCanvas& c, std::string_view prefix) { return static_cast<int>(c.opsStartingWith(prefix).size()); }

bool has(const test::RecordingCanvas& c, const std::string& op)
{
    return std::find(c.ops.begin(), c.ops.end(), op) != c.ops.end();
}

} // namespace

class SketchTest : public QObject {
    Q_OBJECT

private slots:
    void sameCellRendersIdentically()
    {
        const auto a = paintVertex("v1", "rounded=0;sketch=1;fillColor=#ff0000;");
        const auto b = paintVertex("v1", "rounded=0;sketch=1;fillColor=#ff0000;");
        QCOMPARE(a.text(), b.text());
        // A second SVG render of the same cell is byte-identical too.
        const auto svg = [] {
            const auto doc = documentWith(vertex("v1", "sketch=1;fillColor=#ff0000;"));
            const auto context = render::RenderContext::defaults();
            const auto view = render::PageView::build(*doc.page(0), context);
            render::SvgDocumentOptions options;
            options.width = 200;
            options.height = 100;
            render::SvgCanvas canvas(options);
            render::SceneRenderer(context).paintPage(canvas, view);
            return canvas.toSvg();
        };
        QCOMPARE(svg(), svg());
    }

    void differsFromPlainAndBetweenCells()
    {
        const auto plain = paintVertex("v1", "rounded=0;fillColor=#ff0000;");
        const auto sketch = paintVertex("v1", "rounded=0;sketch=1;fillColor=#ff0000;");
        const auto other = paintVertex("v2", "rounded=0;sketch=1;fillColor=#ff0000;");
        QVERIFY(plain.text() != sketch.text());
        QVERIFY(sketch.text() != other.text()); // the seed comes from the cell id
    }

    void filledRectangleGetsHachureLines()
    {
        const auto c = paintVertex("v1", "rounded=0;sketch=1;fillColor=#ff0000;strokeColor=#000000;");
        QVERIFY(!has(c, "rect 10 10 120 60"));
        // Hachure strokes use the fill color, the outline the stroke color.
        QVERIFY(has(c, "setStrokeColor #ff0000"));
        QVERIFY(count(c, "curveTo") > 20);
        QVERIFY(count(c, "moveTo") > 20);
        // The outline is drawn twice: four sides, two passes.
        QVERIFY(has(c, "setStrokeColor #000000"));
    }

    void whiteOrGradientFillIsSolid()
    {
        const auto white = paintVertex("v1", "rounded=0;sketch=1;fillColor=#ffffff;");
        QVERIFY(has(white, "fill"));
        QVERIFY(count(white, "curveTo") <= 8); // outline only
        const auto gradient = paintVertex("v1", "rounded=0;sketch=1;fillColor=#ff0000;gradientColor=#0000ff;");
        QVERIFY(has(gradient, "fill"));
    }

    void fillStyleSolidAndHachureGap()
    {
        const auto solid = paintVertex("v1", "rounded=0;sketch=1;fillColor=#ff0000;fillStyle=solid;");
        QVERIFY(has(solid, "fill"));
        const auto dense = paintVertex("v1", "rounded=0;sketch=1;fillColor=#ff0000;hachureGap=2;");
        const auto sparse = paintVertex("v1", "rounded=0;sketch=1;fillColor=#ff0000;hachureGap=16;");
        QVERIFY(count(dense, "curveTo") > count(sparse, "curveTo"));
        const auto cross = paintVertex("v1", "rounded=0;sketch=1;fillColor=#ff0000;fillStyle=cross-hatch;");
        const auto hachure = paintVertex("v1", "rounded=0;sketch=1;fillColor=#ff0000;");
        QVERIFY(count(cross, "curveTo") > count(hachure, "curveTo"));
    }

    void ellipseIsDrawnAsCurves()
    {
        const auto c = paintVertex("v1", "ellipse;sketch=1;fillColor=#ff0000;");
        QVERIFY(count(c, "ellipse ") == 0);
        QVERIFY(count(c, "curveTo") > 20);
    }

    void edgesAreSketched()
    {
        const auto doc = documentWith(
            "<mxCell id=\"e\" style=\"sketch=1;endArrow=classic;\" edge=\"1\" parent=\"1\"><mxGeometry relative=\"1\" as=\"geometry\">"
            "<mxPoint x=\"0\" y=\"0\" as=\"sourcePoint\"/><mxPoint x=\"200\" y=\"0\" as=\"targetPoint\"/></mxGeometry></mxCell>");
        const auto c = paint(doc);
        QVERIFY(count(c, "curveTo") >= 2); // line, two passes
    }

    void comicJigglesStraightSegments()
    {
        const auto plain = paintVertex("v1", "rounded=0;");
        const auto comic = paintVertex("v1", "rounded=0;comic=1;");
        QVERIFY(count(comic, "lineTo") > count(plain, "lineTo") * 5);
        QVERIFY(has(comic, "close"));
        // sketch=1 with sketchStyle=comic is the same decorator.
        const auto viaSketch = paintVertex("v1", "rounded=0;sketch=1;sketchStyle=comic;");
        QVERIFY(count(viaSketch, "lineTo") > count(plain, "lineTo") * 5);
        QVERIFY(plain.text() != comic.text());
    }

    void labelsAreNotJiggled()
    {
        const auto plain = paintVertex("v1", "rounded=0;");
        const auto sketch = paintVertex("v1", "rounded=0;sketch=1;");
        QCOMPARE(plain.opsStartingWith("text "), sketch.opsStartingWith("text "));
    }

    void paintBoundsGrow()
    {
        const auto bounds = [](const char* style) {
            const auto doc = documentWith(vertex("v1", style));
            const auto context = render::RenderContext::defaults();
            return render::PageView::build(*doc.page(0), context).states().back().paintBounds;
        };
        QVERIFY(bounds("rounded=0;sketch=1;").width > bounds("rounded=0;").width);
        QVERIFY(bounds("rounded=0;comic=1;").width > bounds("rounded=0;").width);
    }
};

QTEST_MAIN(SketchTest)
#include "tst_sketch.moc"
