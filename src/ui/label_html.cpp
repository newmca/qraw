#include "label_html.h"

#include "qraw/render/text.h"

#include <QTextBlock>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextFragment>
#include <QTextList>

#include <cmath>

namespace qraw::ui::label {

namespace {

constexpr double kPixelsPerPoint = 96.0 / 72.0; // CSS points to pixels

QString escapeText(const QString& text, bool& previousWasSpace)
{
    QString out;
    for (const QChar c : text) {
        if (c == QLatin1Char('&')) {
            out += QLatin1String("&amp;");
        } else if (c == QLatin1Char('<')) {
            out += QLatin1String("&lt;");
        } else if (c == QLatin1Char('>')) {
            out += QLatin1String("&gt;");
        } else if (c == QChar::Nbsp) {
            out += QLatin1String("&nbsp;");
        } else if (c == QChar::LineSeparator) {
            out += QLatin1String("<br>");
            previousWasSpace = true; // a leading space after a break must not collapse
            continue;
        } else if (c == QLatin1Char(' ')) {
            out += previousWasSpace ? QLatin1String("&nbsp;") : QLatin1String(" ");
            previousWasSpace = true;
            continue;
        } else {
            out += c;
        }
        previousWasSpace = false;
    }
    return out;
}

bool hasBold(const QTextCharFormat& f, const QFont& base)
{
    return f.hasProperty(QTextFormat::FontWeight) ? f.fontWeight() >= QFont::Bold : base.bold();
}
bool hasItalic(const QTextCharFormat& f, const QFont& base)
{
    return f.hasProperty(QTextFormat::FontItalic) ? f.fontItalic() : base.italic();
}
bool hasUnderline(const QTextCharFormat& f, const QFont& base)
{
    return f.hasProperty(QTextFormat::FontUnderline) ? f.fontUnderline() : base.underline();
}
bool hasStrike(const QTextCharFormat& f, const QFont& base)
{
    return f.hasProperty(QTextFormat::FontStrikeOut) ? f.fontStrikeOut() : base.strikeOut();
}

// One fragment as markup: nested wrappers around the escaped text. Wrapper
// order is fixed (a, font, span, b, i, u, s, sub/sup) for stable output.
QString fragmentMarkup(const QTextFragment& fragment, const QFont& base, const QColor& baseColor, double zoom,
                       bool& previousWasSpace)
{
    const QTextCharFormat f = fragment.charFormat();
    QString open;
    QString close;
    const auto wrap = [&](const QString& begin, const QString& end) {
        open += begin;
        close.prepend(end);
    };

    const bool anchor = f.isAnchor();
    if (anchor)
        wrap(QStringLiteral("<a href=\"%1\">").arg(f.anchorHref().toHtmlEscaped()), QStringLiteral("</a>"));
    // Qt styles links itself (blue, underlined): that is not user formatting.
    if (!anchor && f.hasProperty(QTextFormat::ForegroundBrush) && f.foreground().style() != Qt::NoBrush &&
        f.foreground().color() != baseColor)
        wrap(QStringLiteral("<font color=\"%1\">").arg(f.foreground().color().name()), QStringLiteral("</font>"));

    QString declarations;
    const double pixelSize = f.property(QTextFormat::FontPixelSize).toDouble();
    if (pixelSize > 0 && std::lround(pixelSize) != base.pixelSize())
        declarations += QStringLiteral("font-size: %1px; ").arg(std::lround(pixelSize / zoom));
    if (f.hasProperty(QTextFormat::FontFamilies)) {
        const QString family = f.fontFamilies().toStringList().value(0);
        if (!family.isEmpty() && family != base.family())
            declarations += QStringLiteral("font-family: %1; ").arg(family.toHtmlEscaped());
    }
    if (!declarations.isEmpty())
        wrap(QStringLiteral("<span style=\"%1\">").arg(declarations.trimmed()), QStringLiteral("</span>"));

    if (hasBold(f, base) && !base.bold())
        wrap(QStringLiteral("<b>"), QStringLiteral("</b>"));
    if (hasItalic(f, base) && !base.italic())
        wrap(QStringLiteral("<i>"), QStringLiteral("</i>"));
    if (!anchor && hasUnderline(f, base) && !base.underline())
        wrap(QStringLiteral("<u>"), QStringLiteral("</u>"));
    if (hasStrike(f, base) && !base.strikeOut())
        wrap(QStringLiteral("<s>"), QStringLiteral("</s>"));
    if (f.verticalAlignment() == QTextCharFormat::AlignSubScript)
        wrap(QStringLiteral("<sub>"), QStringLiteral("</sub>"));
    else if (f.verticalAlignment() == QTextCharFormat::AlignSuperScript)
        wrap(QStringLiteral("<sup>"), QStringLiteral("</sup>"));

    return open + escapeText(fragment.text(), previousWasSpace) + close;
}

QString blockContent(const QTextBlock& block, const QFont& base, const QColor& baseColor, double zoom)
{
    QString content;
    bool previousWasSpace = true; // the first space of a block would collapse
    for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it)
        content += fragmentMarkup(it.fragment(), base, baseColor, zoom, previousWasSpace);
    return content;
}

QString listTag(const QTextList& list)
{
    switch (list.format().style()) {
    case QTextListFormat::ListDecimal:
    case QTextListFormat::ListLowerAlpha:
    case QTextListFormat::ListUpperAlpha:
    case QTextListFormat::ListLowerRoman:
    case QTextListFormat::ListUpperRoman:
        return QStringLiteral("ol");
    default:
        return QStringLiteral("ul");
    }
}

} // namespace

void scaleExplicitSizes(QTextDocument& document, double factor)
{
    if (std::abs(factor - 1) < 1e-9)
        return;
    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        for (QTextBlock::iterator it = block.begin(); !it.atEnd(); ++it) {
            const QTextFragment fragment = it.fragment();
            QTextCharFormat format = fragment.charFormat();
            double pixels = 0;
            if (format.hasProperty(QTextFormat::FontPixelSize))
                pixels = format.property(QTextFormat::FontPixelSize).toDouble();
            else if (format.hasProperty(QTextFormat::FontPointSize))
                pixels = format.fontPointSize() * kPixelsPerPoint;
            if (pixels <= 0)
                continue;
            format.setProperty(QTextFormat::FontPixelSize, pixels * factor);
            format.clearProperty(QTextFormat::FontPointSize);
            QTextCursor cursor(&document);
            cursor.setPosition(fragment.position());
            cursor.setPosition(fragment.position() + fragment.length(), QTextCursor::KeepAnchor);
            cursor.setCharFormat(format);
        }
    }
}

void loadHtml(QTextDocument& document, std::string_view html, double zoom)
{
    const std::string clean = render::sanitizeHtml(html);
    document.setHtml(QString::fromUtf8(clean.data(), static_cast<qsizetype>(clean.size())));
    scaleExplicitSizes(document, zoom);
}

std::string exportHtml(const QTextDocument& document, double zoom, const QColor& baseColor)
{
    const QFont base = document.defaultFont();
    QString result;
    const QTextList* openList = nullptr;
    bool firstParagraph = true;

    const auto closeList = [&] {
        if (openList != nullptr) {
            result += QStringLiteral("</%1>").arg(listTag(*openList));
            openList = nullptr;
        }
    };

    for (QTextBlock block = document.begin(); block.isValid(); block = block.next()) {
        const QString content = blockContent(block, base, baseColor, zoom);
        if (QTextList* list = block.textList()) {
            if (openList != list) {
                closeList();
                result += QStringLiteral("<%1>").arg(listTag(*list));
                openList = list;
            }
            result += QStringLiteral("<li>%1</li>").arg(content);
            firstParagraph = false;
            continue;
        }
        closeList();
        if (firstParagraph) {
            result += content;
            firstParagraph = false;
        } else {
            result += QStringLiteral("<div>%1</div>").arg(content.isEmpty() ? QStringLiteral("<br>") : content);
        }
    }
    closeList();
    return result.toStdString();
}

} // namespace qraw::ui::label
