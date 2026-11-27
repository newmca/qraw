#include "svg_writer.h"

#include <cmath>
#include <cstdio>

namespace qraw::render::svg {

std::string number(double value)
{
    if (!std::isfinite(value))
        return "0";
    constexpr double kLimit = 1e15; // keeps value * 100 finite and exactly representable enough
    value = std::fmax(-kLimit, std::fmin(kLimit, value));
    const double rounded = std::round(value * 100) / 100;
    if (rounded == 0)
        return "0";
    char buffer[40];
    std::snprintf(buffer, sizeof buffer, "%.2f", rounded);
    std::string text(buffer);
    while (text.back() == '0')
        text.pop_back();
    if (text.back() == '.')
        text.pop_back();
    return text;
}

std::string hexColor(const QColor& color) { return color.name(QColor::HexRgb).toStdString(); }

namespace {

void appendEscaped(std::string& out, std::string_view text, bool attribute)
{
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"':
            out += attribute ? "&quot;" : "\"";
            break;
        case '\n':
            out += attribute ? "&#xa;" : "\n";
            break;
        case '\t':
            out += attribute ? "&#x9;" : "\t";
            break;
        case '\r':
            if (attribute)
                out += "&#xd;";
            break;
        case 0xEF: // U+FFFE and U+FFFF are not XML characters
            if (i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xBF &&
                static_cast<unsigned char>(text[i + 2]) >= 0xBE)
                i += 2;
            else
                out += static_cast<char>(c);
            break;
        default:
            if (c >= 0x20)
                out += static_cast<char>(c);
            break;
        }
    }
}

} // namespace

std::string escapeText(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    appendEscaped(out, text, false);
    return out;
}

std::string escapeAttribute(std::string_view value)
{
    std::string out;
    out.reserve(value.size());
    appendEscaped(out, value, true);
    return out;
}

Tag& Tag::attr(std::string_view name, std::string_view value)
{
    text_ += ' ';
    text_ += name;
    text_ += "=\"";
    appendEscaped(text_, value, true);
    text_ += '"';
    return *this;
}

} // namespace qraw::render::svg
