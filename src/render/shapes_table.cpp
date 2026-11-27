// Table geometry shared by the table painters (shapes_table.h).
//
// Port of js/grapheditor/Graph.js (visitTableCells, isCollapsedTable,
// isCollapsedTableChildShape) and mxgraph/src/view/mxGraph.js
// (getSwimlaneDirection, getActualStartSize) (draw.io, Apache-2.0), modified.

#include "shapes_table.h"

#include <algorithm>
#include <cmath>

namespace qraw::render::detail {

namespace {

using geometry::Direction;

bool isSwimlaneLike(const ResolvedStyle& style)
{
    const std::string_view shape = style.shape();
    return shape == "swimlane" || shape == "table" || shape == "tableRow";
}

// The `rowspan` or `colspan` of a cell: parseInt(style[key] || 1), where
// anything below 2 counts as no span.
int spanOf(const ResolvedStyle* style, std::string_view key)
{
    if (style == nullptr)
        return 1;
    const double value = style->number(key, 1);
    return value >= 2 ? static_cast<int>(std::min(value, 1e6)) : 1;
}

} // namespace

Direction swimlaneDirection(const ResolvedStyle& style)
{
    int n = style.horizontal() ? 0 : 3;
    switch (style.direction()) {
    case Direction::North:
        --n;
        break;
    case Direction::West:
        n += 2;
        break;
    case Direction::South:
        n += 1;
        break;
    case Direction::East:
        break;
    }
    const int mod = ((n % 2) + 2) % 2;
    if (style.flipH() && mod == 1)
        n += 2;
    if (style.flipV() && mod == 0)
        n += 2;
    static constexpr Direction order[] = {Direction::North, Direction::East, Direction::South, Direction::West};
    return order[((n % 4) + 4) % 4];
}

StartInsets actualStartInsets(const ResolvedStyle* style)
{
    StartInsets result;
    if (style == nullptr || !isSwimlaneLike(*style))
        return result;
    const double size = std::trunc(style->startSize());
    switch (swimlaneDirection(*style)) {
    case Direction::North:
        result.top = size;
        break;
    case Direction::West:
        result.left = size;
        break;
    case Direction::South:
        result.bottom = size;
        break;
    case Direction::East:
        result.right = size;
        break;
    }
    return result;
}

double swimlaneArcSize(const ResolvedStyle& style, double w, double h, double start)
{
    if (style.absoluteArcSize())
        return std::min(w / 2, std::min(h / 2, style.number("arcSize", geometry::kLineArcSize) / 2));
    const double f = style.number("arcSize", kRectangleRoundingFactor * 100) / 100;
    return start * f * 3;
}

std::vector<TableGridCell> visitTableCells(const ResolvedStyle& tableStyle, const std::vector<ShapeChild>& rows)
{
    const StartInsets start = actualStartInsets(&tableStyle);
    std::vector<TableGridCell> result;
    std::vector<std::size_t> lastRow;
    for (std::size_t i = 0; i < rows.size(); ++i) {
        const ShapeChild& row = rows[i];
        const StartInsets rowStart = actualStartInsets(row.style);
        // The title strip of the row counts for the lines when the row has a title border.
        const bool head = row.style != nullptr && row.style->flag("swimlaneHead", true)
                          && !row.style->string("strokeColor").empty() && row.style->string("strokeColor") != "none";
        std::vector<std::size_t> current;
        for (std::size_t j = 0; j < row.children.size(); ++j) {
            const ShapeChild& cell = row.children[j];
            TableGridCell col;
            col.cell = &cell;
            col.row = &row;
            col.rowIndex = static_cast<int>(i);
            col.colIndex = static_cast<int>(j);
            col.rowCount = static_cast<int>(rows.size());
            col.colCount = static_cast<int>(row.children.size());
            col.actual = result.size();
            const Rect size = cell.alternateBounds.value_or(cell.bounds);
            col.point.x = size.width + (!current.empty() ? result[current.back()].point.x : start.left + rowStart.left);
            col.point.y = size.height + (!lastRow.empty() ? result[lastRow.front()].point.y : start.top + rowStart.top);

            if (j < lastRow.size() && result[lastRow[j]].rowspan > 1) {
                const TableGridCell& above = result[lastRow[j]];
                col.rowspan = above.rowspan - 1;
                col.colspan = above.colspan;
                col.actual = above.actual;
            } else if (!current.empty() && result[current.back()].colspan > 1) {
                const TableGridCell& left = result[current.back()];
                col.rowspan = left.rowspan;
                col.colspan = left.colspan - 1;
                col.actual = left.actual;
            } else {
                col.rowspan = spanOf(cell.style, "rowspan");
                col.colspan = spanOf(cell.style, "colspan");
            }
            col.origin = {start.left + (head ? rowStart.left : 0), start.top + (head ? rowStart.top : 0)};
            current.push_back(result.size());
            result.push_back(col);
        }
        lastRow = std::move(current);
    }
    return result;
}

bool isCollapsedTable(const ResolvedStyle& style)
{
    return style.string("tableRender") == "collapsed" && style.string("childLayout") == "tableLayout"
           && style.shape() == "table";
}

bool isCollapsedTableChildShape(const ResolvedStyle& style)
{
    const std::string_view shape = style.shape();
    if (shape == "tableRow" || shape == "partialRectangle")
        return true;
    return (shape.empty() || shape == "rectangle" || shape == "swimlane") && !style.rounded();
}

} // namespace qraw::render::detail
