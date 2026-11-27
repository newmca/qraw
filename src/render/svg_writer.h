#pragma once

// Private to svg_*.cpp: the text-level building blocks of the SVG writer
// (number format, escaping, colors, a tag builder).

#include <QColor>

#include <string>
#include <string_view>

namespace qraw::render::svg {

// mxSvgCanvas2D.prototype.format: rounded to two decimals, shortest text, no
// "-0". Non-finite values yield "0" (painting never fails).
[[nodiscard]] std::string number(double value);

// "#rrggbb", lowercase. The alpha channel is not part of it.
[[nodiscard]] std::string hexColor(const QColor& color);

// Text content: & < > escaped; characters that cannot appear in XML 1.0
// (control characters except tab and newline, '\r', U+FFFE/U+FFFF) dropped.
[[nodiscard]] std::string escapeText(std::string_view text);

// Attribute value in double quotes: escapeText plus " and the whitespace
// characters that attribute-value normalization would otherwise fold
// (&#xa; &#xd; &#x9;), so the value round-trips through an XML parser.
[[nodiscard]] std::string escapeAttribute(std::string_view value);

// Builds one start tag with its attributes in call order.
class Tag {
public:
    explicit Tag(std::string_view name) : text_("<") { text_ += name; }

    Tag& attr(std::string_view name, std::string_view value);
    Tag& attr(std::string_view name, double value) { return attr(name, number(value)); }
    Tag& attr(std::string_view name, const char* value) { return attr(name, std::string_view(value)); }
    Tag& attr(std::string_view name, const std::string& value) { return attr(name, std::string_view(value)); }

    [[nodiscard]] std::string open() const { return text_ + ">"; }
    [[nodiscard]] std::string selfClosed() const { return text_ + "/>"; }

private:
    std::string text_;
};

} // namespace qraw::render::svg
