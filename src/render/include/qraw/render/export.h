#pragma once

// Headless export of pages to SVG, PNG, JPEG and PDF, with optional
// embedding of the editable diagram (draw.io's "include a copy of my
// diagram").

#include "qraw/core/diagnostic.h"
#include "qraw/core/document.h"
#include "qraw/render/page_view.h"

#include <QByteArray>
#include <QColor>
#include <QString>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::render {

enum class ExportFormat { Svg, Png, Jpeg, Pdf };

// "svg", "png", "jpg"/"jpeg", "pdf" (case-insensitive).
[[nodiscard]] std::optional<ExportFormat> parseExportFormat(std::string_view name);
// Default file extension without the dot ("svg", "png", "jpg", "pdf").
[[nodiscard]] std::string_view extensionFor(ExportFormat format);

struct ExportOptions {
    ExportFormat format = ExportFormat::Svg;
    // Output units per diagram unit. PNG/JPEG pixels = ceil(width * scale)
    // + 2 * border.
    double scale = 1;
    // Margin around the content in output units (draw.io's border).
    double border = 0;
    // PNG and SVG: no background at all (JPEG ignores it and uses white).
    bool transparent = false;
    // Overrides the page background (mxGraphModel@background). Without an
    // override: the page background if set, else white for PNG/JPEG/PDF and
    // none for SVG.
    std::optional<QColor> background;
    // Embed the diagram (embeddedXml()) in the output: SVG root `content`,
    // PNG tEXt chunk, PDF Info /Subject.
    bool embed = false;
    // Export only these cells and their descendants, cropped to them.
    // Empty: the whole page.
    std::vector<std::string> cellIds;
    // JPEG quality 0..100.
    int jpegQuality = 90;
    // PNG/JPEG: if the image would exceed this many pixels in either
    // dimension, the scale is reduced to fit (0 = no limit). A diagnostic
    // reports the reduction.
    int maxPixelSize = 0;
    // The dark scheme (draw.io's "Dark" export theme): default colors white on #121212 and, as the page's
    // adaptiveColors says, inverted explicit colors. An SVG without a background stays transparent.
    bool dark = false;
    // Rendering context (theme, registries, image lookup base directory).
    // Default: RenderContext::defaults().
    std::optional<RenderContext> context;
};

struct ExportResult {
    QByteArray data;                          // the file bytes; empty on failure
    std::vector<core::Diagnostic> diagnostics; // warnings and the error, if any
    [[nodiscard]] bool ok() const { return !data.isEmpty(); }
};

// The XML that `embed` writes: the whole document with all pages
// uncompressed (draw.io's default since Editor.defaultCompressed = false),
// written by format::writeDocument.
[[nodiscard]] std::string embeddedXml(const core::Document& document);

// Exports one page (0-based) as SVG, PNG or JPEG; with format Pdf a
// one-page PDF. Content bounds: PageView::bounds() (or boundsOf(cellIds)),
// translated so the content starts at `border`. An empty page exports a
// 1x1 unit image. An out-of-range page index fails with an error.
[[nodiscard]] ExportResult exportPage(const core::Document& document, std::size_t pageIndex,
                                      const ExportOptions& options);

// A PDF with one PDF page per listed page index (empty = all pages), each
// sized to its content plus border (96 units per inch: 1 unit = 0.75 pt).
[[nodiscard]] ExportResult exportPdf(const core::Document& document, std::span<const std::size_t> pageIndices,
                                     const ExportOptions& options);

} // namespace qraw::render
