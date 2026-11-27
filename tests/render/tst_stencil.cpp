// mxStencil interpreter (src/render/stencil.cpp) on hand-written XML, checked
// through the calls it makes on a RecordingCanvas.

#include "qraw/format/xml_io.h"
#include "qraw/render/stencil.h"

#include "support/recording_canvas.h"

#include <QTest>

#include <algorithm>
#include <initializer_list>
#include <string>
#include <utility>
#include <vector>

using namespace qraw;
using Ops = std::vector<std::string>;
using Styles = std::initializer_list<std::pair<const char*, const char*>>;

namespace {

render::Stencil makeStencil(const std::string& xml)
{
    const auto parsed = format::parseXml(QByteArrayView(xml.data(), qsizetype(xml.size())));
    Q_ASSERT(parsed.document);
    auto stencil = render::Stencil::fromXml(parsed.document->root);
    Q_ASSERT(stencil);
    return std::move(*stencil);
}

render::ResolvedStyle makeStyle(Styles values)
{
    render::StyleMap map;
    for (const auto& [key, value] : values)
        map.set(key, value);
    return render::ResolvedStyle(map, false);
}

struct Painted {
    test::RecordingCanvas canvas;
    const std::vector<std::string>& ops() const { return canvas.ops; }
    bool has(const std::string& op) const { return std::find(canvas.ops.begin(), canvas.ops.end(), op) != canvas.ops.end(); }
};

Painted paint(const render::Stencil& stencil, double x, double y, double w, double h, Styles styles = {},
              const render::ImageResolveContext* images = nullptr)
{
    Painted painted;
    const render::ResolvedStyle style = makeStyle(styles);
    render::ShapePaintContext context{style, images, {}, false};
    stencil.paint(painted.canvas, context, x, y, w, h);
    return painted;
}

// A <shape> with the given attributes and body.
std::string shapeXml(const std::string& attributes, const std::string& body)
{
    return "<shape " + attributes + ">" + body + "</shape>";
}

// Background path from (0,0) to (100,50) plus the given foreground.
std::string diagonal(const std::string& attributes, const std::string& foreground = "<fillstroke/>")
{
    return shapeXml(attributes, "<background><path><move x=\"0\" y=\"0\"/><line x=\"100\" y=\"50\"/><close/></path>"
                                "</background><foreground>" + foreground + "</foreground>");
}

} // namespace

class TstStencil : public QObject {
    Q_OBJECT
private slots:
    void fromXml();
    void variableAspectScales();
    void fixedAspectCenters();
    void directionSouthSwapsAxes();
    void strokeWidth();
    void colorResolution();
    void alphaQuirk();
    void roundrect();
    void arcScalesRadii();
    void textOp();
    void backgroundThenForegroundAndShadow();
    void unbalancedSaveRestore();
    void styleOps();
    void roundedPath();
    void images();
    void includeShape();
    void connections();
    void labelBounds();
    void key();
};

void TstStencil::fromXml()
{
    QVERIFY(!render::Stencil::fromXml(core::XmlNode::element("shapes")));
    std::vector<core::Diagnostic> diagnostics;
    QVERIFY(!render::Stencil::fromXml(core::XmlNode::element("path"), &diagnostics));
    QCOMPARE(diagnostics.size(), std::size_t(1));

    const render::Stencil defaults = makeStencil("<shape name=\"d\"/>");
    QCOMPARE(defaults.name(), std::string("d"));
    QCOMPARE(defaults.width(), 100.0);
    QCOMPARE(defaults.height(), 100.0);
    QVERIFY(!defaults.fixedAspect());

    const render::Stencil fixed = makeStencil("<shape name=\"f\" w=\"30\" h=\"20\" aspect=\"fixed\"/>");
    QCOMPARE(fixed.width(), 30.0);
    QCOMPARE(fixed.height(), 20.0);
    QVERIFY(fixed.fixedAspect());
    // "1" (104 shipped shapes) behaves as variable.
    QVERIFY(!makeStencil("<shape aspect=\"1\"/>").fixedAspect());
}

void TstStencil::variableAspectScales()
{
    const Painted p = paint(makeStencil(diagonal("name=\"t\" w=\"100\" h=\"50\" aspect=\"variable\"")), 10, 20, 200, 100);
    const Ops expected = {"setStrokeWidth 2", "begin", "moveTo 10 20", "lineTo 210 120", "close", "fillAndStroke", "setShadow 0"};
    QCOMPARE(p.ops(), expected);

    // Different horizontal and vertical scales.
    const Painted q = paint(makeStencil(diagonal("w=\"100\" h=\"50\"")), 0, 0, 100, 200);
    QVERIFY(q.has("lineTo 100 200"));
}

void TstStencil::fixedAspectCenters()
{
    const render::Stencil stencil = makeStencil(diagonal("w=\"100\" h=\"100\" aspect=\"fixed\""));
    // Scale min(3, 1) = 1; the 100x100 shape is centered horizontally: x0 = 100.
    const Painted p = paint(stencil, 0, 0, 300, 100);
    QVERIFY(p.has("moveTo 100 0"));
    QVERIFY(p.has("lineTo 200 50"));
}

void TstStencil::directionSouthSwapsAxes()
{
    const render::Stencil stencil =
        makeStencil(shapeXml("w=\"100\" h=\"50\"", "<foreground><path><move x=\"100\" y=\"0\"/></path><fill/></foreground>"));
    // computeAspect swaps: sx = h / w0 = 1, sy = w / h0 = 1, x0 += (w - h) / 2, y0 -= (w - h) / 2.
    const Painted p = paint(stencil, 0, 0, 50, 100, {{"direction", "south"}});
    QVERIFY2(p.has("moveTo 75 25"), p.canvas.text().c_str());
    QVERIFY(stencil.handlesDirection());
}

void TstStencil::strokeWidth()
{
    const std::string body = "<foreground><strokewidth width=\"2\"/><strokewidth width=\"2\" fixed=\"1\"/></foreground>";
    // inherit: the cell's strokeWidth, not scaled.
    const Painted inherit = paint(makeStencil(shapeXml("strokewidth=\"inherit\"", body)), 0, 0, 200, 200, {{"strokeWidth", "3"}});
    QCOMPARE(inherit.canvas.opsStartingWith("setStrokeWidth"), (Ops{"setStrokeWidth 3", "setStrokeWidth 4", "setStrokeWidth 2"}));
    // A number is scaled by minScale (2 here), and defaults to 1.
    const Painted numeric = paint(makeStencil(shapeXml("strokewidth=\"1.5\"", "")), 0, 0, 200, 200, {{"strokeWidth", "3"}});
    QCOMPARE(numeric.ops().front(), std::string("setStrokeWidth 3"));
    const Painted missing = paint(makeStencil(shapeXml("", "<foreground><fill/></foreground>")), 0, 0, 200, 200);
    QCOMPARE(missing.ops().front(), std::string("setStrokeWidth 2"));
}

void TstStencil::colorResolution()
{
    const auto fillOf = [](const std::string& element, Styles styles) {
        const Painted p = paint(makeStencil(shapeXml("", "<foreground>" + element + "</foreground>")), 0, 0, 100, 100, styles);
        const auto set = p.canvas.opsStartingWith("setFillColor");
        const auto stroke = p.canvas.opsStartingWith("setStrokeColor");
        return !set.empty() ? set.front() : (!stroke.empty() ? stroke.front() : std::string());
    };
    // A style key, then the default attribute.
    QCOMPARE(fillOf("<fillcolor color=\"fillColor2\" default=\"#ffffff\"/>", {{"fillColor2", "#00ff00"}}),
             std::string("setFillColor #00ff00"));
    QCOMPARE(fillOf("<fillcolor color=\"fillColor2\" default=\"#123456\"/>", {}), std::string("setFillColor #123456"));
    QCOMPARE(fillOf("<fillcolor color=\"fillColor2\" default=\"none\"/>", {}), std::string("setFillColor none"));
    // The style value wins over the default, "none" included.
    QCOMPARE(fillOf("<fillcolor color=\"fillColor2\" default=\"#123456\"/>", {{"fillColor2", "none"}}),
             std::string("setFillColor none"));
    // fill / stroke / font tokens use the style colors.
    QCOMPARE(fillOf("<fillcolor color=\"fill\"/>", {{"fillColor", "#ff0000"}}), std::string("setFillColor #ff0000"));
    QCOMPARE(fillOf("<strokecolor color=\"stroke\"/>", {{"strokeColor", "#0000ff"}}), std::string("setStrokeColor #0000ff"));
    // Literal colors, names and none.
    QCOMPARE(fillOf("<strokecolor color=\"#abc\"/>", {}), std::string("setStrokeColor #aabbcc"));
    QCOMPARE(fillOf("<fillcolor color=\"red\"/>", {}), std::string("setFillColor #ff0000"));
    QCOMPARE(fillOf("<strokecolor color=\"none\"/>", {}), std::string("setStrokeColor none"));
    // `default` is the theme color (white fill, black stroke).
    QCOMPARE(fillOf("<fillcolor color=\"default\"/>", {}), std::string("setFillColor #ffffff"));
    QCOMPARE(fillOf("<strokecolor color=\"default\"/>", {}), std::string("setStrokeColor #000000"));
}

void TstStencil::alphaQuirk()
{
    // fillalpha and strokealpha both call setAlpha upstream; copied on purpose.
    const Painted p = paint(makeStencil(shapeXml("", "<foreground><alpha alpha=\"0.9\"/><fillalpha alpha=\"0.5\"/>"
                                                     "<strokealpha alpha=\"0.25\"/></foreground>")),
                            0, 0, 100, 100);
    QCOMPARE(p.canvas.opsStartingWith("setAlpha"), (Ops{"setAlpha 0.9", "setAlpha 0.5", "setAlpha 0.25"}));
    QVERIFY(p.canvas.opsStartingWith("setFillAlpha").empty());
    QVERIFY(p.canvas.opsStartingWith("setStrokeAlpha").empty());
}

void TstStencil::roundrect()
{
    const render::Stencil stencil =
        makeStencil(shapeXml("", "<foreground><roundrect x=\"0\" y=\"0\" w=\"100\" h=\"50\" arcsize=\"20\"/>"
                                 "<roundrect x=\"0\" y=\"0\" w=\"100\" h=\"50\" arcsize=\"0\"/></foreground>"));
    const Painted p = paint(stencil, 0, 0, 200, 100);
    // radius = min(w, h) * arcsize / 100; arcsize 0 means 15.
    QCOMPARE(p.canvas.opsStartingWith("roundrect"), (Ops{"roundrect 0 0 200 50 10 10", "roundrect 0 0 200 50 7.5 7.5"}));
}

void TstStencil::arcScalesRadii()
{
    const render::Stencil stencil = makeStencil(shapeXml(
        "", "<foreground><path><move x=\"0\" y=\"0\"/><arc rx=\"10\" ry=\"5\" x-axis-rotation=\"30\" "
            "large-arc-flag=\"1\" sweep-flag=\"0\" x=\"50\" y=\"25\"/></path><stroke/></foreground>"));
    const Painted p = paint(stencil, 0, 0, 200, 200);
    // Radii scale, the rotation does not; flags are booleans.
    QCOMPARE(p.canvas.opsStartingWith("arcTo"), (Ops{"arcTo 20 10 30 1 0 100 50"}));
}

void TstStencil::textOp()
{
    const render::Stencil stencil = makeStencil(shapeXml(
        "", "<foreground><fontcolor color=\"#ff0000\"/><fontsize size=\"10\"/>"
            "<text str=\"Hi\" x=\"50\" y=\"25\" align=\"center\" valign=\"middle\"/>"
            "<text str=\"Top\" x=\"0\" y=\"0\"/></foreground>"));
    const Painted p = paint(stencil, 0, 0, 200, 100);
    QCOMPARE(p.canvas.opsStartingWith("text "),
             (Ops{"text 100 25 0 0 center middle Hi", "text 0 0 0 0 left top Top"}));
}

void TstStencil::backgroundThenForegroundAndShadow()
{
    const Painted p = paint(makeStencil(diagonal("w=\"100\" h=\"50\"", "<fillstroke/><stroke/>")), 0, 0, 100, 50);
    // The background path is drawn by the first foreground op; the shadow
    // (set by the caller) goes off after each paint op of the foreground.
    const Ops expected = {"setStrokeWidth 1", "begin",         "moveTo 0 0",    "lineTo 100 50", "close",
                          "fillAndStroke",    "setShadow 0",   "stroke",        "setShadow 0"};
    QCOMPARE(p.ops(), expected);
    QVERIFY(!p.has("setShadow 1"));
}

void TstStencil::unbalancedSaveRestore()
{
    // A restore without save must not pop the caller's state; saves left
    // open are closed at the end (mxStencil.drawShape restores the stack).
    const Painted p = paint(makeStencil(shapeXml("", "<foreground><restore/><save/><save/><fill/></foreground>")), 0, 0, 10, 10);
    const Ops expected = {"setStrokeWidth 0.1", "save", "save", "fill", "setShadow 0", "restore", "restore"};
    QCOMPARE(p.ops(), expected);
}

void TstStencil::styleOps()
{
    const Painted p = paint(makeStencil(shapeXml(
                                "", "<foreground><dashed dashed=\"1\"/><dashpattern pattern=\"3  1.5\"/><dashpattern dash=\"1\"/>"
                                    "<linecap cap=\"flat\"/><linecap cap=\"round\"/><linejoin join=\"bevel\"/>"
                                    "<miterlimit limit=\"4\"/><bogus/><fillstrokecolor/></foreground>")),
                            0, 0, 200, 200);
    const Ops expected = {"setStrokeWidth 2", "setDashed 1 0", "setDashPattern 6 3", "setLineCap 0", "setLineCap 1",
                          "setLineJoin 2",    "setMiterLimit 4"};
    QCOMPARE(p.ops(), expected);
}

void TstStencil::roundedPath()
{
    // Curves inside a rounded path: interpreted regularly.
    const render::Stencil withCurve = makeStencil(shapeXml(
        "", "<foreground><path rounded=\"1\" arcSize=\"10\"><move x=\"0\" y=\"0\"/><curve x1=\"1\" y1=\"1\" x2=\"2\" y2=\"2\" x3=\"3\" y3=\"3\"/>"
            "</path><fill/></foreground>"));
    const Painted regular = paint(withCurve, 0, 0, 100, 100);
    QVERIFY(regular.has("moveTo 0 0"));
    QVERIFY(regular.has("curveTo 1 1 2 2 3 3"));

    // Only move/line: the outline goes through geometry::polylinePath.
    const render::Stencil rounded = makeStencil(shapeXml(
        "", "<foreground><path rounded=\"1\" arcSize=\"10\"><move x=\"0\" y=\"0\"/><line x=\"100\" y=\"0\"/>"
            "<line x=\"100\" y=\"100\"/><line x=\"0\" y=\"0\"/></path><fill/></foreground>"));
    const Painted p = paint(rounded, 0, 0, 100, 100);
    QVERIFY(p.has("begin"));
    QVERIFY(!p.canvas.opsStartingWith("addPath").empty());
    QVERIFY(p.canvas.opsStartingWith("moveTo").empty());
}

void TstStencil::images()
{
    const render::Stencil stencil = makeStencil(shapeXml(
        "", "<foreground><image src=\"data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR4nGP4z8DwHwAFAAH/"
            "iZk9HQAAAABJRU5ErkJggg==\" x=\"10\" y=\"10\" w=\"50\" h=\"50\"/></foreground>"));
    // No image context: images are skipped.
    QVERIFY(paint(stencil, 0, 0, 100, 100).canvas.opsStartingWith("image").empty());

    render::ImageResolveContext context;
    const Painted p = paint(stencil, 0, 0, 200, 200, {}, &context);
    QCOMPARE(p.canvas.opsStartingWith("image"), (Ops{"image 20 20 100 100 image/png 0 0 0"}));
}

void TstStencil::includeShape()
{
    const auto parsed = format::parseXml(QByteArray(
        "<shapes name=\"Test\">"
        "<shape name=\"Inner\" w=\"10\" h=\"10\"><foreground><path><move x=\"0\" y=\"0\"/><line x=\"10\" y=\"10\"/></path><stroke/></foreground></shape>"
        "<shape name=\"Outer\"><foreground><include-shape name=\"TEST.inner\" x=\"0\" y=\"0\" w=\"50\" h=\"50\"/></foreground></shape>"
        "<shape name=\"Loop\"><foreground><include-shape name=\"test.loop\" x=\"0\" y=\"0\" w=\"50\" h=\"50\"/><stroke/></foreground></shape>"
        "</shapes>"));
    QVERIFY(parsed.document);
    render::StencilRegistry::builtin().addSet(parsed.document->root);
    auto& registry = render::StencilRegistry::builtin();

    const render::Stencil* outer = registry.find("test.outer");
    QVERIFY(outer);
    test::RecordingCanvas canvas;
    const render::ResolvedStyle style = makeStyle({});
    outer->paint(canvas, render::ShapePaintContext{style, nullptr, {}, false}, 0, 0, 100, 100);
    // The inner stencil (10x10) fills the 50x50 box at scale 5.
    QVERIFY2(std::find(canvas.ops.begin(), canvas.ops.end(), "lineTo 50 50") != canvas.ops.end(), canvas.text().c_str());

    // A shape including itself stops at a depth limit instead of recursing forever.
    const render::Stencil* loop = registry.find("test.loop");
    QVERIFY(loop);
    test::RecordingCanvas loopCanvas;
    loop->paint(loopCanvas, render::ShapePaintContext{style, nullptr, {}, false}, 0, 0, 100, 100);
    QVERIFY(loopCanvas.paints > 1);
}

void TstStencil::connections()
{
    const render::Stencil stencil = makeStencil(shapeXml(
        "", "<connections><constraint x=\"0.5\" y=\"0\" perimeter=\"1\" name=\"N\"/>"
            "<constraint x=\"1\" y=\"0.5\" perimeter=\"0\" dx=\"-2\" dy=\"3\"/><constraint/></connections><foreground/>"));
    const auto constraints = stencil.connectionConstraints(makeStyle({}), 100, 100);
    QCOMPARE(constraints.size(), std::size_t(3));
    QCOMPARE(constraints[0].point, (render::Point{0.5, 0}));
    QVERIFY(constraints[0].perimeter);
    QCOMPARE(constraints[0].name, std::string("N"));
    QCOMPARE(constraints[1].point, (render::Point{1, 0.5}));
    QVERIFY(!constraints[1].perimeter);
    QCOMPARE(constraints[1].dx, -2.0);
    QCOMPARE(constraints[1].dy, 3.0);
    // Number(null) is 0.
    QCOMPARE(constraints[2].point, (render::Point{0, 0}));
    QVERIFY(makeStencil("<shape/>").connectionConstraints(makeStyle({}), 1, 1).empty());
}

void TstStencil::labelBounds()
{
    const render::Stencil stencil = makeStencil(shapeXml(
        "w=\"100\" h=\"100\"",
        "<labelBounds if=\"boundedLbl\" x=\"10\" y=\"20\" w=\"80\" h=\"50\"/><foreground/>"));
    const render::Rect rect{0, 0, 200, 200};
    // Scale 2: left 20, top 40, right 200 - 20 - 160, bottom 200 - 40 - 100.
    QCOMPARE(stencil.labelBounds(makeStyle({{"boundedLbl", "1"}}), rect), (render::Rect{20, 40, 160, 100}));

    // Direction rotates the margins (east left/top become south top/right...).
    const render::Rect south = stencil.labelBounds(makeStyle({{"boundedLbl", "1"}, {"direction", "south"}}), rect);
    QCOMPARE(south, (render::Rect{60, 20, 100, 160}));

    // Without a matching labelBounds the shape default applies.
    QCOMPARE(stencil.labelBounds(makeStyle({}), rect), rect);

    // Unconditional entry with defaults: x=y=0, w/h = stencil size.
    const render::Stencil plain = makeStencil(shapeXml("w=\"100\" h=\"100\"", "<labelBounds/><foreground/>"));
    QCOMPARE(plain.labelBounds(makeStyle({}), rect), rect);
}

void TstStencil::key()
{
    QCOMPARE(render::stencilKey("mxGraph.Basic", "4 Point Star"), std::string("mxgraph.basic.4_point_star"));
    QCOMPARE(render::stencilKey("a", "B"), std::string("a.b"));
}

QTEST_MAIN(TstStencil)
#include "tst_stencil.moc"
