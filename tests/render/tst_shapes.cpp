// The native shape painters, observed through the recording canvas. Exact
// rounded-geometry checks need the path builders.

#include "qraw/render/shape.h"

#include "support/recording_canvas.h"

#include <QTest>

#include <algorithm>

using namespace qraw;
using render::Rect;
using render::ResolvedStyle;
using render::ShapeChild;
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

const render::VertexShape& shapeNamed(const char* name)
{
    const render::VertexShape* shape = ShapeRegistry::builtin().find(name);
    if (!shape)
        qFatal("shape %s is not registered", name);
    return *shape;
}

// Paints `name` at (0,0,w,h) straight through the shape (no pipeline).
RecordingCanvas paintShape(const char* name, const ShapePaintContext& context, double w = 100, double h = 60)
{
    RecordingCanvas c;
    shapeNamed(name).paint(c, context, 0, 0, w, h);
    return c;
}

RecordingCanvas paintShape(const char* name, const ResolvedStyle& style, double w = 100, double h = 60)
{
    return paintShape(name, ShapePaintContext{style, nullptr, {}, false}, w, h);
}

bool contains(const RecordingCanvas& c, const std::string& op)
{
    return std::find(c.ops.begin(), c.ops.end(), op) != c.ops.end();
}

const QByteArray kPngBase64 =
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==";

} // namespace

class ShapesTest : public QObject {
    Q_OBJECT

private slots:
    void registryHasEveryShape()
    {
        const auto& registry = ShapeRegistry::builtin();
        for (const char* name :
             {"rectangle", "label", "ellipse", "doubleEllipse", "rhombus", "triangle", "hexagon", "cylinder",
              "cylinder3", "actor", "cloud", "line", "image", "swimlane", "table", "tableRow", "partialRectangle",
              "process", "document", "parallelogram", "trapezoid", "step", "card", "tape", "note",
              "internalStorage", "manualInput", "cross", "plus", "callout", "text", "connector"})
            QVERIFY2(registry.find(name), name);
        QVERIFY(!registry.find("nope"));
        QVERIFY(!registry.find("Rectangle")); // case-sensitive
        const auto names = registry.names();
        QVERIFY(std::is_sorted(names.begin(), names.end()));
    }

    void everyShapePaintsWithoutThrowing()
    {
        const ResolvedStyle style = makeStyle({{"rounded", "1"}});
        for (const std::string& name : ShapeRegistry::builtin().names()) {
            RecordingCanvas c;
            QVERIFY2(([&] {
                         ShapeRegistry::builtin().find(name)->paint(c, ShapePaintContext{style, nullptr, {}, false},
                                                                    0, 0, 80, 40);
                         return true;
                     })(),
                     name.c_str());
        }
    }

    void textAndConnectorPaintNothing()
    {
        QVERIFY(paintShape("text", makeStyle({})).ops.empty());
        QVERIFY(paintShape("connector", makeStyle({})).ops.empty());
    }

    void fallbackIsRectangleWithMarker()
    {
        RecordingCanvas c;
        ShapeRegistry::builtin().fallback().paint(c, ShapePaintContext{makeStyle({}), nullptr, {}, false}, 0, 0, 100,
                                                  60);
        QCOMPARE(c.ops.front(), std::string("rect 0 0 100 60"));
        QVERIFY(contains(c, "setShadow 0"));
        QVERIFY(contains(c, "setStrokeAlpha 0.5"));
        QVERIFY(contains(c, "moveTo 92 0"));
        QVERIFY(contains(c, "lineTo 100 8"));
    }

    void upstreamOnlyNamesAreNotRegistered()
    {
        // The list of names Qraw cannot paint yet must not contain a registered shape.
        const auto names = ShapeRegistry::builtin().names();
        for (const auto& name : names) {
            if (!name.starts_with("mxgraph.")) // the prefix covers stencils; some are native
                QVERIFY2(!ShapeRegistry::isUpstreamOnly(name), name.c_str());
        }
        QVERIFY(ShapeRegistry::isUpstreamOnly("xor"));
        QVERIFY(ShapeRegistry::isUpstreamOnly("mxgraph.aws3d.ec2"));
        QVERIFY(!ShapeRegistry::isUpstreamOnly("rect"));
        QVERIFY(!ShapeRegistry::isUpstreamOnly("star"));
    }

    void rectangleRoundedArcSize()
    {
        const auto plain = paintShape("rectangle", makeStyle({}));
        QCOMPARE(plain.ops.front(), std::string("rect 0 0 100 60"));
        // Relative: 15% of the smaller side.
        QCOMPARE(paintShape("rectangle", makeStyle({{"rounded", "1"}})).ops.front(),
                 std::string("roundrect 0 0 100 60 9 9"));
        QCOMPARE(paintShape("rectangle", makeStyle({{"rounded", "1"}, {"arcSize", "30"}})).ops.front(),
                 std::string("roundrect 0 0 100 60 18 18"));
        // Absolute: arcSize / 2 in pixels.
        QCOMPARE(paintShape("rectangle", makeStyle({{"rounded", "1"}, {"absoluteArcSize", "1"}, {"arcSize", "10"}}))
                     .ops.front(),
                 std::string("roundrect 0 0 100 60 5 5"));
    }

    void ellipseCall()
    {
        const auto c = paintShape("ellipse", makeStyle({}));
        QCOMPARE(c.ops.front(), std::string("ellipse 0 0 100 60"));
        QCOMPARE(c.paints, 1);
    }

    void rhombusIsOnePolygonPath()
    {
        const auto c = paintShape("rhombus", makeStyle({}));
        QVERIFY(!c.opsStartingWith("addPath").empty());
        QCOMPARE(c.paints, 1);
        QCOMPARE(c.opsStartingWith("addPath").front(), std::string("addPath 5")); // 4 points, closed
    }

    void processInnerLines()
    {
        const auto c = paintShape("process", makeStyle({}));
        QCOMPARE(c.ops.front(), std::string("rect 0 0 100 60"));
        QVERIFY(contains(c, "moveTo 10 0"));
        QVERIFY(contains(c, "lineTo 10 60"));
        QVERIFY(contains(c, "moveTo 90 0"));
        QVERIFY(contains(c, "lineTo 90 60"));
        // Fixed size: pixels instead of a fraction of the width.
        const auto fixed = paintShape("process", makeStyle({{"size", "25"}, {"fixedSize", "1"}}));
        QVERIFY(contains(fixed, "moveTo 25 0"));
        QVERIFY(contains(fixed, "moveTo 75 0"));
    }

    void documentWaveEndpoints()
    {
        const auto c = paintShape("document", makeStyle({}));
        QCOMPARE(c.ops.front(), std::string("translate 0 0"));
        QVERIFY(contains(c, "lineTo 100 51"));   // right side stops at h - dy/2
        QVERIFY(contains(c, "quadTo 75 34.8 50 51"));
        QVERIFY(contains(c, "quadTo 25 67.2 0 51"));
        QVERIFY(contains(c, "lineTo 0 9"));
        QCOMPARE(c.ops.back(), std::string("fillAndStroke"));
    }

    void swimlaneHorizontal()
    {
        const auto c = paintShape("swimlane", makeStyle({{"startSize", "23"}, {"swimlaneLine", "1"}}));
        // Title: a stroked-and-filled U of height startSize, then the body.
        QVERIFY(contains(c, "moveTo 0 23"));
        QVERIFY(contains(c, "lineTo 100 0"));
        QVERIFY(contains(c, "lineTo 100 23"));
        QVERIFY(contains(c, "lineTo 0 60"));
        // swimlaneLine: the separator between title and body.
        const auto without = paintShape("swimlane", makeStyle({{"startSize", "23"}, {"swimlaneLine", "0"}}));
        QCOMPARE(c.ops.size(), without.ops.size() + 4); // begin, moveTo, lineTo, stroke
        QCOMPARE(c.ops[c.ops.size() - 3], std::string("moveTo 0 23"));
        QCOMPARE(c.ops[c.ops.size() - 2], std::string("lineTo 100 23"));
    }

    void swimlaneVertical()
    {
        const auto c = paintShape("swimlane", makeStyle({{"startSize", "23"}, {"horizontal", "0"}}));
        QVERIFY(contains(c, "moveTo 23 0"));
        QVERIFY(contains(c, "lineTo 0 60"));
        QVERIFY(contains(c, "lineTo 23 60"));
        QVERIFY(contains(c, "lineTo 100 60"));
        QCOMPARE(c.ops[c.ops.size() - 3], std::string("moveTo 23 0"));
        QCOMPARE(c.ops[c.ops.size() - 2], std::string("lineTo 23 60"));
    }

    void swimlaneCollapsedPaintsTitleOnly()
    {
        const ResolvedStyle style = makeStyle({{"startSize", "23"}});
        const auto c = paintShape("swimlane", ShapePaintContext{style, nullptr, {}, true});
        QVERIFY(contains(c, "lineTo 100 23"));
        QVERIFY(!contains(c, "lineTo 0 60")); // no body
        QCOMPARE(c.paints, 2); // title + divider
    }

    void swimlaneLabelBoundsIsTitleArea()
    {
        const auto& shape = shapeNamed("swimlane");
        const Rect r = shape.labelBounds(makeStyle({{"startSize", "23"}}), {10, 20, 100, 60});
        QCOMPARE(r.x, 10.0);
        QCOMPARE(r.y, 20.0);
        QCOMPARE(r.width, 100.0);
        QCOMPARE(r.height, 23.0);
        // Rotated direction: the title is on the side (the label layout rotates
        // the text for horizontal=0, as upstream's state.text does).
        const Rect v = shape.labelBounds(makeStyle({{"startSize", "23"}, {"direction", "south"}}), {10, 20, 100, 60});
        QCOMPARE(v.width, 23.0);
        QCOMPARE(v.height, 60.0);
    }

    void tableGridLinesAtRowAndCellBoundaries()
    {
        const ResolvedStyle cellStyle = makeStyle({});
        std::vector<ShapeChild> rows;
        for (int r = 0; r < 2; ++r) {
            ShapeChild row;
            row.id = "row";
            row.bounds = {0, r * 30.0, 100, 30};
            row.style = &cellStyle;
            for (int i = 0; i < 2; ++i) {
                ShapeChild cell;
                cell.id = "cell";
                cell.bounds = {i * 50.0, 0, 50, 30};
                cell.style = &cellStyle;
                row.children.push_back(cell);
            }
            rows.push_back(row);
        }
        const ResolvedStyle table = makeStyle({{"startSize", "0"}});
        const auto c = paintShape("table", ShapePaintContext{table, nullptr, [&] { return rows; }, false});
        // One row line at y=30 and one column line at x=50.
        QVERIFY(contains(c, "moveTo 0 30"));
        QVERIFY(contains(c, "lineTo 100 30"));
        QVERIFY(contains(c, "moveTo 50 0"));
        QVERIFY(contains(c, "lineTo 50 60"));

        // Without children: the outline only.
        const auto outline = paintShape("table", table);
        QVERIFY(!contains(outline, "moveTo 0 30"));
        QVERIFY(outline.paints >= 1);
    }

    void partialRectangleOmitsDisabledSides()
    {
        const auto all = paintShape("partialRectangle", makeStyle({}));
        QVERIFY(contains(all, "lineTo 100 0"));
        const auto c = paintShape("partialRectangle", makeStyle({{"top", "0"}}));
        QVERIFY(!contains(c, "lineTo 100 0"));
        QVERIFY(contains(c, "lineTo 100 60"));
        QVERIFY(contains(c, "lineTo 0 60"));
        QVERIFY(contains(c, "lineTo 0 0"));
    }

    void imageShapeDrawsResolvedDataUri()
    {
        const std::string uri = "data:image/png;base64," + kPngBase64.toStdString();
        const ResolvedStyle style = makeStyle({{"image", uri.c_str()}});
        const render::ImageResolveContext images;
        const auto c = paintShape("image", ShapePaintContext{style, &images, {}, false});
        QVERIFY(contains(c, "image 0 0 100 60 image/png 1 0 0"));
    }

    void imageShapeWithoutImageDrawsPlaceholder()
    {
        const ResolvedStyle style = makeStyle({{"image", "https://example.com/a.png"}});
        const render::ImageResolveContext images;
        const auto c = paintShape("image", ShapePaintContext{style, &images, {}, false});
        QVERIFY(c.opsStartingWith("image").empty());
        QVERIFY(c.paints >= 1);
    }

    void labelDrawsRectThenImageAtLeft()
    {
        const std::string uri = "data:image/png;base64," + kPngBase64.toStdString();
        const ResolvedStyle style = makeStyle({{"image", uri.c_str()}, {"imageWidth", "20"}, {"imageHeight", "20"}});
        const render::ImageResolveContext images;
        const auto c = paintShape("label", ShapePaintContext{style, &images, {}, false});
        QCOMPARE(c.ops.front(), std::string("rect 0 0 100 60"));
        // spacing (2) + 5 from the left, vertically centered.
        QCOMPARE(c.opsStartingWith("image").size(), std::size_t(1));
        QCOMPARE(c.opsStartingWith("image").front(), std::string("image 7 20 20 20 image/png 0 0 0"));
        // spacing is honored.
        const ResolvedStyle spaced =
            makeStyle({{"image", uri.c_str()}, {"imageWidth", "20"}, {"imageHeight", "20"}, {"spacing", "10"}});
        const auto s = paintShape("label", ShapePaintContext{spaced, &images, {}, false});
        QCOMPARE(s.opsStartingWith("image").front(), std::string("image 15 20 20 20 image/png 0 0 0"));
    }

    void processLabelMargins()
    {
        const Rect r = shapeNamed("process").labelBounds(makeStyle({}), {0, 0, 100, 60});
        QCOMPARE(r.x, 10.0);
        QCOMPARE(r.width, 80.0);
    }
};

QTEST_MAIN(ShapesTest)
#include "tst_shapes.moc"
