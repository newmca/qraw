#pragma once

#include "qraw/core/diagnostic.h"
#include "qraw/core/xml_node.h"

#include <QByteArrayView>

#include <optional>
#include <string>
#include <vector>

namespace qraw::format {

// A parsed XML file: the root element plus what surrounds it.
struct XmlDocument {
    core::XmlDeclaration declaration;
    std::vector<core::XmlNode> prolog; // comments, PIs, DOCTYPE before the root
    core::XmlNode root;
    std::vector<core::XmlNode> epilog; // comments and PIs after the root
};

struct XmlParseResult {
    std::optional<XmlDocument> document;
    core::Diagnostic error; // set when `document` is empty (line/column filled)
    std::vector<core::Diagnostic> warnings; // non-fatal findings, e.g. possible unexpanded entities
};

// Deepest element nesting accepted. Real files nest a handful of levels; the
// limit keeps the recursive tree algorithms (copy, compare, destruction)
// within the stack on hostile input.
inline constexpr std::size_t kMaxXmlDepth = 512;

// Lossless generic XML parser (QXmlStreamReader, namespace prefixes kept
// verbatim). Indentation (whitespace-only text with a line break) is dropped
// from elements that contain child elements/comments and no other text,
// except inside `xml:space="preserve"`; all other whitespace is kept.
// References the reader cannot expand are kept as EntityReference nodes.
// Documents nested deeper than kMaxXmlDepth are rejected with an error.
[[nodiscard]] XmlParseResult parseXml(QByteArrayView data);

struct SerializeOptions {
    bool pretty = true;            // indent element-only content by two spaces
    bool spaceBeforeSelfClose = true; // "<a />" (draw.io file style) vs "<a/>"
};

// Deterministic serializer. Attribute values escape &, <, >, " and the
// characters \n, \r, \t (as &#xa; etc.) so that they survive a round trip.
[[nodiscard]] std::string serializeElement(const core::XmlNode& node, const SerializeOptions& options = {});
[[nodiscard]] std::string serializeDocument(const XmlDocument& document, const SerializeOptions& options = {});

} // namespace qraw::format
