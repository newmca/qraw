#pragma once

// Table geometry and painting shared by the container shapes
// (shapes_container.cpp) and the collapsed table painter
// (shapes_table_collapsed.cpp). Private to qraw_render.
//
// Port of the table code of js/grapheditor/Graph.js (visitTableCells,
// getTableLines, isCollapsedTable, isCollapsedTableChildShape) and of
// mxgraph/src/view/mxGraph.js (getSwimlaneDirection, getActualStartSize)
// (draw.io, Apache-2.0), modified.

#include "qraw/render/shape.h"

#include <cstddef>
#include <vector>

namespace qraw::render::detail {

// mxGraph.getSwimlaneDirection: the side of the title.
geometry::Direction swimlaneDirection(const ResolvedStyle& style);

// mxGraph.getActualStartSize: the strip the title of a swimlane, table or
// table row takes, on the side the direction puts it (upstream returns them
// as x, y, height and width).
struct StartInsets {
    double left = 0, top = 0, right = 0, bottom = 0;
    [[nodiscard]] bool empty() const { return left == 0 && top == 0 && right == 0 && bottom == 0; }
};
StartInsets actualStartInsets(const ResolvedStyle* style);

// mxSwimlane.getSwimlaneArcSize: the corner radius of a rounded swimlane whose
// title strip is `start` wide.
double swimlaneArcSize(const ResolvedStyle& style, double w, double h, double start);

// One cell of Graph.visitTableCells: the grid position, the span that reaches
// the cell and the bottom right corner of its column and row.
struct TableGridCell {
    const ShapeChild* cell = nullptr;
    const ShapeChild* row = nullptr;
    int rowIndex = 0;
    int colIndex = 0;
    int rowCount = 0;
    int colCount = 0;
    int rowspan = 1; // what is left of the span that covers the cell, as upstream counts it
    int colspan = 1;
    std::size_t actual = 0; // index of the covering cell in the result; its own when no span covers it
    Point point;            // bottom right corner in table coordinates, from the single column and row size
    Point origin;           // where the lines of the row start (the visitor's x0 and y0)
};

// Graph.visitTableCells for the rows (and their cells) of a table, hidden cells
// included: the rows of a table's children listing, with `visible` false for the
// cells a merged cell covers.
std::vector<TableGridCell> visitTableCells(const ResolvedStyle& tableStyle, const std::vector<ShapeChild>& rows);

// Graph.isCollapsedTable: tableRender=collapsed on a table with its layout.
bool isCollapsedTable(const ResolvedStyle& style);
// Graph.isCollapsedTableChildShape: a row or cell whose fill and borders the
// collapsed table paints (rectangular shapes; other shapes paint themselves).
bool isCollapsedTableChildShape(const ResolvedStyle& style);

// TableShape.paintCollapsedTable. (x, y, w, h) is the table's own, unrotated
// rectangle, the canvas has the table's rotation and flips undone.
void paintCollapsedTable(Canvas& c, const ResolvedStyle& style, const std::vector<ShapeChild>& rows, double x, double y,
                         double w, double h);

} // namespace qraw::render::detail
