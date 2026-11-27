// FindEngine: the search and replacement behind the Find panel (draw.io's
// FindWindow, diagramly).
#include "docks_find_engine.h"

#include "qraw/core/cell.h"
#include "qraw/core/document.h"
#include "qraw/core/page.h"
#include "qraw/core/style.h"

#include <QTextDocumentFragment>

#include <optional>
#include <vector>

namespace qraw::ui {

namespace {

// Attributes of a wrapper that Find does not search (FindWindow skips the
// id, the label, which is the cell's value, and the placeholders).
bool isSearchedAttribute(const std::string& name)
{
    return name != "id" && name != "label" && name != "placeholders";
}

// The pages to search, in order: all of them from the current page on (and
// then the ones before it) when allPages, otherwise the current page only.
std::vector<std::size_t> pageOrder(std::size_t count, std::size_t current, bool allPages)
{
    std::vector<std::size_t> order;
    if (count == 0)
        return order;
    if (!allPages) {
        order.push_back(current < count ? current : 0);
        return order;
    }
    for (std::size_t i = 0; i < count; ++i)
        order.push_back((current + i) % count);
    return order;
}

// The shape type of a cell: the `shape` style value, else the first bare
// style name (ellipse, text, swimlane, ...), else "rectangle" for vertices and
// "connector" for edges.
std::string shapeTypeOf(const core::Cell& cell)
{
    const core::Style style = cell.style();
    if (std::optional<std::string> shape = style.value("shape"); shape && !shape->empty())
        return *shape;
    for (const core::Style::Token& token : style.tokens()) {
        if (token.isName())
            return token.key;
    }
    return cell.isEdge() ? "connector" : "rectangle";
}

void appendCellMatches(std::size_t page, const core::Cell& cell, const QRegularExpression& pattern,
                       const FindOptions& options, std::vector<FindMatch>& out)
{
    const QString label = docks::plainLabelText(QString::fromStdString(cell.value()), docks::isHtmlLabel(cell));
    if (docks::containsMatch(label, pattern))
        out.push_back(FindMatch{page, cell.id(), "label"});
    // The root and the layers have no shape.
    const bool drawn = cell.isVertex() || cell.isEdge();
    if (options.shapes && drawn && docks::containsMatch(QString::fromStdString(shapeTypeOf(cell)), pattern))
        out.push_back(FindMatch{page, cell.id(), kShapeMatchAttribute});

    if (!options.includeData || cell.wrapper() == nullptr)
        return;
    for (const auto& attribute : cell.wrapper()->attributes) {
        if (isSearchedAttribute(attribute.name) && docks::containsMatch(QString::fromStdString(attribute.value), pattern))
            out.push_back(FindMatch{page, cell.id(), attribute.name});
    }
}

// The basic entities a text node may use. "&amp;" is decoded last so that
// "&amp;lt;" becomes the text "&lt;" and not "<".
QString decodeEntities(QString text)
{
    text.replace(QStringLiteral("&nbsp;"), QString(QChar(0x00A0)));
    text.replace(QStringLiteral("&lt;"), QStringLiteral("<"));
    text.replace(QStringLiteral("&gt;"), QStringLiteral(">"));
    text.replace(QStringLiteral("&quot;"), QStringLiteral("\""));
    text.replace(QStringLiteral("&#39;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&apos;"), QStringLiteral("'"));
    text.replace(QStringLiteral("&amp;"), QStringLiteral("&"));
    return text;
}

QString encodeText(QString text)
{
    text.replace(QLatin1Char('&'), QStringLiteral("&amp;"));
    text.replace(QLatin1Char('<'), QStringLiteral("&lt;"));
    text.replace(QLatin1Char('>'), QStringLiteral("&gt;"));
    return text;
}

QString replaceTextNode(const QString& raw, const std::function<QString(const QString&)>& replaceText)
{
    const QString decoded = decodeEntities(raw);
    const QString replaced = replaceText(decoded);
    return replaced == decoded ? raw : encodeText(replaced);
}

} // namespace

namespace docks {

QRegularExpression findPattern(const QString& text, const FindOptions& options)
{
    QString body = options.regex ? text : QRegularExpression::escape(text);
    if (options.wholeWord)
        body = QStringLiteral("\\b") + body + QStringLiteral("\\b");
    QRegularExpression::PatternOptions flags = QRegularExpression::NoPatternOption;
    if (!options.matchCase)
        flags |= QRegularExpression::CaseInsensitiveOption;
    return QRegularExpression(body, flags);
}

bool containsMatch(const QString& text, const QRegularExpression& pattern)
{
    QRegularExpressionMatchIterator it = pattern.globalMatch(text);
    while (it.hasNext()) {
        if (it.next().capturedLength() > 0)
            return true;
    }
    return false;
}

QString replaceMatches(const QString& fragment, const QRegularExpression& pattern, const QString& replacement,
                       const FindOptions& options)
{
    if (options.regex) {
        QString out = fragment;
        out.replace(pattern, replacement);
        return out;
    }
    QString out;
    qsizetype last = 0;
    QRegularExpressionMatchIterator it = pattern.globalMatch(fragment);
    while (it.hasNext()) {
        const QRegularExpressionMatch match = it.next();
        if (match.capturedLength() == 0)
            continue;
        out += fragment.mid(last, match.capturedStart() - last);
        out += replacement;
        last = match.capturedEnd();
    }
    out += fragment.mid(last);
    return out;
}

QString replaceInMarkup(const QString& markup, const std::function<QString(const QString&)>& replaceText)
{
    QString out;
    qsizetype pos = 0;
    while (pos < markup.size()) {
        if (markup.at(pos) == QLatin1Char('<')) {
            qsizetype end = markup.indexOf(QLatin1Char('>'), pos);
            end = end < 0 ? markup.size() : end + 1;
            out += markup.mid(pos, end - pos);
            pos = end;
        } else {
            qsizetype next = markup.indexOf(QLatin1Char('<'), pos);
            if (next < 0)
                next = markup.size();
            out += replaceTextNode(markup.mid(pos, next - pos), replaceText);
            pos = next;
        }
    }
    return out;
}

QString plainLabelText(const QString& label, bool html)
{
    return html ? QTextDocumentFragment::fromHtml(label).toPlainText() : label;
}

bool isHtmlLabel(const core::Cell& cell)
{
    return cell.style().value("html", "0") == "1";
}

} // namespace docks

std::vector<FindMatch> FindEngine::findAll(const core::Document& document, std::size_t currentPage,
                                           const QString& text, const FindOptions& options)
{
    std::vector<FindMatch> matches;
    const QRegularExpression pattern = docks::findPattern(text, options);
    if (text.isEmpty() || !pattern.isValid())
        return matches;

    const auto pages = document.pages();
    for (std::size_t index : pageOrder(pages.size(), currentPage, options.allPages)) {
        for (const core::Cell* cell : pages[index]->cells())
            appendCellMatches(index, *cell, pattern, options, matches);
    }
    return matches;
}

std::string FindEngine::replaceIn(const std::string& value, bool html, const QString& text,
                                  const QString& replacement, const FindOptions& options)
{
    const QRegularExpression pattern = docks::findPattern(text, options);
    if (text.isEmpty() || !pattern.isValid())
        return value;
    const auto replaceText = [&](const QString& fragment) {
        return docks::replaceMatches(fragment, pattern, replacement, options);
    };
    const QString raw = QString::fromStdString(value);
    const QString result = html ? docks::replaceInMarkup(raw, replaceText) : replaceText(raw);
    return result.toStdString();
}

} // namespace qraw::ui
