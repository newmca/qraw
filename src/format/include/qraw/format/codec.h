#pragma once

#include "qraw/core/diagnostic.h"
#include "qraw/core/document.h"
#include "qraw/format/xml_io.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <optional>
#include <vector>

namespace qraw::format {

struct ReadResult {
    std::optional<core::Document> document; // empty on a fatal error
    std::vector<core::Diagnostic> diagnostics;

    [[nodiscard]] bool ok() const { return document.has_value(); }
    [[nodiscard]] bool hasErrors() const;
};

enum class PageEncodingPolicy {
    Preserve,     // each page as it was read (default)
    Compressed,   // force compressed <diagram> payloads
    Uncompressed, // force inline <mxGraphModel>
};

struct WriteOptions {
    PageEncodingPolicy pageEncoding = PageEncodingPolicy::Preserve;
};

// Reads a .drawio <mxfile> or a bare <mxGraphModel> .xml file. Malformed XML
// is a fatal error with line/column; unknown constructs are kept and at
// most produce warnings.
[[nodiscard]] ReadResult readDocument(QByteArrayView data);
[[nodiscard]] ReadResult readFile(const QString& path);

// Deterministic: equal documents produce equal bytes.
[[nodiscard]] QByteArray writeDocument(const core::Document& document, const WriteOptions& options = {});
// Atomic write (QSaveFile). Returns false and fills `error` on failure.
bool writeFile(const core::Document& document, const QString& path, const WriteOptions& options = {},
               QString* error = nullptr);

// --- one page's model as text (Extras > Edit Diagram) ------------------------
// The page's <mxGraphModel> as indented UTF-8 XML, the text draw.io's Edit
// Diagram dialog shows. Deterministic; parsePageModelText() reads it back to
// the same model.
[[nodiscard]] std::string pageModelText(const core::Page& page);

struct ModelTextResult {
    // The model parts (attributes, foreign children, <root> entries) of a page;
    // <diagram> attributes and encoding are left at their defaults. Empty on error.
    std::optional<core::Page> page;
    core::Diagnostic error;                   // set when `page` is empty (line/column filled)
    std::vector<core::Diagnostic> diagnostics; // warnings
};

// Parses the text of the Edit Diagram dialog: an <mxGraphModel>, or an <mxfile>
// whose first page is used (draw.io's dialog takes both).
[[nodiscard]] ModelTextResult parsePageModelText(QByteArrayView text);

// The XML tree that writeDocument() serializes.
[[nodiscard]] XmlDocument toXml(const core::Document& document, const WriteOptions& options = {});
// Builds the model from a parsed tree; diagnostics are appended.
[[nodiscard]] std::optional<core::Document> fromXml(XmlDocument xml, std::vector<core::Diagnostic>& diagnostics);

} // namespace qraw::format
