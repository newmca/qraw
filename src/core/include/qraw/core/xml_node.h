#pragma once

#include "qraw/core/attributes.h"

#include <string>
#include <string_view>
#include <vector>

namespace qraw::core {

// Generic, lossless XML node. Holds every construct the typed model does not
// understand so that it can be written back unchanged and in place.
struct XmlNode {
    enum class Kind { Element, Text, CData, Comment, ProcessingInstruction, Doctype, EntityReference };

    Kind kind = Kind::Element;
    std::string name;          // element name (qualified, prefix kept), PI target or entity name
    std::string text;          // Text / CData / Comment content, PI data, raw DOCTYPE
    Attributes attributes;     // elements only
    std::vector<XmlNode> children;
    int sourceLine = 0;        // diagnostics only; never serialized or compared

    static XmlNode element(std::string name, Attributes attributes = {});
    static XmlNode textNode(std::string text);
    static XmlNode cdata(std::string text);
    static XmlNode comment(std::string text);

    [[nodiscard]] bool isElement() const { return kind == Kind::Element; }
    [[nodiscard]] bool isElement(std::string_view elementName) const
    {
        return kind == Kind::Element && name == elementName;
    }
    [[nodiscard]] const XmlNode* firstChild(std::string_view elementName) const;
    [[nodiscard]] XmlNode* firstChild(std::string_view elementName);
    [[nodiscard]] bool hasElementChildren() const;
    // Concatenated Text and CData content of the direct children.
    [[nodiscard]] std::string textContent() const;

    // Structural equality: kind, name, text, ordered attributes and children.
    // `sourceLine` is ignored.
    friend bool operator==(const XmlNode& a, const XmlNode& b);
};

// The <?xml ...?> declaration as found in the source file.
struct XmlDeclaration {
    bool present = false;
    std::string version;
    std::string encoding;   // empty: the declaration had no encoding attribute
    std::string standalone; // empty: the declaration had no standalone attribute

    friend bool operator==(const XmlDeclaration&, const XmlDeclaration&) = default;
};

[[nodiscard]] bool isXmlWhitespace(std::string_view text);

} // namespace qraw::core
