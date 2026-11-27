#pragma once

// Helpers shared by the embed_*.cpp files; not part of the public API.

#include <string_view>

namespace qraw::format {

// Name of the root element of an XML document held in `bytes`, after the BOM,
// white space, XML declaration, processing instructions, comments and DOCTYPE.
// Empty when `bytes` does not start like an XML document.
[[nodiscard]] std::string_view documentRootName(std::string_view bytes);

// True for a serialized draw.io document (<mxfile> or <mxGraphModel> root),
// with or without an XML declaration in front.
[[nodiscard]] inline bool isDiagramXml(std::string_view bytes)
{
    const std::string_view root = documentRootName(bytes);
    return root == "mxfile" || root == "mxGraphModel";
}

} // namespace qraw::format
