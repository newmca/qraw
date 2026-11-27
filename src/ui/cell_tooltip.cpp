#include "qraw/ui/cell_tooltip.h"

#include "qraw/core/cell.h"
#include "qraw/core/page.h"
#include "qraw/render/text.h"

#include <QStringList>


#include <algorithm>
#include <vector>

namespace qraw::ui {

namespace {

// Graph.prototype.builtInProperties and builtInPropertyPrefixes: they are not listed; `link`
// and `linkTarget` are left out of the lines (the link has its own line), and the wrapper's
// `id` is the cell id, which draw.io's codec takes off the value.
bool isHiddenProperty(const std::string& name)
{
    static const char* const builtIn[] = {"label", "tooltip", "placeholders", "placeholder", "note", "id", "link",
                                          "linkTarget"};
    if (std::find(std::begin(builtIn), std::end(builtIn), name) != std::end(builtIn))
        return true;
    return name.starts_with("plantUml") || name.starts_with("mermaid") || name.starts_with("dataSourceBase");
}

// Text that has no markup is plain text: its line breaks are kept, and "a < b" stays what it says.
// A tag (`<b>`, `</b>`, `<!--`) or an entity (`&amp;`, `&#65;`) makes it HTML, as draw.io treats a tooltip.
bool looksLikeHtml(const std::string& text)
{
    const auto isLetter = [](char c) { return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z'); };
    for (std::size_t i = 0; i + 1 < text.size(); ++i) {
        if (text[i] == '<' && (isLetter(text[i + 1]) || text[i + 1] == '/' || text[i + 1] == '!'))
            return true;
        if (text[i] == '&' && (isLetter(text[i + 1]) || text[i + 1] == '#')) {
            const std::size_t end = text.find(';', i);
            if (end != std::string::npos && end - i <= 10)
                return true;
        }
    }
    return false;
}

QString escaped(const std::string& text)
{
    return QString::fromStdString(text).toHtmlEscaped();
}

} // namespace

QString cellTooltip(const core::Page& page, const core::Cell& cell, const render::PlaceholderVariables& variables,
                    const std::function<QString(const std::string&)>& linkText)
{
    const core::CellWrapper* wrapper = cell.wrapper();
    if (wrapper == nullptr)
        return {};

    QString linkLine;
    if (const std::string* link = wrapper->attributes.find("link"); link != nullptr && !link->empty())
        linkLine = escaped(linkText ? linkText(*link).toStdString() : *link);

    // convertValueToTooltip: a present `tooltip` decides, even when it is empty.
    if (const std::string* tooltip = wrapper->attributes.find("tooltip")) {
        std::string text = *tooltip;
        if (render::cellUsesPlaceholders(cell))
            text = render::replaceCellPlaceholders(page, cell, text, variables);
        QString html;
        if (!text.empty()) {
            html = looksLikeHtml(text) ? QString::fromStdString(render::sanitizeHtml(text))
                                       : escaped(text).replace(QLatin1Char('\n'), QStringLiteral("<br>"));
            html = QStringLiteral("<div>%1</div>").arg(html);
        }
        if (!linkLine.isEmpty())
            html = QStringLiteral("<div><i>%1</i></div>").arg(linkLine) + html;
        return html;
    }

    std::vector<std::pair<std::string, std::string>> properties;
    for (const core::Attribute& attribute : wrapper->attributes) {
        const std::string& value = attribute.value;
        if (!isHiddenProperty(attribute.name) && !value.empty())
            properties.emplace_back(attribute.name, value);
    }
    std::sort(properties.begin(), properties.end());

    QStringList lines;
    if (!linkLine.isEmpty())
        lines.append(QStringLiteral("<i>%1</i>").arg(linkLine));
    for (const auto& [name, value] : properties)
        lines.append(QStringLiteral("<b>%1:</b> %2").arg(escaped(name), escaped(value)));
    if (lines.isEmpty())
        return {};
    return QStringLiteral("<div>%1</div>").arg(lines.join(QStringLiteral("<br>")));
}

} // namespace qraw::ui
