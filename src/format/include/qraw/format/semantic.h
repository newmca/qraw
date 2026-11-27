#pragma once

#include "qraw/core/document.h"
#include "qraw/core/xml_node.h"
#include "qraw/format/xml_io.h"

#include <QByteArrayView>

#include <string>
#include <vector>

namespace qraw::format {

// Canonical form used for semantic comparison: compressed <diagram> payloads
// are inflated (with their own declaration, prolog and epilog), attributes
// are sorted by name, CDATA becomes text and adjacent text is merged. Element
// order, all values and all whitespace text are kept: the parser has already
// removed indentation, so what remains is content.
[[nodiscard]] core::XmlNode canonicalTree(const core::XmlNode& root);

// Canonical declaration (without its encoding), prolog, root and epilog of a
// document, in order. Document-level comparisons use this, so a lost comment,
// PI, DOCTYPE or declaration is a difference.
[[nodiscard]] std::vector<core::XmlNode> canonicalDocumentContent(const XmlDocument& document);

// Compares two canonical trees. On a mismatch, `difference` (if given)
// receives a path and description of the first difference.
[[nodiscard]] bool semanticEqual(const core::XmlNode& a, const core::XmlNode& b, std::string* difference = nullptr);
// Parses both files and compares their canonical documents (declaration,
// prolog, root, epilog). Unparsable input never compares equal.
[[nodiscard]] bool semanticEqual(QByteArrayView a, QByteArrayView b, std::string* difference = nullptr);
[[nodiscard]] bool semanticEqual(const core::Document& a, const core::Document& b, std::string* difference = nullptr);

} // namespace qraw::format
