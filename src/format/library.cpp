// <mxlibrary> reader and writer (see library.h).
#include "qraw/format/library.h"

#include "embed_internal.h"
#include "qraw/core/number.h"
#include "qraw/format/compression.h"
#include "qraw/format/xml_io.h"

#include <QByteArray>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace qraw::format {

namespace {

void report(std::vector<core::Diagnostic>* out, core::Diagnostic::Severity severity, std::string message)
{
    if (out == nullptr)
        return;
    core::Diagnostic d;
    d.severity = severity;
    d.message = std::move(message);
    out->push_back(std::move(d));
}

std::string trimmed(const std::string& text)
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; };
    std::size_t first = 0;
    std::size_t last = text.size();
    while (first < last && isSpace(text[first]))
        ++first;
    while (last > first && isSpace(text[last - 1]))
        --last;
    return text.substr(first, last - first);
}

// --- JSON.stringify(value, null, 2) for the entry fields ----------------------

std::string jsonString(std::string_view text)
{
    static constexpr char kHex[] = "0123456789abcdef";
    std::string out = "\"";
    for (const char c : text) {
        switch (c) {
        case '"': out += "\\\""; break;
        case '\\': out += "\\\\"; break;
        case '\n': out += "\\n"; break;
        case '\r': out += "\\r"; break;
        case '\t': out += "\\t"; break;
        case '\b': out += "\\b"; break;
        case '\f': out += "\\f"; break;
        default:
            if (static_cast<unsigned char>(c) < 0x20) {
                out += "\\u00";
                out += kHex[(c >> 4) & 0xf];
                out += kHex[c & 0xf];
            } else {
                out += c;
            }
        }
    }
    out += '"';
    return out;
}

// The text of an XML element (EditorUi.createLibraryDataFromImages sets it as
// text content; mxUtils.getXml escapes &, < and >).
std::string xmlText(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        default: out += c;
        }
    }
    return out;
}

std::string entryJson(const LibraryEntry& entry)
{
    std::vector<std::string> fields;
    fields.push_back(entry.data.empty() ? "\"xml\": " + jsonString(entry.xml) : "\"data\": " + jsonString(entry.data));
    fields.push_back("\"w\": " + core::formatNumber(entry.w));
    fields.push_back("\"h\": " + core::formatNumber(entry.h));
    if (!entry.title.empty())
        fields.push_back("\"title\": " + jsonString(entry.title));
    if (!entry.aspect.empty())
        fields.push_back("\"aspect\": " + jsonString(entry.aspect));

    std::string out = "  {\n";
    for (std::size_t i = 0; i < fields.size(); ++i)
        out += "    " + fields[i] + (i + 1 < fields.size() ? ",\n" : "\n");
    return out + "  }";
}

} // namespace

bool isLibraryDiagnostic(const core::Diagnostic& diagnostic)
{
    return diagnostic.severity == core::Diagnostic::Severity::Error && diagnostic.message == kLibraryDiagnostic;
}

bool isLibraryXml(std::string_view bytes)
{
    return documentRootName(bytes) == "mxlibrary";
}

std::string emptyLibraryXml()
{
    return "<mxlibrary>[]</mxlibrary>";
}

std::string convertDataUri(std::string_view data)
{
    if (data.substr(0, 5) != "data:")
        return std::string(data);
    const std::size_t semi = data.find(';');
    if (semi == std::string_view::npos)
        return std::string(data);
    const std::size_t comma = data.find(',', semi + 1);
    if (comma == std::string_view::npos)
        return std::string(data);
    return std::string(data.substr(0, semi)) + std::string(data.substr(comma));
}

std::optional<Library> parseLibrary(std::string_view bytes, std::vector<core::Diagnostic>* diagnostics)
{
    using Severity = core::Diagnostic::Severity;
    XmlParseResult parsed = parseXml(QByteArrayView(bytes.data(), static_cast<qsizetype>(bytes.size())));
    if (!parsed.document) {
        if (diagnostics != nullptr)
            diagnostics->push_back(parsed.error);
        return std::nullopt;
    }
    const core::XmlNode& root = parsed.document->root;
    if (!root.isElement("mxlibrary")) {
        report(diagnostics, Severity::Error, "not a library file: the root element is <" + root.name + ">");
        return std::nullopt;
    }

    QJsonParseError error;
    const QJsonDocument doc = QJsonDocument::fromJson(QByteArray::fromStdString(root.textContent()), &error);
    if (error.error != QJsonParseError::NoError || !doc.isArray()) {
        report(diagnostics, Severity::Error,
               error.error != QJsonParseError::NoError
                   ? "invalid library JSON: " + error.errorString().toStdString()
                   : "invalid library JSON: an array of entries was expected");
        return std::nullopt;
    }

    Library library;
    int index = 0;
    for (const QJsonValue& value : doc.array()) {
        ++index;
        const QJsonObject object = value.toObject();
        LibraryEntry entry;
        entry.w = object.value("w").toDouble(0);
        entry.h = object.value("h").toDouble(0);
        entry.title = object.value("title").toString().toStdString();
        entry.aspect = object.value("aspect").toString().toStdString();
        const std::string label = "library entry " + std::to_string(index) + " skipped: ";
        if (object.value("data").isString()) {
            entry.data = object.value("data").toString().toStdString();
        } else if (object.value("xml").isString()) {
            std::string xml = object.value("xml").toString().toStdString();
            if (xml.empty() || xml.front() != '<') {
                // Graph.decompress: a legacy compressed entry.
                std::optional<std::string> plain = decompressDiagram(xml);
                if (!plain) {
                    report(diagnostics, Severity::Warning, label + "its xml cannot be decoded");
                    continue;
                }
                xml = std::move(*plain);
            }
            entry.xml = trimmed(xml);
        } else {
            report(diagnostics, Severity::Warning, label + "it has neither xml nor data");
            continue;
        }
        library.entries.push_back(std::move(entry));
    }
    return library;
}

std::string serializeLibrary(const Library& library)
{
    std::string json = "[]";
    if (!library.entries.empty()) {
        json = "[\n";
        for (std::size_t i = 0; i < library.entries.size(); ++i)
            json += entryJson(library.entries[i]) + (i + 1 < library.entries.size() ? ",\n" : "\n");
        json += "]";
    }
    return "<mxlibrary>" + xmlText(json) + "</mxlibrary>\n";
}

} // namespace qraw::format
