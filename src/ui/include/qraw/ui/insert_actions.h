#pragma once
// The model side of the Insert menu (draw.io's Insert menu and Graph.createTable):
// text boxes, links, images, lines and tables, as free functions on a
// DocumentSession like edit_actions.h. Each insertion is one edit (one undo
// step) through commands::ops, puts the new cell's centre at `at` (snapped to
// the grid as the palette does, Graph.getCenterInsertPoint), takes the stored
// default style (Set as Default Style), goes into the current layer or entered
// group (DocumentSession::insertionParent) and selects the new cell.
//
// Insert Text is not here: it opens the label editor on a pending text cell
// (LabelEditor::insertTextAt), which inserts it on commit like a double click.

#include "qraw/commands/import_diagram.h"

#include <QPointF>
#include <QSize>
#include <QSizeF>
#include <QString>

#include <string>

class QMimeData;

namespace qraw::ui {

class DocumentSession;

namespace actions {

// EditorUi.maxImageSize: an inserted image is scaled down to fit in this square.
inline constexpr double kMaxInsertedImageSize = 1200;
// The size of an image cell for an image of `natural` pixels: the natural size,
// scaled down (never up) to fit kMaxInsertedImageSize. 120 x 120 where the size
// is unknown (an image at a web address is never downloaded; ImageDialog's fallback).
[[nodiscard]] QSizeF insertedImageSize(QSize natural);

// The table picker's range (Menus.addInsertTableItem).
inline constexpr int kMaxTableSize = 20;

// Whether anything can be inserted: a page whose insertion parent is not locked
// (Actions.js 'insertImage': graph.isEnabled() && !isCellLocked(defaultParent)).
[[nodiscard]] bool canInsert(const DocumentSession& session);

// 'line;strokeWidth=2;html=1;' 160 x 10, and direction=south 10 x 160 (Sidebar.js).
bool insertLine(DocumentSession& session, bool vertical, QPointF at);

// Actions.js 'insertLink': a rounded, underlined blue text cell whose label is the
// last part of the link (Graph.getLinkTitle; the page name for a link to a page),
// sized to its label, carrying `link` (and linkTarget=_blank with `newWindow`).
// False for a blank link.
bool insertLinkCell(DocumentSession& session, const std::string& link, bool newWindow, QPointF at);

// Actions.js 'image' without a selection: an image shape (aspect=fixed) of `size`
// showing `image`, an `image` style value (a data URI from image_embed.h, or a
// reference). `pasted` uses the style of an image that arrives by paste or drop
// (it adds labelBackgroundColor=default). False for an empty image.
bool insertImage(DocumentSession& session, const std::string& image, QSizeF size, QPointF at, bool pasted = false);

// Ctrl+V of an image (EditorUi paste handler -> importFiles): embeds the image of
// `mime` as PNG and inserts it at its natural size within kMaxInsertedImageSize.
// False when `mime` has no image or it is refused; `error` then says why when it
// is refused (too large).
bool insertClipboardImage(DocumentSession& session, const QMimeData& mime, QPointF at, QString* error = nullptr);

// Graph.createTable(rows, columns, 60, 40): a table of `rows` x `columns` empty
// cells of 60 x 40 (the structure of the tables palette: shape=table, tableRow,
// partialRectangle; the table layout keeps the cells in place). 1..kMaxTableSize
// each; false outside that.
bool insertTable(DocumentSession& session, int rows, int columns, QPointF at);

// Insert > Advanced (From Text, CSV, Mermaid): the cells an importer made
// (commands/import_diagram.h), inserted as one edit named `name` with their
// centre at `at`, in the current layer or entered group, with the default style
// applied to what has no style of its own, and selected. False when the result
// has no cells or nothing can be inserted.
bool insertImported(DocumentSession& session, const commands::imports::Result& result, QPointF at, const QString& name);

// The size of a label as the importers need it: what Autosize would give a shape
// with `style` and that label (the label of the raster canvas, not wrapped).
// `session` must outlive the function.
[[nodiscard]] commands::imports::LabelMeasure importLabelMeasure(const DocumentSession& session);

} // namespace actions
} // namespace qraw::ui
