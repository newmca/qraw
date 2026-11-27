#pragma once

// draw.io tables: a vertex with `childLayout=tableLayout` (the table) whose
// child vertices are the rows (`shape=tableRow`) and whose rows' child
// vertices are the cells. Queries and editing operations ported from
// js/grapheditor/Graph.js (isTable, isTableRow, isTableCell, insertTableRow,
// insertTableColumn, deleteTableRow, deleteTableColumn, setTableRowHeight,
// setTableColumnWidth) and the tableLayout (TableLayout), which
// ops_layout.h's applyLayouts runs after every edit that touches a table.
//
// Same conventions as ops_cells.h (run inside a Transaction, compose
// primitives, failure means "roll back"). The operations change the cells
// and the table's size the way upstream does and rely on the layout, which
// DocumentSession::edit runs inside the same edit, to position rows and
// cells; tests that call an operation on a bare Transaction call
// applyLayouts themselves.
//
// Merged cells: a cell with `rowspan` / `colspan` covers the cells to its right
// and below it. The table layout grows the covering cell over them and hides
// the covered ones (visible=0), as TableLayout.execute does after
// Graph.visitTableCells; inserted cells lose both styles and nothing else
// adjusts a span (upstream does not either). mergeTableCells and
// unmergeTableCells are the `mergeCells` / `unmergeCells` actions.
//
// Not supported: column moves (moveTableColumn). Upstream paths are relative to
// $DRAWIO_SRC/src/main/webapp/.

#include "qraw/commands/result.h"
#include "qraw/core/cell.h"
#include "qraw/core/page.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::commands {
class Transaction;
}

namespace qraw::commands::ops {

// Graph.minTableColumnWidth / minTableRowHeight (Graph.js:2011, 2016): the
// smallest size the resize handles allow.
inline constexpr double kMinTableColumnWidth = 20;
inline constexpr double kMinTableRowHeight = 20;

// --- queries ---------------------------------------------------------------

// Graph.isTable: the cell's own style has `childLayout=tableLayout`.
[[nodiscard]] bool isTable(const core::Cell& cell);
// Graph.isTableRow: a vertex whose parent is a table.
[[nodiscard]] bool isTableRow(const core::Page& page, const core::Cell& cell);
// Graph.isTableCell: a vertex whose parent is a table row.
[[nodiscard]] bool isTableCell(const core::Page& page, const core::Cell& cell);

// The table that `id` is, or that contains the row or cell `id`; "" when
// the cell is none of them.
[[nodiscard]] std::string tableOf(const core::Page& page, std::string_view id);
// The row that `id` is or that contains the cell `id`; "" for the table and
// for cells that are not part of a table.
[[nodiscard]] std::string rowOf(const core::Page& page, std::string_view id);

// mxGraphModel.getChildCells(parent, true): the child vertices in order.
[[nodiscard]] std::vector<const core::Cell*> childVertices(const core::Page& page, std::string_view parent);

// One cell of Graph.visitTableCells: the grid position of a table cell and
// the part of a span that is still to come at it.
struct TableCellInfo {
    std::string id;
    int row = 0;      // index among the row vertices of the table
    int col = 0;      // index among the cell vertices of its row
    // What is left of the covering span from this cell on: a 2x2 merged cell
    // has 2x2 at its own position, 2x1 right of it, 1x2 below it and 1x1 at
    // the diagonal (visitTableCells keeps counting down while it walks on).
    int rowspan = 1;
    int colspan = 1;
    // Index (in the vector tableCells returns) of the cell whose span covers
    // this one; the cell's own index when nothing covers it.
    std::size_t actual = 0;
};

// Graph.visitTableCells: every table cell with a geometry, row by row, with
// the spans resolved. The `rowspan` and `colspan` styles are read from the
// cell's own style (upstream: its current style, which a stylesheet could
// set); values below 1 and unparsable values count as 1.
[[nodiscard]] std::vector<TableCellInfo> tableCells(const core::Page& page, std::string_view table);

// The cell range that Merge Cells would merge (EditorUi.updateSelectionStateForTableCells):
// at least two cells of one table that fill a rectangle of the grid and none
// of which is merged already.
struct MergeRange {
    std::string cell; // the top left cell, which gets the spans
    int rowspan = 1;
    int colspan = 1;
};
[[nodiscard]] std::optional<MergeRange> mergeRange(const core::Page& page, const std::vector<std::string>& cells);
// The cells of the grid rectangle that has the table cells `from` and `to` in
// opposite corners (what a Shift+click selects, `from` first); empty when they
// are not cells of one table. A cell that a merged cell covers is not in it.
[[nodiscard]] std::vector<std::string> tableCellRange(const core::Page& page, const std::string& from,
                                                     const std::string& to);
// A table cell with `rowspan` or `colspan` above 1 (the Unmerge Cells rule).
[[nodiscard]] bool isMergedCell(const core::Page& page, const core::Cell& cell);

// --- editing ---------------------------------------------------------------

// The `mergeCells` action: sets `rowspan` and `colspan` on the top left cell
// of mergeRange(cells), and the layout (which the edit runs afterwards) grows
// it over the others. Failure when the cells are no mergeable range.
Result mergeTableCells(Transaction& tx, std::size_t page, const std::vector<std::string>& cells);

// The `unmergeCells` action: removes `rowspan` and `colspan` from the cells
// (the layout then restores the covered cells). A cell without them is left
// alone; failure when no cell is a table cell.
Result unmergeTableCells(Transaction& tx, std::size_t page, const std::vector<std::string>& cells);

// Graph.insertTableRow: a copy of the row of `cell` (a table cell, a row, or
// the table: its first row when `before`, else its last), without its
// children and value, goes before or after that row, with a copy of each of
// its cells (value, `rowspan`/`colspan` and alternate bounds dropped, height
// of the row) and the table grows by the row height. A table without rows
// gets a one-cell row. Failure when `cell` is not part of a table.
Result insertTableRow(Transaction& tx, std::size_t page, const std::string& cell, bool before);

// Graph.insertTableColumn: in every row, a copy of the cell in the column of
// `cell` (a table cell; for a row or the table, its first or, unless
// `before`, last column) goes before or after it, and the table grows by the
// copy's width. Failure when `cell` is not part of a table or the table has
// no rows.
Result insertTableColumn(Transaction& tx, std::size_t page, const std::string& cell, bool before);

// Graph.deleteTableRow: removes the row of `cell` (the last row for the
// table) and shrinks the table by its height; the only row removes the
// table. Edges connected to removed cells are disconnected (removeCells).
Result deleteTableRow(Transaction& tx, std::size_t page, const std::string& cell);

// Graph.deleteTableColumn: removes the column of `cell` (the last column for
// a row or the table) from every row and shrinks the table by the widest
// removed cell; the only column removes the table.
Result deleteTableColumn(Transaction& tx, std::size_t page, const std::string& cell);

// Graph.setTableRowHeight: the row grows by `dy`. With `extend` the table
// grows by `dy` and the rows below move; without it the next row shrinks by
// `dy` and moves (the table grows only for the last row). `row` must be a
// table row.
Result setTableRowHeight(Transaction& tx, std::size_t page, const std::string& row, double dy, bool extend = true);

// Graph.setTableColumnWidth: the column of the table cell `col` grows by `dx`
// in every row. With `extend` the next column only moves and the table grows
// by `dx`; without it the next column shrinks by `dx` (the table grows only
// for the last column). `col` must be a table cell.
Result setTableColumnWidth(Transaction& tx, std::size_t page, const std::string& col, double dx, bool extend = false);

} // namespace qraw::commands::ops
