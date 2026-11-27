// Port of js/diagramly/Editor.js extractGraphModelFromSvg (draw.io, Apache-2.0), modified.

#include "qraw/format/embed.h"

#include "qraw/format/compression.h"
#include "qraw/format/xml_io.h"

#include <QByteArray>
#include <QString>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>

namespace qraw::format {
namespace {

int hexValue(char16_t c)
{
    if (c >= u'0' && c <= u'9')
        return c - u'0';
    if (c >= u'a' && c <= u'f')
        return c - u'a' + 10;
    if (c >= u'A' && c <= u'F')
        return c - u'A' + 10;
    return -1;
}

// JavaScript unescape(): %XX and %uXXXX escapes over UTF-16 code units.
// Malformed escapes are kept literally.
std::u16string jsUnescape(const std::u16string& units)
{
    std::u16string out;
    out.reserve(units.size());
    std::size_t i = 0;
    while (i < units.size()) {
        if (units[i] == u'%' && i + 5 < units.size() && units[i + 1] == u'u') {
            int value = 0;
            bool ok = true;
            for (std::size_t k = 2; k <= 5 && ok; ++k) {
                const int digit = hexValue(units[i + k]);
                ok = digit >= 0;
                value = value * 16 + digit;
            }
            if (ok) {
                out.push_back(static_cast<char16_t>(value));
                i += 6;
                continue;
            }
        }
        if (units[i] == u'%' && i + 2 < units.size()) {
            const int high = hexValue(units[i + 1]);
            const int low = hexValue(units[i + 2]);
            if (high >= 0 && low >= 0) {
                out.push_back(static_cast<char16_t>(high * 16 + low));
                i += 3;
                continue;
            }
        }
        out.push_back(units[i]);
        ++i;
    }
    return out;
}

// Legacy content: base64 of the escape()-style string. atob yields one code
// unit per byte, which unescape() then reads; the result is UTF-8 text.
std::string decodeLegacyContent(const std::string& base64)
{
    const QByteArray binary = QByteArray::fromBase64(QByteArray(base64.data(), static_cast<qsizetype>(base64.size())));
    std::u16string units;
    units.reserve(static_cast<std::size_t>(binary.size()));
    for (const char byte : binary)
        units.push_back(static_cast<char16_t>(static_cast<unsigned char>(byte)));
    const std::u16string unescaped = jsUnescape(units);
    return QString::fromUtf16(unescaped.data(), static_cast<qsizetype>(unescaped.size())).toUtf8().toStdString();
}

bool isSvgRoot(const std::string& name)
{
    return name == "svg" || (name.size() > 4 && name.compare(name.size() - 4, 4, ":svg") == 0);
}

} // namespace

std::optional<std::string> extractFromSvg(QByteArrayView svg)
{
    const XmlParseResult parsed = parseXml(svg);
    if (!parsed.document || !isSvgRoot(parsed.document->root.name))
        return std::nullopt;
    const std::string* content = parsed.document->root.attributes.find("content");
    if (!content || content->empty())
        return std::nullopt;

    std::string text = *content;
    if (text.front() != '<' && text.front() != '%')
        text = decodeLegacyContent(text);
    if (!text.empty() && text.front() == '%')
        text = decodeUriComponent(text);
    if (text.empty())
        return std::nullopt;
    return text;
}

} // namespace qraw::format
