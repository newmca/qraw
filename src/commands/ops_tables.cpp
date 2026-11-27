#include "qraw/commands/ops_tables.h"

#include "layout_support.h"
#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/commands/primitives.h"
#include "qraw/commands/queries.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/number.h"
#include "qraw/core/style.h"

#include <algorithm>
#include <cmath>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <utility>

namespace qraw::commands::ops {

namespace {

using core::Cell;
using core::Geometry;
using core::Page;
using detail::Insets;

// JavaScript's Math.round: halves round up.
double jsRound(double value) { return std::floor(value + 0.5); }

// Writes the cell back when `edit` changed its geometry (a cell without
// geometry is left alone). Upstream clones the geometry for every
// model.setGeometry; we only write real changes.
bool editGeometry(Transaction& tx, std::size_t pageIndex, const std::string& id,
                  const std::function<void(Geometry&)>& edit)
{
    const Cell* current = tx.page(pageIndex)->findCell(id);
    if (current == nullptr || current->geometry() == nullptr)
        return true;
    Cell changed = *current;
    edit(*changed.geometry());
    return changed == *current || tx.emplace<ReplaceCell>(pageIndex, std::move(changed));
}

const Geometry* geometryOf(const Page& page, std::string_view id)
{
    const Cell* cell = page.findCell(id);
    return cell != nullptr ? cell->geometry() : nullptr;
}

std::vector<std::string> childVertexIds(const Page& page, std::string_view parent)
{
    std::vector<std::string> ids;
    for (const Cell* child : childVertices(page, parent))
        ids.push_back(child->id());
    return ids;
}

std::size_t indexOf(const std::vector<std::string>& ids, std::string_view id)
{
    return static_cast<std::size_t>(std::ranges::find(ids, id) - ids.begin());
}

// The table cell that is `id`, else its row or table: what the table
// functions call `cell`.
struct Target {
    std::string table;
    std::string row; // "" when the target is the table itself
    std::string cell; // "" unless the target is a table cell
};

Target targetOf(const Page& page, std::string_view id)
{
    Target target;
    const Cell* cell = page.findCell(id);
    if (cell == nullptr)
        return target;
    if (isTableCell(page, *cell)) {
        target.cell = cell->id();
        target.row = cell->parent();
        target.table = page.findCell(cell->parent())->parent();
    } else if (isTableRow(page, *cell)) {
        target.row = cell->id();
        target.table = cell->parent();
    } else if (isTable(*cell)) {
        target.table = cell->id();
    }
    return target;
}

// mxCell.clone without children, then the resets insertTableRow and
// insertTableColumn apply: no value, no merge styles, no alternate bounds.
Cell blankCopy(Transaction& tx, std::size_t pageIndex, const Cell& original, bool isCell)
{
    Cell copy = original;
    copy.setWrapper(std::nullopt);
    copy.setId(tx.newCellId(pageIndex));
    copy.setValue("");
    if (isCell) {
        core::Style style = copy.style();
        style.remove("rowspan");
        style.remove("colspan");
        copy.setStyle(style);
        if (Geometry* geometry = copy.geometry()) {
            if (const auto alternate = geometry->alternateBounds()) {
                geometry->setWidth(alternate->width);
                geometry->setHeight(alternate->height);
                geometry->setAlternateBounds(std::nullopt);
            }
        }
    }
    return copy;
}

Result growTable(Transaction& tx, std::size_t pageIndex, const std::string& table, double dw, double dh)
{
    if (!editGeometry(tx, pageIndex, table, [&](Geometry& g) {
            g.setWidth(g.width() + dw);
            g.setHeight(g.height() + dh);
        }))
        return Result::failure("could not resize table '" + table + "'");
    return Result::success();
}

} // namespace

// --- queries -------------------------------------------------------------------

bool isTable(const Cell& cell) { return cell.style().value("childLayout", "") == "tableLayout"; }

bool isTableRow(const Page& page, const Cell& cell)
{
    const Cell* parent = cell.isVertex() ? page.findCell(cell.parent()) : nullptr;
    return parent != nullptr && isTable(*parent);
}

bool isTableCell(const Page& page, const Cell& cell)
{
    const Cell* parent = cell.isVertex() ? page.findCell(cell.parent()) : nullptr;
    return parent != nullptr && isTableRow(page, *parent);
}

std::string tableOf(const Page& page, std::string_view id) { return targetOf(page, id).table; }

std::string rowOf(const Page& page, std::string_view id) { return targetOf(page, id).row; }

std::vector<const Cell*> childVertices(const Page& page, std::string_view parent)
{
    std::vector<const Cell*> result;
    for (const Cell* child : page.children(parent)) {
        if (child->isVertex())
            result.push_back(child);
    }
    return result;
}

// --- merged cells ----------------------------------------------------------------

namespace {

// parseInt(style[key] || 1), where a value below 1 and one that is no number
// count as 1 (upstream compares the result with `> 1` only).
int spanOf(const Cell& cell, const char* key)
{
    const auto value = cell.style().value(key);
    if (!value)
        return 1;
    const double number = core::parseNumber(*value, 1);
    return number >= 2 ? static_cast<int>(std::min(number, 1e6)) : 1;
}

} // namespace

std::vector<TableCellInfo> tableCells(const Page& page, std::string_view table)
{
    std::vector<TableCellInfo> result;
    std::vector<std::optional<std::size_t>> lastRow;
    const std::vector<const Cell*> rows = childVertices(page, table);
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const std::vector<const Cell*> cols = childVertices(page, rows[i]->id());
        std::vector<std::optional<std::size_t>> row;
        std::optional<std::size_t> lastCol;
        for (std::size_t j = 0; j < cols.size(); ++j) {
            // A cell without geometry takes its place in the row but not in the grid.
            if (cols[j]->geometry() == nullptr) {
                row.push_back(std::nullopt);
                continue;
            }
            TableCellInfo info;
            info.id = cols[j]->id();
            info.row = static_cast<int>(i);
            info.col = static_cast<int>(j);
            info.actual = result.size();
            const std::optional<std::size_t> above = j < lastRow.size() ? lastRow[j] : std::nullopt;
            if (above && result[*above].rowspan > 1) {
                info.rowspan = result[*above].rowspan - 1;
                info.colspan = result[*above].colspan;
                info.actual = result[*above].actual;
            } else if (lastCol && result[*lastCol].colspan > 1) {
                info.rowspan = result[*lastCol].rowspan;
                info.colspan = result[*lastCol].colspan - 1;
                info.actual = result[*lastCol].actual;
            } else {
                info.rowspan = spanOf(*cols[j], "rowspan");
                info.colspan = spanOf(*cols[j], "colspan");
            }
            row.push_back(result.size());
            lastCol = result.size();
            result.push_back(std::move(info));
        }
        lastRow = std::move(row);
    }
    return result;
}

bool isMergedCell(const Page& page, const Cell& cell)
{
    return isTableCell(page, cell) && (spanOf(cell, "rowspan") > 1 || spanOf(cell, "colspan") > 1);
}

std::optional<MergeRange> mergeRange(const Page& page, const std::vector<std::string>& ids)
{
    if (ids.size() < 2)
        return std::nullopt;
    const std::string table = tableOf(page, ids.front());
    if (table.empty())
        return std::nullopt;
    std::map<std::string, const TableCellInfo*> grid;
    const std::vector<TableCellInfo> cells = tableCells(page, table);
    for (const TableCellInfo& info : cells)
        grid.emplace(info.id, &info);

    std::set<std::pair<int, int>> selected;
    int firstRow = 0, lastRow = 0, firstCol = 0, lastCol = 0;
    for (const std::string& id : ids) {
        const Cell* cell = page.findCell(id);
        const auto found = grid.find(id);
        // Cells of the table only, none of them merged or covered by a merged cell.
        if (cell == nullptr || !isTableCell(page, *cell) || found == grid.end() || tableOf(page, id) != table)
            return std::nullopt;
        const TableCellInfo& info = *found->second;
        if (cells[info.actual].id != id || info.rowspan > 1 || info.colspan > 1)
            return std::nullopt;
        if (!selected.emplace(info.row, info.col).second)
            return std::nullopt;
        const bool first = selected.size() == 1;
        firstRow = first ? info.row : std::min(firstRow, info.row);
        lastRow = first ? info.row : std::max(lastRow, info.row);
        firstCol = first ? info.col : std::min(firstCol, info.col);
        lastCol = first ? info.col : std::max(lastCol, info.col);
    }
    MergeRange range;
    range.rowspan = lastRow - firstRow + 1;
    range.colspan = lastCol - firstCol + 1;
    if (selected.size() != static_cast<std::size_t>(range.rowspan) * static_cast<std::size_t>(range.colspan))
        return std::nullopt;
    for (const TableCellInfo& info : cells) {
        if (info.row == firstRow && info.col == firstCol)
            range.cell = info.id;
    }
    return range;
}

std::vector<std::string> tableCellRange(const Page& page, const std::string& from, const std::string& to)
{
    const std::string table = tableOf(page, from);
    const Cell* a = page.findCell(from);
    const Cell* b = page.findCell(to);
    if (table.empty() || a == nullptr || b == nullptr || !isTableCell(page, *a) || !isTableCell(page, *b)
        || tableOf(page, to) != table)
        return {};
    const std::vector<TableCellInfo> cells = tableCells(page, table);
    const auto find = [&](const std::string& id) {
        return std::ranges::find(cells, id, &TableCellInfo::id);
    };
    const auto first = find(from);
    const auto last = find(to);
    if (first == cells.end() || last == cells.end())
        return {};
    const int rows[] = {std::min(first->row, last->row), std::max(first->row, last->row)};
    const int cols[] = {std::min(first->col, last->col), std::max(first->col, last->col)};
    std::vector<std::string> range{from};
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const TableCellInfo& info = cells[i];
        if (info.row >= rows[0] && info.row <= rows[1] && info.col >= cols[0] && info.col <= cols[1]
            && info.actual == i && info.id != from)
            range.push_back(info.id);
    }
    return range;
}

// --- tableLayout -----------------------------------------------------------------

namespace {

// TableLayout.getSize: total width or height of the visible cells.
double visibleSize(const Page& page, const std::vector<std::string>& ids, bool horizontal)
{
    double total = 0;
    for (const std::string& id : ids) {
        const Cell* cell = page.findCell(id);
        if (cell != nullptr && cell->isVisible() && cell->geometry() != nullptr)
            total += horizontal ? cell->geometry()->width() : cell->geometry()->height();
    }
    return total;
}

// TableLayout.getRowLayout: the right edge of each cell of `row` for a table
// of width `width`, cell widths taken from the alternate bounds.
std::vector<double> rowLayout(const Page& page, const std::string& rowId, double width)
{
    const Cell& row = *page.findCell(rowId);
    const std::vector<std::string> ids = childVertexIds(page, rowId);
    const Insets off = detail::startInsets(page, row);
    // Upstream divides by the width of the visible cells, which is the sum of
    // the single column widths as long as every covered cell of a merged cell
    // exists. Summing the single column widths of all cells gives the same
    // there, and still spreads the columns when a covered cell was deleted.
    double sw = 0;
    for (const std::string& id : ids) {
        if (const Geometry* geometry = geometryOf(page, id)) {
            const auto alternate = geometry->alternateBounds();
            sw += alternate ? alternate->width : geometry->width();
        }
    }
    const double rw = std::max(0.0, width - off.left - off.right);
    std::vector<double> result;
    double x = off.left;
    for (const std::string& id : ids) {
        const Geometry* geometry = geometryOf(page, id);
        if (geometry == nullptr)
            continue;
        if (sw > 0) {
            const auto alternate = geometry->alternateBounds();
            x += (alternate ? alternate->width : geometry->width()) * rw / sw;
        } else {
            x += rw / static_cast<double>(ids.size());
        }
        result.push_back(jsRound(x));
    }
    return result;
}

// TableLayout.layoutRow: places the cells of a row of `height` in a table of
// inner width `tw`, at `positions` (right edges) or, without, one after the
// other with the last cell filling the row. Returns the width of the cells
// before the last one.
Result layoutRow(Transaction& tx, std::size_t pageIndex, const std::string& rowId,
                 const std::optional<std::vector<double>>& positions, double height, double tw, double& stackWidth)
{
    const Page& page = *tx.page(pageIndex);
    const Insets off = detail::startInsets(page, *page.findCell(rowId));
    const std::vector<std::string> ids = childVertexIds(page, rowId);

    std::vector<double> edges;
    if (positions) {
        edges.push_back(off.left);
        edges.insert(edges.end(), positions->begin(), positions->end());
    }
    double x = off.left;
    double sw = 0;
    for (std::size_t i = 0; i < ids.size(); ++i) {
        const Geometry* source = geometryOf(*tx.page(pageIndex), ids[i]);
        if (source == nullptr)
            continue;
        const double cellHeight = std::max(0.0, height - off.top - off.bottom);
        double cellX = source->x();
        double cellWidth = source->width();
        if (positions) {
            // A row with more cells than the first one has no edge for them: they fill the rest.
            if (i + 1 < edges.size()) {
                cellX = edges[i];
                cellWidth = std::max(0.0, edges[i + 1] - cellX);
            } else if (i < edges.size()) {
                cellX = edges[i];
            }
            // Fills with the last cell if the row has fewer cells.
            if ((i == ids.size() - 1 && i + 2 < edges.size()) || i + 1 >= edges.size())
                cellWidth = std::max(0.0, tw - cellX - off.left - off.right);
        } else {
            cellX = x;
            x += cellWidth;
            if (i == ids.size() - 1)
                cellWidth = std::max(0.0, tw - off.left - off.right - sw);
            else
                sw += cellWidth;
        }
        const core::Rect alternate{0, 0, cellWidth, cellHeight};
        if (!editGeometry(tx, pageIndex, ids[i], [&](Geometry& g) {
                if (g.x() != cellX)
                    g.setX(cellX);
                if (g.y() != off.top)
                    g.setY(off.top);
                if (g.width() != cellWidth)
                    g.setWidth(cellWidth);
                if (g.height() != cellHeight)
                    g.setHeight(cellHeight);
                if (g.alternateBounds() != alternate)
                    g.setAlternateBounds(alternate);
            }))
            return Result::failure("could not lay out cell '" + ids[i] + "'");
    }
    stackWidth = sw;
    return Result::success();
}

// The last step of TableLayout.execute: shows the cells that no span covers
// and hides the covered ones, and grows each covering cell by the single
// column width of the cells it covers in its row and the single row height of
// the cells it covers in its column (their alternate bounds, which the layout
// has just set to their own size). Cells in a row without geometry are not
// laid out, so a span does not grow over them.
Result applyTableSpans(Transaction& tx, std::size_t pageIndex, const std::string& tableId)
{
    const Page& page = *tx.page(pageIndex);
    const std::vector<TableCellInfo> cells = tableCells(page, tableId);
    std::map<std::string, std::pair<double, double>> growth;
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const TableCellInfo& info = cells[i];
        const TableCellInfo& actual = cells[info.actual];
        if (info.actual == i || geometryOf(page, rowOf(page, actual.id)) == nullptr)
            continue;
        const Geometry* geometry = geometryOf(page, info.id);
        const auto alternate = geometry->alternateBounds();
        std::pair<double, double>& grow = growth[actual.id];
        if (actual.row == info.row)
            grow.first += alternate ? alternate->width : geometry->width();
        if (actual.col == info.col)
            grow.second += alternate ? alternate->height : geometry->height();
    }
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const std::string& id = cells[i].id;
        if (const auto it = growth.find(id); it != growth.end()) {
            if (!editGeometry(tx, pageIndex, id, [&](Geometry& g) {
                    g.setWidth(g.width() + it->second.first);
                    g.setHeight(g.height() + it->second.second);
                }))
                return Result::failure("could not grow merged cell '" + id + "'");
        }
        const Cell* cell = tx.page(pageIndex)->findCell(id);
        const bool visible = cells[i].actual == i;
        if (cell != nullptr && cell->isVisible() != visible) {
            Cell changed = *cell;
            changed.setVisible(visible);
            if (!tx.emplace<ReplaceCell>(pageIndex, std::move(changed)))
                return Result::failure("could not change the visibility of '" + id + "'");
        }
    }
    return Result::success();
}

} // namespace

namespace detail {

// TableLayout.execute (Graph.js:26660).
Result executeTableLayout(Transaction& tx, std::size_t pageIndex, const std::string& tableId)
{
    const Page* page = tx.page(pageIndex);
    const Cell* tableCell = page->findCell(tableId);
    if (tableCell == nullptr || tableCell->geometry() == nullptr)
        return Result::success();
    const core::Style style = tableCell->style();
    const Insets offset = startInsets(*page, *tableCell);
    const bool resizeLastRow = flag(style, "resizeLastRow", false);
    const bool resizeLast = flag(style, "resizeLast", false);
    const bool fixedRows = flag(style, "fixedRows", false);

    const auto setVisible = [&](const std::string& id, bool visible) {
        const Cell* cell = tx.page(pageIndex)->findCell(id);
        if (cell == nullptr || cell->isVisible() == visible)
            return true;
        Cell changed = *cell;
        changed.setVisible(visible);
        return tx.emplace<ReplaceCell>(pageIndex, std::move(changed));
    };
    const auto resizeTable = [&](double width, double height) {
        return editGeometry(tx, pageIndex, tableId, [&](Geometry& g) {
            g.setWidth(width);
            g.setHeight(height);
        });
    };

    const std::vector<std::string> rows = childVertexIds(*page, tableId);
    for (const std::string& row : rows) {
        if (!setVisible(row, true))
            return Result::failure("could not show row '" + row + "'");
    }

    double tableWidth = tableCell->geometry()->width();
    double tableHeight = tableCell->geometry()->height();
    if (!rows.empty() && !tableCell->isCollapsed()) {
        // Grows the table if the start sizes leave no room for content.
        double start = 0;
        for (const std::string& row : rows) {
            const Insets rs = startInsets(*tx.page(pageIndex), *tx.page(pageIndex)->findCell(row));
            start = std::max(start, rs.left + rs.right);
        }
        double minWidth = offset.left + offset.right + start;
        double minHeight = offset.top + offset.bottom;
        minWidth += minWidth > 0 ? kMinTableColumnWidth : 0;
        minHeight += minHeight > 0 ? kMinTableRowHeight : 0;
        if (tableWidth < minWidth || tableHeight < minHeight) {
            tableWidth = std::max(tableWidth, minWidth);
            tableHeight = std::max(tableHeight, minHeight);
            if (!resizeTable(tableWidth, tableHeight))
                return Result::failure("could not resize table '" + tableId + "'");
        }
    }

    const double th = tableHeight - offset.top - offset.bottom;
    const double tw = tableWidth - offset.left - offset.right;
    const double sh = visibleSize(*tx.page(pageIndex), rows, false);
    if (!(th > 0 && tw > 0 && !rows.empty() && sh > 0)) {
        for (const std::string& row : rows) {
            if (!setVisible(row, false))
                return Result::failure("could not hide row '" + row + "'");
        }
        return Result::success();
    }

    if (resizeLastRow) {
        const double delta = th - sh;
        if (!editGeometry(tx, pageIndex, rows.back(), [&](Geometry& g) { g.setHeight(delta + g.height()); }))
            return Result::failure("could not resize the last row");
    }

    std::optional<std::vector<double>> positions;
    if (!resizeLast)
        positions = rowLayout(*tx.page(pageIndex), rows.front(), tw);

    double y = offset.top;
    double stackWidth = 0;
    for (const std::string& row : rows) {
        const Geometry* source = geometryOf(*tx.page(pageIndex), row);
        if (source == nullptr)
            continue;
        const double rowY = jsRound(y);
        y += (resizeLastRow || fixedRows) ? source->height() : source->height() / sh * th;
        const double rowHeight = jsRound(y) - rowY;
        const double rowX = offset.left;
        if (!editGeometry(tx, pageIndex, row, [&](Geometry& g) {
                if (g.x() != rowX)
                    g.setX(rowX);
                if (g.y() != rowY)
                    g.setY(rowY);
                if (g.width() != tw)
                    g.setWidth(tw);
                if (g.height() != rowHeight)
                    g.setHeight(rowHeight);
            }))
            return Result::failure("could not place row '" + row + "'");
        double rowStack = 0;
        if (Result r = layoutRow(tx, pageIndex, row, positions, rowHeight, tw, rowStack); !r)
            return r;
        stackWidth = std::max(stackWidth, rowStack);
    }

    if (fixedRows && th < sh)
        tableHeight = y + offset.bottom;
    if (resizeLast && tw < stackWidth + kMinTableColumnWidth)
        tableWidth = stackWidth + offset.right + offset.left + kMinTableColumnWidth;
    if (!resizeTable(tableWidth, tableHeight))
        return Result::failure("could not resize table '" + tableId + "'");
    return applyTableSpans(tx, pageIndex, tableId);
}

} // namespace detail

// --- insert ----------------------------------------------------------------------------

Result insertTableRow(Transaction& tx, std::size_t pageIndex, const std::string& cellId, bool before)
{
    const Page* page = tx.page(pageIndex);
    if (page == nullptr)
        return Result::failure("unknown page");
    const Target target = targetOf(*page, cellId);
    if (target.table.empty())
        return Result::failure("'" + cellId + "' is not part of a table");

    std::string rowId = target.row;
    const std::vector<std::string> rows = childVertexIds(*page, target.table);
    if (rowId.empty() && !rows.empty())
        rowId = before ? rows.front() : rows.back();

    if (rowId.empty()) {
        // Empty table: a first row with a single cell.
        const Cell& table = *page->findCell(target.table);
        if (table.geometry() == nullptr)
            return Result::success();
        const double rowHeight = 40;
        const double rowWidth = table.geometry()->width();
        const std::string sides = table.style().value("tableRender", "") == "collapsed" ? "" : "top=0;left=0;bottom=0;right=0;";
        const std::string rowStyle =
            "shape=tableRow;horizontal=0;startSize=0;swimlaneHead=0;swimlaneBody=0;strokeColor=inherit;" + sides +
            "collapsible=0;dropTarget=0;fillColor=none;points=[[0,0.5],[1,0.5]];portConstraint=eastwest;fixedHeader=1;";
        const std::string cellStyle = "shape=partialRectangle;html=1;whiteSpace=wrap;connectable=0;strokeColor=inherit;"
                                      "overflow=hidden;fillColor=none;" +
                                      sides + "pointerEvents=1;";
        Cell row = Cell::makeVertex(tx.newCellId(pageIndex), target.table, "", rowStyle, {0, 0, rowWidth, rowHeight});
        Cell cell = Cell::makeVertex(tx.newCellId(pageIndex), row.id(), "", cellStyle, {0, 0, rowWidth, rowHeight});
        if (Result added = addCells(tx, pageIndex, {std::move(row), std::move(cell)}); !added)
            return added;
        return growTable(tx, pageIndex, target.table, 0, rowHeight);
    }

    const Cell& original = *page->findCell(rowId);
    const Geometry* rowGeometry = original.geometry();
    if (rowGeometry == nullptr)
        return Result::success(); // upstream does nothing without a row geometry
    const double rowHeight = rowGeometry->height();
    const std::size_t index = page->childPosition(rowId).value_or(0);

    std::vector<Cell> batch;
    batch.push_back(blankCopy(tx, pageIndex, original, false));
    for (const Cell* cell : childVertices(*page, rowId)) {
        Cell copy = blankCopy(tx, pageIndex, *cell, true);
        copy.setParent(batch.front().id());
        if (Geometry* geometry = copy.geometry())
            geometry->setHeight(rowHeight);
        batch.push_back(std::move(copy));
    }
    if (Result added = addCells(tx, pageIndex, std::move(batch), index + (before ? 0 : 1)); !added)
        return added;
    return growTable(tx, pageIndex, target.table, 0, rowHeight);
}

Result insertTableColumn(Transaction& tx, std::size_t pageIndex, const std::string& cellId, bool before)
{
    const Page* page = tx.page(pageIndex);
    if (page == nullptr)
        return Result::failure("unknown page");
    const Target target = targetOf(*page, cellId);
    if (target.table.empty())
        return Result::failure("'" + cellId + "' is not part of a table");
    const std::vector<std::string> rows = childVertexIds(*page, target.table);
    if (rows.empty())
        return Result::failure("the table has no rows");

    std::size_t index = 0;
    if (!target.cell.empty()) {
        index = indexOf(childVertexIds(*page, target.row), target.cell);
    } else if (!before) {
        const std::string& row = target.row.empty() ? rows.front() : target.row;
        const std::size_t count = childVertexIds(*page, row).size();
        index = count > 0 ? count - 1 : 0;
    }

    double width = kMinTableColumnWidth;
    for (const std::string& rowId : rows) {
        page = tx.page(pageIndex);
        const std::vector<const Cell*> cells = childVertices(*page, rowId);
        // Handles a possible missing cell in the row.
        Cell copy;
        if (index < cells.size()) {
            copy = blankCopy(tx, pageIndex, *cells[index], true);
        } else {
            copy = Cell(tx.newCellId(pageIndex));
            copy.setVertex(true);
            copy.setValue("");
        }
        copy.setParent(rowId);
        if (Geometry* geometry = copy.geometry()) {
            width = geometry->width();
            if (const Geometry* rowGeometry = geometryOf(*page, rowId))
                geometry->setHeight(rowGeometry->height());
        }
        const std::size_t position = index < cells.size() ? page->childPosition(cells[index]->id()).value_or(0) : page->childCount(rowId);
        if (Result added = addCells(tx, pageIndex, {std::move(copy)}, position + (before || index >= cells.size() ? 0 : 1));
            !added)
            return added;
    }
    return growTable(tx, pageIndex, target.table, width, 0);
}

// --- delete -------------------------------------------------------------------------------

Result deleteTableRow(Transaction& tx, std::size_t pageIndex, const std::string& cellId)
{
    const Page* page = tx.page(pageIndex);
    if (page == nullptr)
        return Result::failure("unknown page");
    const Target target = targetOf(*page, cellId);
    if (target.table.empty())
        return Result::failure("'" + cellId + "' is not part of a table");
    const std::vector<std::string> rows = childVertexIds(*page, target.table);
    if (rows.size() <= 1)
        return removeCells(tx, pageIndex, {target.table}, {.respectDeletable = false, .shrinkTables = false});

    const std::string row = target.row.empty() ? rows.back() : target.row;
    const Geometry* geometry = geometryOf(*page, row);
    const double height = geometry != nullptr ? geometry->height() : 0;
    if (Result removed = removeCells(tx, pageIndex, {row}, {.respectDeletable = false, .shrinkTables = false}); !removed)
        return removed;
    return growTable(tx, pageIndex, target.table, 0, -height);
}

Result deleteTableColumn(Transaction& tx, std::size_t pageIndex, const std::string& cellId)
{
    const Page* page = tx.page(pageIndex);
    if (page == nullptr)
        return Result::failure("unknown page");
    const Target target = targetOf(*page, cellId);
    if (target.table.empty())
        return Result::failure("'" + cellId + "' is not part of a table");
    const std::vector<std::string> rows = childVertexIds(*page, target.table);
    const RemoveOptions options{.respectDeletable = false, .shrinkTables = false};
    if (rows.empty())
        return removeCells(tx, pageIndex, {target.table}, options);

    const std::vector<std::string> cells = childVertexIds(*page, target.row.empty() ? rows.front() : target.row);
    if (cells.size() <= 1)
        return removeCells(tx, pageIndex, {target.table}, options);

    const std::size_t index = target.cell.empty() ? cells.size() - 1 : indexOf(cells, target.cell);
    double width = 0;
    std::vector<std::string> doomed;
    for (const std::string& row : rows) {
        const std::vector<std::string> rowCells = childVertexIds(*page, row);
        if (index >= rowCells.size())
            continue;
        doomed.push_back(rowCells[index]);
        if (const Geometry* geometry = geometryOf(*page, rowCells[index]))
            width = std::max(width, geometry->width());
    }
    if (Result removed = removeCells(tx, pageIndex, doomed, options); !removed)
        return removed;
    return growTable(tx, pageIndex, target.table, -width, 0);
}

// --- merge ---------------------------------------------------------------------------------------

Result mergeTableCells(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& cells)
{
    const Page* page = tx.page(pageIndex);
    if (page == nullptr)
        return Result::failure("unknown page");
    const std::optional<MergeRange> range = mergeRange(*page, cells);
    if (!range)
        return Result::failure("the cells are not a rectangle of table cells that are not merged");
    return setStyles(tx, pageIndex, {range->cell},
                     {{"rowspan", std::to_string(range->rowspan)}, {"colspan", std::to_string(range->colspan)}});
}

Result unmergeTableCells(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& cells)
{
    const Page* page = tx.page(pageIndex);
    if (page == nullptr)
        return Result::failure("unknown page");
    std::vector<std::string> tableCellIds;
    for (const std::string& id : cells) {
        const Cell* cell = page->findCell(id);
        if (cell != nullptr && isTableCell(*page, *cell))
            tableCellIds.push_back(id);
    }
    if (tableCellIds.empty())
        return Result::failure("none of the cells is a table cell");
    return setStyles(tx, pageIndex, tableCellIds, {{"rowspan", std::nullopt}, {"colspan", std::nullopt}});
}

// --- resize ------------------------------------------------------------------------------------

Result setTableRowHeight(Transaction& tx, std::size_t pageIndex, const std::string& rowId, double dy, bool extend)
{
    const Page* page = tx.page(pageIndex);
    const Cell* row = page != nullptr ? page->findCell(rowId) : nullptr;
    if (row == nullptr || !isTableRow(*page, *row))
        return Result::failure("'" + rowId + "' is not a table row");
    if (row->geometry() == nullptr)
        return Result::success();
    const std::string tableId = row->parent();
    const std::vector<std::string> rows = childVertexIds(*page, tableId);

    if (!editGeometry(tx, pageIndex, rowId, [&](Geometry& g) { g.setHeight(g.height() + dy); }))
        return Result::failure("could not resize row '" + rowId + "'");
    // Shifts and resizes the neighbour row.
    if (!extend) {
        const std::size_t index = indexOf(rows, rowId);
        if (index + 1 < rows.size() &&
            !editGeometry(tx, pageIndex, rows[index + 1], [&](Geometry& g) {
                g.setY(g.y() + dy);
                g.setHeight(g.height() - dy);
            }))
            return Result::failure("could not resize the next row");
        // Always extends for the last row.
        extend = rowId == rows.back();
    }
    if (extend && !editGeometry(tx, pageIndex, tableId, [&](Geometry& g) { g.setHeight(g.height() + dy); }))
        return Result::failure("could not resize table '" + tableId + "'");
    return Result::success();
}

Result setTableColumnWidth(Transaction& tx, std::size_t pageIndex, const std::string& colId, double dx, bool extend)
{
    const Page* page = tx.page(pageIndex);
    const Cell* col = page != nullptr ? page->findCell(colId) : nullptr;
    if (col == nullptr || !isTableCell(*page, *col))
        return Result::failure("'" + colId + "' is not a table cell");
    const std::string rowId = col->parent();
    const std::string tableId = page->findCell(rowId)->parent();
    const std::size_t index = indexOf(childVertexIds(*page, rowId), colId);
    const bool lastColumn = index + 1 == childVertexIds(*page, rowId).size();

    for (const std::string& row : childVertexIds(*page, tableId)) {
        const std::vector<std::string> cells = childVertexIds(*tx.page(pageIndex), row);
        if (index >= cells.size())
            continue;
        // The column grows, keeping the alternate bounds (the layout's source of widths) in step.
        if (!editGeometry(tx, pageIndex, cells[index], [&](Geometry& g) {
                g.setWidth(g.width() + dx);
                if (auto alternate = g.alternateBounds()) {
                    alternate->width += dx;
                    g.setAlternateBounds(alternate);
                }
            }))
            return Result::failure("could not resize cell '" + cells[index] + "'");
        // Shifts and resizes the neighbour column.
        if (index + 1 < cells.size() &&
            !editGeometry(tx, pageIndex, cells[index + 1], [&](Geometry& g) {
                g.setX(g.x() + dx);
                if (!extend) {
                    g.setWidth(g.width() - dx);
                    if (auto alternate = g.alternateBounds()) {
                        alternate->width -= dx;
                        g.setAlternateBounds(alternate);
                    }
                }
            }))
            return Result::failure("could not resize cell '" + cells[index + 1] + "'");
    }
    if ((lastColumn || extend) && !editGeometry(tx, pageIndex, tableId, [&](Geometry& g) { g.setWidth(g.width() + dx); }))
        return Result::failure("could not resize table '" + tableId + "'");
    return Result::success();
}

} // namespace qraw::commands::ops
