// PainterCanvas: pixels drawn into a QImage.


#include "qraw/render/painter_canvas.h"
#include "qraw/render/shape.h"
#include "qraw/render/text_layout_cache.h"

#include <QBuffer>
#include <QImage>
#include <QPainter>
#include <QTest>

using namespace qraw::render;

namespace {

constexpr int kSize = 100;

// Draws with a PainterCanvas onto a transparent image.
template <typename Fn>
QImage render(Fn&& draw, int w = kSize, int h = kSize)
{
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        PainterCanvas canvas(painter);
        draw(static_cast<Canvas&>(canvas));
    }
    return image;
}

// Bounding box of pixels whose alpha is above `threshold`; empty if none.
QRect inkBounds(const QImage& image, int threshold = 0)
{
    QRect box;
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > threshold)
                box = box.united(QRect(x, y, 1, 1));
        }
    }
    return box;
}

QByteArray pngBytes(const QColor& color)
{
    QImage image(8, 8, QImage::Format_ARGB32);
    image.fill(color);
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    image.save(&buffer, "PNG");
    return bytes;
}

} // namespace

class TstPainterCanvas : public QObject {
    Q_OBJECT
private slots:
    // textOnPath: glyphs follow the curve (here the arc curvedText draws for a
    // 120x80 cell) and nothing is drawn away from it.
    void textOnPathFollowsTheArc()
    {
        ResolvedStyle style(StyleMap{}, false);
        const VertexShape* shape = ShapeRegistry::builtin().find("curvedText");
        QVERIFY(shape != nullptr);
        ShapePaintContext context{style, nullptr, {}, false, nullptr};
        context.valueText = "Curved Text";
        const QImage img = render([&](Canvas& c) { shape->paint(c, context, 0, 0, 120, 80); }, 120, 80);
        // Defaults: the middle point is 25 above the chord, so the ink hugs
        // the arc: the middle of the text sits near the top of the curve
        // (y about 27.5) and the ends near y 52.5.
        const QRect ink = inkBounds(img, 100);
        QVERIFY(!ink.isEmpty());
        int topRowInk = 0;
        for (int x = 50; x < 70; ++x)
            for (int y = 15; y < 40; ++y)
                topRowInk += qAlpha(img.pixel(x, y)) > 100;
        QVERIFY2(topRowInk > 5, "the middle of the text is near the top of the arc");
        for (const QPoint corner : {QPoint(2, 2), QPoint(117, 2), QPoint(2, 77), QPoint(117, 77)})
            QCOMPARE(qAlpha(img.pixel(corner)), 0);
        // Nothing above the arc's apex.
        QCOMPARE(inkBounds(img.copy(0, 0, 120, 12), 0), QRect());
    }

    void textOnPathWithoutAPathOrTextDrawsNothing()
    {
        const QImage img = render([](Canvas& c) {
            PathText t;
            t.text = "x";
            c.textOnPath(t); // no path
            t.path.moveTo({0, 50});
            t.path.lineTo({100, 50});
            t.text.clear();
            c.textOnPath(t); // no text
        });
        QCOMPARE(inkBounds(img), QRect());
    }

    void filledRect()
    {
        const QImage img = render([](Canvas& c) {
            c.setFillColor(QColor(255, 0, 0));
            c.begin();
            c.rect(20, 30, 40, 20);
            c.fill();
        });
        QCOMPARE(img.pixelColor(40, 40), QColor(255, 0, 0));
        QCOMPARE(qAlpha(img.pixel(10, 40)), 0);
        QCOMPARE(qAlpha(img.pixel(40, 60)), 0);
        QCOMPARE(inkBounds(img), QRect(20, 30, 40, 20));
    }

    // The path is consumed by fill()/stroke(): a shape that calls rect()
    // without begin() must not repaint the earlier shape (which used to wipe
    // out the label drawn on top of it).
    void paintConsumesPath()
    {
        const QImage img = render([](Canvas& c) {
            c.setFillColor(QColor(255, 0, 0));
            c.begin();
            c.rect(10, 10, 20, 20);
            c.fill();
            c.setFillColor(QColor(0, 0, 255));
            c.rect(60, 10, 20, 20); // no begin()
            c.fill();
        });
        QCOMPARE(img.pixelColor(20, 20), QColor(255, 0, 0));
        QCOMPARE(img.pixelColor(70, 20), QColor(0, 0, 255));
    }

    void strokeWidth()
    {
        const QImage img = render([](Canvas& c) {
            c.setStrokeColor(QColor(0, 0, 255));
            c.setStrokeWidth(4);
            c.begin();
            c.moveTo(10, 50);
            c.lineTo(90, 50);
            c.stroke();
        });
        QCOMPARE(img.pixelColor(50, 48), QColor(0, 0, 255));
        QCOMPARE(img.pixelColor(50, 51), QColor(0, 0, 255));
        QCOMPARE(qAlpha(img.pixel(50, 46)), 0);
        QCOMPARE(qAlpha(img.pixel(50, 54)), 0);
    }

    void alphaMultiplies()
    {
        const QImage img = render([](Canvas& c) {
            c.setFillColor(QColor(0, 0, 0));
            c.setAlpha(0.5);
            c.begin();
            c.rect(10, 10, 50, 50);
            c.fill();
        });
        QVERIFY(qAbs(qAlpha(img.pixel(30, 30)) - 128) <= 2);
    }

    void scaleAndTranslate()
    {
        const QImage img = render([](Canvas& c) {
            c.translate(10, 10);
            c.scale(2);
            c.setFillColor(QColor(0, 128, 0));
            c.begin();
            c.rect(5, 5, 10, 10); // (x + 10) * 2
            c.fill();
        });
        QCOMPARE(inkBounds(img), QRect(30, 30, 20, 20));
    }

    void rotateMovesBar()
    {
        const QImage img = render([](Canvas& c) {
            c.rotate(90, false, false, 50, 50);
            c.setFillColor(QColor(0, 0, 0));
            c.begin();
            c.rect(10, 45, 80, 10); // horizontal bar
            c.fill();
        });
        const QRect box = inkBounds(img, 128);
        QVERIFY2(qAbs(box.width() - 10) <= 1 && qAbs(box.height() - 80) <= 1, qPrintable(QString::number(box.width()) + "x" + QString::number(box.height())));
        QCOMPARE(box.center().x(), 49);
    }

    void flipMirrorsAroundCenter()
    {
        const QImage img = render([](Canvas& c) {
            c.rotate(0, true, false, 50, 50);
            c.setFillColor(QColor(0, 0, 0));
            c.begin();
            c.rect(10, 40, 20, 20);
            c.fill();
        });
        QCOMPARE(inkBounds(img), QRect(70, 40, 20, 20));
    }

    void shadowIsOffset()
    {
        const QImage img = render([](Canvas& c) {
            c.setFillColor(QColor(255, 0, 0));
            c.setShadow(true);
            c.begin();
            c.rect(20, 20, 30, 30);
            c.fill();
        });
        QCOMPARE(img.pixelColor(30, 30), QColor(255, 0, 0));           // shape on top
        QVERIFY(qAlpha(img.pixel(51, 52)) > 0);                        // shadow +2,+3
        QVERIFY(qAbs(qAlpha(img.pixel(51, 52)) - 64) <= 2);            // 0.25
        QCOMPARE(qAlpha(img.pixel(21, 51)), 0);                        // not left of the offset
    }

    void dashedLeavesGaps()
    {
        const QImage img = render([](Canvas& c) {
            c.setStrokeColor(QColor(0, 0, 0));
            c.setStrokeWidth(2);
            c.setDashed(true, false);
            c.begin();
            c.moveTo(0, 50);
            c.lineTo(100, 50);
            c.stroke();
        });
        int opaque = 0;
        for (int x = 0; x < kSize; ++x)
            opaque += qAlpha(img.pixel(x, 50)) > 200;
        QVERIFY(opaque > 30 && opaque < 70); // 6 on / 6 off
    }

    void gradientRunsTopToBottom()
    {
        const QImage img = render([](Canvas& c) {
            c.setGradient(QColor(255, 255, 255), QColor(0, 0, 0), {0, 0, 100, 100}, GradientDirection::South);
            c.begin();
            c.rect(0, 0, 100, 100);
            c.fill();
        });
        QVERIFY(qRed(img.pixel(50, 5)) > 220);
        QVERIFY(qRed(img.pixel(50, 95)) < 40);
    }

    void roundrectAndEllipseCoverCenter()
    {
        const QImage img = render([](Canvas& c) {
            c.setFillColor(QColor(0, 0, 0));
            c.begin();
            c.roundrect(0, 0, 40, 40, 20, 20);
            c.ellipse(50, 50, 40, 40);
            c.fill();
        });
        QCOMPARE(qAlpha(img.pixel(1, 1)), 0);  // rounded corner
        QCOMPARE(qAlpha(img.pixel(20, 20)), 255);
        QCOMPARE(qAlpha(img.pixel(51, 51)), 0); // ellipse corner
        QCOMPARE(qAlpha(img.pixel(70, 70)), 255);
    }

    void textDrawsNearAnchor()
    {
        const QImage img = render([](Canvas& c) {
            TextBlock b;
            b.x = 50;
            b.y = 50;
            b.text = makeRichText("Hello", false);
            b.fontSize = 16;
            c.text(b);
        });
        const QRect ink = inkBounds(img, 100);
        QVERIFY(!ink.isEmpty());
        QVERIFY(qAbs(ink.center().x() - 50) <= 4);
        QVERIFY(qAbs(ink.center().y() - 50) <= 8);
    }

    void htmlTextUsesMarkup()
    {
        const QImage plain = render([](Canvas& c) {
            TextBlock b;
            b.x = 50;
            b.y = 50;
            b.text = makeRichText("one<br>two", true);
            c.text(b);
        });
        QVERIFY(inkBounds(plain, 100).height() > 14); // two lines
    }

    void wrapAddsLines()
    {
        auto drawn = [](bool wrap) {
            return render([wrap](Canvas& c) {
                TextBlock b;
                b.x = 50;
                b.y = 50;
                b.width = 40;
                b.height = 40;
                b.wrap = wrap;
                b.text = makeRichText("alpha beta gamma delta", false);
                b.fontSize = 12;
                c.text(b);
            });
        };
        const QRect wrapped = inkBounds(drawn(true), 100);
        const QRect single = inkBounds(drawn(false), 100);
        QVERIFY(wrapped.height() > single.height() + 8);
        QVERIFY(wrapped.width() < single.width());
    }

    void clipCutsOverflow()
    {
        const QImage img = render([](Canvas& c) {
            TextBlock b;
            b.x = 50;
            b.y = 50;
            b.width = 20;
            b.height = 20;
            b.clip = true;
            b.text = makeRichText("WWWWWWWWWWWWWWWW", false);
            b.fontSize = 14;
            c.text(b);
        });
        const QRect ink = inkBounds(img, 20);
        QVERIFY(!ink.isEmpty());
        QVERIFY(ink.left() >= 40 && ink.right() <= 60);
    }

    void textBackgroundBehindText()
    {
        TextBlock b;
        b.x = 50;
        b.y = 50;
        b.text = makeRichText("W", false);
        b.backgroundColor = QColor(255, 255, 0);
        b.padding = 4;
        const QImage img = render([&](Canvas& c) { c.text(b); });
        // The frame is the text's rectangle grown by the padding; where that is depends on the font's
        // metrics, so it is taken from the layout rather than assumed. A pixel in the padding above the
        // text is the background, one beyond the padding is not.
        const QRectF text = labelTextRect(b);
        const int x = int(text.center().x());
        QCOMPARE(img.pixelColor(x, int(text.top() - b.padding / 2)), QColor(255, 255, 0));
        QCOMPARE(img.pixelColor(x, int(text.top() - b.padding - 2)).alpha(), 0);
        QCOMPARE(img.pixelColor(x, int(text.bottom() + b.padding / 2)), QColor(255, 255, 0));
    }

    void imageIsDrawn()
    {
        const QImage img = render([](Canvas& c) {
            c.image(10, 10, 40, 20, ImageData{pngBytes(QColor(0, 255, 0)), QStringLiteral("image/png")}, false, false, false);
            c.image(10, 50, 40, 20, ImageData{pngBytes(QColor(0, 0, 255)), QStringLiteral("image/png")}, true, false, false);
            c.image(0, 80, 10, 10, ImageData{QByteArray("junk"), QStringLiteral("image/png")}, false, false, false);
        });
        QCOMPARE(img.pixelColor(30, 20), QColor(0, 255, 0));
        QCOMPARE(inkBounds(img.copy(0, 0, 100, 40)), QRect(10, 10, 40, 20));
        // aspect: the square image is fitted into 40x20 -> 20x20 centered
        QCOMPARE(inkBounds(img.copy(0, 40, 100, 40)), QRect(20, 10, 20, 20));
        QCOMPARE(qAlpha(img.pixel(5, 85)), 0); // undecodable draws nothing
    }

    void groupsAreNoOps()
    {
        const QImage img = render([](Canvas& c) {
            c.beginGroup("x");
            c.setFillColor(QColor(0, 0, 0));
            c.begin();
            c.rect(0, 0, 10, 10);
            c.fill();
            c.endGroup();
            c.endGroup();
        });
        QCOMPARE(inkBounds(img), QRect(0, 0, 10, 10));
    }
};

QTEST_MAIN(TstPainterCanvas)
#include "tst_painter_canvas.moc"
