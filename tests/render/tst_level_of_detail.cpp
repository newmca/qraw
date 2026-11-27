// Level of detail (level_of_detail.h): at a far zoom, replay skips cells
// smaller than a device pixel and labels too small to read; nothing else, and
// never export. The thresholds are tested at their boundaries.

#include "qraw/format/codec.h"
#include "qraw/render/display_list.h"
#include "qraw/render/export.h"
#include "qraw/render/image_cache.h"
#include "qraw/render/level_of_detail.h"
#include "qraw/render/page_view.h"
#include "qraw/render/render_snapshot.h"
#include "qraw/render/scene_renderer.h"
#include "qraw/render/stats.h"
#include "qraw/render/tile_rasterizer.h"

#include <QBuffer>
#include <QImage>
#include <QTest>

using namespace qraw;
using namespace qraw::render;
using stats::Counter;

namespace {

// One cell: a size x size square with a label of `fontSize`.
DisplayList recordCell(double size, double fontSize)
{
    RecordingCanvas canvas;
    canvas.setFillColor(QColor(Qt::red));
    canvas.begin();
    canvas.rect(0, 0, size, size);
    canvas.fill();
    canvas.markLabelStart();
    if (fontSize > 0) {
        TextBlock block;
        block.x = size / 2;
        block.y = size / 2;
        block.fontSize = fontSize;
        block.text.lines = {"Label"};
        block.text.content = "Label";
        canvas.text(block);
    }
    return canvas.finish();
}

std::string pngDataUri(int w, int h)
{
    QImage image(w, h, QImage::Format_ARGB32);
    image.fill(Qt::blue);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return "data:image/png," + bytes.toBase64().toStdString();
}

// A flat page: a big labelled vertex, a 3 x 3 one with a label, and an
// image cell (1000 x 600 natural pixels, 250 x 150 on the page).
core::Document testPage()
{
    const std::string xml = R"(<mxfile><diagram id="p" name="P"><mxGraphModel grid="0" page="0"><root><mxCell id="0"/><mxCell id="1" parent="0"/>)"
                            R"(<mxCell id="big" value="Big" style="fillColor=#dae8fc;fontSize=12;" vertex="1" parent="1"><mxGeometry x="0" y="0" width="200" height="100" as="geometry"/></mxCell>)"
                            R"(<mxCell id="tiny" value="" style="fillColor=#f8cecc;" vertex="1" parent="1"><mxGeometry x="300" y="0" width="3" height="3" as="geometry"/></mxCell>)"
                            R"(<mxCell id="img" value="" style="shape=image;image=)"
        + pngDataUri(1000, 600) + R"(;" vertex="1" parent="1"><mxGeometry x="0" y="200" width="250" height="150" as="geometry"/></mxCell>)"
                                  R"(</root></mxGraphModel></diagram></mxfile>)";
    auto read = format::readDocument(QByteArray(xml.data(), qsizetype(xml.size())));
    return std::move(*read.document);
}

struct Snapshot {
    core::Document document = testPage();
    RenderContext context = RenderContext::defaults();
    PageView view = PageView::build(*document.page(0), context);
    SceneRenderer renderer{context};
    std::shared_ptr<const RenderSnapshot> snapshot = [this] {
        SnapshotInput input;
        input.view = &view;
        input.renderer = &renderer;
        return buildSnapshot(input);
    }();
};

} // namespace

class TestLevelOfDetail : public QObject {
    Q_OBJECT

private slots:
    void cleanup() { lod::setEnabled(true); }

    // A cell is skipped when it is below a device pixel in BOTH directions.
    void cellThresholdBoundary()
    {
        const DisplayList wide = recordCell(10, 0);
        const double belowScale = lod::kMinCellDevicePx / 10 * 0.99;
        const double atScale = lod::kMinCellDevicePx / 10;
        QVERIFY(!lod::detailFor(wide, belowScale).cell);
        QVERIFY(lod::detailFor(wide, atScale).cell);
        QVERIFY(lod::detailFor(wide, 1).cell);
        // One direction above the threshold keeps the cell: a long thin line.
        RecordingCanvas canvas;
        canvas.setFillColor(QColor(Qt::red));
        canvas.begin();
        canvas.rect(0, 0, 1000, 1);
        canvas.fill();
        const DisplayList line = canvas.finish();
        QVERIFY(lod::detailFor(line, 0.5).cell);
        QVERIFY(!lod::detailFor(line, 0.0009).cell);
        // Without a scale (0), and with the level of detail off, everything is drawn.
        QVERIFY(lod::detailFor(wide, 0).cell);
        QVERIFY(lod::detailFor(wide, -1).cell);
        lod::setEnabled(false);
        QVERIFY(lod::detailFor(wide, belowScale).cell);
    }

    // A label is skipped when its font is rendered below the threshold; the shape is not.
    void labelThresholdBoundary()
    {
        const DisplayList cell = recordCell(100, 10);
        QCOMPARE(cell.cost().labelFontPx, 10.0);
        const double at = lod::kMinLabelFontPx / 10;
        QVERIFY(lod::detailFor(cell, at).label);
        QVERIFY(!lod::detailFor(cell, at * 0.99).label);
        QVERIFY(lod::detailFor(cell, at * 0.99).cell); // 39 px wide: still drawn
        // A cell without a label has nothing to skip.
        QVERIFY(lod::detailFor(recordCell(100, 0), 0.01).label);
    }

    // At a far zoom the tiny cell and the labels are skipped and counted; at
    // 100 % nothing is, and the pixels equal those with the level of detail off.
    void tilesSkipOnlyWhatIsInvisible()
    {
        Snapshot page;
        const auto tile = [&](double scale) { return rasterizeTile(*page.snapshot, {scale, 0, 0}, 1); };

        stats::reset();
        const QImage full = tile(1);
        QCOMPARE(stats::get(Counter::cellsSkippedLod), std::uint64_t(0));
        QCOMPARE(stats::get(Counter::labelsSkippedLod), std::uint64_t(0));
        lod::setEnabled(false);
        QCOMPARE(tile(1), full);
        lod::setEnabled(true);

        // 0.2: "Big" is labelled at 12 px, 2.4 px on the screen: no label.
        stats::reset();
        const QImage far = tile(0.2);
        QVERIFY(stats::get(Counter::labelsSkippedLod) >= std::uint64_t(1));
        QCOMPARE(stats::get(Counter::cellsSkippedLod), std::uint64_t(0)); // the tiny cell is 2.6 px with its overhang
        lod::setEnabled(false);
        stats::reset();
        const QImage unskipped = tile(0.2);
        QCOMPARE(stats::get(Counter::labelsSkippedLod), std::uint64_t(0));
        QVERIFY(far != unskipped); // the label of "Big" is not drawn...
        QCOMPARE(far.pixel(5, 5), unskipped.pixel(5, 5)); // ...and only the label: the fill is the same
        lod::setEnabled(true);

        // 0.05: the tiny cell (13 x 13 with what it can paint) is 0.65 px.
        stats::reset();
        (void)tile(0.05);
        QCOMPARE(stats::get(Counter::cellsSkippedLod), std::uint64_t(1));
    }

    // A skipped cell is only hidden from the screen: it stays in the snapshot
    // and the display list is whole.
    void skippedCellsStayInTheSnapshot()
    {
        Snapshot page;
        QVERIFY(page.snapshot->find("tiny") != nullptr);
        QVERIFY(!page.snapshot->find("tiny")->display.empty());
    }

    // Export paints directly: its output does not depend on the level of detail.
    void exportIgnoresTheLevelOfDetail()
    {
        Snapshot page;
        stats::reset();
        ExportOptions options;
        options.format = ExportFormat::Png;
        options.scale = 0.2;
        const ExportResult withLod = exportPage(page.document, 0, options);
        QVERIFY(withLod.ok());
        lod::setEnabled(false);
        const ExportResult without = exportPage(page.document, 0, options);
        QCOMPARE(withLod.data, without.data);
        QCOMPARE(stats::get(Counter::cellsSkippedLod), std::uint64_t(0));
    }

    // Images use the mip level that is at most twice too large for the
    // zoom: at 25 % the 250 x 150 level, at 15 % the same, at 10 % 125 x 75.
    void imagesPickTheMipLevelOfTheZoom_data()
    {
        QTest::addColumn<double>("zoom");
        QTest::addColumn<int>("level");
        QTest::newRow("25 %") << 0.25 << 2;
        QTest::newRow("15 %") << 0.15 << 2;
        QTest::newRow("10 %") << 0.10 << 3;
    }
    void imagesPickTheMipLevelOfTheZoom()
    {
        QFETCH(double, zoom);
        QFETCH(int, level);
        ImageCache cache(64 << 20);
        const auto image = cache.get(pngDataUri(1000, 600), {});
        QVERIFY(image);
        // The 1000 x 600 picture shown at `zoom` of its natural size.
        QCOMPARE(image->levelFor(QSizeF(1000 * zoom, 600 * zoom)), level);
    }
};

QTEST_MAIN(TestLevelOfDetail)
#include "tst_level_of_detail.moc"
