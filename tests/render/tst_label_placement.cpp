// Where a label's lines sit vertically, against what draw.io does (docs/FIDELITY.md
// class 1): plain labels follow mxSvgCanvas2D.plainText, HTML labels the CSS line box. The
// expectations are the formulas, not measurements of one font: the ink of a capital H ends on the
// baseline, so the row after the last dark row is the baseline.

#include "qraw/render/painter_canvas.h"
#include "qraw/render/text_layout_cache.h"
#include "qraw/render/text_metrics.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextDocument>
#include <QTextLayout>
#include <QImage>
#include <QPainter>
#include <QTest>

#include <cmath>
#include <vector>

using namespace qraw::render;
using qraw::geometry::HAlign;
using qraw::geometry::VAlign;

namespace {

constexpr int kFont = 40;
constexpr double kLine = 1.2;

TextBlock label(const QByteArray& text, bool html, VAlign valign, double y, const QString& family = "Helvetica")
{
    TextBlock b;
    b.text = makeRichText(text.toStdString(), html);
    b.fontFamily = family;
    b.fontSize = kFont;
    b.lineHeight = kLine;
    b.x = 10;
    b.y = y;
    b.width = b.height = 0;
    b.align = HAlign::Left;
    b.verticalAlign = valign;
    b.wrap = false;
    b.fontColor = Qt::black;
    return b;
}

// The baselines of the lines of capital H, from the dark rows of a paint at scale 1.
std::vector<int> baselines(const TextBlock& block)
{
    QImage image(120, 260, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::white);
    {
        QPainter painter(&image);
        PainterCanvas canvas(painter);
        canvas.text(block);
    }
    std::vector<int> result;
    bool inInk = false;
    for (int y = 0; y < image.height(); ++y) {
        bool dark = false;
        for (int x = 0; x < image.width() && !dark; ++x)
            dark = qGray(image.pixel(x, y)) < 128;
        if (inInk && !dark)
            result.push_back(y);
        inInk = dark;
    }
    return result;
}

// What a failure needs to be diagnosed from a CI log alone: the metrics of the font as QFontMetricsF reports
// them and as the first line of the laid-out document has them (they come from the platform's font engine,
// which differs between FreeType, DirectWrite and Core Text), the line height QTextDocumentLayout derives
// (ceil(ascent + descent + leading)) and the document's own numbers.
QString describe(const TextBlock& block)
{
    const QFontMetricsF metrics(diagramFont(block.fontFamily, int(block.fontSize), 0));
    QString text = QStringLiteral("font '%1' %2px: QFontMetricsF ascent %3 descent %4 leading %5 height %6")
                       .arg(QFontInfo(diagramFont(block.fontFamily, int(block.fontSize), 0)).family())
                       .arg(block.fontSize)
                       .arg(metrics.ascent())
                       .arg(metrics.descent())
                       .arg(metrics.leading())
                       .arg(metrics.height());
    QTextDocument doc;
    configureTextDocument(doc, block, 1, 0);
    doc.setTextWidth(-1);
    const LabelMetrics laid = labelMetrics(block);
    text += QStringLiteral("; document %1 high, exact pitch %2, line box tops").arg(doc.size().height()).arg(hasExactLinePitch(doc));
    for (const QRectF& line : laid.lines)
        text += QStringLiteral(" %1").arg(line.top());
    text += QStringLiteral("; placement shift %1 block height %2").arg(laid.placement.shift).arg(laid.placement.blockHeight);
    if (const QTextLayout* layout = doc.begin().layout(); layout != nullptr && layout->lineCount() > 0) {
        const QTextLine line = layout->lineAt(0);
        text += QStringLiteral("; QTextLine ascent %1 descent %2 leading %3 height %4 y %5 ceil(a+d+l) %6")
                    .arg(line.ascent())
                    .arg(line.descent())
                    .arg(line.leading())
                    .arg(line.height())
                    .arg(line.y())
                    .arg(std::ceil(line.ascent() + line.descent() + line.leading()));
    }
    return text;
}

void compare(const TextBlock& block, const std::vector<double>& expected)
{
    const std::vector<int> actual = baselines(block);
    QVERIFY2(actual.size() == expected.size(),
             qPrintable(QStringLiteral("%1 lines of ink, expected %2; %3").arg(actual.size()).arg(expected.size()).arg(describe(block))));
    for (std::size_t i = 0; i < expected.size(); ++i)
        QVERIFY2(std::abs(actual[i] - expected[i]) <= 1.01,
                 qPrintable(QStringLiteral("line %1: baseline %2, expected %3; %4")
                                .arg(i)
                                .arg(actual[i])
                                .arg(expected[i])
                                .arg(describe(block))));
}

} // namespace

class TstLabelPlacement : public QObject {
    Q_OBJECT

private slots:
    // plainText: lh = round(size * 1.2); the block is size + (n - 1) * lh high, its first baseline
    // is size - 1 below its top, a bottom anchored block is one pixel higher. Whatever the font.
    void plainLabelsFollowPlainText_data()
    {
        QTest::addColumn<QString>("family");
        QTest::newRow("Helvetica") << QStringLiteral("Helvetica");
        QTest::newRow("Times") << QStringLiteral("Times New Roman");
        QTest::newRow("Courier") << QStringLiteral("Courier New");
    }
    void plainLabelsFollowPlainText()
    {
        QFETCH(QString, family);
        const double lh = std::round(kFont * kLine);
        for (const int lines : {1, 3}) {
            const QByteArray text = lines == 1 ? QByteArray("H") : QByteArray("H\nH\nH");
            const double block = kFont + (lines - 1) * lh;
            const double y = 100;
            const auto expected = [&](double firstBaseline) {
                std::vector<double> v;
                for (int i = 0; i < lines; ++i)
                    v.push_back(firstBaseline + i * lh);
                return v;
            };
            compare(label(text, false, VAlign::Top, y, family), expected(y + kFont - 1));
            compare(label(text, false, VAlign::Middle, y, family), expected(y - block / 2 + kFont - 1));
            compare(label(text, false, VAlign::Bottom, y + 150, family), expected(y + 150 - block - 1 + kFont - 1));
        }
    }

    // CSS: the line box is `line-height` tall and the font's ascent and descent are centred in it.
    void htmlLabelsFollowTheCssLineBox_data() { plainLabelsFollowPlainText_data(); }
    void htmlLabelsFollowTheCssLineBox()
    {
        QFETCH(QString, family);
        const QFontMetricsF metrics(diagramFont(family, kFont, 0));
        const double lineBox = kLine * kFont;
        const double baselineInLine = lineBox / 2 + (metrics.ascent() - metrics.descent()) / 2;
        for (const int lines : {1, 3}) {
            const QByteArray text = lines == 1 ? QByteArray("H") : QByteArray("H<br>H<br>H");
            const double block = lines * lineBox;
            const double y = 100;
            const auto expected = [&](double top) {
                std::vector<double> v;
                for (int i = 0; i < lines; ++i)
                    v.push_back(top + baselineInLine + i * lineBox);
                return v;
            };
            compare(label(text, true, VAlign::Top, y, family), expected(y));
            compare(label(text, true, VAlign::Middle, y, family), expected(y - block / 2));
            compare(label(text, true, VAlign::Bottom, y + 150, family), expected(y + 150 - block));
        }
    }

    // The pitch of the lines and the height of the block are `line-height` whatever the font's metrics
    // (they come from the platform's font engine: Courier New on Windows gave 2 px more or less per line
    // with Qt's proportional line height), for plain text (rounded, as draw.io does) and HTML.
    void linePitchIsTheLineHeight_data() { plainLabelsFollowPlainText_data(); }
    void linePitchIsTheLineHeight()
    {
        QFETCH(QString, family);
        for (const bool html : {false, true}) {
            const TextBlock block = label(html ? "H<br>H<br>H" : "H\nH\nH", html, VAlign::Top, 0, family);
            const LabelMetrics metrics = labelMetrics(block);
            const double pitch = html ? kLine * kFont : std::round(kLine * kFont);
            const double height = html ? 3 * pitch : kFont + 2 * pitch; // plainText: size + (n - 1) * lh
            QVERIFY2(metrics.lines.size() == 3, qPrintable(describe(block)));
            for (std::size_t i = 1; i < metrics.lines.size(); ++i)
                QVERIFY2(std::abs(metrics.lines[i].top() - metrics.lines[i - 1].top() - pitch) < 0.01,
                         qPrintable(QStringLiteral("%1 line %2 starts %3 below the one before, expected %4; %5")
                                        .arg(html ? "html" : "plain")
                                        .arg(i)
                                        .arg(metrics.lines[i].top() - metrics.lines[i - 1].top())
                                        .arg(pitch)
                                        .arg(describe(block))));
            if (html)
                QVERIFY2(std::abs(metrics.size.height() - height) < 0.01,
                         qPrintable(QStringLiteral("height %1, expected %2; %3").arg(metrics.size.height()).arg(height).arg(describe(block))));
        }
    }

    // Text larger than the label's own keeps a line box of its size: the document does not give every line
    // the pitch of the label's font (a heading, a span with another size).
    void largerTextMakesItsLineTaller()
    {
        const TextBlock plainSized = label("H<br>H", true, VAlign::Top, 0);
        const TextBlock larger = label("H<br><span style=\"font-size: 80px\">H</span>", true, VAlign::Top, 0);
        QVERIFY(labelMetrics(larger).size.height() > labelMetrics(plainSized).size.height() + 40);
    }

    // `<p>row<br></p>` rows are as tall as `<p>row</p>` rows (FIDELITY class 4): the browser ends the
    // line at the <br> and draws no empty one; Qt's rich text did.
    void trailingBreakAddsNoLine()
    {
        const char* rows = "<p style=\"margin: 0px\">a%1</p><p style=\"margin: 0px\">b%1</p><p style=\"margin: 0px\">c</p>";
        const TextBlock withBreaks = label(QByteArray(QString(rows).arg("<br>").toUtf8()), true, VAlign::Top, 0);
        const TextBlock without = label(QByteArray(QString(rows).arg("").toUtf8()), true, VAlign::Top, 0);
        const double expected = labelMetrics(without).size.height();
        QCOMPARE(labelMetrics(withBreaks).size.height(), expected);
        QCOMPARE(labelMetrics(withBreaks).lines.size(), std::size_t(3));
    }

    // The rectangle the display list and the hit tests use is the one the text is painted in.
    void textRectIncludesThePlacement()
    {
        const TextBlock plain = label("H\nH", false, VAlign::Top, 50);
        const QRectF rect = labelTextRect(plain);
        const LabelMetrics metrics = labelMetrics(plain);
        QCOMPARE(rect.top(), metrics.placement.contentTop(50, 0));
        QVERIFY(std::abs(rect.top() - 50) > 0.5); // not simply the anchor: the first baseline is size - 1 down
    }
};

QTEST_MAIN(TstLabelPlacement)
#include "tst_label_placement.moc"
