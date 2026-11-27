// Line jumps: the intersection computation (geometry/jumps.cpp), the routed
// points on CellState and the painted jump styles.

#include "qraw/format/codec.h"
#include "qraw/geometry/jumps.h"
#include "qraw/render/scene_renderer.h"
#include "qraw/render/svg_canvas.h"

#include <QTest>

#include <string>
#include <vector>

using namespace qraw;
using geometry::Point;

namespace {

// "h" runs left to right at y=50, "v" top to bottom at x=50; v is the later edge
// and carries `vStyle` (e.g. "jumpStyle=arc;").
core::Document crossingEdges(const std::string& vStyle, const std::string& hStyle = {})
{
    const std::string xml =
        "<mxfile><diagram id=\"p\" name=\"p\"><mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
        "<mxCell id=\"h\" style=\"" + hStyle + "\" edge=\"1\" parent=\"1\"><mxGeometry relative=\"1\" as=\"geometry\">"
        "<mxPoint x=\"0\" y=\"50\" as=\"sourcePoint\"/><mxPoint x=\"100\" y=\"50\" as=\"targetPoint\"/></mxGeometry></mxCell>"
        "<mxCell id=\"v\" style=\"" + vStyle + "\" edge=\"1\" parent=\"1\"><mxGeometry relative=\"1\" as=\"geometry\">"
        "<mxPoint x=\"50\" y=\"0\" as=\"sourcePoint\"/><mxPoint x=\"50\" y=\"100\" as=\"targetPoint\"/></mxGeometry></mxCell>"
        "</root></mxGraphModel></diagram></mxfile>";
    auto read = format::readDocument(QByteArray::fromStdString(xml));
    if (!read.ok())
        qFatal("fixture does not parse");
    return std::move(*read.document);
}

std::string svgOf(const render::PageView& view, std::string_view id)
{
    render::SvgDocumentOptions options;
    options.width = 200;
    options.height = 200;
    render::SvgCanvas canvas(options);
    render::SceneRenderer(render::RenderContext::defaults()).paintShape(canvas, *view.find(id));
    return canvas.toSvg();
}

} // namespace

class JumpsTest : public QObject {
    Q_OBJECT

private slots:
    void crossingSegmentsJump()
    {
        const std::vector<Point> earlier{{0, 50}, {100, 50}};
        const geometry::JumpTarget target = geometry::makeJumpTarget(earlier);
        const geometry::JumpTarget* targets[] = {&target};
        const std::vector<Point> line{{50, 0}, {50, 100}};
        const auto routed = geometry::routeJumps(line, targets);
        QCOMPARE(routed.size(), std::size_t(3));
        QVERIFY(!routed[0].jump);
        QVERIFY(routed[1].jump);
        QCOMPARE(routed[1].point, (Point{50, 50}));
        QVERIFY(!routed[2].jump);
    }

    void collinearTouchingAndParallelSegmentsDoNotJump()
    {
        const auto jumps = [](std::vector<Point> earlier, std::vector<Point> line) {
            const geometry::JumpTarget target = geometry::makeJumpTarget(earlier);
            const geometry::JumpTarget* targets[] = {&target};
            return geometry::routeJumps(line, targets);
        };
        QVERIFY(jumps({{0, 50}, {100, 50}}, {{20, 50}, {80, 50}}).empty());  // collinear overlap
        QVERIFY(jumps({{0, 50}, {100, 50}}, {{0, 60}, {100, 60}}).empty());  // parallel
        QVERIFY(jumps({{0, 50}, {100, 50}}, {{50, 50}, {50, 100}}).empty()); // the line starts on the other
        QVERIFY(jumps({{0, 50}, {100, 50}}, {{50, 0}, {50, 49}}).empty());   // stops short
    }

    void jumpsAreOrderedAlongTheLine()
    {
        const geometry::JumpTarget a = geometry::makeJumpTarget(std::vector<Point>{{0, 80}, {100, 80}});
        const geometry::JumpTarget b = geometry::makeJumpTarget(std::vector<Point>{{0, 20}, {100, 20}});
        const geometry::JumpTarget* targets[] = {&a, &b};
        const auto routed = geometry::routeJumps(std::vector<Point>{{50, 0}, {50, 100}}, targets);
        QCOMPARE(routed.size(), std::size_t(4));
        QCOMPARE(routed[1].point, (Point{50, 20}));
        QCOMPARE(routed[2].point, (Point{50, 80}));
    }

    void onlyLaterEdgesWithAJumpStyleGetRoutedPoints()
    {
        const auto doc = crossingEdges("jumpStyle=arc;");
        const auto view = render::PageView::build(*doc.page(0), render::RenderContext::defaults());
        QVERIFY(view.find("h")->routedPoints.empty());
        QCOMPARE(view.find("v")->routedPoints.size(), std::size_t(3));

        // noJump on the earlier edge: nothing to jump over.
        const auto skipped = crossingEdges("jumpStyle=arc;", "noJump=1;");
        const auto skippedView = render::PageView::build(*skipped.page(0), render::RenderContext::defaults());
        QVERIFY(skippedView.find("v")->routedPoints.empty());

        // The earlier edge never jumps over the later one.
        const auto reversed = crossingEdges("", "jumpStyle=arc;");
        const auto reversedView = render::PageView::build(*reversed.page(0), render::RenderContext::defaults());
        QVERIFY(reversedView.find("h")->routedPoints.empty());
    }

    void jumpStylesChangeOnlyTheJumpingEdge()
    {
        const auto plain = crossingEdges("");
        const auto plainView = render::PageView::build(*plain.page(0), render::RenderContext::defaults());
        const std::string straight = svgOf(plainView, "v");
        QVERIFY(straight.find(" C ") == std::string::npos);

        const auto arc = crossingEdges("jumpStyle=arc;");
        const auto arcView = render::PageView::build(*arc.page(0), render::RenderContext::defaults());
        QVERIFY(svgOf(arcView, "v").find(" C ") != std::string::npos); // a bezier bridge at the crossing
        QCOMPARE(svgOf(arcView, "h"), svgOf(plainView, "h"));

        const auto gap = crossingEdges("jumpStyle=gap;");
        const auto gapView = render::PageView::build(*gap.page(0), render::RenderContext::defaults());
        const std::string gapSvg = svgOf(gapView, "v");
        // jumpSize 6, stroke 1: the line stops (6-2)/2+1 = 3 before the crossing and resumes 3 after.
        QVERIFY(gapSvg.find("L 50 47") != std::string::npos);
        QVERIFY(gapSvg.find("M 50 53") != std::string::npos);
    }
};

QTEST_MAIN(JumpsTest)
#include "tst_jumps.moc"
