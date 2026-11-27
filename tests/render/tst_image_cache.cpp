// The decoded-image cache: keys, negative entries, mip levels, the byte
// budget, concurrent requests, SVG rasters and pixel identity with the
// uncached path.

#include "qraw/render/image_cache.h"
#include "qraw/render/painter_canvas.h"
#include "qraw/render/stats.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QPainter>
#include <QTemporaryDir>
#include <QTest>
#include <QThreadPool>

#include <atomic>
#include <future>

using namespace qraw::render;
namespace stats = qraw::render::stats;
namespace core = qraw::core;

namespace {

// A PNG of w x h pixels with a smooth colour field and varied alpha, so that
// premultiplication and filtering have something to do.
QImage patternImage(int w, int h, bool alpha = true, int seed = 0)
{
    QImage image(w, h, QImage::Format_ARGB32);
    for (int y = 0; y < h; ++y) {
        auto* row = reinterpret_cast<QRgb*>(image.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int a = alpha ? 40 + (x * 3 + y * 5 + seed) % 200 : 255;
            row[x] = qRgba((x * 255 / w + seed) & 255, (y * 255 / h) & 255, ((x + y) * 7) & 255, a);
        }
    }
    return image;
}

QByteArray pngOf(const QImage& image)
{
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

std::string dataUri(const QByteArray& png)
{
    return "data:image/png," + png.toBase64().toStdString();
}

constexpr char kSvg[] = "<svg xmlns='http://www.w3.org/2000/svg' width='40' height='20' viewBox='0 0 40 20'>"
                        "<rect width='40' height='20' fill='#ff0000'/></svg>";

} // namespace

class TstImageCache : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        ImageCache::instance().setEnabled(true);
        ImageCache::instance().setBudget(256LL * 1024 * 1024);
        ImageCache::instance().clear();
        stats::reset();
    }

    void repeatedGetResolvesAndDecodesOnce()
    {
        ImageCache cache(64 << 20);
        const std::string uri = dataUri(pngOf(patternImage(64, 48)));
        const ImageResolveContext context;
        const auto first = cache.get(uri, context);
        QVERIFY(first);
        QVERIFY(!first->decodeFailed());
        QCOMPARE(first->size(), QSize(64, 48));
        QCOMPARE(first->mimeType(), QStringLiteral("image/png"));
        for (int i = 0; i < 5; ++i)
            QCOMPARE(cache.get(uri, context).get(), first.get());
        QCOMPARE(stats::get(stats::Counter::imagesResolved), 1u);
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 1u);
        QCOMPARE(cache.entryCount(), std::size_t(1));
    }

    void levelZeroIsPremultiplied()
    {
        ImageCache cache(64 << 20);
        const auto image = cache.get(dataUri(pngOf(patternImage(16, 16))), {});
        QVERIFY(image);
        QCOMPARE(image->level(0).format(), QImage::Format_ARGB32_Premultiplied);
    }

    void sharedValueAndStringViewShareAnEntry()
    {
        ImageCache cache(64 << 20);
        const std::string uri = dataUri(pngOf(patternImage(32, 32)));
        const core::SharedValue shared(uri); // above the threshold? not needed: the key is the content
        const auto a = cache.getShared(shared, {});
        const auto b = cache.get(std::string_view(uri), {});
        QVERIFY(a);
        QCOMPARE(a.get(), b.get());
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 1u);
    }

    void largeSharedSourcesAreHashedOnce()
    {
        ImageCache cache(64 << 20);
        // Noise does not compress: a PNG of more than 16 KiB.
        QImage noisy(256, 256, QImage::Format_ARGB32);
        quint32 state = 12345;
        for (int y = 0; y < 256; ++y) {
            for (int x = 0; x < 256; ++x) {
                state = state * 1664525u + 1013904223u;
                noisy.setPixel(x, y, 0xFF000000u | (state >> 8));
            }
        }
        const std::string uri = dataUri(pngOf(noisy));
        QVERIFY(uri.size() >= core::kSharedValueThreshold);
        const core::SharedValue shared{std::string(uri)};
        QVERIFY(shared.isShared());
        const core::SharedValue copy = shared;
        QVERIFY(copy.sameBuffer(shared));
        QCOMPARE(cache.getShared(shared, {}).get(), cache.getShared(copy, {}).get());
        QCOMPARE(shared.contentHash(), core::hashBytes(uri));
    }

    void keyIncludesTheResolveContext()
    {
        QTemporaryDir one;
        QTemporaryDir two;
        QVERIFY(one.isValid() && two.isValid());
        QVERIFY(patternImage(20, 10, true, 0).save(one.filePath("pic.png")));
        QVERIFY(patternImage(30, 15, true, 9).save(two.filePath("pic.png")));

        ImageCache cache(64 << 20);
        ImageResolveContext a;
        a.baseDirectory = one.path();
        ImageResolveContext b;
        b.baseDirectory = two.path();
        const auto imageA = cache.get("pic.png", a);
        const auto imageB = cache.get("pic.png", b);
        QVERIFY(imageA && imageB);
        QVERIFY(imageA.get() != imageB.get());
        QCOMPARE(imageA->size(), QSize(20, 10));
        QCOMPARE(imageB->size(), QSize(30, 15));
        QCOMPARE(cache.get("pic.png", a).get(), imageA.get());

        // Local files not allowed: nothing to resolve.
        ImageResolveContext denied = a;
        denied.allowLocalFiles = false;
        QVERIFY(!cache.get("pic.png", denied));

        // The resolve limit is part of the key.
        ImageResolveContext tiny = a;
        tiny.maxBytes = 8;
        QVERIFY(!cache.get("pic.png", tiny));
    }

    void changedFileIsLoadedAgain()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QString path = dir.filePath("pic.png");
        QVERIFY(patternImage(20, 10).save(path));
        ImageCache cache(64 << 20);
        ImageResolveContext context;
        context.baseDirectory = dir.path();
        const auto before = cache.get("pic.png", context);
        QVERIFY(before);
        QCOMPARE(before->size(), QSize(20, 10));
        QVERIFY(patternImage(40, 30, true, 3).save(path)); // another size, so another key
        const auto after = cache.get("pic.png", context);
        QVERIFY(after);
        QCOMPARE(after->size(), QSize(40, 30));
    }

    void failuresAreCached()
    {
        ImageCache cache(64 << 20);
        // Resolves (a PNG data URI) but is not an image.
        const std::string broken = "data:image/png;base64," + QByteArray("this is not a png").toBase64().toStdString();
        for (int i = 0; i < 4; ++i) {
            const auto image = cache.get(broken, {});
            QVERIFY(image);
            QVERIFY(image->decodeFailed());
            QVERIFY(image->level(0).isNull());
        }
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 1u);

        // Does not resolve at all: no entry, and no retry per paint.
        stats::reset();
        for (int i = 0; i < 4; ++i)
            QVERIFY(!cache.get("data:image/png;base64,", {}));
        QVERIFY(!cache.get("https://example.invalid/a.png", {}));
        QVERIFY(!cache.get("", {}));
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 0u);
    }

    void headerSizeIsCheckedBeforeDecoding()
    {
        const std::string uri = dataUri(pngOf(patternImage(600, 600))); // 1.4 MB decoded
        const int previous = QImageReader::allocationLimit();
        QImageReader::setAllocationLimit(1); // MB
        ImageCache cache(64 << 20);
        const auto image = cache.get(uri, {});
        QImageReader::setAllocationLimit(previous);
        QVERIFY(image);
        QVERIFY(image->decodeFailed());
        // The failure is cached: the limit is not consulted again.
        QCOMPARE(cache.get(uri, {}).get(), image.get());
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 1u);
    }

    void mipLevelsHalveDownToAboutThirtyTwoPixels()
    {
        ImageCache cache(64 << 20);
        const auto image = cache.get(dataUri(pngOf(patternImage(1000, 600))), {});
        QVERIFY(image);
        QCOMPARE(image->levelSize(0), QSize(1000, 600));
        QCOMPARE(image->levelSize(1), QSize(500, 300));
        QCOMPARE(image->levelSize(2), QSize(250, 150));
        QCOMPARE(image->levelSize(3), QSize(125, 75));
        QCOMPARE(image->levelSize(4), QSize(63, 38));
        QCOMPARE(image->levelSize(5), QSize(32, 19));
        QCOMPARE(image->levelCount(), 6);
        for (int level = 0; level < image->levelCount(); ++level)
            QCOMPARE(image->level(level).size(), image->levelSize(level));
        QVERIFY(image->level(6).isNull());
    }

    void levelChoiceNeverGoesMoreThanTwiceTooLarge()
    {
        ImageCache cache(64 << 20);
        const auto image = cache.get(dataUri(pngOf(patternImage(1000, 600))), {});
        QVERIFY(image);
        QCOMPARE(image->levelFor({2000, 1200}), 0); // upscaled from the full image
        QCOMPARE(image->levelFor({1000, 600}), 0);
        QCOMPARE(image->levelFor({999, 599}), 0);
        QCOMPARE(image->levelFor({500, 300}), 1);
        QCOMPARE(image->levelFor({400, 240}), 1);
        QCOMPARE(image->levelFor({250, 150}), 2);
        QCOMPARE(image->levelFor({249, 149}), 2);
        QCOMPARE(image->levelFor({126, 76}), 2);
        QCOMPARE(image->levelFor({125, 75}), 3);
        QCOMPARE(image->levelFor({10, 6}), 5);
        QCOMPARE(image->levelFor({1, 1}), 5);
        // Larger than the chosen level by less than 2x, in both directions.
        for (double w = 5; w < 1000; w *= 1.37) {
            const QSizeF device(w, w * 0.6);
            const QSize chosen = image->levelSize(image->levelFor(device));
            QVERIFY2(chosen.width() >= std::ceil(device.width() - 0.01) || image->levelFor(device) == 0, "too small");
            QVERIFY2(chosen.width() < 2 * device.width() + 2 || image->levelFor(device) == image->levelCount() - 1,
                     "more than 2x too large");
        }
    }

    void devicePixelRatioPicksALargerLevel()
    {
        const std::string uri = dataUri(pngOf(patternImage(1000, 600)));
        const ImageResolveContext context;
        const auto drawAt = [&](double ratio) {
            ImageCache::instance().clear();
            QImage target(QSize(120, 80) * ratio, QImage::Format_ARGB32_Premultiplied);
            target.setDevicePixelRatio(ratio);
            target.fill(Qt::white);
            QPainter painter(&target);
            PainterCanvas canvas(painter);
            canvas.setImageQuality(ImageQuality::Interactive);
            const auto image = cachedImage(uri, context);
            canvas.image(0, 0, 100, 60, *image, false, false, false); // 100 x 60 logical px
            return image->cached->byteSize();
        };
        // 100 px wide: level 3 (125 px) at ratio 1, level 2 (250 px) at ratio 2.
        const std::int64_t at1 = drawAt(1);
        const std::int64_t at2 = drawAt(2);
        const std::int64_t level3 = 125 * 75 * 4;
        QCOMPARE(at2 - at1, -std::int64_t(level3)); // the chain stops one level earlier
    }

    void boxFilterAveragesPremultipliedPixels()
    {
        // Raw premultiplied words (setPixel would premultiply again).
        QImage source(4, 2, QImage::Format_ARGB32_Premultiplied);
        const quint32 pixels[2][4] = {{qRgba(0, 0, 0, 0), qRgba(100, 0, 0, 100), qRgba(10, 20, 30, 40), qRgba(10, 20, 30, 40)},
                                      {qRgba(200, 0, 0, 200), qRgba(0, 0, 0, 0), qRgba(11, 21, 31, 41), qRgba(10, 20, 30, 40)}};
        for (int y = 0; y < 2; ++y)
            for (int x = 0; x < 4; ++x)
                reinterpret_cast<quint32*>(source.scanLine(y))[x] = pixels[y][x];
        const QImage half = halveBoxFilter(source);
        QCOMPARE(half.size(), QSize(2, 1));
        const auto* row = reinterpret_cast<const quint32*>(half.constScanLine(0));
        // red (0 + 100 + 200 + 0 + 2) / 4 = 75 and alpha the same.
        QCOMPARE(row[0], quint32(qRgba(75, 0, 0, 75)));
        // (41 + 2) / 4 = 10, (81 + 2) / 4 = 20, (121 + 2) / 4 = 30, (161 + 2) / 4 = 40.
        QCOMPARE(row[1], quint32(qRgba(10, 20, 30, 40)));
    }

    void boxFilterRepeatsTheEdgeOfOddSizes()
    {
        QImage source(3, 3, QImage::Format_ARGB32_Premultiplied);
        source.fill(QColor::fromRgba(qRgba(40, 80, 120, 255)));
        reinterpret_cast<quint32*>(source.scanLine(2))[2] = qRgba(200, 200, 200, 255);
        const QImage half = halveBoxFilter(source);
        QCOMPARE(half.size(), QSize(2, 2));
        QCOMPARE(reinterpret_cast<const quint32*>(half.constScanLine(0))[0], quint32(qRgba(40, 80, 120, 255)));
        // The last pixel, repeated by all four taps.
        QCOMPARE(reinterpret_cast<const quint32*>(half.constScanLine(1))[1], quint32(qRgba(200, 200, 200, 255)));
        QCOMPARE(halveBoxFilter(QImage(1, 1, QImage::Format_ARGB32_Premultiplied)).size(), QSize(1, 1));
    }

    void byteBudgetEvictsTheColdestImagesWhenTheFrameMovesOn()
    {
        const auto uri = [](int seed) { return dataUri(pngOf(patternImage(100, 100, true, seed))); };
        // 100 x 100 x 4 = 40,000 bytes of level 0 (plus a few small levels): room for two.
        ImageCache cache(90'000);
        const std::string a = uri(1), b = uri(2), c = uri(3);
        QVERIFY(cache.get(a, {}));
        cache.beginFrame();
        QVERIFY(cache.get(b, {}));
        cache.beginFrame();
        QVERIFY(cache.get(c, {}));
        QCOMPARE(cache.entryCount(), std::size_t(2)); // a, the coldest, went
        QVERIFY(cache.usedBytes() <= 90'000);
        stats::reset();
        QVERIFY(cache.get(b, {}));
        QVERIFY(cache.get(c, {}));
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 0u);
        QVERIFY(cache.get(a, {}));
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 1u);
    }

    void imagesOfTheCurrentFrameAreNotEvicted()
    {
        const auto uri = [](int seed) { return dataUri(pngOf(patternImage(100, 100, true, seed))); };
        ImageCache cache(100'000); // two images fit, three do not
        for (int seed = 0; seed < 3; ++seed)
            QVERIFY(cache.get(uri(seed), {}));
        QCOMPARE(cache.entryCount(), std::size_t(3)); // all used in this frame: kept, over the budget
        QVERIFY(cache.usedBytes() > 100'000);
        cache.beginFrame();
        cache.trim();
        QCOMPARE(cache.entryCount(), std::size_t(2)); // the frame moved on: the coldest goes
        QVERIFY(cache.usedBytes() <= 100'000);
    }

    void aFrameThatNeverEndsStillRespectsTwiceTheBudget()
    {
        const auto uri = [](int seed) { return dataUri(pngOf(patternImage(100, 100, true, seed))); };
        ImageCache cache(60'000); // hard limit 120,000: three images of about 40,000
        for (int seed = 0; seed < 10; ++seed)
            QVERIFY(cache.get(uri(seed), {})); // all in one frame
        QVERIFY(cache.usedBytes() <= 120'000);
        QVERIFY(cache.entryCount() >= 1);
    }

    void exactImageIsTheDecodersImageAndDecodesOnlyWhenItWasConverted()
    {
        ImageCache cache(256 << 20);
        const auto image = cache.get(dataUri(pngOf(patternImage(64, 48))), {});
        QVERIFY(image);
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 1u);
        // A process that only exports: the decoder's image, as often as asked, no second decode.
        const std::int64_t before = image->byteSize();
        QCOMPARE(image->exactImage().format(), QImage::Format_ARGB32);
        QCOMPARE(image->exactImage().format(), QImage::Format_ARGB32);
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 1u);
        QCOMPARE(image->byteSize(), before);
        // The first on-screen use converts it (and does not keep both).
        QCOMPARE(image->level(0).format(), QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(image->byteSize(), before);
        // An exact draw after that decodes again, once, and keeps the result.
        QCOMPARE(image->exactImage().format(), QImage::Format_ARGB32);
        QCOMPARE(image->exactImage().format(), QImage::Format_ARGB32);
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 2u);
        QVERIFY(image->byteSize() > before);
        // Unused for a frame: dropped again.
        cache.beginFrame();
        QVERIFY(image->trimUnused(cache.frame()) > 0);
        QCOMPARE(image->byteSize(), before);
        QCOMPARE(image->exactImage().format(), QImage::Format_ARGB32);
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 3u);
    }

    void unusedMipLevelsAreDroppedBeforeTheImageIsLost()
    {
        ImageCache cache(256 << 20);
        const auto image = cache.get(dataUri(pngOf(patternImage(400, 400))), {});
        QVERIFY(image);
        const std::int64_t base = image->byteSize();
        QVERIFY(!image->level(3).isNull());
        QVERIFY(image->byteSize() > base);
        cache.beginFrame();
        QVERIFY(image->trimUnused(cache.frame()) > 0);
        QCOMPARE(image->byteSize(), base);
        QVERIFY(!image->level(0).isNull()); // level 0 stays
    }

    void concurrentRequestsDecodeOnce()
    {
        ImageCache cache(64 << 20);
        const std::string uri = dataUri(pngOf(patternImage(300, 300)));
        QThreadPool pool;
        pool.setMaxThreadCount(8);
        std::atomic<const CachedImage*> seen{nullptr};
        std::atomic<int> different{0};
        std::vector<std::future<void>> done;
        for (int i = 0; i < 32; ++i) {
            auto task = std::make_shared<std::promise<void>>();
            done.push_back(task->get_future());
            pool.start([&, task] {
                const auto image = cache.get(uri, {});
                const CachedImage* expected = nullptr;
                if (!seen.compare_exchange_strong(expected, image.get()) && expected != image.get())
                    ++different;
                (void)image->level(3);
                task->set_value();
            });
        }
        for (auto& f : done)
            f.wait();
        pool.waitForDone();
        QCOMPARE(different.load(), 0);
        QCOMPARE(stats::get(stats::Counter::imagesResolved), 1u);
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 1u);
    }

    void svgIsRasterisedPerPowerOfTwoBucket()
    {
        ImageCache cache(64 << 20);
        const auto svg = cache.get(std::string("data:image/svg+xml,") + QByteArray(kSvg).toBase64().toStdString(), {});
        QVERIFY(svg);
        QVERIFY(svg->isSvg());
        QCOMPARE(svg->size(), QSize(40, 20));
        const QImage small = svg->svgRaster({100, 60});
        QCOMPARE(small.size(), QSize(128, 64));
        QCOMPARE(small.format(), QImage::Format_ARGB32_Premultiplied);
        QCOMPARE(small.pixelColor(64, 32), QColor(255, 0, 0));
        QCOMPARE(svg->svgRaster({101, 40}).constBits(), small.constBits()); // the same bucket
        QCOMPARE(svg->svgRaster({130, 60}).size(), QSize(256, 64));
        QVERIFY(svg->byteSize() > 128 * 64 * 4 + 256 * 64 * 4);
        QVERIFY(svg->svgRaster({1e6, 1e6}).isNull()); // over the raster limit: drawn as vectors
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 1u);
    }

    void svgEncodedBytesComeBack()
    {
        ImageCache cache(64 << 20);
        const auto svg = cache.get(std::string("data:image/svg+xml,") + QByteArray(kSvg).toBase64().toStdString(), {});
        QVERIFY(svg);
        QCOMPARE(svg->encodedBytes(), QByteArray(kSvg));
    }

    void pngEncodedBytesAreReadAgainOnDemand()
    {
        ImageCache cache(64 << 20);
        const QByteArray png = pngOf(patternImage(24, 24));
        const auto image = cache.get(dataUri(png), {});
        QVERIFY(image);
        QCOMPARE(image->encodedBytes(), png);
    }

    void disabledCacheDecodesEveryTimeInTheDecodersFormat()
    {
        ImageCache cache(64 << 20);
        cache.setEnabled(false);
        const std::string uri = dataUri(pngOf(patternImage(16, 16)));
        const auto first = cache.get(uri, {});
        const auto second = cache.get(uri, {});
        QVERIFY(first && second);
        QVERIFY(first.get() != second.get());
        QCOMPARE(first->level(0).format(), QImage::Format_ARGB32); // as QImage::fromData returns it
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 2u);
        QCOMPARE(cache.entryCount(), std::size_t(0));
    }

    void fromEncodedDecodesOncePerContent()
    {
        ImageCache cache(64 << 20);
        const QByteArray png = pngOf(patternImage(16, 16));
        const auto a = cache.fromEncoded(png, QStringLiteral("image/png"));
        const auto b = cache.fromEncoded(png, QStringLiteral("image/png"));
        QCOMPARE(a.get(), b.get());
        QCOMPARE(stats::get(stats::Counter::imagesDecoded), 1u);
        QVERIFY(!cache.fromEncoded({}, {}));
    }

    // Exact quality is QPainter's own scaling of the decoded image: the same
    // pixels as drawing QImage::fromData's result, for an image with alpha at
    // several scales, with and without flips.
    void exactQualityMatchesDrawingTheDecodedImage()
    {
        const QImage source = patternImage(97, 61);
        const QByteArray png = pngOf(source);
        const QImage decoded = QImage::fromData(png);
        QVERIFY(!decoded.isNull());
        const std::string uri = dataUri(png);

        for (const double scale : {0.37, 1.0, 2.5}) {
            QImage viaCache(260, 180, QImage::Format_ARGB32_Premultiplied);
            QImage reference(260, 180, QImage::Format_ARGB32_Premultiplied);
            viaCache.fill(QColor(230, 240, 255));
            reference.fill(QColor(230, 240, 255));
            {
                QPainter painter(&viaCache);
                PainterCanvas canvas(painter);
                canvas.scale(scale);
                const auto image = cachedImage(uri, {});
                QVERIFY(image);
                canvas.image(5.5, 7.25, 97, 61, *image, false, false, false);
            }
            {
                QPainter painter(&reference);
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                painter.drawImage(QRectF(5.5 * scale, 7.25 * scale, 97 * scale, 61 * scale), decoded);
            }
            QVERIFY2(viaCache == reference, qPrintable(QStringLiteral("scale %1").arg(scale)));
        }
    }

    void interactiveQualityStaysCloseToExact()
    {
        const QByteArray png = pngOf(patternImage(1000, 600, false));
        const std::string uri = dataUri(png);
        QImage exact(180, 120, QImage::Format_ARGB32_Premultiplied);
        QImage fast(180, 120, QImage::Format_ARGB32_Premultiplied);
        for (QImage* target : {&exact, &fast}) {
            target->fill(Qt::white);
            QPainter painter(target);
            PainterCanvas canvas(painter);
            canvas.setImageQuality(target == &fast ? ImageQuality::Interactive : ImageQuality::Exact);
            const auto image = cachedImage(uri, {});
            QVERIFY(image);
            canvas.image(0, 0, 180, 108, *image, false, false, false);
        }
        // A smooth field: the two scalers agree to within a few levels.
        long worst = 0;
        for (int y = 0; y < 108; ++y)
            for (int x = 0; x < 180; ++x)
                worst = std::max<long>(worst, std::abs(qRed(exact.pixel(x, y)) - qRed(fast.pixel(x, y))));
        QVERIFY2(worst <= 12, qPrintable(QString::number(worst)));
    }

    void failedImageDrawsNothing()
    {
        QImage target(20, 20, QImage::Format_ARGB32_Premultiplied);
        target.fill(Qt::white);
        const QImage before = target;
        QPainter painter(&target);
        PainterCanvas canvas(painter);
        canvas.image(0, 0, 20, 20, ImageData{QByteArray("junk"), QStringLiteral("image/png")}, false, false, false);
        QCOMPARE(target, before);
    }
};

QTEST_MAIN(TstImageCache)
#include "tst_image_cache.moc"
