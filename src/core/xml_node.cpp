#include "qraw/core/xml_node.h"

#include <algorithm>
#include <utility>

namespace qraw::core {

XmlNode XmlNode::element(std::string name, Attributes attributes)
{
    XmlNode n;
    n.kind = Kind::Element;
    n.name = std::move(name);
    n.attributes = std::move(attributes);
    return n;
}

XmlNode XmlNode::textNode(std::string text)
{
    XmlNode n;
    n.kind = Kind::Text;
    n.text = std::move(text);
    return n;
}

XmlNode XmlNode::cdata(std::string text)
{
    XmlNode n = textNode(std::move(text));
    n.kind = Kind::CData;
    return n;
}

XmlNode XmlNode::comment(std::string text)
{
    XmlNode n = textNode(std::move(text));
    n.kind = Kind::Comment;
    return n;
}

const XmlNode* XmlNode::firstChild(std::string_view elementName) const
{
    for (const auto& c : children) {
        if (c.isElement(elementName))
            return &c;
    }
    return nullptr;
}

XmlNode* XmlNode::firstChild(std::string_view elementName)
{
    return const_cast<XmlNode*>(std::as_const(*this).firstChild(elementName));
}

bool XmlNode::hasElementChildren() const
{
    return std::any_of(children.begin(), children.end(), [](const XmlNode& c) { return c.isElement(); });
}

std::string XmlNode::textContent() const
{
    std::string out;
    for (const auto& c : children) {
        if (c.kind == Kind::Text || c.kind == Kind::CData)
            out += c.text;
    }
    return out;
}

bool operator==(const XmlNode& a, const XmlNode& b)
{
    return a.kind == b.kind && a.name == b.name && a.text == b.text && a.attributes == b.attributes
        && a.children == b.children;
}

bool isXmlWhitespace(std::string_view text)
{
    return std::all_of(text.begin(), text.end(),
                       [](char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r'; });
}

} // namespace qraw::core
