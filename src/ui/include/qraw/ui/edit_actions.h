#pragma once
// The model side of the Edit and Arrange menu actions, as free functions on
// a DocumentSession: each composes commands::ops inside one
// DocumentSession::edit (one undo entry, with a translated name) and applies
// draw.io's selection rules.
// MainWindow, the format panel, the palette and the canvas call these;
// nobody else composes these operations.
//
// Every function works on the current page and the current selection, does
// nothing (returns false) when its preconditions fail (empty selection,
// locked cells, ...), and returns true when the model changed.

#include "qraw/commands/ops_layout.h"
#include "qraw/commands/ops_structure.h"
#include "qraw/core/geometry.h"
#include "qraw/core/page.h"
#include "qraw/ui/cell_transfer.h"

#include <QPointF>
#include <QString>

#include <optional>
#include <string>
#include <vector>

class QClipboard;
class QMimeData;

namespace qraw::ui {

class DocumentSession;

namespace actions {

// --- clipboard (Actions.js:157-229 cut/copy/paste, EditorUi.copyXml,
// --- EditorUi.pasteCells js/diagramly/EditorUi.js:27948) ----------------
// Copies encodeCells(selection) to the clipboard (kCellsMimeType +
// text/plain). Cut also deletes them (deleteSelection(false)) as "Cut".
bool copySelection(DocumentSession& session, QClipboard& clipboard);
bool cutSelection(DocumentSession& session, QClipboard& clipboard);
// Pastes cells from `mime` (cellsFromMimeData). Without `at`: the n-th
// consecutive paste of the same payload is offset by n * gridSize from the
// copied position (EditorUi pasteCounter; the first paste of a copy lands
// on top of the original + gridSize, a paste after cut lands in place).
// With `at` (Paste Here, drop): the bundle's top-left is placed at `at`,
// snapped to the grid. Parent: DocumentSession::insertionParent(). Plain
// text that is not a cell payload is pasted as a new text vertex
// ("text;html=1;whiteSpace=wrap;" 120x40 at `at` or the paste position) --
// Graph.pasteCells with plainText. Selects the pasted top-level cells.
bool paste(DocumentSession& session, const QMimeData& mime, std::optional<QPointF> at = std::nullopt);

// Edit > Copy as Image (draw.io 'copyAsImage', Ctrl+Alt+X): renders the selected
// cells, or the page when nothing is selected, at the screen's pixel ratio with
// the export renderer and puts it on the clipboard as an image. The PNG flavour
// carries the diagram (the selected cells, or the page's model) the way the
// export's "include a copy of my diagram" does. The background is transparent
// unless the page has a background color. False when there is nothing to draw
// or the rendering failed (the reason goes to the status bar).
bool copyAsImage(DocumentSession& session, QClipboard& clipboard, qreal devicePixelRatio = 1);
// True when the page has anything to copy as an image.
[[nodiscard]] bool canCopyAsImage(const DocumentSession& session);

// --- delete / duplicate --------------------------------------------------
// Actions.js:411 'delete' / :430 'deleteAll' (Ctrl+Delete): ops::removeCells
// with includeEdges; locked cells and deletable=0 are skipped.
bool deleteSelection(DocumentSession& session, bool includeEdges);
// Graph.duplicateCells (js/grapheditor/Graph.js:39913), Ctrl+D: clones the
// selection (encode/decode + insertBundle) offset by (gridSize, gridSize)
// into the same parent and selects the clones.
bool duplicateSelection(DocumentSession& session);

// --- selection (Actions.js:525-528) ---------------------------------------
// Selection only (no edit). Scope: the children of insertionParent() that
// are selectable (cell_queries.h); selectVertices/selectEdges take all
// descendants of that scope (mxGraph.selectCells(vertices, edges, parent)
// with descendants).
void selectAll(DocumentSession& session);
void selectVertices(DocumentSession& session);
void selectEdges(DocumentSession& session);
void selectNone(DocumentSession& session);

// --- arrange --------------------------------------------------------------
bool group(DocumentSession& session);            // ops::groupCells, Ctrl+G
bool ungroup(DocumentSession& session);          // ops::ungroupCells, Ctrl+Shift+U
bool removeFromGroup(DocumentSession& session);  // ops::removeFromGroup
bool order(DocumentSession& session, commands::ops::ZOrder order);
// Graph.alignCells (Graph.js:40214): left/center/right use the vertices'
// absolute bounds; align to the min left / max right; Center and Middle use
// the first selected cell's centre, as mxGraph.alignCells does (top, bottom
// likewise); relative-geometry and edge cells are skipped.
enum class Align { Left, Center, Right, Top, Middle, Bottom };
bool align(DocumentSession& session, Align how);
// Graph.distributeCells (Graph.js:40430): equal spacing between centres of
// the selected vertices, first and last stay; needs >= 3 vertices.
bool distribute(DocumentSession& session, bool horizontal);
// Actions.js:534 'lockUnlock' (Ctrl+L): toggles movable, resizable,
// rotatable, deletable, editable, connectable and `locked` on the selection
// based on the first cell's editable value.
bool toggleLock(DocumentSession& session);
// Edit Shape: stores `shapeValue` (stencil(...), see edit_shape_dialog.h) as
// the `shape` style of the selected vertex, as one edit.
bool setShape(DocumentSession& session, const std::string& shapeValue);
// Actions.js:1918 'clearWaypoints' (Alt+Shift+R): ops::setWaypoints({}) on
// the selected edges.
bool clearWaypoints(DocumentSession& session);

// --- direction, size and geometry (Arrange menu, Format panel) -------------
enum class Flip { Horizontal, Vertical };
// Menus.js 'direction' flipH / flipV (Menus.toggleStyle): the first unlocked
// selected cell's flipH (flipV) decides, and every unlocked selected cell gets
// the opposite value ("1" or "0") in one edit.
bool flip(DocumentSession& session, Flip axis);
// Actions.js:483 'turn' (Ctrl+R): ops::turnCells on the unlocked selection.
// `backwards` (Ctrl+Shift+R in draw.io) turns the other way.
bool turn(DocumentSession& session, bool backwards = false);
// Actions.js:1014 'autosize' (Ctrl+Shift+Y): every selected leaf vertex takes
// the preferred size of its label (label::preferredBounds, the label editor's
// rule without the autosize=1 gate). Groups, tables and swimlanes are left
// alone (KNOWN_ISSUES.md). Relative geometries are skipped.
bool autosize(DocumentSession& session);
// Edit Geometry (EditorUi 'editGeometry', Ctrl+Shift+M). Each value that is set
// replaces that field of every selected vertex with a non-relative geometry;
// nullopt keeps the field. One edit.
struct GeometryEdit {
    std::optional<double> x;
    std::optional<double> y;
    std::optional<double> width;
    std::optional<double> height;
};
bool editGeometry(DocumentSession& session, const GeometryEdit& edit);

// Extras > Edit Diagram (EditDiagramDialog): replaces the content of the current
// page by `model` (format::parsePageModelText) as one undoable edit. False, with
// the reason on the status bar, when the page cannot take it.
bool replacePageModel(DocumentSession& session, core::Page model);
// Enabled when a vertex is selected and no selected vertex is transparentBounds
// (EditorUi.js:39322).
[[nodiscard]] bool canEditGeometry(const DocumentSession& session);
// The bounds of the selected vertices with a non-relative geometry, in
// selection order (what the Edit Geometry dialog shows).
[[nodiscard]] std::vector<core::Rect> selectedVertexBounds(const DocumentSession& session);

// --- layouts (Arrange > Layout) ---------------------------------------------
// ops::arrangeLayout on what the selection names (ops::layoutScopeForSelection;
// the entered group or the current layer when nothing is selected). One edit.
// Shows a status message and returns false when there is nothing to arrange
// (a tree layout without a root: the cells are not connected into a tree).
bool arrangeLayout(DocumentSession& session, commands::ops::LayoutKind kind);
// Enabled with a page that has vertices to lay out, and no selection of edges only.
[[nodiscard]] bool canArrangeLayout(const DocumentSession& session);

// --- tables (ArrangePanel.addTable, Format.js:2401) ---------------------------
// The table cell, row or table the table buttons act on: the first selected
// vertex when it is part of a table (a table, one of its rows or cells; with
// several cells selected the table, as upstream lifts a cell to its row and
// the row to its table), else "". Locked cells have no target.
[[nodiscard]] std::string tableTarget(const DocumentSession& session);
enum class TableEdit { InsertRowBefore, InsertRowAfter, InsertColumnBefore, InsertColumnAfter, DeleteRow, DeleteColumn };
// ops::insertTableRow / insertTableColumn / deleteTableRow / deleteTableColumn
// on tableTarget(), one edit; the selection stays (deleted cells leave it).
bool editTable(DocumentSession& session, TableEdit edit);

// Merge Cells / Unmerge Cells (Actions.js 'mergeCells' and 'unmergeCells',
// enabled as Format.js:2604 and Menus.js:792 do): Merge Cells when the
// selection is two or more cells of one table that fill a rectangle of the
// grid and are not merged (ops::mergeRange), Unmerge Cells when a merged cell
// is selected. Locked cells are left out of the selection.
[[nodiscard]] bool canMergeCells(const DocumentSession& session);
[[nodiscard]] bool canUnmergeCells(const DocumentSession& session);
// ops::mergeTableCells / unmergeTableCells on the selection, one edit; the
// table layout (which the edit runs) grows or restores the cells.
bool mergeCells(DocumentSession& session);
bool unmergeCells(DocumentSession& session);

// --- style ---------------------------------------------------------------
// Graph.copyStyle / pasteStyle (Graph.js:11210/11218), Alt+Shift+Q/W: the
// first selected cell's style text is kept in `buffer`; paste replaces the
// style of every selected cell with it (ops::setStyleText).
bool copyStyle(const DocumentSession& session, std::string& buffer);
bool pasteStyle(DocumentSession& session, const std::string& buffer);

// Set as Default Style (Ctrl+Shift+D, EditorUi.setDefaultStyle) and Clear
// Default Style (Ctrl+Shift+R): the style of the one selected cell becomes the
// default of its kind (vertex or edge) for new cells, stored in AppSettings;
// not a document change, so not an undo step. Only one cell may be selected.
[[nodiscard]] bool canSetDefaultStyle(const DocumentSession& session);
bool setAsDefaultStyle(const DocumentSession& session);
void clearDefaultStyle();

// --- insertion (palette, templates, drop) ----------------------------------
// Inserts `bundle` so that its bounds are centred on `center`, then its
// top-left corner is snapped to the grid when the page grid is on, into insertionParent(), as one edit
// named `name`; selects the new top-level cells. Sidebar.itemClicked /
// createDropHandler (js/grapheditor/Sidebar.js:6723, :4918).
// `container` (a drop onto a container, see drop_target.h) replaces
// insertionParent() as the parent; the container then grows to hold the new
// cells and a stack layout places them among its children by `center`.
bool insertBundle(DocumentSession& session, const CellBundle& bundle, QPointF center, const QString& name,
                  const std::string& container = {});

} // namespace actions
} // namespace qraw::ui
