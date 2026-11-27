#pragma once

// Diagrams embedded in exported files (*.drawio.png, *.drawio.svg, PDF), as
// draw.io writes and reads them (docs/research/drawio-rendering-data.md
// section 7). QtCore + zlib only.

#include "qraw/format/codec.h"

#include <QByteArray>
#include <QByteArrayView>
#include <QString>

#include <optional>
#include <string>
#include <string_view>

namespace qraw::format {

enum class ContainerKind {
    Xml,     // .drawio / .xml (anything XML whose root element is not <svg>)
    Svg,     // XML whose root element is <svg>
    Png,     // PNG signature
    Pdf,     // starts with "%PDF-"
    Unknown, // none of the above (empty, binary, ...)
};

// Looks at the first bytes only: PNG signature, "%PDF-", else XML with the
// first element name (after an optional UTF-8 BOM, whitespace, the XML
// declaration, comments, PIs and a DOCTYPE).
[[nodiscard]] ContainerKind sniffContainer(QByteArrayView data);

// --- reading -------------------------------------------------------------
// Each returns the embedded diagram as XML text (an <mxfile> or
// <mxGraphModel> document), or nullopt when there is none.

// Editor.extractGraphModelFromPng (js/grapheditor/Editor.js:925): walks the
// chunks up to the first IDAT; accepts tEXt and zTXt with keyword `mxfile`
// or `mxGraphModel`. zTXt is zlib-inflated (raw-inflate fallback), then '+'
// becomes ' ' (Java URLEncoder). Values starting with '%' are
// decodeURIComponent'ed. CRCs are not verified (draw.io does not either).
[[nodiscard]] std::optional<std::string> extractFromPng(QByteArrayView png);

// The root <svg> `content` attribute (js/diagramly/Editor.js:2651, :4880):
// plain XML (starts with '<'), URI-encoded (starts with '%'), or legacy
// base64 (atob + unescape).
[[nodiscard]] std::optional<std::string> extractFromSvg(QByteArrayView svg);

// Editor.extractGraphModelFromPdf (js/diagramly/Editor.js:2791), the
// variants Qraw needs: the last literal `/Subject (...)` string whose
// unescaped (\( \) \\ and octal escapes) value decodeURIComponent's to an
// <mxfile ...> or <mxGraphModel ...> document; and a PDF 1.7 EmbeddedFile
// stream with /Subtype /application#2Fvnd.jgraph.mxfile (FlateDecode).
[[nodiscard]] std::optional<std::string> extractFromPdf(QByteArrayView pdf);

// --- writing -------------------------------------------------------------

// Editor.writeGraphModelToPng (js/diagramly/Editor.js:7391): a tEXt chunk
// with keyword `mxfile` and value encodeUriComponent(xml), with a correct
// CRC, inserted immediately before the first IDAT. Existing tEXt/zTXt
// chunks with keyword mxfile or mxGraphModel are removed first. nullopt
// when `png` is not a PNG with an IHDR and an IDAT chunk.
[[nodiscard]] std::optional<QByteArray> embedInPng(QByteArrayView png, std::string_view xml);

// Appends a PDF incremental update (new Info dictionary object, xref
// section and trailer with /Prev) whose Info carries
// /Subject (encodeUriComponent(xml)) as a literal string with '(' ')' and
// '\' escaped, keeping the previous Info entries. Works on QPdfWriter
// output (classic xref table + trailer). nullopt when the trailer
// (/Root, /Size, startxref) cannot be found.
[[nodiscard]] std::optional<QByteArray> embedInPdf(QByteArrayView pdf, std::string_view xml);

// --- convenience -----------------------------------------------------------

// Reads any supported container: XML directly; PNG/SVG/PDF via the
// extractors above. A PNG/SVG/PDF without an embedded diagram fails with an
// error diagnostic "no embedded diagram". Diagnostics of the inner document
// are returned as usual.
[[nodiscard]] ReadResult readAnyDocument(QByteArrayView data);
[[nodiscard]] ReadResult readAnyFile(const QString& path);

} // namespace qraw::format
