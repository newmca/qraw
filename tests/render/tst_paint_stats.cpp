// The work counters and scope timers of the paint path (render/stats.h,
// render/perf_scope.h): what the paint benchmark builds its gates on.

#include "qraw/format/codec.h"
#include "qraw/render/image_cache.h"
#include "qraw/render/page_view.h"
#include "qraw/render/painter_canvas.h"
#include "qraw/render/perf_scope.h"
#include "qraw/render/scene_renderer.h"
#include "qraw/render/stats.h"
#include "qraw/render/text_layout_cache.h"

#include <QBuffer>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>

#include <chrono>
#include <thread>

using namespace qraw::render;
namespace stats = qraw::render::stats;
namespace perf = qraw::render::perf;

namespace {

constexpr int kPngSize = 8;

QByteArray pngBytes()
{
    QImage image(kPngSize, kPngSize, QImage::Format_ARGB32);
    image.fill(Qt::red);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

// draw.io's form: no ";base64" marker.
std::string drawioDataUri(const QByteArray& png)
{
    return "data:image/png," + png.toBase64().toStdString();
}

void spinFor(std::chrono::microseconds duration)
{
    const auto end = std::chrono::steady_clock::now() + duration;
    while (std::chrono::steady_clock::now() < end) {
    }
}

constexpr char kDiagram[] =
    R"(<mxfile><diagram id="p" name="P"><mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/>)"
    R"(<mxCell id="a" value="A" style="rounded=1;" vertex="1" parent="1"><mxGeometry x="10" y="10" width="80" height="40" as="geometry"/></mxCell>)"
    R"(<mxCell id="b" value="B" style="ellipse;html=1;" vertex="1" parent="1"><mxGeometry x="200" y="10" width="80" height="40" as="geometry"/></mxCell>)"
    R"(<mxCell id="c" value="" style="rounded=0;" vertex="1" parent="1"><mxGeometry x="10" y="100" width="80" height="40" as="geometry"/></mxCell>)"
    R"(<mxCell id="e" style="endArrow=classic;" edge="1" parent="1" source="a" target="b"><mxGeometry relative="1" as="geometry"/></mxCell>)"
    R"(</root></mxGraphModel></diagram></mxfile>)";

} // namespace

class TstPaintStats : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        ImageCache::instance().clear();
        TextLayoutCache::forThisThread().clear();
        clearLabelMetrics();
        stats::reset();
    }

    void everyCounterHasAName()
    {
        for (std::size_t i = 0; i < stats::kCounterCount; ++i)
            QVERIFY(!stats::name(static_cast<stats::Counter>(i)).empty());
        QCOMPARE(stats::name(stats::Counter::cellsPainted), std::string_view("cellsPainted"));
    }

    void countersAddAndReset()
    {
        stats::add(stats::Counter::tilesBlitted, 3);
        stats::add(stats::Counter::tilesBlitted);
        QCOMPARE(stats::get(stats::Counter::tilesBlitted), 4u);
        QCOMPARE(stats::snapshot()[static_cast<std::size_t>(stats::Counter::tilesBlitted)], 4u);
        stats::reset();
        QCOMPARE(stats::get(stats::Counter::tilesBlitted), 0u);
    }

    // Resolving and decoding are counted once per distinct image, however
    // often it is drawn (the image cache); an image drawn at its own size is
    // not scaled.
    void imagePathCounters()
    {
        const QByteArray png = pngBytes();
        const std::string source = drawioDataUri(png);
        ImageResolveContext context;
        const auto image = resolveImage(source, context);
        QVERIFY(image);
        QCOMPARE(stats::get(stats::Counter::imagesResolved), 1u);
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 0u);
        const std::uint64_t base64Bytes = stats::get(stats::Counter::bytesDecoded);
        QCOMPARE(base64Bytes, std::uint64_t(png.toBase64().size()));

        QImage target(32, 32, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::transparent);
        QPainter painter(&target);
        PainterCanvas canvas(painter);
        canvas.image(0, 0, 16, 16, *image, false, false, false); // 8 px drawn at 16 px
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 1u);
        QCOMPARE(stats::get(stats::Counter::imageScales), 1u);
        QCOMPARE(stats::get(stats::Counter::bytesDecoded), base64Bytes + std::uint64_t(png.size()));
        canvas.image(0, 0, kPngSize, kPngSize, *image, false, false, false); // 1:1, from the cache
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 1u);
        QCOMPARE(stats::get(stats::Counter::imageScales), 1u);
        QCOMPARE(stats::get(stats::Counter::bytesDecoded), base64Bytes + std::uint64_t(png.size()));
    }

    void unresolvableImagesCountNothing()
    {
        QVERIFY(!resolveImage("http://example.invalid/x.png", {}));
        QVERIFY(!resolveImage("", {}));
        QCOMPARE(stats::get(stats::Counter::imagesResolved), 0u);
    }

    void pageViewAndPaintCounters()
    {
        auto read = qraw::format::readDocument(QByteArrayView(kDiagram, qsizetype(sizeof kDiagram - 1)));
        QVERIFY(read.ok());
        const PageView view = PageView::build(*read.document->page(0), RenderContext::defaults());
        QCOMPARE(stats::get(stats::Counter::pageViewBuilds), 1u);
        QCOMPARE(view.states().size(), std::size_t(4));
        stats::reset();

        QImage target(320, 160, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::white);
        QPainter painter(&target);
        PainterCanvas canvas(painter);
        SceneRenderer(RenderContext::defaults()).paintPage(canvas, view, {});
        QCOMPARE(stats::get(stats::Counter::cellsPainted), 4u);
        QCOMPARE(stats::get(stats::Counter::labelsPainted), 2u); // "A" and "B"; "c" and the edge have none
        QCOMPARE(stats::get(stats::Counter::textLayouts), 2u);
        QCOMPARE(stats::get(stats::Counter::pageViewBuilds), 0u);
    }

    void scopesAreFreeWhenDisabled()
    {
        perf::setEnabled(false);
        perf::reset();
        {
            QRAW_PERF_SCOPE(ShapePaint);
        }
        for (const perf::ScopeTotals& t : perf::snapshot())
            QCOMPARE(t.count, std::uint64_t(0));
    }

    // Self time excludes the scopes nested inside; inclusive time does not.
    void scopesRecordInclusiveAndSelfTime()
    {
        perf::setEnabled(true);
        perf::reset();
        for (int i = 0; i < 3; ++i) {
            QRAW_PERF_SCOPE(CellItemPaint);
            spinFor(std::chrono::microseconds(300));
            {
                QRAW_PERF_SCOPE(Raster);
                spinFor(std::chrono::microseconds(700));
            }
        }
        const std::vector<perf::ScopeTotals> totals = perf::snapshot();
        perf::setEnabled(false);
        const perf::ScopeTotals& outer = totals[static_cast<std::size_t>(perf::Scope::CellItemPaint)];
        const perf::ScopeTotals& inner = totals[static_cast<std::size_t>(perf::Scope::Raster)];
        QCOMPARE(outer.count, std::uint64_t(3));
        QCOMPARE(inner.count, std::uint64_t(3));
        QVERIFY(inner.wallNs >= 3 * 700'000);
        QVERIFY(outer.wallNs >= 3 * 1'000'000);
        QVERIFY(outer.wallNs >= outer.selfWallNs + inner.wallNs - 1000); // the children are not in the self time
        QVERIFY(outer.selfWallNs < outer.wallNs);
        QVERIFY(outer.selfWallNs >= 3 * 300'000);
        QCOMPARE(inner.selfWallNs, inner.wallNs); // a leaf
        if (perf::cpuTimeAvailable())
            QVERIFY(outer.cpuNs > 0);
    }

    void scopesNestAcrossThreadsIndependently()
    {
        perf::setEnabled(true);
        perf::reset();
        {
            QRAW_PERF_SCOPE(ViewPaint);
            std::thread worker([] {
                QRAW_PERF_SCOPE(TextLayout);
                spinFor(std::chrono::microseconds(200));
            });
            worker.join();
        }
        const std::vector<perf::ScopeTotals> totals = perf::snapshot();
        perf::setEnabled(false);
        QCOMPARE(totals[static_cast<std::size_t>(perf::Scope::TextLayout)].count, std::uint64_t(1));
        // The worker's scope is not a child of the GUI thread's scope.
        const perf::ScopeTotals& view = totals[static_cast<std::size_t>(perf::Scope::ViewPaint)];
        QCOMPARE(view.selfWallNs, view.wallNs);
    }

    void traceWritesChromeTraceEvents()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        perf::startTrace(dir.filePath("trace.json").toStdString());
        QVERIFY(perf::tracing());
        const std::uint64_t start = perf::nowNs();
        {
            QRAW_PERF_SCOPE(PageViewBuild);
        }
        perf::span("scenario:test", start, perf::nowNs());
        QVERIFY(perf::writeTrace());
        perf::setEnabled(false);

        QFile file(dir.filePath("trace.json"));
        QVERIFY(file.open(QIODevice::ReadOnly));
        const QJsonDocument json = QJsonDocument::fromJson(file.readAll());
        QVERIFY(json.isObject());
        const QJsonArray events = json.object().value("traceEvents").toArray();
        QVERIFY(events.size() >= 2);
        QStringList names;
        for (const QJsonValue& event : events) {
            QCOMPARE(event.toObject().value("ph").toString(), QString("X"));
            QVERIFY(event.toObject().contains("ts"));
            QVERIFY(event.toObject().contains("dur"));
            QVERIFY(event.toObject().contains("tid"));
            names << event.toObject().value("name").toString();
        }
        QVERIFY(names.contains("PageViewBuild"));
        QVERIFY(names.contains("scenario:test"));
    }
};

QTEST_MAIN(TstPaintStats)
#include "tst_paint_stats.moc"
