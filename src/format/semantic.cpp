#include "qraw/format/semantic.h"

#include "qraw/format/codec.h"
#include "qraw/format/compression.h"
#include "qraw/format/xml_io.h"

#include <algorithm>
#include <iterator>

namespace qraw::format {

using core::XmlNode;

namespace {

bool isText(const XmlNode& n) { return n.kind == XmlNode::Kind::Text; }

std::string trimmed(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos)
        return {};
    const auto last = s.find_last_not_of(" \t\r\n");
    return std::string(s.substr(first, last - first + 1));
}

// The XML declaration as a comparable node. The encoding is left out: files
// are always written as UTF-8 whatever they declared.
std::optional<XmlNode> declarationNode(const core::XmlDeclaration& declaration)
{
    if (!declaration.present)
        return std::nullopt;
    XmlNode n;
    n.kind = XmlNode::Kind::ProcessingInstruction;
    n.name = "xml";
    n.text = "version=\"" + declaration.version + "\"";
    if (!declaration.standalone.empty())
        n.text += " standalone=\"" + declaration.standalone + "\"";
    return n;
}

// Inflates a compressed <diagram> payload into its canonical content: the
// declaration, prolog, <mxGraphModel> and epilog, in document order.
std::optional<std::vector<XmlNode>> inflatedContent(const XmlNode& diagram)
{
    const std::string payload = diagram.textContent();
    if (diagram.hasElementChildren() || core::isXmlWhitespace(payload))
        return std::nullopt;
    const auto xml = decompressDiagram(payload);
    if (!xml)
        return std::nullopt;
    auto parsed = parseXml(QByteArrayView(xml->data(), static_cast<qsizetype>(xml->size())));
    if (!parsed.document || !parsed.document->root.isElement("mxGraphModel"))
        return std::nullopt;
    return canonicalDocumentContent(*parsed.document);
}

std::string describe(const XmlNode& n)
{
    switch (n.kind) {
    case XmlNode::Kind::Element: return "<" + n.name + ">";
    case XmlNode::Kind::Text:
    case XmlNode::Kind::CData: return "text \"" + n.text.substr(0, 40) + "\"";
    case XmlNode::Kind::Comment: return "comment";
    case XmlNode::Kind::ProcessingInstruction: return "<?" + n.name + "?>";
    case XmlNode::Kind::Doctype: return "DOCTYPE";
    case XmlNode::Kind::EntityReference: return "&" + n.name + ";";
    }
    return "node";
}

bool compare(const XmlNode& a, const XmlNode& b, const std::string& path, std::string* difference)
{
    auto fail = [&](const std::string& what) {
        if (difference)
            *difference = path + ": " + what;
        return false;
    };
    if (a.kind != b.kind || a.name != b.name)
        return fail(describe(a) + " vs " + describe(b));
    if (a.text != b.text)
        return fail("text \"" + a.text.substr(0, 60) + "\" vs \"" + b.text.substr(0, 60) + "\"");
    for (const auto& attr : a.attributes) {
        const std::string* other = b.attributes.find(attr.name);
        if (!other)
            return fail("attribute '" + attr.name + "' missing on the right");
        if (*other != attr.value)
            return fail("attribute '" + attr.name + "': \"" + attr.value.str().substr(0, 60) + "\" vs \""
                        + other->substr(0, 60) + "\"");
    }
    for (const auto& attr : b.attributes) {
        if (!a.attributes.has(attr.name))
            return fail("attribute '" + attr.name + "' missing on the left");
    }
    const std::size_t common = std::min(a.children.size(), b.children.size());
    for (std::size_t i = 0; i < common; ++i) {
        const XmlNode& child = a.children[i];
        const std::string childPath = path + "/" + (child.isElement() ? child.name : describe(child)) + "["
            + std::to_string(i) + "]";
        if (!compare(child, b.children[i], childPath, difference))
            return false;
    }
    if (a.children.size() != b.children.size())
        return fail("child count " + std::to_string(a.children.size()) + " vs " + std::to_string(b.children.size()));
    return true;
}

} // namespace

XmlNode canonicalTree(const XmlNode& node)
{
    if (!node.isElement()) {
        XmlNode copy = node;
        copy.children.clear();
        copy.sourceLine = 0;
        if (copy.kind == XmlNode::Kind::CData)
            copy.kind = XmlNode::Kind::Text;
        return copy;
    }

    XmlNode out = XmlNode::element(node.name);
    std::vector<core::Attribute> attrs(node.attributes.begin(), node.attributes.end());
    std::sort(attrs.begin(), attrs.end(), [](const auto& l, const auto& r) { return l.name < r.name; });
    for (const auto& a : attrs)
        out.attributes.set(a.name, a.value);

    // A decodable compressed payload is replaced by its content; comments
    // and PIs next to the payload keep their position around it.
    std::optional<std::vector<XmlNode>> inflated;
    if (node.isElement("diagram"))
        inflated = inflatedContent(node);

    for (const auto& child : node.children) {
        if (inflated && (child.kind == XmlNode::Kind::Text || child.kind == XmlNode::Kind::CData)) {
            std::move(inflated->begin(), inflated->end(), std::back_inserter(out.children));
            inflated->clear(); // later text nodes belong to the same payload
            continue;
        }
        XmlNode c = canonicalTree(child);
        if (isText(c) && !out.children.empty() && isText(out.children.back()))
            out.children.back().text += c.text;
        else
            out.children.push_back(std::move(c));
    }
    if (node.isElement("diagram") && !inflated) {
        // An undecodable payload: surrounding whitespace is not significant.
        for (auto& c : out.children) {
            if (isText(c))
                c.text = trimmed(c.text);
        }
    }
    return out;
}

std::vector<XmlNode> canonicalDocumentContent(const XmlDocument& document)
{
    std::vector<XmlNode> out;
    if (auto declaration = declarationNode(document.declaration))
        out.push_back(std::move(*declaration));
    for (const auto& n : document.prolog)
        out.push_back(canonicalTree(n));
    out.push_back(canonicalTree(document.root));
    for (const auto& n : document.epilog)
        out.push_back(canonicalTree(n));
    return out;
}

namespace {
bool documentsEqual(const XmlDocument& a, const XmlDocument& b, std::string* difference)
{
    XmlNode left = XmlNode::element("#document");
    XmlNode right = XmlNode::element("#document");
    left.children = canonicalDocumentContent(a);
    right.children = canonicalDocumentContent(b);
    return compare(left, right, "", difference);
}
} // namespace

bool semanticEqual(const XmlNode& a, const XmlNode& b, std::string* difference)
{
    return compare(a, b, "/" + a.name, difference);
}

bool semanticEqual(QByteArrayView a, QByteArrayView b, std::string* difference)
{
    const auto left = parseXml(a);
    const auto right = parseXml(b);
    if (!left.document || !right.document) {
        if (difference)
            *difference = "unparsable input: " + (left.document ? right.error.message : left.error.message);
        return false;
    }
    return documentsEqual(*left.document, *right.document, difference);
}

bool semanticEqual(const core::Document& a, const core::Document& b, std::string* difference)
{
    return documentsEqual(toXml(a), toXml(b), difference);
}

} // namespace qraw::format
