// The shared vertex paint pipeline (mxShape.paint/configureCanvas/
// getLabelBounds): canvas configuration, direction and flip handling, and the
// default label bounds. Uses test shapes, so it needs no other package.

#include "qraw/render/shape.h"

#include "support/recording_canvas.h"

#include <QTest>

#include <stdexcept>

using namespace qraw;
using render::Rect;
using render::ResolvedStyle;
using render::StyleMap;

namespace {

ResolvedStyle makeStyle(std::initializer_list<std::pair<const char*, const char*>> values)
{
    StyleMap map;
    for (const auto& [key, value] : values)
        map.set(key, value);
    return ResolvedStyle(std::move(map), false);
}

render::ShapePaintContext contextFor(const ResolvedStyle& style)
{
    return {style, nullptr, {}, false}; // style, images, children, collapsed
}

// Records the bounds it is asked to paint.
class RecordingShape : public render::VertexShape {
public:
    explicit RecordingShape(bool handlesDirection = false) : handlesDirection_(handlesDirection) {}

    void paint(render::Canvas& c, const render::ShapePaintContext&, double x, double y, double w,
               double h) const override
    {
        painted = Rect{x, y, w, h};
        ++calls;
        c.rect(x, y, w, h);
    }
    [[nodiscard]] bool handlesDirection() const override { return handlesDirection_; }
    [[nodiscard]] std::optional<geometry::Insets> labelMargins(const ResolvedStyle&, const Rect&) const override
    {
        return margins;
    }

    mutable Rect painted;
    mutable int calls = 0;
    std::optional<geometry::Insets> margins;

private:
    bool handlesDirection_;
};

class ThrowingShape final : public render::VertexShape {
public:
    void paint(render::Canvas&, const render::ShapePaintContext&, double, double, double, double) const override
    {
        throw std::runtime_error("boom");
    }
};

} // namespace

class ShapePipelineTest : public QObject {
    Q_OBJECT

private slots:
    void configureCanvasSequence()
    {
        const ResolvedStyle style = makeStyle({{"opacity", "50"},
                                               {"dashed", "1"},
                                               {"fillColor", "#ff0000"},
                                               {"gradientColor", "#0000ff"},
                                               {"linecap", "round"},
                                               {"strokeColor", "#000000"},
                                               {"strokeWidth", "2"}});
        test::RecordingCanvas c;
        render::configureCanvas(c, style, Rect{1, 2, 30, 40});
        const std::string expected =
            "setAlpha 0.5\nsetFillAlpha 1\nsetStrokeAlpha 1\nsetShadow 0\nsetDashed 1 0\n"
            "setGradient #ff0000 #0000ff 1 2 30 40 0 1 1\nsetLineCap 1\nsetStrokeColor #000000\nsetStrokeWidth 2\n";
        QCOMPARE(QString::fromStdString(c.text()), QString::fromStdString(expected));
    }

    void configureCanvasWithoutGradientUsesFillColor()
    {
        const ResolvedStyle style =
            makeStyle({{"fillColor", "#00ff00"}, {"dashPattern", "4 2"}, {"shadow", "1"}, {"miterlimit", "7"}});
        test::RecordingCanvas c;
        render::configureCanvas(c, style, Rect{0, 0, 10, 10});
        QVERIFY(c.text().find("setFillColor #00ff00") != std::string::npos);
        QVERIFY(c.text().find("setGradient") == std::string::npos);
        QVERIFY(c.text().find("setShadow 1") != std::string::npos);
        QVERIFY(c.text().find("setDashPattern 4 2") != std::string::npos);
        QVERIFY(c.text().find("setMiterLimit 7") != std::string::npos);
        // linecap/linejoin are only set when the style has them
        QVERIFY(c.text().find("setLineCap") == std::string::npos);
        QVERIFY(c.text().find("setLineJoin") == std::string::npos);
    }

    void rotationAndFlipRotateAroundTheCenter()
    {
        const ResolvedStyle style = makeStyle({{"rotation", "30"}, {"flipH", "1"}});
        RecordingShape shape;
        test::RecordingCanvas c;
        render::paintVertex(c, shape, contextFor(style), Rect{10, 20, 100, 40});
        QCOMPARE(c.ops.front(), std::string("save"));
        QCOMPARE(c.ops[1], std::string("rotate 30 1 0 60 40"));
        QCOMPARE(c.ops.back(), std::string("restore"));
        QCOMPARE(shape.calls, 1);
        QCOMPARE(shape.painted, (Rect{10, 20, 100, 40}));
    }

    void noTransformWithoutRotationOrFlip()
    {
        const ResolvedStyle style = makeStyle({});
        RecordingShape shape;
        test::RecordingCanvas c;
        render::paintVertex(c, shape, contextFor(style), Rect{0, 0, 10, 10});
        QVERIFY(c.opsStartingWith("rotate").empty());
    }

    void directionSouthInvertsBoundsOfPlainShapes()
    {
        const ResolvedStyle style = makeStyle({{"direction", "south"}});
        RecordingShape shape;
        test::RecordingCanvas c;
        render::paintVertex(c, shape, contextFor(style), Rect{0, 0, 100, 40});
        // t = (100 - 40) / 2: x += t, y -= t, w and h swap.
        QCOMPARE(shape.painted, (Rect{30, -30, 40, 100}));
        QCOMPARE(c.ops[1], std::string("rotate 90 0 0 50 20"));
    }

    void directionIsLeftToShapesThatHandleIt()
    {
        const ResolvedStyle style = makeStyle({{"direction", "south"}});
        RecordingShape shape(/*handlesDirection=*/true);
        test::RecordingCanvas c;
        render::paintVertex(c, shape, contextFor(style), Rect{0, 0, 100, 40});
        QCOMPARE(shape.painted, (Rect{0, 0, 100, 40}));
        QCOMPARE(c.ops[1], std::string("rotate 90 0 0 50 20")); // the rotation still applies
    }

    void directionAnglesAndFlipSwapForVerticalDirections()
    {
        {
            const ResolvedStyle style = makeStyle({{"direction", "north"}, {"rotation", "10"}});
            RecordingShape shape;
            test::RecordingCanvas c;
            render::paintVertex(c, shape, contextFor(style), Rect{0, 0, 40, 40});
            QCOMPARE(c.ops[1], std::string("rotate 280 0 0 20 20"));
        }
        {
            const ResolvedStyle style = makeStyle({{"direction", "west"}});
            RecordingShape shape;
            test::RecordingCanvas c;
            render::paintVertex(c, shape, contextFor(style), Rect{0, 0, 40, 20});
            QCOMPARE(c.ops[1], std::string("rotate 180 0 0 20 10"));
            QCOMPARE(shape.painted, (Rect{0, 0, 40, 20})); // west keeps the bounds
        }
        {
            // mxShape.apply swaps flipH/flipV when the direction is vertical.
            const ResolvedStyle style = makeStyle({{"direction", "south"}, {"flipH", "1"}});
            RecordingShape shape;
            test::RecordingCanvas c;
            render::paintVertex(c, shape, contextFor(style), Rect{0, 0, 40, 40});
            QCOMPARE(c.ops[1], std::string("rotate 90 0 1 20 20"));
        }
    }

    void emptyOrInvalidBoundsPaintNothing()
    {
        const ResolvedStyle style = makeStyle({});
        RecordingShape shape;
        const double nan = std::numeric_limits<double>::quiet_NaN();
        for (const Rect& bounds : {Rect{0, 0, 0, 10}, Rect{0, 0, 10, -1}, Rect{0, 0, nan, 10}, Rect{nan, 0, 10, 10}}) {
            test::RecordingCanvas c;
            render::paintVertex(c, shape, contextFor(style), bounds);
            QVERIFY(c.ops.empty());
        }
        QCOMPARE(shape.calls, 0);
    }

    void aThrowingPainterStillRestoresTheCanvas()
    {
        const ResolvedStyle style = makeStyle({});
        ThrowingShape shape;
        test::RecordingCanvas c;
        render::paintVertex(c, shape, contextFor(style), Rect{0, 0, 10, 10});
        QCOMPARE(c.ops.front(), std::string("save"));
        QCOMPARE(c.ops.back(), std::string("restore"));
    }

    void defaultLabelBoundsWithoutMargins()
    {
        RecordingShape shape;
        QCOMPARE(shape.labelBounds(makeStyle({}), Rect{1, 2, 30, 40}), (Rect{1, 2, 30, 40}));
    }

    void labelBoundsAppliesMargins()
    {
        RecordingShape shape;
        shape.margins = geometry::Insets{/*top=*/5, /*left=*/10, /*bottom=*/8, /*right=*/20};
        const Rect rect{0, 0, 200, 100};
        QCOMPARE(shape.labelBounds(makeStyle({}), rect), (Rect{10, 5, 170, 87}));
        // flipH swaps left and right for horizontal directions
        QCOMPARE(shape.labelBounds(makeStyle({{"flipH", "1"}}), rect), (Rect{20, 5, 170, 87}));
        // south turns the margins: top = left, left = bottom, right = top, bottom = right
        QCOMPARE(shape.labelBounds(makeStyle({{"direction", "south"}}), rect), (Rect{8, 10, 187, 70}));
        // south with flipH swaps top and bottom first
        QCOMPARE(shape.labelBounds(makeStyle({{"direction", "south"}, {"flipH", "1"}}), rect), (Rect{5, 10, 187, 70}));
    }

    void labelMarginsAreClampedAndRounded()
    {
        RecordingShape shape;
        shape.margins = geometry::Insets{/*top=*/-5, /*left=*/10.6, /*bottom=*/500, /*right=*/0};
        // top clamps to 0, left rounds to 11, bottom clamps to the height
        QCOMPARE(shape.labelBounds(makeStyle({}), Rect{0, 0, 100, 50}), (Rect{11, 0, 89, 0}));
    }
};

QTEST_MAIN(ShapePipelineTest)
#include "tst_shape_pipeline.moc"
