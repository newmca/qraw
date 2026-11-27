// Native painters for the shapes that draw.io implements only in JavaScript
// (Shapes.js, BPMN 2, floorplan, basic.rect, arrows2): structural checks of
// what each painter issues to the canvas. The full pictures are in the
// js-shapes golden fixture.

#include "qraw/render/shape.h"
#include "qraw/render/stencil.h"

#include "support/recording_canvas.h"

#include <QTest>

#include <algorithm>
#include <cmath>
#include <string>

using namespace qraw;
using render::ResolvedStyle;
using render::ShapePaintContext;
using render::ShapeRegistry;
using render::StyleMap;
using test::RecordingCanvas;

namespace {

ResolvedStyle makeStyle(std::initializer_list<std::pair<const char*, const char*>> values)
{
    StyleMap map;
    for (const auto& [key, value] : values)
        map.set(key, value);
    return ResolvedStyle(std::move(map), false);
}

RecordingCanvas paintShape(const char* name, const ResolvedStyle& style, double w, double h)
{
    const render::VertexShape* shape = ShapeRegistry::builtin().find(name);
    if (!shape)
        qFatal("shape %s is not registered", name);
    RecordingCanvas c;
    const ShapePaintContext context{style, nullptr, {}, false, &render::StencilRegistry::builtin()};
    shape->paint(c, context, 0, 0, w, h);
    return c;
}

int count(const RecordingCanvas& c, const std::string& prefix)
{
    return static_cast<int>(c.opsStartingWith(prefix).size());
}

// curvedText with the cell value `text` (the page view hands it over because
// the style has noLabel=1).
RecordingCanvas paintCurvedText(const ResolvedStyle& style, double w, double h, std::string_view text = "Curved Text",
                                bool hidden = false)
{
    const render::VertexShape* shape = ShapeRegistry::builtin().find("curvedText");
    RecordingCanvas c;
    ShapePaintContext context{style, nullptr, {}, false, &render::StencilRegistry::builtin()};
    context.valueText = text;
    context.valueHidden = hidden;
    shape->paint(c, context, 0, 0, w, h);
    return c;
}

bool near(geometry::Point a, geometry::Point b)
{
    return std::abs(a.x - b.x) < 1e-6 && std::abs(a.y - b.y) < 1e-6;
}

bool has(const RecordingCanvas& c, const std::string& op)
{
    return std::find(c.ops.begin(), c.ops.end(), op) != c.ops.end();
}

} // namespace

class ShapesJsTest : public QObject {
    Q_OBJECT

private slots:
    void everyPortedNameIsRegistered()
    {
        const auto names = ShapeRegistry::builtin().names();
        for (const char* name :
             {"umlLifeline", "folder", "umlActor", "umlFrame", "cube", "component", "module", "message", "startState",
              "endState", "singleArrow", "doubleArrow", "datastore", "offPageConnector", "note2", "process2",
              "curlyBracket", "crossbar", "lineEllipse", "ext", "waypoint", "dimension", "mxgraph.bpmn.task2",
              "mxgraph.bpmn.gateway2", "mxgraph.bpmn.event", "mxgraph.bpmn.shape", "mxgraph.basic.rect",
              "mxgraph.arrows2.arrow", "mxgraph.arrows2.twoWayArrow", "mxgraph.floorplan.wall",
              "mxgraph.floorplan.wallCorner", "mxgraph.floorplan.wallU", "mxgraph.floorplan.window", "mxgraph.floorplan.dimension",
              "mxgraph.floorplan.doorLeft", "mxgraph.floorplan.doorRight", "mxgraph.floorplan.doorDouble",
              "curvedText"})
            QVERIFY2(std::find(names.begin(), names.end(), name) != names.end(), name);
    }

    // mxFloorplanWallU: a band of wallThickness around three sides, not the whole box (FIDELITY class 3).
    void wallUIsABandOpenAtTheBottom()
    {
        const RecordingCanvas c = paintShape("mxgraph.floorplan.wallU", makeStyle({{"wallThickness", "12"}}), 200, 100);
        QCOMPARE(count(c, "moveTo"), 1);
        QVERIFY(has(c, "moveTo 0 100"));
        QVERIFY(has(c, "lineTo 188 100"));
        QVERIFY(has(c, "lineTo 188 12"));
        QVERIFY(has(c, "lineTo 12 12"));
        QVERIFY(has(c, "lineTo 12 100"));
        QVERIFY(has(c, "close"));
        QVERIFY(c.ops.back().rfind("fillAndStroke", 0) == 0);
    }

    void lifelineHeadAndDashedLine()
    {
        const RecordingCanvas c = paintShape("umlLifeline", makeStyle({{"size", "40"}}), 100, 200);
        QVERIFY(has(c, "rect 0 0 100 40")); // the head only
        QVERIFY(has(c, "setDashed 1 0"));   // lifelineDashed defaults to 1
        QVERIFY(has(c, "moveTo 50 40"));
        QVERIFY(has(c, "lineTo 50 200"));
    }

    void lifelineMirrorRepeatsTheHead()
    {
        const RecordingCanvas c =
            paintShape("umlLifeline", makeStyle({{"size", "40"}, {"lifelineMirror", "1"}}), 100, 200);
        QVERIFY(has(c, "rect 0 0 100 40"));
        QVERIFY(has(c, "rect 0 160 100 40"));
        QVERIFY(has(c, "lineTo 50 160")); // the line stops at the foot box
    }

    void cubeOutlineAndDarkFaces()
    {
        const RecordingCanvas c = paintShape("cube", makeStyle({{"size", "20"}, {"darkOpacity", "0.4"}}), 100, 60);
        QVERIFY(has(c, "lineTo 80 0"));
        QVERIFY(has(c, "lineTo 100 20"));
        QVERIFY(has(c, "setFillAlpha 0.4")); // the top face
        QCOMPARE(count(c, "fillAndStroke"), 1);
        QCOMPARE(count(c, "stroke"), 1); // the three inner edges
    }

    void folderTabOnTheLeft()
    {
        const RecordingCanvas c =
            paintShape("folder", makeStyle({{"tabWidth", "40"}, {"tabHeight", "14"}, {"tabPosition", "left"}}), 100, 70);
        QVERIFY(has(c, "moveTo 0 14"));
        QVERIFY(has(c, "lineTo 40 0"));
    }

    void roundedFolderUsesArcs()
    {
        const RecordingCanvas c = paintShape("folder", makeStyle({{"rounded", "1"}}), 100, 70);
        QVERIFY(count(c, "arcTo") == 4);
    }

    void bpmnEventWithOutlineAndSymbol()
    {
        // catching: outer + inner ring; message: envelope rectangle + flap
        const RecordingCanvas c =
            paintShape("mxgraph.bpmn.event", makeStyle({{"outline", "catching"}, {"symbol", "message"}}), 50, 50);
        QCOMPARE(count(c, "ellipse"), 2);
        QCOMPARE(count(c, "rect"), 1);
        QVERIFY(has(c, "ellipse 2 2 46 46"));
    }

    void bpmnEndEventThickensTheStroke()
    {
        const RecordingCanvas c = paintShape(
            "mxgraph.bpmn.event", makeStyle({{"outline", "end"}, {"symbol", "terminate"}, {"strokeWidth", "2"}}), 50, 50);
        QVERIFY(has(c, "setStrokeWidth 6"));
        QVERIFY(has(c, "setStrokeWidth 2")); // back to the cell's width
    }

    void bpmnTaskDrawsFrameAndMarkers()
    {
        const auto plain = makeStyle({{"rectStyle", "rounded"}, {"size", "10"}});
        const auto marked = makeStyle(
            {{"rectStyle", "rounded"}, {"size", "10"}, {"taskMarker", "service"}, {"isLoopStandard", "1"}});
        const RecordingCanvas a = paintShape("mxgraph.bpmn.task2", plain, 100, 70);
        const RecordingCanvas b = paintShape("mxgraph.bpmn.task2", marked, 100, 70);
        QVERIFY(count(a, "arcTo") >= 4); // rounded frame corners
        // The loop and service stencils add paths on top of the frame.
        QVERIFY(b.paints > a.paints);
        QVERIFY(count(b, "arcTo") > count(a, "arcTo"));
        QVERIFY(has(b, "setStrokeWidth 1")); // markers at width 1
    }

    void bpmnGatewayExclusive()
    {
        const RecordingCanvas c = paintShape("mxgraph.bpmn.gateway2", makeStyle({{"gwType", "exclusive"}}), 50, 50);
        QCOMPARE(count(c, "fillAndStroke"), 2); // the diamond and the cross
        QCOMPARE(count(c, "close"), 2);
    }

    void floorplanWallThickness()
    {
        const RecordingCanvas c = paintShape("mxgraph.floorplan.wall", makeStyle({{"wallThickness", "20"}}), 100, 60);
        QVERIFY(has(c, "rect 0 20 100 20"));
    }

    void floorplanDoorSwing()
    {
        const RecordingCanvas c = paintShape("mxgraph.floorplan.doorRight", makeStyle({}), 60, 70);
        QVERIFY(has(c, "rect 0 0 60 5"));
        QCOMPARE(count(c, "arcTo"), 1);
    }

    void basicRectCorners()
    {
        const RecordingCanvas snip = paintShape("mxgraph.basic.rect", makeStyle({{"rectStyle", "snip"}, {"size", "10"}}), 100, 60);
        QVERIFY(has(snip, "moveTo 0 10"));
        QCOMPARE(count(snip, "arcTo"), 0);
        const RecordingCanvas round = paintShape("mxgraph.basic.rect", makeStyle({{"rectStyle", "rounded"}}), 100, 60);
        QVERIFY(count(round, "arcTo") >= 4);
        const RecordingCanvas fold = paintShape("mxgraph.basic.rect", makeStyle({{"rectStyle", "fold"}, {"fillColor", "none"}}), 100, 60);
        QVERIFY(fold.paints > snip.paints - 1);
    }

    void arrows2Arrow()
    {
        const RecordingCanvas c = paintShape(
            "mxgraph.arrows2.arrow", makeStyle({{"dy", "0.6"}, {"dx", "30"}, {"notch", "15"}}), 110, 60);
        QVERIFY(has(c, "lineTo 15 30")); // notch at the tail
        QVERIFY(has(c, "moveTo 0 18"));  // shaft top: h * 0.5 * 0.6
    }

    void messageAndStatesAndArrows()
    {
        QVERIFY(has(paintShape("message", makeStyle({}), 60, 40), "lineTo 30 20"));
        QCOMPARE(count(paintShape("endState", makeStyle({}), 40, 40), "ellipse"), 2);
        QCOMPARE(count(paintShape("startState", makeStyle({}), 40, 40), "ellipse"), 1);
        // Polygons go to the canvas as one prepared path (mxShape.addPoints).
        QCOMPARE(count(paintShape("singleArrow", makeStyle({}), 100, 50), "addPath"), 1);
        QCOMPARE(count(paintShape("doubleArrow", makeStyle({}), 100, 50), "addPath"), 1);
    }

    void extDrawsSymbols()
    {
        const RecordingCanvas c = paintShape(
            "ext", makeStyle({{"double", "1"}, {"symbol0", "umlActor"}, {"symbol0Width", "10"}, {"symbol0Height", "20"}}),
            100, 60);
        QCOMPARE(count(c, "ellipse"), 1); // the actor's head
        QVERIFY(count(c, "rect") >= 1);
    }

    // --- curvedText (Shapes.js CurvedTextShape) ---------------------------

    void curvedTextPaintsTheRectangleAndTheValueAlongAnArc()
    {
        // 70x50, font size 11: inset 5.5, the curve spans (5.5, 37.5) to
        // (64.5, 37.5) through the middle point (35, 12.5).
        const RecordingCanvas c = paintCurvedText(makeStyle({{"strokeColor", "none"}, {"fillColor", "none"}}), 70, 50);
        QVERIFY(has(c, "rect 0 0 70 50"));
        QVERIFY(has(c, "fillAndStroke"));
        QCOMPARE(c.pathTexts.size(), std::size_t(1));
        const render::PathText& t = c.pathTexts.front();
        QCOMPARE(t.text, std::string("Curved Text"));
        QCOMPARE(t.align, geometry::HAlign::Center);
        const auto& elements = t.path.elements();
        QCOMPARE(elements.front().p[0], (geometry::Point{5.5, 37.5}));
        QVERIFY(elements.size() > 2); // moveTo + the arc as cubics, not a line
        QCOMPARE(elements[1].kind, geometry::PathElement::Kind::CubicTo);
        QVERIFY(near(elements.back().p[2], {64.5, 37.5}));
        // Arc of radius 29.905 over a 59 chord with a 25 sagitta (R*theta).
        QVERIFY(std::abs(t.textLength - 84.095) < 0.01);
        QCOMPARE(t.fontSize, 11.0);
    }

    void curvedTextIsALineWhenTheSagittaIsBelowHalfAUnit()
    {
        const RecordingCanvas c = paintCurvedText(makeStyle({{"arcMidY", "0.2"}}), 70, 50);
        const render::PathText& t = c.pathTexts.front();
        QCOMPARE(t.path.elements().size(), std::size_t(2)); // moveTo + lineTo
        QCOMPARE(t.path.elements().back().kind, geometry::PathElement::Kind::LineTo);
        QVERIFY(std::abs(t.textLength - 59) < 1e-9);
    }

    void curvedTextHonoursArcValuesVerticalAlignAndClamping()
    {
        // arcStartY 10, mid offset -10 (middle at 10), arcEndY 30: the curve
        // spans y 10..30, so verticalAlign=top starts at y = inset.
        auto c = paintCurvedText(makeStyle({{"arcStartY", "10"}, {"arcMidY", "-10"}, {"arcEndY", "30"},
                                            {"verticalAlign", "top"}}),
                                 70, 50);
        auto elements = c.pathTexts.front().path.elements();
        QCOMPARE(elements.front().p[0], (geometry::Point{5.5, 5.5}));
        QVERIFY(near(elements.back().p[2], {64.5, 25.5}));
        // bottom: the lowest point of the curve (30) sits at h - inset.
        c = paintCurvedText(makeStyle({{"arcStartY", "10"}, {"arcMidY", "-10"}, {"arcEndY", "30"},
                                       {"verticalAlign", "bottom"}}),
                            70, 50);
        elements = c.pathTexts.front().path.elements();
        QCOMPARE(elements.front().p[0], (geometry::Point{5.5, 24.5}));
        QVERIFY(near(elements.back().p[2], {64.5, 44.5}));
        // The middle offset is clamped to the default range (25): -100 is -25.
        const auto defaults = paintCurvedText(makeStyle({}), 70, 50).pathTexts.front();
        const auto clamped = paintCurvedText(makeStyle({{"arcMidY", "-100"}}), 70, 50).pathTexts.front();
        QCOMPARE(clamped.path, defaults.path);
    }

    void curvedTextOtherCurveTypesAreAQuadraticBezier()
    {
        const RecordingCanvas c = paintCurvedText(makeStyle({{"curveType", "quad"}}), 70, 50);
        const auto& elements = c.pathTexts.front().path.elements();
        QCOMPARE(elements.size(), std::size_t(2));
        QCOMPARE(elements.back().kind, geometry::PathElement::Kind::QuadTo);
        QCOMPARE(elements.back().p[0], (geometry::Point{35, 12.5})); // control point
        QCOMPARE(elements.back().p[1], (geometry::Point{64.5, 37.5}));
    }

    void curvedTextAlignmentFontAndHiddenValue()
    {
        for (const auto& [align, expected] :
             {std::pair{"left", geometry::HAlign::Left}, {"center", geometry::HAlign::Center},
              {"right", geometry::HAlign::Right}}) {
            const auto c = paintCurvedText(makeStyle({{"align", align}}), 70, 50);
            QCOMPARE(c.pathTexts.front().align, expected);
        }
        const auto styled = paintCurvedText(makeStyle({{"fontSize", "20"}, {"fontStyle", "3"}, {"fontColor", "#ff0000"},
                                                       {"fontFamily", "Courier New"}}),
                                            200, 100)
                                .pathTexts.front();
        QCOMPARE(styled.fontSize, 20.0);
        QCOMPARE(styled.fontStyle, 3);
        QCOMPARE(styled.fontColor, QColor(255, 0, 0));
        QCOMPARE(styled.fontFamily, QStringLiteral("Courier New"));
        // The rectangle is still painted without a value, while the editor
        // has it, and with an empty value.
        for (const RecordingCanvas& c :
             {paintCurvedText(makeStyle({}), 70, 50, "", false), paintCurvedText(makeStyle({}), 70, 50, "x", true)}) {
            QVERIFY(c.pathTexts.empty());
            QVERIFY(has(c, "fillAndStroke"));
        }
    }
};

QTEST_MAIN(ShapesJsTest)
#include "tst_shapes_js.moc"
