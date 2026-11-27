#pragma once
// File > Export As... (draw.io ExportDialog, js/grapheditor/Dialogs.js:3299)
// and the export runner.

#include "qraw/core/document.h"
#include "qraw/render/export.h"

#include <QDialog>
#include <QString>

#include <cstddef>
#include <memory>
#include <string>
#include <vector>

namespace qraw::ui {

// What to export (draw.io's "Selection only" / "All pages" options).
enum class ExportScope { CurrentPage, AllPages, Selection };

struct ExportRequest {
    render::ExportOptions options; // format, scale, border, transparent, embed, ...
    ExportScope scope = ExportScope::CurrentPage;
    QString path;                  // output file
};

// Runs a request against a document without any UI: CurrentPage ->
// exportPage(currentPage); Selection -> exportPage with cellIds; AllPages ->
// one PDF (exportPdf) or, for images, one file per page with "-<n>"
// inserted before the extension (like qraw-cli --all-pages). Writes with
// QSaveFile. Returns false and fills `error` (translated) on failure.
bool runExport(const core::Document& document, std::size_t currentPage, const std::vector<std::string>& selection,
               const ExportRequest& request, QString* error = nullptr);

// Controls: format (PNG, JPEG, SVG, PDF), scope (current page, all pages --
// PDF only or numbered images --, selection only -- disabled without a
// selection), zoom/scale in % (100), DPI preset for PNG/JPEG (96 -> scale 1,
// 192 -> 2, 300 -> 3.125; editing one updates the other), border width in
// px (0), transparent background (PNG/SVG only), "Dark appearance", "Include a copy of my
// diagram" (embed; PNG/SVG/PDF), JPEG quality (90). OK asks for the output
// path (QFileDialog, extension from the format) unless one was preset.
class ExportDialog : public QDialog {
    Q_OBJECT

public:
    ExportDialog(bool hasSelection, const QString& suggestedPath, QWidget* parent = nullptr);
    ~ExportDialog() override;

    // The request built from the controls (path = chosen or preset path).
    [[nodiscard]] ExportRequest request() const;
    // Presets for tests and "Export again": format and path skip the file
    // dialog when set.
    void setFormat(render::ExportFormat format);
    void setOutputPath(const QString& path);
    // Sets "Dark appearance"; the Export As command passes the scheme the canvas shows now, so the default
    // follows View > Diagram Appearance (and so the theme). Exports keep their own explicit choice.
    void setDark(bool dark);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
