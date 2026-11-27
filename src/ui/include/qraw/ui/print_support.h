#pragma once
// Printing (File > Print..., Ctrl+P) with print preview. Implemented in
// src/ui/print_support.cpp.

#include "qraw/core/document.h"

#include <cstddef>
#include <vector>

class QPrinter;
class QWidget;

namespace qraw::ui {

struct PrintOptions {
    std::vector<std::size_t> pages; // page indices to print; empty = all pages
    bool fitToPage = true;          // scale each diagram page to the printable area (keep aspect)
    double scale = 1;               // used when !fitToPage (1 diagram unit = 1/96 inch)
};

// Paints the pages onto `printer`, one printer page per diagram page:
// render::PageView + SceneRenderer::paintPage on a PainterCanvas, content
// bounds (PageView::bounds) centred in QPrinter::pageRect. Page background
// is painted when set. Returns false when nothing could be printed.
bool printDocument(const core::Document& document, QPrinter& printer, const PrintOptions& options);

// Opens a QPrintPreviewDialog whose paintRequested runs printDocument for
// all pages (draw.io's print dialog offers "all pages" by default).
void showPrintPreview(const core::Document& document, QWidget* parent);
// QPrintDialog then printDocument (page range from the dialog).
void showPrintDialog(const core::Document& document, QWidget* parent);

} // namespace qraw::ui
