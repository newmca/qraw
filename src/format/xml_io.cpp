#include "qraw/format/xml_io.h"

#include <QBuffer>
#include <QXmlStreamReader>

#include <algorithm>
#include <cctype>

namespace qraw::format {

using core::XmlNode;

namespace {

std::string toStd(QStringView s) { return s.toString().toStdString(); }

bool isStructural(const XmlNode& n)
{
    return n.kind == XmlNode::Kind::Element || n.kind == XmlNode::Kind::Comment
        || n.kind == XmlNode::Kind::ProcessingInstruction;
}

bool hasXmlSpacePreserve(const XmlNode& element)
{
    const std::string* space = element.attributes.find("xml:space");
    return space && *space == "preserve";
}

// Drops indentation: whitespace-only text containing a line break in an
// element whose other children are all structural (elements, comments, PIs).
// A run of whitespace without a line break (`<b>a</b> <i>b</i>`) is content,
// and so is everything inside an `xml:space="preserve"` scope.
void dropIndentation(XmlNode& element, bool preserveSpace)
{
    if (preserveSpace)
        return;
    const bool hasStructural = std::any_of(element.children.begin(), element.children.end(), isStructural);
    const bool hasRealText = std::any_of(element.children.begin(), element.children.end(), [](const XmlNode& c) {
        return c.kind == XmlNode::Kind::CData || c.kind == XmlNode::Kind::EntityReference
            || (c.kind == XmlNode::Kind::Text && !core::isXmlWhitespace(c.text));
    });
    if (hasStructural && !hasRealText) {
        std::erase_if(element.children, [](const XmlNode& c) {
            return c.kind == XmlNode::Kind::Text && c.text.find('\n') != std::string::npos;
        });
    }
}

// True for text that contains `&name;`, which the reader could not have
// produced from a well-formed predefined reference (those are decoded).
bool containsEntityReferenceSyntax(std::string_view text)
{
    for (auto amp = text.find('&'); amp != std::string_view::npos; amp = text.find('&', amp + 1)) {
        const auto semicolon = text.find(';', amp);
        if (semicolon == std::string_view::npos)
            return false;
        const auto name = text.substr(amp + 1, semicolon - amp - 1);
        const bool isName = !name.empty() && std::all_of(name.begin(), name.end(), [](char c) {
            return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '.' || c == ':';
        });
        if (isName)
            return true;
    }
    return false;
}

std::string trimmedWhitespace(std::string text)
{
    const auto first = text.find_first_not_of(" \t\r\n");
    if (first == std::string::npos)
        return {};
    const auto last = text.find_last_not_of(" \t\r\n");
    return text.substr(first, last - first + 1);
}

void appendText(std::vector<XmlNode>& children, XmlNode::Kind kind, std::string text)
{
    // The reader may split character data; keep one node per run.
    if (kind == XmlNode::Kind::Text && !children.empty() && children.back().kind == XmlNode::Kind::Text) {
        children.back().text += text;
        return;
    }
    XmlNode n = XmlNode::textNode(std::move(text));
    n.kind = kind;
    children.push_back(std::move(n));
}

// --- serialization -------------------------------------------------------

void escapeInto(std::string& out, std::string_view text, bool attribute)
{
    for (char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"':
            if (attribute)
                out += "&quot;";
            else
                out += c;
            break;
        case '\n':
            if (attribute)
                out += "&#xa;";
            else
                out += c;
            break;
        case '\t':
            if (attribute)
                out += "&#x9;";
            else
                out += c;
            break;
        case '\r': out += "&#xd;"; break;
        default: out += c;
        }
    }
}

class Writer {
public:
    explicit Writer(const SerializeOptions& options) : options_(options) {}

    void node(const XmlNode& n, int depth, bool pretty)
    {
        switch (n.kind) {
        case XmlNode::Kind::Text:
            escapeInto(out, n.text, false);
            break;
        case XmlNode::Kind::CData: {
            out += "<![CDATA[";
            std::string_view rest = n.text;
            for (auto pos = rest.find("]]>"); pos != std::string_view::npos; pos = rest.find("]]>")) {
                out += rest.substr(0, pos + 2);
                out += "]]><![CDATA[";
                rest.remove_prefix(pos + 2);
            }
            out += rest;
            out += "]]>";
            break;
        }
        case XmlNode::Kind::Comment:
            out += "<!--" + n.text + "-->";
            break;
        case XmlNode::Kind::ProcessingInstruction:
            out += "<?" + n.name;
            if (!n.text.empty())
                out += " " + n.text;
            out += "?>";
            break;
        case XmlNode::Kind::Doctype:
            out += n.text;
            break;
        case XmlNode::Kind::EntityReference:
            out += "&" + n.name + ";";
            break;
        case XmlNode::Kind::Element:
            element(n, depth, pretty);
            break;
        }
    }

    void element(const XmlNode& n, int depth, bool pretty)
    {
        out += '<';
        out += n.name;
        for (const auto& a : n.attributes) {
            out += ' ';
            out += a.name;
            out += "=\"";
            escapeInto(out, a.value, true);
            out += '"';
        }
        if (n.children.empty()) {
            out += options_.spaceBeforeSelfClose ? " />" : "/>";
            return;
        }
        out += '>';
        // Indent only element-only content; anything with text is written
        // verbatim so that the text survives exactly.
        // xml:space="preserve" suppresses indentation for the whole subtree.
        const bool block = pretty && !hasXmlSpacePreserve(n)
            && std::all_of(n.children.begin(), n.children.end(), isStructural);
        for (const auto& c : n.children) {
            if (block) {
                out += '\n';
                indent(depth + 1);
            }
            node(c, depth + 1, block);
        }
        if (block) {
            out += '\n';
            indent(depth);
        }
        out += "</" + n.name + ">";
    }

    void indent(int depth) { out.append(static_cast<std::size_t>(depth) * 2, ' '); }

    std::string out;

private:
    const SerializeOptions& options_;
};

} // namespace

XmlParseResult parseXml(QByteArrayView data)
{
    QByteArray bytes = data.toByteArray();
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::ReadOnly);
    QXmlStreamReader reader(&buffer);
    reader.setNamespaceProcessing(false); // keep prefixes and xmlns attributes verbatim

    XmlDocument doc;
    std::vector<XmlNode> stack;
    std::vector<bool> preserveSpace; // xml:space scope of each open element
    std::vector<core::Diagnostic> warnings;
    bool rootDone = false;
    bool hasDtd = false;
    bool warnedEntityInAttribute = false;
    std::string failure; // set when we stop early for a reason the reader does not know

    auto addMisc = [&](XmlNode n) {
        if (!stack.empty())
            stack.back().children.push_back(std::move(n));
        else
            (rootDone ? doc.epilog : doc.prolog).push_back(std::move(n));
    };

    while (!reader.atEnd() && failure.empty()) {
        switch (reader.readNext()) {
        case QXmlStreamReader::StartDocument:
            doc.declaration.present = !reader.documentVersion().isEmpty();
            doc.declaration.version = toStd(reader.documentVersion());
            doc.declaration.encoding = toStd(reader.documentEncoding());
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
            if (reader.hasStandaloneDeclaration())
                doc.declaration.standalone = reader.isStandaloneDocument() ? "yes" : "no";
#endif
            break;
        case QXmlStreamReader::DTD: {
            XmlNode n;
            n.kind = XmlNode::Kind::Doctype;
            // The reader's text includes the whitespace before "<!DOCTYPE".
            n.text = trimmedWhitespace(toStd(reader.text()));
            hasDtd = true;
            addMisc(std::move(n));
            break;
        }
        case QXmlStreamReader::StartElement: {
            if (stack.size() >= kMaxXmlDepth) {
                failure = "elements are nested deeper than " + std::to_string(kMaxXmlDepth) + " levels";
                break;
            }
            XmlNode n = XmlNode::element(toStd(reader.qualifiedName()));
            n.sourceLine = static_cast<int>(reader.lineNumber());
            for (const auto& a : reader.attributes()) {
                n.attributes.setOwned(toStd(a.qualifiedName()), toStd(a.value()));
                if (hasDtd && !warnedEntityInAttribute && containsEntityReferenceSyntax(a.value().toUtf8().constData())) {
                    warnedEntityInAttribute = true;
                    core::Diagnostic w;
                    w.severity = core::Diagnostic::Severity::Warning;
                    w.message = "attribute '" + toStd(a.qualifiedName())
                        + "' contains '&name;' text; if this was an entity reference, it is not expanded "
                          "(external DTDs are not loaded) and is saved as literal text";
                    w.line = static_cast<int>(reader.lineNumber());
                    warnings.push_back(std::move(w));
                }
            }
            const bool inheritsPreserve = !preserveSpace.empty() && preserveSpace.back();
            const std::string* space = n.attributes.find("xml:space");
            preserveSpace.push_back(space ? *space == "preserve" : inheritsPreserve);
            stack.push_back(std::move(n));
            break;
        }
        case QXmlStreamReader::EndElement: {
            XmlNode n = std::move(stack.back());
            stack.pop_back();
            dropIndentation(n, preserveSpace.back());
            preserveSpace.pop_back();
            if (stack.empty()) {
                doc.root = std::move(n);
                rootDone = true;
            } else {
                stack.back().children.push_back(std::move(n));
            }
            break;
        }
        case QXmlStreamReader::Characters:
            if (!stack.empty())
                appendText(stack.back().children, reader.isCDATA() ? XmlNode::Kind::CData : XmlNode::Kind::Text,
                           toStd(reader.text()));
            break;
        case QXmlStreamReader::EntityReference:
            // A reference the reader could not expand (undeclared, or defined
            // in an external DTD). Keep it as written instead of dropping it.
            if (!stack.empty()) {
                XmlNode n;
                n.kind = XmlNode::Kind::EntityReference;
                n.name = toStd(reader.name());
                stack.back().children.push_back(std::move(n));
            }
            break;
        case QXmlStreamReader::Comment:
            addMisc(XmlNode::comment(toStd(reader.text())));
            break;
        case QXmlStreamReader::ProcessingInstruction: {
            XmlNode n;
            n.kind = XmlNode::Kind::ProcessingInstruction;
            n.name = toStd(reader.processingInstructionTarget());
            n.text = toStd(reader.processingInstructionData());
            addMisc(std::move(n));
            break;
        }
        default:
            break;
        }
    }

    XmlParseResult result;
    result.warnings = std::move(warnings);
    if (!failure.empty() || reader.hasError() || !rootDone) {
        result.error.severity = core::Diagnostic::Severity::Error;
        result.error.message = !failure.empty() ? failure
            : reader.hasError()                 ? reader.errorString().toStdString()
                                                : "no root element";
        result.error.line = static_cast<int>(reader.lineNumber());
        result.error.column = static_cast<int>(reader.columnNumber());
        return result;
    }
    result.document = std::move(doc);
    return result;
}

std::string serializeElement(const XmlNode& node, const SerializeOptions& options)
{
    Writer w(options);
    w.node(node, 0, options.pretty);
    return std::move(w.out);
}

std::string serializeDocument(const XmlDocument& document, const SerializeOptions& options)
{
    Writer w(options);
    const char* separator = options.pretty ? "\n" : ""; // compact output is a single line
    const auto& decl = document.declaration;
    if (decl.present) {
        w.out += "<?xml version=\"" + (decl.version.empty() ? std::string("1.0") : decl.version) + "\"";
        if (!decl.encoding.empty())
            w.out += " encoding=\"UTF-8\""; // output is always UTF-8
        if (!decl.standalone.empty())
            w.out += " standalone=\"" + decl.standalone + "\"";
        w.out += "?>";
        w.out += separator;
    }
    for (const auto& n : document.prolog) {
        w.node(n, 0, options.pretty);
        w.out += separator;
    }
    w.node(document.root, 0, options.pretty);
    w.out += separator;
    for (const auto& n : document.epilog) {
        w.node(n, 0, options.pretty);
        w.out += separator;
    }
    return std::move(w.out);
}

} // namespace qraw::format
