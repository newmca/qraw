// Container sniffing and the readAny* entry points for diagrams embedded in
// PNG, SVG and PDF files. The format-specific extractors and writers live in
// embed_png.cpp, embed_svg.cpp and embed_pdf.cpp.

#include "qraw/format/embed.h"
#include "embed_internal.h"

#include <QFile>

#include <cstddef>
#include <string_view>

namespace qraw::format {
namespace {

constexpr std::string_view kBom{"\xEF\xBB\xBF", 3};

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n'; }

bool startsAt(std::string_view bytes, std::size_t pos, std::string_view literal)
{
    return pos <= bytes.size() && bytes.substr(pos).substr(0, literal.size()) == literal;
}

std::size_t skipSpaces(std::string_view bytes, std::size_t pos)
{
    while (pos < bytes.size() && isSpace(bytes[pos]))
        ++pos;
    return pos;
}

// Skips a <!DOCTYPE ...> declaration, which may contain an internal subset
// in square brackets. Returns the position after its '>' or npos.
std::size_t skipDoctype(std::string_view bytes, std::size_t pos)
{
    bool inSubset = false;
    for (std::size_t i = pos + 9; i < bytes.size(); ++i) {
        if (bytes[i] == '[')
            inSubset = true;
        else if (bytes[i] == ']')
            inSubset = false;
        else if (bytes[i] == '>' && !inSubset)
            return i + 1;
    }
    return std::string_view::npos;
}

bool isNameStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_' || c == ':'; }
bool isNameChar(char c)
{
    return isNameStart(c) || (c >= '0' && c <= '9') || c == '-' || c == '.';
}

// Reads the element name at pos (just after '<'). Empty when there is none.
std::string_view readElementName(std::string_view bytes, std::size_t pos)
{
    if (pos >= bytes.size() || !isNameStart(bytes[pos]))
        return {};
    std::size_t end = pos + 1;
    while (end < bytes.size() && isNameChar(bytes[end]))
        ++end;
    return bytes.substr(pos, end - pos);
}

bool endsWith(std::string_view text, std::string_view suffix)
{
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

ReadResult noEmbeddedDiagram()
{
    ReadResult result;
    core::Diagnostic d;
    d.severity = core::Diagnostic::Severity::Error;
    d.message = "no embedded diagram";
    result.diagnostics.push_back(std::move(d));
    return result;
}

} // namespace

std::string_view documentRootName(std::string_view bytes)
{
    std::size_t pos = startsAt(bytes, 0, kBom) ? kBom.size() : 0;
    for (;;) {
        pos = skipSpaces(bytes, pos);
        if (pos >= bytes.size())
            return {};
        if (startsAt(bytes, pos, "<?")) {
            const std::size_t end = bytes.find("?>", pos + 2);
            if (end == std::string_view::npos)
                return {};
            pos = end + 2;
        } else if (startsAt(bytes, pos, "<!--")) {
            const std::size_t end = bytes.find("-->", pos + 4);
            if (end == std::string_view::npos)
                return {};
            pos = end + 3;
        } else if (startsAt(bytes, pos, "<!DOCTYPE")) {
            pos = skipDoctype(bytes, pos);
            if (pos == std::string_view::npos)
                return {};
        } else {
            break;
        }
    }
    if (pos >= bytes.size() || bytes[pos] != '<')
        return {};
    return readElementName(bytes, pos + 1);
}

ContainerKind sniffContainer(QByteArrayView data)
{
    const std::string_view bytes(data.data(), static_cast<std::size_t>(data.size()));
    if (startsAt(bytes, 0, std::string_view("\x89PNG\r\n\x1a\n", 8)))
        return ContainerKind::Png;
    if (startsAt(bytes, 0, "%PDF-"))
        return ContainerKind::Pdf;

    const std::string_view name = documentRootName(bytes);
    if (name.empty())
        return ContainerKind::Unknown;
    if (name == "svg" || endsWith(name, ":svg"))
        return ContainerKind::Svg;
    return ContainerKind::Xml;
}

ReadResult readAnyDocument(QByteArrayView data)
{
    std::optional<std::string> xml;
    switch (sniffContainer(data)) {
    case ContainerKind::Xml:
    case ContainerKind::Unknown:
        return readDocument(data);
    case ContainerKind::Png:
        xml = extractFromPng(data);
        break;
    case ContainerKind::Svg:
        xml = extractFromSvg(data);
        break;
    case ContainerKind::Pdf:
        xml = extractFromPdf(data);
        break;
    }
    if (!xml)
        return noEmbeddedDiagram();
    return readDocument(QByteArrayView(xml->data(), static_cast<qsizetype>(xml->size())));
}

ReadResult readAnyFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        ReadResult result;
        core::Diagnostic d;
        d.severity = core::Diagnostic::Severity::Error;
        d.message = "cannot open " + path.toStdString() + ": " + file.errorString().toStdString();
        result.diagnostics.push_back(std::move(d));
        return result;
    }
    return readAnyDocument(file.readAll());
}

} // namespace qraw::format
