// The content layer of the canvas: tile geometry, the tile cache, and what a
// paint does and does not rasterise (docs/BENCHMARKS.md, "Canvas paint benchmark").

#include "support/test_support.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_structure.h"
#include "qraw/render/compositor.h"
#include "qraw/render/page_view.h"
#include "qraw/render/stats.h"
#include "qraw/render/tile_cache.h"
#include "qraw/render/tile_rasterizer.h"

#include <QEvent>
#include <QPaintEvent>
#include <QPainter>
#include <QRegion>
#include <QScrollBar>
#include <QSignalSpy>

#include <cmath>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;
namespace ops = qraw::commands::ops;
using render::stats::Counter;

namespace {

// Records the paint events of the viewport: how many and which region.
class PaintLog : public QObject {
public:
    explicit PaintLog(QWidget* viewport) : QObject(viewport) { viewport->installEventFilter(this); }
    bool eventFilter(QObject*, QEvent* event) override
    {
        if (event->type() == QEvent::Paint) {
            ++paints;
            region += static_cast<QPaintEvent*>(event)->region();
        }
        return false;
    }
    void clear()
    {
        paints = 0;
        region = QRegion();
    }
    int paints = 0;
    QRegion region;
};

// Everything the previous steps scheduled is painted.
void settle(CanvasView& view)
{
    QTest::qWait(30);
    view.viewport()->repaint(); // a paint even when nothing was scheduled
    QTest::qWait(10);
}

std::uint64_t count(Counter c)
{
    return render::stats::get(c);
}

// 1,500 vertices in a 50 x 30 grid: several screens of content.
std::string wideDiagram()
{
    std::string xml = R"(<mxfile><diagram id="w" name="Wide"><mxGraphModel grid="1" gridSize="10" page="1" pageWidth="827" pageHeight="1169"><root><mxCell id="0"/><mxCell id="1" parent="0"/>)";
    for (int i = 0; i < 1500; ++i) {
        xml += "<mxCell id=\"v" + std::to_string(i) + "\" value=\"V" + std::to_string(i)
            + "\" style=\"rounded=1;\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"" + std::to_string((i % 50) * 80)
            + "\" y=\"" + std::to_string((i / 50) * 60) + "\" width=\"60\" height=\"40\" as=\"geometry\"/></mxCell>";
    }
    xml += "</root></mxGraphModel></diagram></mxfile>";
    return xml;
}

// A page without page tiles or grid: a flat white background, so that the
// pixels compared below are the cells' alone.
std::string flatDiagram()
{
    std::string xml = kSimpleDiagram;
    const auto replace = [&](const std::string& from, const std::string& to) {
        const auto at = xml.find(from);
        if (at != std::string::npos)
            xml.replace(at, from.size(), to);
    };
    replace("grid=\"1\"", "grid=\"0\"");
    replace("page=\"1\"", "page=\"0\"");
    return xml;
}

// The visible tiles of `view` at its raster scale.
render::TileRange visibleTiles(const CanvasView& view)
{
    const QRectF scene = view.mapToScene(view.viewport()->rect()).boundingRect();
    const double s = view.rasterScale();
    return render::tilesCoveringDevice(QRectF(scene.left() * s, scene.top() * s, scene.width() * s, scene.height() * s));
}

// The viewport painted cell by cell, without tiles, at the viewport's device resolution: what
// the composited viewport has to equal. Scene (0, 0) sits at the whole device pixel the tiles
// are placed from (`round(dx * dpr)`: at a fractional device pixel ratio the scroll offset in
// logical pixels is not a whole number of device pixels), and the scene is scaled by
// zoom x dpr, as the tile rasteriser does.
//
// The picture is painted with a margin on the left and above, and cropped. Qt's raster engine places a
// glyph whose origin is at a negative device coordinate by a different rounding than one at a positive
// coordinate (the same text moved right by whole pixels differs by up to a quarter pixel of
// antialiasing, which shows with fonts that position glyphs at fractions of a pixel, such as a variable
// font at a ratio of 1.25), so a label cut by the viewport's edge would differ from the tiles, whose
// pixels around it are all at positive coordinates.
QImage directRender(CanvasView& view, const QSize& size, const QColor& background)
{
    constexpr int kMargin = 128;
    const double dpr = view.viewport()->devicePixelRatioF();
    const QTransform viewTransform = view.viewportTransform();
    QImage direct(size + QSize(kMargin, kMargin), QImage::Format_ARGB32_Premultiplied);
    direct.fill(background);
    {
        QPainter painter(&direct);
        painter.translate(kMargin + std::lround(viewTransform.dx() * dpr), kMargin + std::lround(viewTransform.dy() * dpr));
        painter.scale(viewTransform.m11() * dpr, viewTransform.m22() * dpr);
        const DiagramScene& scene = view.diagramScene();
        for (const render::CellState& state : scene.pageView().states()) {
            render::PainterCanvas target(painter);
            target.setImageQuality(render::ImageQuality::Interactive);
            scene.renderer().paintShape(target, state, &scene.pageView(), false);
            scene.renderer().paintLabel(target, state);
        }
    }
    return direct.copy(kMargin, kMargin, size.width(), size.height());
}

int differingPixels(const QImage& a, const QImage& b)
{
    int different = 0;
    for (int y = 0; y < a.height(); ++y) {
        for (int x = 0; x < a.width(); ++x)
            different += a.pixel(x, y) != b.pixel(x, y) ? 1 : 0;
    }
    return different;
}

} // namespace

// Records where the overlay painter puts scene (0, 0) on the device.
class OriginProbe : public InteractionHandler {
public:
    void paintOverlay(QPainter& painter, const OverlayContext&) const override
    {
        origin = painter.deviceTransform().map(QPointF(0, 0));
        ++paints;
    }
    mutable QPointF origin;
    mutable int paints = 0;
};

class TestCanvasTiles : public QObject {
    Q_OBJECT

private slots:
    void tileGeometryHasIntegerDeviceOrigins_data()
    {
        QTest::addColumn<double>("zoom");
        QTest::addColumn<double>("dpr");
        QTest::newRow("1x") << 1.0 << 1.0;
        QTest::newRow("1x zoomed out") << 0.35 << 1.0;
        QTest::newRow("1.25x") << 1.0 << 1.25;
        QTest::newRow("1.25x zoomed") << 2.5 << 1.25;
        QTest::newRow("2x") << 1.0 << 2.0;
        QTest::newRow("2x zoomed out") << 0.15 << 2.0;
    }
    void tileGeometryHasIntegerDeviceOrigins()
    {
        QFETCH(double, zoom);
        QFETCH(double, dpr);
        const double scale = zoom * dpr;
        for (int tx = -3; tx <= 3; ++tx) {
            const render::TileKey key{scale, tx, 2};
            const QRect device = render::tileDeviceRect(key);
            QCOMPARE(device.size(), QSize(render::kTileSize, render::kTileSize));
            QCOMPARE(device.left(), tx * render::kTileSize);
            // The next tile starts where this one ends: no seam, no overlap.
            QCOMPARE(render::tileDeviceRect({scale, tx + 1, 2}).left(), device.right() + 1);
            // The scene area behind the pixels, scaled back, is the pixel rectangle.
            const QRectF scene = render::tileSceneRect(key);
            QVERIFY(std::abs(scene.left() * scale - device.left()) < 1e-6);
            QVERIFY(std::abs(scene.width() * scale - render::kTileSize) < 1e-6);
        }
        // A rectangle of exactly one tile covers one tile, one pixel more two.
        QCOMPARE(render::tilesCoveringDevice(QRectF(0, 0, 512, 512)).count(), 1LL);
        QCOMPARE(render::tilesCoveringDevice(QRectF(0, 0, 513, 512)).count(), 2LL);
        QCOMPARE(render::tilesCoveringDevice(QRectF(-1, -1, 2, 2)).count(), 4LL);
        // A change ending on a tile border reaches the next tile through the antialiasing slack.
        // (one pixel of slack on every side: three tiles per axis).
        const QRectF edge(0, 0, 512 / scale, 512 / scale);
        QCOMPARE(render::tilesCovering(edge, scale).count(), 9LL);
        const QRectF inner(8 / scale, 8 / scale, 400 / scale, 400 / scale);
        QCOMPARE(render::tilesCovering(inner, scale).count(), 1LL);

        // A rasterised tile is 512 device pixels square with the frame's ratio, opaque.
        const auto document = simpleDocument();
        const auto context = render::RenderContext::defaults();
        const render::PageView view = render::PageView::build(*document.page(0), context);
        const render::SceneRenderer renderer(context);
        render::SnapshotInput input;
        input.view = &view;
        input.renderer = &renderer;
        const auto snapshot = render::buildSnapshot(input);
        const QImage tile = render::rasterizeTile(*snapshot, {scale, 0, 0}, dpr);
        QCOMPARE(tile.size(), QSize(render::kTileSize, render::kTileSize));
        QCOMPARE(tile.devicePixelRatio(), dpr);
        QCOMPARE(tile.format(), QImage::Format_ARGB32_Premultiplied);
        for (const QPoint p : {QPoint(0, 0), QPoint(511, 0), QPoint(0, 511), QPoint(511, 511), QPoint(256, 256)})
            QCOMPARE(qAlpha(tile.pixel(p)), 255);
    }

    // The compositor puts a tile on the screen 1:1 at integer device pixels,
    // whatever the device pixel ratio of the target.
    void compositorBlitsTilesOneToOne_data()
    {
        QTest::addColumn<double>("dpr");
        QTest::newRow("1x") << 1.0;
        QTest::newRow("1.25x") << 1.25;
        QTest::newRow("2x") << 2.0;
    }
    void compositorBlitsTilesOneToOne()
    {
        QFETCH(double, dpr);
        QImage tile(render::kTileSize, render::kTileSize, QImage::Format_ARGB32_Premultiplied);
        for (int y = 0; y < tile.height(); ++y) {
            for (int x = 0; x < tile.width(); ++x)
                tile.setPixel(x, y, qRgb(x & 255, y & 255, (x * 7 + y * 13) & 255));
        }
        tile.setDevicePixelRatio(dpr);
        QImage target(QSize(700, 700), QImage::Format_ARGB32_Premultiplied);
        target.setDevicePixelRatio(dpr);
        target.fill(Qt::black);
        {
            QPainter painter(&target);
            painter.scale(1.5, 1.5); // the compositor ignores the painter's transform
            render::FrameDescription frame;
            frame.devicePixelRatio = dpr;
            frame.deviceSize = target.size();
            frame.tiles.push_back({{1, 0, 0}, 1, tile, QPoint(37, 21), QRect(QPoint(0, 0), tile.size()), QSize()});
            render::CpuCompositor(painter).present(frame);
        }
        const QImage device = target; // pixels, ratio aside
        int wrong = 0;
        for (int y = 0; y < 400; ++y) {
            for (int x = 0; x < 400; ++x) {
                if (device.pixel(37 + x, 21 + y) != tile.pixel(x, y))
                    ++wrong;
            }
        }
        QCOMPARE(wrong, 0);
        QCOMPARE(device.pixel(36, 21), qRgb(0, 0, 0)); // nothing outside the tile
        QCOMPARE(device.pixel(37, 20), qRgb(0, 0, 0));
    }

    // A tile of another zoom bucket (or a bucket between two zoom steps) is drawn
    // scaled into the rectangle the destination size gives, smooth, and
    // nothing outside it changes; two tiles whose edges are rounded to the same
    // device pixel leave no seam.
    void compositorScalesTilesToTheirDestination()
    {
        QImage left(render::kTileSize, render::kTileSize, QImage::Format_ARGB32_Premultiplied);
        left.fill(qRgb(200, 30, 30));
        QImage right = left;
        right.fill(qRgb(30, 30, 200));
        QImage target(QSize(700, 400), QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::black);
        {
            QPainter painter(&target);
            painter.setRenderHint(QPainter::Antialiasing, true); // the view's hint: the compositor must not blend edges with it
            render::FrameDescription frame;
            frame.devicePixelRatio = 1;
            frame.deviceSize = target.size();
            // Two neighbours of a bucket drawn at 0.6x: 307.2 px each, edges rounded (0, 307, 614 -> clipped).
            frame.tiles.push_back({{1, 0, 0}, 1, left, QPoint(10, 10), QRectF(left.rect()), QSize(307, 307)});
            frame.tiles.push_back({{1, 1, 0}, 1, right, QPoint(317, 10), QRectF(right.rect()), QSize(307, 307)});
            render::CpuCompositor(painter).present(frame);
        }
        QCOMPARE(target.pixel(9, 10), qRgb(0, 0, 0));
        QCOMPARE(target.pixel(10, 9), qRgb(0, 0, 0));
        QCOMPARE(target.pixel(10, 10), qRgb(200, 30, 30));
        QCOMPARE(target.pixel(316, 200), qRgb(200, 30, 30));
        QCOMPARE(target.pixel(317, 200), qRgb(30, 30, 200)); // no seam, no blended column
        QCOMPARE(target.pixel(623, 316), qRgb(30, 30, 200)); // the last pixel of the band
        QCOMPARE(target.pixel(624, 100), qRgb(0, 0, 0));
        QCOMPARE(target.pixel(200, 317), qRgb(0, 0, 0));
    }

    void damageMarksExactlyTheIntersectingTiles()
    {
        render::TileCache cache(std::size_t(1) << 30);
        const double scale = 1;
        for (int ty = 0; ty < 4; ++ty) {
            for (int tx = 0; tx < 4; ++tx)
                cache.put({scale, tx, ty}, QImage(8, 8, QImage::Format_ARGB32_Premultiplied), 1);
        }
        cache.put({2.0, 0, 0}, QImage(8, 8, QImage::Format_ARGB32_Premultiplied), 1); // another scale
        const auto dirtyTiles = [&] {
            std::vector<std::pair<int, int>> dirty;
            for (int ty = 0; ty < 4; ++ty) {
                for (int tx = 0; tx < 4; ++tx) {
                    if (cache.peek({scale, tx, ty})->dirty)
                        dirty.emplace_back(tx, ty);
                }
            }
            return dirty;
        };
        const auto fresh = [&] {
            for (int ty = 0; ty < 4; ++ty) {
                for (int tx = 0; tx < 4; ++tx)
                    cache.put({scale, tx, ty}, QImage(8, 8, QImage::Format_ARGB32_Premultiplied), 1);
            }
        };

        render::Damage inside;
        inside.add(QRectF(600, 600, 10, 10)); // inside tile (1, 1), clear of its borders
        QCOMPARE(cache.invalidate(inside, scale), std::size_t(1));
        QCOMPARE(dirtyTiles(), (std::vector<std::pair<int, int>>{{1, 1}}));
        QVERIFY(!cache.peek({2.0, 0, 0})->dirty);
        QVERIFY(cache.peek({scale, 1, 1})->image.width() == 8); // stale image kept
        fresh();

        render::Damage across; // spans the border between tiles (0, 0) and (1, 0)
        across.add(QRectF(500, 100, 24, 10));
        cache.invalidate(across, scale);
        QCOMPARE(dirtyTiles(), (std::vector<std::pair<int, int>>{{0, 0}, {1, 0}}));
        fresh();

        render::Damage nearBorder; // its last pixel is the border's: the slack reaches the next tile
        nearBorder.add(QRectF(100, 100, 412, 10));
        cache.invalidate(nearBorder, scale);
        QCOMPARE(dirtyTiles(), (std::vector<std::pair<int, int>>{{0, 0}, {1, 0}}));
        fresh();
        render::Damage clear; // ends one pixel before the border: it stays in the first tile
        clear.add(QRectF(100, 100, 411, 10));
        cache.invalidate(clear, scale);
        QCOMPARE(dirtyTiles(), (std::vector<std::pair<int, int>>{{0, 0}}));
        fresh();

        render::Damage all;
        all.everything = true;
        cache.invalidate(all, scale);
        QCOMPARE(dirtyTiles().size(), std::size_t(16));
        QVERIFY(cache.peek({2.0, 0, 0})->dirty); // everything is every tile
    }

    void cacheKeepsToItsBudgetAndNeverEvictsVisibleTiles()
    {
        const auto tile = [] { return QImage(64, 64, QImage::Format_ARGB32_Premultiplied); };
        const std::size_t one = std::size_t(tile().sizeInBytes());
        render::TileCache cache(3 * one);
        for (int i = 0; i < 3; ++i)
            cache.put({1, i, 0}, tile(), 1);
        QCOMPARE(cache.size(), std::size_t(3));

        cache.beginFrame();
        QVERIFY(cache.get({1, 0, 0}) != nullptr); // visible in this frame
        QVERIFY(cache.get({1, 2, 0}) != nullptr);
        cache.put({1, 3, 0}, tile(), 1); // over budget: the one tile nobody displays goes
        QCOMPARE(cache.size(), std::size_t(3));
        QVERIFY(cache.peek({1, 1, 0}) == nullptr);
        QVERIFY(cache.peek({1, 0, 0}) != nullptr);
        QVERIFY(cache.peek({1, 3, 0}) != nullptr);

        // Everything is visible: the budget is exceeded rather than a visible tile evicted.
        cache.put({1, 4, 0}, tile(), 1);
        cache.put({1, 5, 0}, tile(), 1);
        QVERIFY(cache.bytes() >= 4 * one);
        QVERIFY(cache.peek({1, 4, 0}) != nullptr && cache.peek({1, 5, 0}) != nullptr);

        // Least recently displayed goes first.
        cache.beginFrame();
        QVERIFY(cache.get({1, 5, 0}) != nullptr);
        cache.beginFrame();
        QVERIFY(cache.get({1, 4, 0}) != nullptr);
        cache.beginFrame();
        cache.put({1, 6, 0}, tile(), 1);
        QVERIFY(cache.peek({1, 4, 0}) != nullptr); // newest of the displayed ones
        QVERIFY(cache.peek({1, 0, 0}) == nullptr); // oldest
        QVERIFY(cache.bytes() <= 3 * one);

        cache.dropOtherScales(2);
        QCOMPARE(cache.size(), std::size_t(0)); // a scale change drops the other scale's tiles
    }

    // The composited viewport equals painting the cells directly with
    // SceneRenderer at zoom 1, pixel for pixel: tiles are replays of the same
    // calls on the same transform shifted by whole pixels, so nothing differs
    // at tile borders either (antialiased edges meet exactly).
    void compositedViewportEqualsADirectRender()
    {
        DocumentSession session(documentFromXml(flatDiagram()));
        auto canvas = makeCanvas(session, {700, 500});
        canvas->centerOn(QRectF(420, 120, 1, 1)); // content across tile borders
        settle(*canvas);
        const QImage shown = canvas->viewport()->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);

        const QImage direct = directRender(*canvas, shown.size(), Qt::white);
        QCOMPARE(shown.size(), direct.size());
        QCOMPARE(differingPixels(shown, direct), 0);
    }

    // A scroll of one logical pixel is 1.25 device pixels at 125 %: whatever the widget does with the
    // old pixels (it repaints instead of blitting a fractional shift), the picture after every
    // step is the direct render, with nothing smeared or left behind.
    void scrollingInLogicalPixelStepsKeepsThePictureEqualToADirectRender()
    {
        DocumentSession session(documentFromXml(flatDiagram()));
        auto canvas = makeCanvas(session, {700, 500});
        canvas->centerOn(QRectF(420, 120, 1, 1));
        (void)QTest::qWaitForWindowExposed(canvas.get());
        settle(*canvas);
        QScrollBar* bar = canvas->horizontalScrollBar();
        QScrollBar* vbar = canvas->verticalScrollBar();
        for (int step = 0; step < 7; ++step) {
            bar->setValue(bar->value() + 1);
            vbar->setValue(vbar->value() + (step % 2));
            settle(*canvas);
            const QImage shown = canvas->viewport()->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
            QCOMPARE(differingPixels(shown, directRender(*canvas, shown.size(), Qt::white)), 0);
        }
    }

    // Overlays (handles, outlines) are drawn in logical pixels; the tiles are placed at whole device
    // pixels. At a fractional device pixel ratio the two agree only when the overlay is moved to the
    // same whole device pixel (a scroll offset of 3 logical pixels is 3.75 device pixels at 125 %).
    void overlayIsDrawnFromTheWholeDevicePixelTheTilesUse()
    {
        DocumentSession session(documentFromXml(flatDiagram()));
        auto canvas = makeCanvas(session, {700, 500});
        auto probe = std::make_unique<OriginProbe>();
        OriginProbe* observed = probe.get();
        canvas->interaction().addHandler(std::move(probe), 1000);
        (void)QTest::qWaitForWindowExposed(canvas.get()); // repaint() paints nothing in a window that is not exposed
        const double dpr = canvas->viewport()->devicePixelRatioF();
        QScrollBar* bar = canvas->horizontalScrollBar();
        for (int step = 0; step < 6; ++step) {
            bar->setValue(bar->value() + 1); // one logical pixel: 1.25 device pixels at 125 %
            canvas->viewport()->repaint();
            QVERIFY(observed->paints > 0);
            const QTransform view = canvas->viewportTransform();
            QCOMPARE(observed->origin.x(), double(std::lround(view.dx() * dpr)));
            QCOMPARE(observed->origin.y(), double(std::lround(view.dy() * dpr)));
        }
    }

    void darkDiagramToggleRebuildsAndRasterizesAndMatchesADirectDarkRender()
    {
        DocumentSession session(documentFromXml(flatDiagram()));
        auto canvas = makeCanvas(session, {700, 500});
        canvas->centerOn(QRectF(220, 150, 1, 1));
        settle(*canvas);
        const auto grab = [&] {
            return canvas->viewport()->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied);
        };
        const QImage light = grab();
        QCOMPARE(differingPixels(light, directRender(*canvas, light.size(), Qt::white)), 0);

        // View > Dark Diagram: the view is built again in the dark scheme and the visible tiles are rasterised again.
        render::stats::reset();
        session.setDiagramDark(true);
        settle(*canvas);
        QVERIFY(session.diagramDark());
        QVERIFY(canvas->diagramScene().pageView().theme().dark);
        QVERIFY(count(Counter::tilesRasterized) > 0);
        QCOMPARE(count(Counter::pageViewBuilds), std::uint64_t(1));
        const QImage dark = grab();
        QVERIFY(differingPixels(light, dark) > 0);
        QCOMPARE(dark.pixelColor(2, 2), QColor(0x12, 0x12, 0x12)); // the page of the dark scheme
        QCOMPARE(differingPixels(dark, directRender(*canvas, dark.size(), QColor(0x12, 0x12, 0x12))), 0);

        // And back: pixel for pixel the light view again.
        session.setDiagramDark(false);
        settle(*canvas);
        QCOMPARE(differingPixels(grab(), light), 0);
    }

    void aPageThatSaysNoneStaysLightInTheDarkScheme()
    {
        std::string xml = flatDiagram();
        xml.replace(xml.find("<mxGraphModel"), 13, "<mxGraphModel adaptiveColors=\"none\"");
        DocumentSession session(documentFromXml(xml));
        auto canvas = makeCanvas(session, {700, 500});
        canvas->centerOn(QRectF(220, 150, 1, 1));
        settle(*canvas);
        const QImage light = canvas->viewport()->grab().toImage();
        session.setDiagramDark(true);
        settle(*canvas);
        QCOMPARE(differingPixels(canvas->viewport()->grab().toImage().convertToFormat(QImage::Format_ARGB32_Premultiplied),
                                 light.convertToFormat(QImage::Format_ARGB32_Premultiplied)),
                 0);
    }

    void restackExposesTheMovedCellAndRecordsNothing()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        DiagramScene& scene = canvas->diagramScene();
        QSignalSpy damaged(&scene, &DiagramScene::contentDamaged);
        render::stats::reset();
        QVERIFY(session.edit(QStringLiteral("Front"), [](commands::Transaction& tx) {
                    return ops::orderCells(tx, 0, {"a"}, ops::ZOrder::ToFront);
                }).ok);
        QCOMPARE(count(Counter::recordsBuilt), std::uint64_t(0)); // restack = expose, not re-record
        QCOMPARE(damaged.count(), 1);
        const auto damage = damaged.at(0).at(0).value<render::Damage>();
        QVERIFY(!damage.everything);
        QVERIFY(!damage.rects.empty());
        const QRectF a = scene.snapshot()->find("a")->visualBounds;
        QVERIFY(std::any_of(damage.rects.begin(), damage.rects.end(), [&](const QRectF& r) { return r.contains(a); }));
    }

    void hoverAndSelectionRasterizeNothingAndRepaintOnlyTheOverlay()
    {
        DocumentSession session(simpleDocument());
        auto canvas = makeCanvas(session);
        PaintLog log(canvas->viewport());
        settle(*canvas);

        // Hover: the arrows of vertex "a" appear around it.
        log.clear();
        render::stats::reset();
        move(*canvas, QPointF(100, 70), Qt::NoModifier, Qt::NoButton);
        QTest::qWait(30);
        QVERIFY(log.paints >= 1);
        QCOMPARE(count(Counter::tilesRasterized), std::uint64_t(0));
        QCOMPARE(count(Counter::cellsReplayed), std::uint64_t(0));
        QCOMPARE(count(Counter::cellsPainted), std::uint64_t(0));
        QCOMPARE(count(Counter::recordsBuilt), std::uint64_t(0));
        QCOMPARE(count(Counter::viewportFullUpdates), std::uint64_t(0));
        const QRect viewportRect = canvas->viewport()->rect();
        QVERIFY(log.region.boundingRect().width() < viewportRect.width());
        QVERIFY(log.region.boundingRect().height() < viewportRect.height());
        // Only inside the hovered cell and its arrows (20 + 14 px outside it).
        const QRect around = canvas->mapFromScene(QRectF(40, 40, 120, 60).adjusted(-50, -50, 50, 50)).boundingRect();
        QVERIFY2(around.contains(log.region.boundingRect()), "hover repaints only around the cell");

        // Selection by click: handles around "b".
        log.clear();
        render::stats::reset();
        click(*canvas, QPointF(340, 70));
        QTest::qWait(30);
        QVERIFY(session.selection().contains("b"));
        QVERIFY(log.paints >= 1);
        QCOMPARE(count(Counter::tilesRasterized), std::uint64_t(0));
        QCOMPARE(count(Counter::cellsReplayed), std::uint64_t(0));
        QCOMPARE(count(Counter::cellsPainted), std::uint64_t(0));
        QCOMPARE(count(Counter::pageViewBuilds), std::uint64_t(0));
        QVERIFY(count(Counter::tilesBlitted) > 0);
        QVERIFY(log.region.boundingRect().height() < viewportRect.height());
    }

    void scrollingRasterizesOnlyNewlyExposedTiles()
    {
        DocumentSession session(documentFromXml(wideDiagram()));
        auto canvas = makeCanvas(session, {800, 600});
        canvas->centerOn(QRectF(400, 300, 1, 1));
        settle(*canvas);
        PaintLog log(canvas->viewport());

        // Put the right edge of the viewport 12 device pixels before a tile border,
        // then scroll 96 pixels: one new column of tiles comes into view.
        // The scroll bar counts logical pixels, a device pixel ratio above 1 makes each of them several
        // device pixels: the edge lands within one logical pixel of its target.
        const double scale = canvas->rasterScale();
        const double dpr = canvas->viewport()->devicePixelRatioF();
        const double left = canvas->mapToScene(QPoint(0, 0)).x() * scale;
        QScrollBar* bar = canvas->horizontalScrollBar();
        bar->setValue(bar->value()
                      + int(std::lround((3 * render::kTileSize + 500 - canvas->viewport()->width() * dpr - left) / dpr)));
        settle(*canvas);
        const QPoint offset(96, 0); // the scene moves left: the right edge is exposed (a whole number of device pixels at 125 %, 150 % and 156 %)
        const render::TileRange before = visibleTiles(*canvas);
        bar->setValue(bar->value() + offset.x());
        log.clear();
        render::stats::reset();
        QTest::qWait(30);
        const render::TileRange after = visibleTiles(*canvas);
        long long fresh = 0;
        for (int ty = after.ty0; ty < after.ty1; ++ty) {
            for (int tx = after.tx0; tx < after.tx1; ++tx)
                fresh += before.contains(tx, ty) ? 0 : 1;
        }
        QVERIFY(fresh > 0);
        QCOMPARE(count(Counter::tilesRasterized), std::uint64_t(fresh));
        // Scrolling blitted: only the exposed strip was painted.
        QVERIFY2(log.region.boundingRect().width() <= offset.x() + 4,
                 qPrintable(QStringLiteral("repainted %1 px wide").arg(log.region.boundingRect().width())));
        QCOMPARE(count(Counter::cellsPainted), std::uint64_t(0));
    }

    void editingOneCellRecordsOneCellAndRasterizesTheTilesItTouches()
    {
        DocumentSession session(documentFromXml(wideDiagram()));
        auto canvas = makeCanvas(session, {1200, 800});
        canvas->centerOn(QRectF(600, 400, 1, 1));
        settle(*canvas);
        DiagramScene& scene = canvas->diagramScene();
        const QRectF oldBounds = scene.snapshot()->find("v301")->visualBounds;

        render::stats::reset();
        QVERIFY(session.edit(QStringLiteral("Move"), [](commands::Transaction& tx) {
                    return ops::moveCells(tx, 0, {"v301"}, 700, 0); // 70 columns... out of the viewport and back in
                }).ok);
        QTest::qWait(30);
        const QRectF newBounds = scene.snapshot()->find("v301")->visualBounds;
        QCOMPARE(count(Counter::recordsBuilt), std::uint64_t(1));

        // Exactly the visible tiles that touch the old or the new bounds.
        const double s = canvas->rasterScale();
        const render::TileRange visible = visibleTiles(*canvas);
        long long expected = 0;
        for (int ty = visible.ty0; ty < visible.ty1; ++ty) {
            for (int tx = visible.tx0; tx < visible.tx1; ++tx) {
                const render::TileRange a = render::tilesCovering(oldBounds, s);
                const render::TileRange b = render::tilesCovering(newBounds, s);
                expected += (a.contains(tx, ty) || b.contains(tx, ty)) ? 1 : 0;
            }
        }
        QVERIFY(expected >= 1);
        QCOMPARE(count(Counter::tilesRasterized), std::uint64_t(expected));
        // The edit changed the content of one cell: the view is patched, not built again.
        QCOMPARE(count(Counter::pageViewBuilds), std::uint64_t(0));
        QCOMPARE(count(Counter::pageViewUpdates), std::uint64_t(1));
    }
};

QTEST_MAIN(TestCanvasTiles)
#include "tst_canvas_tiles.moc"
