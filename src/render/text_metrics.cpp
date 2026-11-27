// Label text layout shared by PainterCanvas::text and the autosize/editor
// code (text_metrics.h).

#include "qraw/render/text_metrics.h"

#include "qraw/render/stats.h"
#include "qraw/render/text_layout_cache.h"

#include <QAbstractTextDocumentLayout>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QScreen>
#include <QFont>
#include <QFontMetricsF>
#include <QImage>
#include <QPaintDevice>
#include <QTextBlock>
#include <QTextBlockFormat>
#include <QTextLayout>
#include <QTextLine>
#include <QTextFragment>
#include <QTextCursor>
#include <QTextOption>
#include <QTextTable>
#include <QTextTableFormat>

#include <algorithm>
#include <cmath>
#include <string>
#include <unordered_map>

namespace qraw::render {

QString firstFontFamily(const QString& cssList)
{
    QString first = cssList.section(QLatin1Char(','), 0, 0).trimmed();
    if (first.size() >= 2 && (first.startsWith(QLatin1Char('"')) || first.startsWith(QLatin1Char('\''))))
        first = first.mid(1, first.size() - 2);
    return first.isEmpty() ? QStringLiteral("Helvetica") : first;
}

QFont diagramFont(const QString& family, int pixelSize, int fontStyle)
{
    QFont font(family);
    font.setPixelSize(std::max(1, pixelSize));
    font.setHintingPreference(QFont::PreferNoHinting);
    font.setBold(fontStyle & FontBold);
    font.setItalic(fontStyle & FontItalic);
    font.setUnderline(fontStyle & FontUnderline);
    font.setStrikeOut(fontStyle & FontStrikethrough);
    return font;
}

namespace {

// The device whose resolution a text document converts point sizes with. HTML labels can say
// "font-size: 12pt" and <font size="5"> (relative, point based); a browser has 96 CSS pixels to the
// inch whatever the screen is, but Qt takes the logical DPI of the platform: 72 on macOS, the
// QT_FONT_DPI or Xft.dpi value when it is below 96. A QImage is a paint device whose DPI is its
// own (dots per metre), and it holds no pixels that anything reads.
QPaintDevice* cssResolutionDevice()
{
    static QImage device = [] {
        QImage image(1, 1, QImage::Format_ARGB32_Premultiplied);
        constexpr int kCssDotsPerMetre = 3780; // 96 dpi
        image.setDotsPerMeterX(kCssDotsPerMetre);
        image.setDotsPerMeterY(kCssDotsPerMetre);
        return image;
    }();
    return &device;
}

// Qt's HTML importer gives blocks margins in absolute pixels (12 around a paragraph, a list, a quote
// or a pre; 18/12 around an <h1>, ...) where browsers use em, the size of the element's own font
// (user agent style sheet: p, ul, ol, blockquote, pre 1em; h1 .67em, h2 .83em, h3 1em, h4 1.33em,
// h5 1.67em). The two agree only at 12 px. A block that has the importer's default margins gets the
// browser's; one that was given margins by a style attribute keeps them. Block margins collapse in
// both, and (Qt drops the first block's top and the last block's bottom margin, a browser's label box
// keeps them) LabelPlacement adds those back.
void useBrowserBlockMargins(QTextDocument& doc, double defaultPixelSize)
{
    struct Margins {
        double top, bottom;   // the importer's default, pixels
        double topEm, bottomEm; // the browser's, em
    };
    static constexpr Margins kParagraph{12, 12, 1, 1};
    static constexpr Margins kHeadings[] = {{18, 12, 0.67, 0.67}, {16, 12, 0.83, 0.83}, {14, 12, 1, 1},
                                            {12, 12, 1.33, 1.33}, {12, 4, 1.67, 1.67}};
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        const int level = format.headingLevel();
        if (level > 5)
            continue;
        const Margins& expected = level == 0 ? kParagraph : kHeadings[level - 1];
        if (format.topMargin() != expected.top || format.bottomMargin() != expected.bottom)
            continue; // set by a style attribute
        double fontPixels = defaultPixelSize;
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QFont font = it.fragment().charFormat().font();
            if (font.pixelSize() > 0) {
                fontPixels = font.pixelSize();
                break;
            }
        }
        format.setTopMargin(expected.topEm * fontPixels);
        format.setBottomMargin(expected.bottomEm * fontPixels);
        QTextCursor(block).setBlockFormat(format);
    }
}

// <hr> is a two pixel rule with .5em above and below (the user agent style sheet: `margin: .5em auto;
// border: 1px inset`); Qt's is an empty line with a rule in its middle. Run after the line heights are set.
void useBrowserRules(QTextDocument& doc, double defaultPixelSize)
{
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        QTextBlockFormat format = block.blockFormat();
        if (!format.hasProperty(QTextFormat::BlockTrailingHorizontalRulerWidth))
            continue;
        constexpr double kRuleHeight = 2;
        format.setLineHeight(kRuleHeight, QTextBlockFormat::FixedHeight);
        format.setTopMargin(0.5 * defaultPixelSize);
        format.setBottomMargin(0.5 * defaultPixelSize);
        QTextCursor(block).setBlockFormat(format);
    }
}

// Table cells are vertically centred in a browser (`td { vertical-align: inherit }` over `tr { vertical-align:
// middle }`), at the top in Qt's importer; a cell that sets its own vertical-align keeps it.
void useBrowserCellAlignment(QTextDocument& doc)
{
    for (QTextFrame* frame : doc.rootFrame()->childFrames()) {
        auto* table = qobject_cast<QTextTable*>(frame);
        if (table == nullptr)
            continue;
        for (int row = 0; row < table->rows(); ++row) {
            for (int column = 0; column < table->columns(); ++column) {
                QTextTableCell cell = table->cellAt(row, column);
                QTextCharFormat format = cell.format();
                if (cell.row() != row || cell.column() != column ||
                    format.verticalAlignment() != QTextCharFormat::AlignNormal)
                    continue;
                format.setVerticalAlignment(QTextCharFormat::AlignMiddle);
                cell.setFormat(format);
            }
        }
    }
}

// A table with `border-collapse: collapse` and a border (`border="1"`, or a border style on the table) is
// drawn with that border by newer Qt: the table's width on the outer edges, one pixel between cells. Qt 6.8
// draws only the borders the cells carry themselves, so such a table showed no lines at all. Cells that
// have no border of their own get what the newer Qt paints for them, which changes nothing there.
void useCollapsedTableBorders(QTextDocument& doc)
{
    constexpr QTextFormat::Property kCellBorders[] = {
        QTextFormat::TableCellTopBorder,      QTextFormat::TableCellBottomBorder,
        QTextFormat::TableCellLeftBorder,     QTextFormat::TableCellRightBorder,
        QTextFormat::TableCellTopBorderBrush, QTextFormat::TableCellTopBorderStyle};
    for (QTextFrame* frame : doc.rootFrame()->childFrames()) {
        auto* table = qobject_cast<QTextTable*>(frame);
        if (table == nullptr)
            continue;
        const QTextTableFormat tableFormat = table->format();
        const qreal outer = tableFormat.border();
        if (outer <= 0 || !tableFormat.borderCollapse())
            continue;
        for (int row = 0; row < table->rows(); ++row) {
            for (int column = 0; column < table->columns(); ++column) {
                QTextTableCell cell = table->cellAt(row, column);
                if (cell.row() != row || cell.column() != column)
                    continue; // covered by a span; the cell is handled at its origin
                QTextTableCellFormat format = cell.format().toTableCellFormat();
                if (std::any_of(std::begin(kCellBorders), std::end(kCellBorders),
                                [&](QTextFormat::Property p) { return format.hasProperty(p); }))
                    continue;
                format.setTopBorder(row == 0 ? outer : 1);
                format.setBottomBorder(row + cell.rowSpan() == table->rows() ? outer : 1);
                format.setLeftBorder(column == 0 ? outer : 1);
                format.setRightBorder(column + cell.columnSpan() == table->columns() ? outer : 1);
                format.setBorderBrush(tableFormat.borderBrush());
                format.setBorderStyle(tableFormat.borderStyle());
                cell.setFormat(format);
            }
        }
    }
}

} // namespace

double naturalLineHeight(const QFont& font)
{
    // Measured, not derived from QFontMetricsF: Qt's text layout rounds the height of a line up and adds the
    // font's leading to it, which QFontMetricsF::height() leaves out (Times New Roman on macOS has a leading
    // of 4 % of its size; FreeType fonts report none). A CSS line height is applied as a percentage of this
    // number, so a number that differs from the layout's own moved every line of such a font by the leading.
    thread_local std::unordered_map<std::string, double> cache;
    const std::string key = font.key().toStdString();
    if (const auto it = cache.find(key); it != cache.end())
        return it->second;
    QTextDocument probe;
    probe.setDocumentMargin(0);
    probe.documentLayout()->setPaintDevice(cssResolutionDevice());
    probe.setDefaultFont(font);
    probe.setPlainText(QStringLiteral("x"));
    probe.setTextWidth(-1);
    double height = probe.size().height(); // lays the document out
    if (const QTextLayout* layout = probe.begin().layout(); layout != nullptr && layout->lineCount() > 0)
        height = layout->lineAt(0).height();
    if (cache.size() >= 256)
        cache.clear();
    cache.emplace(key, height);
    return height;
}

namespace {

constexpr const char* kExactLinePitch = "qraw.exactLinePitch";

// What QTextDocumentLayout multiplies a FixedHeight line height with: the logical DPI of the document's paint
// device over Qt's default DPI (72 on macOS, the screen's elsewhere, 96 with AA_Use96Dpi, 75 without a
// GUI application). Measured on a probe instead of derived from Qt's private default, and cached for as
// long as what it depends on does not change.
double fixedLineHeightScale()
{
    thread_local int cachedKey = -1;
    thread_local double cachedScale = 1;
    const auto* gui = qobject_cast<QGuiApplication*>(QCoreApplication::instance());
    const QScreen* screen = gui != nullptr ? QGuiApplication::primaryScreen() : nullptr;
    const int key = (screen != nullptr ? 1 + std::lround(screen->logicalDotsPerInchY()) : 0) * 4 +
                    (gui != nullptr ? 2 : 0) + (QCoreApplication::testAttribute(Qt::AA_Use96Dpi) ? 1 : 0);
    if (key == cachedKey)
        return cachedScale;
    QTextDocument probe;
    probe.setDocumentMargin(0);
    probe.documentLayout()->setPaintDevice(cssResolutionDevice());
    probe.setPlainText(QStringLiteral("x\nx"));
    constexpr double kProbeHeight = 100;
    QTextBlockFormat format;
    format.setLineHeight(kProbeHeight, QTextBlockFormat::FixedHeight);
    QTextCursor cursor(&probe);
    cursor.select(QTextCursor::Document);
    cursor.mergeBlockFormat(format);
    probe.setTextWidth(-1);
    const double height = probe.size().height(); // two lines
    cachedScale = height > 0 ? height / (2 * kProbeHeight) : 1;
    cachedKey = key;
    return cachedScale;
}

// Whether the text of `doc` is of one size throughout: no fragment with another font size, no sub- or
// superscript, no image. A line of such a document is as tall as the line-height says.
bool hasOneFontSize(const QTextDocument& doc, double defaultPixelSize)
{
    for (QTextBlock block = doc.begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextCharFormat format = it.fragment().charFormat();
            if (format.isImageFormat() || format.verticalAlignment() != QTextCharFormat::AlignNormal ||
                format.hasProperty(QTextFormat::FontSizeAdjustment) ||
                format.hasProperty(QTextFormat::FontSizeIncrement))
                return false;
            double size = defaultPixelSize;
            if (format.hasProperty(QTextFormat::FontPixelSize))
                size = format.property(QTextFormat::FontPixelSize).toDouble();
            else if (format.hasProperty(QTextFormat::FontPointSize))
                size = format.property(QTextFormat::FontPointSize).toDouble() * 96.0 / 72.0; // the 96 dpi device
            if (std::abs(size - defaultPixelSize) > 0.01)
                return false;
        }
    }
    return true;
}

} // namespace

bool hasExactLinePitch(const QTextDocument& doc)
{
    return doc.property(kExactLinePitch).toBool();
}

void configureTextDocument(QTextDocument& doc, const TextBlock& b, double scale, double wrapWidth)
{
    const QFont font = diagramFont(firstFontFamily(b.fontFamily), int(std::lround(b.fontSize * scale)), b.fontStyle);

    const bool wrap = b.wrap && wrapWidth > 0;
    doc.setDocumentMargin(0);
    doc.documentLayout()->setPaintDevice(cssResolutionDevice());
    doc.setDefaultFont(font);
    QTextOption option;
    option.setAlignment(b.align == geometry::HAlign::Left     ? Qt::AlignLeft
                        : b.align == geometry::HAlign::Right ? Qt::AlignRight
                                                              : Qt::AlignHCenter);
    option.setWrapMode(wrap ? QTextOption::WrapAtWordBoundaryOrAnywhere : QTextOption::NoWrap);
    doc.setDefaultTextOption(option);
    const QString content = QString::fromUtf8(b.text.content.data(), qsizetype(b.text.content.size()));
    if (b.text.html) {
        doc.setHtml(content);
        useBrowserBlockMargins(doc, font.pixelSize());
        useBrowserCellAlignment(doc);
        useCollapsedTableBorders(doc);
    } else
        doc.setPlainText(content);

    // CSS line-height as a multiple of the font size. The pitch of the lines is `lineHeight * size` exactly
    // (draw.io rounds it for plain text: mxSvgCanvas2D.plainText), whatever the metrics of the font: Qt's
    // own proportional line height is a percentage of the height it measures for the line, which differs
    // between platforms and font engines (Courier New on Windows came out 2 px off per line). Where the font
    // size is the same throughout, every line gets a fixed height; text of several sizes (a heading, a
    // span with another size, an image) keeps the proportional height, which grows with the larger line as
    // a CSS line box does. Where the first baseline lies is in LabelPlacement.
    if (b.lineHeight > 0) {
        const double pitch = b.lineHeight * font.pixelSize();
        const double exactPitch = b.text.html ? pitch : std::round(pitch);
        QTextBlockFormat format;
        if (!b.text.html || hasOneFontSize(doc, font.pixelSize())) {
            format.setLineHeight(exactPitch / fixedLineHeightScale(), QTextBlockFormat::FixedHeight);
            doc.setProperty(kExactLinePitch, true);
        } else if (const double natural = naturalLineHeight(font); natural > 0) {
            format.setLineHeight(100.0 * pitch / natural, QTextBlockFormat::ProportionalHeight);
        }
        if (format.lineHeightType() != QTextBlockFormat::SingleHeight) {
            QTextCursor cursor(&doc);
            cursor.select(QTextCursor::Document);
            cursor.mergeBlockFormat(format);
        }
    }
    if (b.text.html)
        useBrowserRules(doc, font.pixelSize());
}

QSizeF measureText(const TextBlock& block, std::optional<double> wrapWidth)
{
    if (block.text.empty())
        return {};
    return labelMetrics(block, 1, wrapWidth.value_or(0)).size;
}

TextBlock textBlockForValue(const ResolvedStyle& style, std::string_view value)
{
    TextBlock block;
    block.text = makeRichText(value, style.html());
    block.align = style.labelParams().align;
    block.verticalAlign = style.labelParams().verticalAlign;
    block.wrap = style.wrap();
    block.fontFamily = QString::fromUtf8(style.fontFamily().data(), static_cast<qsizetype>(style.fontFamily().size()));
    block.fontSize = style.fontSize();
    block.fontStyle = style.fontStyle();
    block.fontColor = style.fontColor().value_or(QColor(0, 0, 0));
    return block;
}

} // namespace qraw::render
