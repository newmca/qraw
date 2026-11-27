// The label layout caches: the per-thread laid-out documents and the shared
// metrics.

#include "qraw/render/painter_canvas.h"
#include "qraw/render/stats.h"
#include "qraw/render/text_layout_cache.h"
#include "qraw/render/text_metrics.h"

#include <QAbstractTextDocumentLayout>
#include <QImage>
#include <QPainter>
#include <QPalette>
#include <QTest>

#include <cmath>
#include <thread>

using namespace qraw::render;
namespace stats = qraw::render::stats;
using qraw::geometry::HAlign;
using qraw::geometry::VAlign;

namespace {

TextBlock label(const char* text, bool html = false)
{
    TextBlock b;
    b.text = makeRichText(text, html);
    b.fontFamily = QStringLiteral("Helvetica");
    b.fontSize = 14;
    b.width = 120;
    b.height = 60;
    b.x = 10;
    b.y = 10;
    b.align = HAlign::Left;
    b.verticalAlign = VAlign::Top;
    b.wrap = true;
    return b;
}

QImage paintLabel(const TextBlock& block, double scale = 1)
{
    QImage image(220, 140, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    PainterCanvas canvas(painter);
    canvas.scale(scale);
    canvas.text(block);
    return image;
}

} // namespace

class TstTextLayoutCache : public QObject {
    Q_OBJECT

private slots:
    void init()
    {
        TextLayoutCache::forThisThread().clear();
        TextLayoutCache::forThisThread().setCapacity(8192);
        clearLabelMetrics();
        stats::reset();
    }

    void repeatedPaintsLayOutOnce()
    {
        const TextBlock block = label("The quick brown fox jumps over the lazy dog");
        for (int i = 0; i < 5; ++i)
            paintLabel(block);
        QCOMPARE(stats::get(stats::Counter::textLayouts), 1u);
        QCOMPARE(TextLayoutCache::forThisThread().size(), std::size_t(1));
    }

    void cachedPixelsAreTheFreshLayoutsPixels()
    {
        for (const bool html : {false, true}) {
            TextBlock block = label(html ? "<b>Bold</b> and <i>italic</i> words that wrap" : "Plain words that wrap around", html);
            block.fontColor = QColor(200, 30, 30);
            const QImage cold = paintLabel(block);
            const QImage warm = paintLabel(block);
            QCOMPARE(warm, cold);

            // The pre-cache path: a fresh document per paint.
            QImage reference(220, 140, QImage::Format_ARGB32_Premultiplied);
            reference.fill(Qt::white);
            {
                QPainter painter(&reference);
                painter.setRenderHint(QPainter::Antialiasing, true);
                painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
                QTextDocument doc;
                configureTextDocument(doc, block, 1, block.width);
                doc.setTextWidth(block.width);
                // The document sits where the label placement puts it (LabelPlacement).
                painter.translate(block.x, labelMetrics(block, 1, block.width).placement.contentTop(block.y, 0));
                QAbstractTextDocumentLayout::PaintContext context;
                context.palette.setColor(QPalette::Text, block.fontColor);
                doc.documentLayout()->draw(&painter, context);
            }
            QCOMPARE(cold, reference);
        }
    }

    void everythingThatChangesTheLayoutIsPartOfTheKey()
    {
        const TextBlock base = label("Some label text that is long enough to wrap in a narrow box");
        std::vector<TextBlock> variants;
        variants.push_back(base);
        TextBlock v = base; v.fontFamily = QStringLiteral("Courier"); variants.push_back(v);
        v = base; v.fontSize = 18; variants.push_back(v);
        v = base; v.fontStyle = 1; variants.push_back(v);                    // bold
        v = base; v.width = 80; variants.push_back(v);                        // wrap width
        v = base; v.wrap = false; variants.push_back(v);
        v = base; v.align = HAlign::Center; variants.push_back(v);
        v = base; v.lineHeight = 1.6; variants.push_back(v);
        v = base; v.text = makeRichText("Some label text that is long enough to wrap in a narrow box", true); variants.push_back(v);
        v = base; v.text = makeRichText("Other text", false); variants.push_back(v);
        for (const TextBlock& b : variants)
            paintLabel(b);
        QCOMPARE(stats::get(stats::Counter::textLayouts), std::uint64_t(variants.size()));
        for (const TextBlock& b : variants)
            paintLabel(b);
        QCOMPARE(stats::get(stats::Counter::textLayouts), std::uint64_t(variants.size())); // all hits
    }

    void thingsThatOnlyMovePaintOrColourShareTheLayout()
    {
        const TextBlock base = label("Shared layout");
        paintLabel(base);
        TextBlock v = base; v.fontColor = QColor(10, 200, 10); paintLabel(v);
        v = base; v.x = 50; v.y = 40; paintLabel(v);
        v = base; v.verticalAlign = VAlign::Bottom; paintLabel(v);
        v = base; v.rotation = 30; paintLabel(v);
        v = base; v.clip = true; paintLabel(v);
        v = base; v.opacity = 0.5; paintLabel(v);
        v = base; v.backgroundColor = QColor(Qt::yellow); v.padding = 3; paintLabel(v);
        v = base; v.height = 10; paintLabel(v); // the box height does not wrap
        QCOMPARE(stats::get(stats::Counter::textLayouts), 1u);
    }

    void unwrappedTextIgnoresTheBoxWidth()
    {
        TextBlock a = label("No wrapping here");
        a.wrap = false;
        TextBlock b = a;
        b.width = 400;
        paintLabel(a);
        paintLabel(b);
        QCOMPARE(stats::get(stats::Counter::textLayouts), 1u);
    }

    void pixelSizeNotZoomIsTheKey()
    {
        const TextBlock block = label("Zoom independent");
        // The canvas scale changes the pixel size; the view zoom is only the painter's transform.
        paintLabel(block, 1);
        paintLabel(block, 2);
        QCOMPARE(stats::get(stats::Counter::textLayouts), 2u);
        QImage image(440, 280, QImage::Format_ARGB32_Premultiplied);
        image.fill(Qt::white);
        QPainter painter(&image);
        painter.scale(2, 2); // zoom
        PainterCanvas canvas(painter);
        canvas.text(block);
        QCOMPARE(stats::get(stats::Counter::textLayouts), 2u);
    }

    void leastRecentlyUsedEntriesGoFirst()
    {
        TextLayoutCache::forThisThread().setCapacity(2);
        paintLabel(label("one"));
        paintLabel(label("two"));
        paintLabel(label("one")); // touches one
        paintLabel(label("three")); // evicts two
        QCOMPARE(TextLayoutCache::forThisThread().size(), std::size_t(2));
        stats::reset();
        paintLabel(label("one"));
        QCOMPARE(stats::get(stats::Counter::textLayouts), 0u);
        paintLabel(label("two"));
        QCOMPARE(stats::get(stats::Counter::textLayouts), 1u);
    }

    void everyThreadHasItsOwnCache()
    {
        const TextBlock block = label("Used from two threads");
        paintLabel(block);
        const LaidOutLabel* mine = &TextLayoutCache::forThisThread().layout(block, 1, block.width);
        QCOMPARE(stats::get(stats::Counter::textLayouts), 1u);

        const LaidOutLabel* theirs = nullptr;
        std::thread worker([&] {
            TextLayoutCache& cache = TextLayoutCache::forThisThread();
            theirs = &cache.layout(block, 1, block.width); // its own layout
            QCOMPARE(&cache.layout(block, 1, block.width), theirs);
            QCOMPARE(cache.size(), std::size_t(1));
        });
        worker.join();
        QCOMPARE(stats::get(stats::Counter::textLayouts), 2u);
        QVERIFY(theirs != mine);
        QCOMPARE(TextLayoutCache::forThisThread().size(), std::size_t(1));
    }

    void metricsAreTheLaidOutRectangle()
    {
        const TextBlock block = label("Two words that wrap in this narrow box of text");
        const LaidOutLabel& laid = TextLayoutCache::forThisThread().layout(block, 1, block.width);
        const double width = laid.width;
        const double height = laid.height;
        const LabelMetrics metrics = labelMetrics(block, 1, block.width);
        QCOMPARE(metrics.size.width(), width);
        QCOMPARE(metrics.size.height(), height);
        QVERIFY(metrics.lines.size() >= 2);
        QRectF united;
        for (const QRectF& line : metrics.lines) {
            QVERIFY(line.width() > 0 && line.height() > 0);
            united = united.united(line);
        }
        QVERIFY(united.left() >= -0.5 && united.right() <= width + 0.5);
        // A line rectangle is the font's natural height, put in a line of a fixed height at
        // 4/5 of it above the baseline (QTextDocumentLayout): where the ascent and the leading
        // exceed that, the first line starts above the document (by a quarter of the font size
        // at most), and the last one can reach past its end.
        QVERIFY(united.top() >= -0.25 * block.fontSize && metrics.lines.back().top() < height);
        // The lines fill the document height. Lines are stacked at the font's line
        // spacing, which is the line rectangle's height plus the font's leading (or
        // minus the overlap of a shorter line height), and the document is as tall
        // as the stacked lines: the two heights differ by that gap once per line
        // boundary, at most (fonts differ: Liberation Sans has 0.8 px at 14 px,
        // Arial and Helvetica less, others none).
        const double gap = std::abs((metrics.lines[1].top() - metrics.lines[0].top()) - metrics.lines[0].height());
        const double lineCount = double(metrics.lines.size());
        QVERIFY2(std::abs(united.height() - height) <= gap * lineCount + 0.5,
                 qPrintable(QStringLiteral("lines %1 high, document %2 high, gap per line %3")
                                .arg(united.height()).arg(height).arg(gap)));

        // Through measureText, which the autosize code calls, at scale 1.
        QCOMPARE(measureText(block, block.width), metrics.size);
    }

    void metricsComeFromTheSharedCacheForEveryThread()
    {
        const TextBlock block = label("Measured on one thread, read on another");
        const LabelMetrics first = labelMetrics(block);
        QCOMPARE(labelCacheStats().metricsMisses, 1u);
        LabelMetrics second;
        std::thread worker([&] { second = labelMetrics(block); });
        worker.join();
        QCOMPARE(second.size, first.size);
        QCOMPARE(second.lines, first.lines);
        QCOMPARE(labelCacheStats().metricsHits, 1u);
        QCOMPARE(labelCacheStats().metricsMisses, 1u);
        QCOMPARE(stats::get(stats::Counter::textLayouts), 1u); // the worker did not lay out again
    }

    void measuringAgainLaysNothingOut()
    {
        const TextBlock block = label("Autosize measures this label", true);
        const QSizeF size = measureText(block);
        QVERIFY(size.width() > 0 && size.height() > 0);
        stats::reset();
        for (int i = 0; i < 10; ++i)
            QCOMPARE(measureText(block), size);
        QCOMPARE(stats::get(stats::Counter::textLayouts), 0u);
        QCOMPARE(measureText(TextBlock{}), QSizeF());
    }

    void textRectFollowsTheAnchorAndAlignment()
    {
        TextBlock block = label("Anchored");
        block.wrap = false;
        block.width = block.height = 0; // unbounded: the anchor is the point
        block.x = 100;
        block.y = 50;
        const LabelMetrics metrics = labelMetrics(block);
        const QSizeF size = metrics.size;
        // Horizontally the anchor is the left, centre or right of the text; vertically the placement
        // (the block of the first baseline model, see LabelPlacement) turns it into the top.
        block.align = HAlign::Left;
        block.verticalAlign = VAlign::Top;
        QCOMPARE(labelTextRect(block), QRectF(QPointF(100, metrics.placement.contentTop(50, 0)), size));
        block.align = HAlign::Center;
        block.verticalAlign = VAlign::Middle;
        QCOMPARE(labelTextRect(block), QRectF(QPointF(100 - size.width() / 2, metrics.placement.contentTop(50, 0.5)), size));
        block.align = HAlign::Right;
        block.verticalAlign = VAlign::Bottom;
        QCOMPARE(labelTextRect(block), QRectF(QPointF(100 - size.width(), metrics.placement.contentTop(50, 1)), size));
    }

    void theRectIsWhatIsPainted()
    {
        TextBlock block = label("Painted exactly here");
        block.wrap = false;
        block.width = block.height = 0;
        block.x = 60;
        block.y = 40;
        block.align = HAlign::Center;
        block.verticalAlign = VAlign::Middle;
        block.fontColor = Qt::black;
        const QRectF rect = labelTextRect(block);
        const QImage image = paintLabel(block);
        int minX = 1000, maxX = -1, minY = 1000, maxY = -1;
        for (int y = 0; y < image.height(); ++y) {
            for (int x = 0; x < image.width(); ++x) {
                if (qGray(image.pixel(x, y)) < 200) {
                    minX = std::min(minX, x); maxX = std::max(maxX, x);
                    minY = std::min(minY, y); maxY = std::max(maxY, y);
                }
            }
        }
        QVERIFY(maxX >= 0);
        // The ink lies inside the laid-out rectangle, and fills most of it horizontally.
        QVERIFY(minX >= rect.left() - 1 && maxX <= rect.right() + 1);
        QVERIFY(minY >= rect.top() - 1 && maxY <= rect.bottom() + 1);
        QVERIFY(maxX - minX > rect.width() * 0.8);
    }
};

QTEST_MAIN(TstTextLayoutCache)
#include "tst_text_layout_cache.moc"
