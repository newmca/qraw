#include "qraw/render/label_colors.h"

#include "qraw/render/color.h"

#include <QString>

#include <cstddef>

namespace qraw::render {

namespace {

std::string_view trimmed(std::string_view text)
{
    while (!text.empty() && (text.front() == ' ' || text.front() == '\t'))
        text.remove_prefix(1);
    while (!text.empty() && (text.back() == ' ' || text.back() == '\t'))
        text.remove_suffix(1);
    return text;
}

// A CSS color for `value` in `theme`. Values that are no color (var(), system
// colors, "inherit") stay as written.
std::string adaptCssColor(std::string_view value, const Theme& theme)
{
    value = trimmed(value);
    if (const auto picked = pickLightDark(value, theme.dark))
        return *picked;
    if (!theme.dark || theme.adaptive != AdaptiveColors::Auto)
        return std::string(value);
    const QColor color = parseStyleColor(value, QColor());
    if (!color.isValid() || color.alpha() == 0)
        return std::string(value);
    const QColor adapted = theme.adapt(color);
    if (adapted.alpha() == 255)
        return adapted.name(QColor::HexRgb).toStdString();
    return QStringLiteral("rgba(%1, %2, %3, %4)")
        .arg(adapted.red())
        .arg(adapted.green())
        .arg(adapted.blue())
        .arg(adapted.alphaF(), 0, 'g', 3)
        .toStdString();
}

// The declarations of one style attribute value ("color: red; font-size: 9px;").
std::string adaptStyleValue(std::string_view style, const Theme& theme)
{
    std::string out;
    std::size_t pos = 0;
    while (pos < style.size()) {
        std::size_t end = pos;
        int depth = 0; // a ';' inside rgb(...) does not end the declaration
        while (end < style.size() && (style[end] != ';' || depth > 0)) {
            depth += style[end] == '(' ? 1 : style[end] == ')' ? -1 : 0;
            ++end;
        }
        const std::string_view declaration = trimmed(style.substr(pos, end - pos));
        pos = end + 1;
        if (declaration.empty())
            continue;
        const std::size_t colon = declaration.find(':');
        const std::string_view property = colon == std::string_view::npos ? std::string_view() : trimmed(declaration.substr(0, colon));
        if (property == "color" || property == "background-color")
            out += std::string(property) + ": " + adaptCssColor(declaration.substr(colon + 1), theme) + ";";
        else
            out += std::string(declaration) + ";";
        out += ' ';
    }
    if (!out.empty())
        out.pop_back();
    return out;
}

// Calls `rewrite` on every value of the attribute `name` (` name="..."`) and returns the new markup.
template <typename Rewrite>
std::string rewriteAttribute(std::string_view content, std::string_view name, Rewrite rewrite)
{
    const std::string open = " " + std::string(name) + "=\"";
    std::string out;
    std::size_t pos = 0;
    while (true) {
        const std::size_t at = content.find(open, pos);
        if (at == std::string_view::npos)
            break;
        const std::size_t valueStart = at + open.size();
        const std::size_t valueEnd = content.find('"', valueStart);
        if (valueEnd == std::string_view::npos)
            break;
        out += content.substr(pos, valueStart - pos);
        out += rewrite(content.substr(valueStart, valueEnd - valueStart));
        pos = valueEnd;
    }
    out += content.substr(pos);
    return out;
}

} // namespace

std::string adaptLabelColors(std::string_view content, const Theme& theme)
{
    // Nothing to do without a color, and nothing to change in a light theme without a light-dark() pair.
    if (content.find("color") == std::string_view::npos)
        return std::string(content);
    if (!theme.dark && content.find("light-dark(") == std::string_view::npos)
        return std::string(content);
    const std::string styled =
        rewriteAttribute(content, "style", [&](std::string_view value) { return adaptStyleValue(value, theme); });
    return rewriteAttribute(styled, "color", [&](std::string_view value) { return adaptCssColor(value, theme); });
}

} // namespace qraw::render
