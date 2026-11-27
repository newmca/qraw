// HTML tables in labels (docs/FIDELITY.md class 5): `width: 100%` and `height: 100%` of a table in a
// box with overflow=fill, row backgrounds, cell padding and the vertical centring of cells, as draw.io's
// browser lays them out.

#include "qraw/render/painter_canvas.h"
#include "qraw/render/text_layout_cache.h"
#include "qraw/render/text_metrics.h"

#include <QImage>
#include <QPainter>
#include <QTest>

#include <cmath>

using namespace qraw::render;
using qraw::geometry::HAlign;
using qraw::geometry::Overflow;
using qraw::geometry::VAlign;

namespace {

constexpr double kBoxW = 200;
constexpr double kBoxH = 120;

const char* const kTable =
    "<table border=\"1\" cellpadding=\"4\" style=\"width: 100%; height: 100%; border-collapse: collapse\">"
    "<tr style=\"background-color: #ff0000\"><td>a</td><td>b</td></tr>"
    "<tr><td>c</td><td>d</td></tr></table>";

TextBlock tableLabel(const char* html, Overflow overflow = Overflow::Fill)
{
    TextBlock b;
    b.text = makeRichText(html, true);
    b.fontFamily = "Helvetica";
    b.fontSize = 12;
    b.lineHeight = 1.2;
    b.x = 0;
    b.y = 0;
    b.width = kBoxW;
    b.height = kBoxH;
    b.align = HAlign::Left;
    b.verticalAlign = VAlign::Top;
    b.wrap = true;
    b.overflow = overflow;
    b.fontColor = Qt::black;
    return b;
}

QImage paint(const TextBlock& block)
{
    QImage image(int(kBoxW) + 20, int(kBoxH) + 20, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    QPainter painter(&image);
    PainterCanvas canvas(painter);
    canvas.text(block);
    return image;
}

// The last row (counted from the top) that is dark in column x.
int lastDarkRow(const QImage& image, int x)
{
    int last = -1;
    for (int y = 0; y < image.height(); ++y) {
        if (qGray(image.pixel(x, y)) < 200)
            last = y;
    }
    return last;
}

} // namespace

class TstHtmlTableLabel : public QObject {
    Q_OBJECT
private slots:
    void fillsTheBox()
    {
        const LabelMetrics metrics = labelMetrics(tableLabel(kTable), 1, kBoxW);
        QCOMPARE(metrics.size.width(), kBoxW);
        QVERIFY2(std::abs(metrics.size.height() - kBoxH) < 2, qPrintable(QString::number(metrics.size.height())));

        // The red row spans the whole width, the table the whole height. Where the table starts depends on the
        // font's metrics (the label placement), so the rows are looked up from the layout's own top.
        const int top = int(std::lround(labelTextRect(tableLabel(kTable)).top()));
        const QImage image = paint(tableLabel(kTable));
        QCOMPARE(image.pixel(kBoxW - 6, top + 8), qRgb(255, 0, 0)); // inside the first row, right of the text
        const int bottom = lastDarkRow(image, 60); // the bottom border
        QVERIFY2(bottom >= top + int(kBoxH) - 5 && bottom <= top + int(kBoxH), qPrintable(QString::number(bottom)));
    }

    void otherLabelsKeepTheirWidth()
    {
        // Without a table or without overflow=fill the layout is the text's own.
        TextBlock plain = tableLabel("<div>text</div>");
        plain.wrap = false;
        QVERIFY(labelMetrics(plain, 1, kBoxW).size.width() < kBoxW / 2);
        TextBlock hidden = tableLabel(kTable, Overflow::Hidden);
        hidden.wrap = false;
        QVERIFY(labelMetrics(hidden, 1, kBoxW).size.width() < kBoxW);
    }

    void cellsAreCentredVertically()
    {
        // Two rows of 60 px, one line of text each: the glyphs of the first row sit around y = 30.
        const QImage image = paint(tableLabel(kTable));
        int top = -1, bottom = -1;
        for (int y = 6; y < 54; ++y) {
            for (int x = 6; x < 20; ++x) {
                if (qGray(image.pixel(x, y)) < 100) {
                    if (top < 0)
                        top = y;
                    bottom = y;
                }
            }
        }
        QVERIFY(top >= 0);
        QVERIFY2(std::abs((top + bottom) / 2.0 - 30) < 6, qPrintable(QString("%1..%2").arg(top).arg(bottom)));
    }
};

QTEST_MAIN(TstHtmlTableLabel)
#include "tst_html_table_label.moc"
