#pragma once

// draw.io shape libraries: `<mxlibrary>JSON</mxlibrary>` files
// (js/diagramly/EditorUi.js: emptyLibraryXml, createLibraryDataFromImages,
// loadLibrary, addLibraryEntries). A library is not a diagram (D-008): the
// document reader refuses it with kLibraryDiagnostic so callers can route it
// here. QtCore only.

#include "qraw/core/diagnostic.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::format {

// One library entry. Exactly one of `xml` / `data` is set.
//  * xml: an <mxGraphModel> (always uncompressed here; parseLibrary
//    decompresses legacy Graph.compress'ed entries).
//  * data: an image as "data:image/png;base64,..." (or any image URL).
struct LibraryEntry {
    std::string title;
    std::string xml;
    std::string data;
    double w = 0;
    double h = 0;
    std::string aspect; // "fixed" or empty
};

struct Library {
    std::vector<LibraryEntry> entries;
};

// The message of the error diagnostic readDocument reports for an
// <mxlibrary> root.
inline constexpr char kLibraryDiagnostic[] = "the file is an <mxlibrary> shape library, not a diagram";
[[nodiscard]] bool isLibraryDiagnostic(const core::Diagnostic& diagnostic);

// True when `bytes` is an XML document whose root element is <mxlibrary>.
[[nodiscard]] bool isLibraryXml(std::string_view bytes);

// An empty library file (EditorUi.emptyLibraryXml).
[[nodiscard]] std::string emptyLibraryXml();

// Parses a library file. nullopt (with an error diagnostic) when the bytes
// are not XML, the root is not <mxlibrary> or the JSON is not an array. An
// entry that has neither xml nor data, or whose xml cannot be decoded, is
// skipped with a warning.
[[nodiscard]] std::optional<Library> parseLibrary(std::string_view bytes, std::vector<core::Diagnostic>* diagnostics = nullptr);

// Deterministic: uncompressed xml entries, JSON.stringify(entries, null, 2)
// as the element text (EditorUi.createLibraryDataFromImages).
[[nodiscard]] std::string serializeLibrary(const Library& library);

// EditorUi.convertDataUri: drops the parameters between the first ';' and the
// next ',' of a data URI ("data:image/png;base64,xyz" becomes
// "data:image/png,xyz") so that it survives as a style value (the style
// separator is ';'). Other strings are returned unchanged.
[[nodiscard]] std::string convertDataUri(std::string_view data);

} // namespace qraw::format
