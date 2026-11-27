// Container shapes: swimlane, table, tableRow and partialRectangle.
//
// Port of mxgraph/src/shape/mxSwimlane.js and js/grapheditor/Shapes.js
// (TableShape :207, TableRowShape :923, PartialRectangleShape :5787), with
// the table geometry of Graph.visitTableCells/getTableLines
// (js/grapheditor/Graph.js) (draw.io, Apache-2.0), modified.
//
// Collapsed table rendering (`tableRender=collapsed`) is in
// shapes_table_collapsed.cpp, the table geometry in shapes_table.cpp.
//
// Not painted (docs/design/render.md, section 10): glass, the footer
// (`footerSize`), the swimlane image, striped rows/columns and the repainting of
// grid lines under filled cells of a table that is not collapsed
// (Graph.paintTableCellLines). These depend on the live graph model upstream.

#include "shapes_common.h"
#include "shapes_table.h"

#include <optional>
#include <vector>

namespace qraw::render::detail {

namespace {

using geometry::Direction;

bool isVerticalDirection(Direction d) { return d == Direction::North || d == Direction::South; }

double directionAngle(Direction d)
{
    switch (d) {
    case Direction::North:
        return 270;
    case Direction::West:
        return 180;
    case Direction::South:
        return 90;
    case Direction::East:
        break;
    }
    return 0;
}

// --- swimlane -------------------------------------------------------------------

double titleSize(const ResolvedStyle& style) { return std::max(0.0, style.startSize()); }

// mxSwimlane.paintSwimlane/paintRoundedSwimlane tail: the body is stroked, and
// filled when the lane has a fill color.
void finishBody(Canvas& c, const std::optional<QColor>& laneFill, bool body)
{
    if (body) {
        if (!laneFill)
            c.stroke();
        else
            c.fillAndStroke();
    } else if (laneFill) {
        c.fill();
    }
}

void finishHead(Canvas& c, bool head)
{
    if (head)
        c.fillAndStroke();
    else
        c.fill();
}

// mxSwimlane.paintDivider (mxSwimlane.js:803): `shadow` keeps the shadow of
// the divider when the lane itself is unfilled.
void paintDivider(Canvas& c, const ResolvedStyle& style, double w, double h, double start, bool shadow)
{
    if (start == 0)
        return;
    if (!shadow)
        c.setShadow(false);
    c.begin();
    if (style.horizontal()) {
        c.moveTo(0, start);
        c.lineTo(w, start);
    } else {
        c.moveTo(start, 0);
        c.lineTo(start, h);
    }
    c.stroke();
}

// mxSwimlane.paintSwimlane (mxSwimlane.js:432). `bodyVisible` is false for a
// collapsed lane, which paints only its title.
void paintSwimlane(Canvas& c, const ResolvedStyle& style, double w, double h, double start, bool bodyVisible)
{
    const std::optional<QColor> fill = style.swimlaneFillColor();
    const bool line = style.swimlaneLine();
    const bool head = style.flag("swimlaneHead", true);
    const bool body = style.flag("swimlaneBody", true);

    if (style.horizontal()) {
        c.begin();
        c.moveTo(0, start);
        c.lineTo(0, 0);
        c.lineTo(w, 0);
        c.lineTo(w, start);
        finishHead(c, head);

        if (bodyVisible && start < h) {
            if (fill)
                c.setFillColor(fill);
            c.begin();
            c.moveTo(0, start);
            c.lineTo(0, h);
            c.lineTo(w, h);
            c.lineTo(w, start);
            finishBody(c, fill, body);
        }
    } else {
        c.begin();
        c.moveTo(start, 0);
        c.lineTo(0, 0);
        c.lineTo(0, h);
        c.lineTo(start, h);
        finishHead(c, head);

        if (bodyVisible && start < w) {
            if (fill)
                c.setFillColor(fill);
            c.begin();
            c.moveTo(start, 0);
            c.lineTo(w, 0);
            c.lineTo(w, h);
            c.lineTo(start, h);
            finishBody(c, fill, body);
        }
    }

    if (line)
        paintDivider(c, style, w, h, start, !fill);
}

// mxSwimlane.paintRoundedSwimlane (mxSwimlane.js:564)
void paintRoundedSwimlane(Canvas& c, const ResolvedStyle& style, double w, double h, double start, double r,
                          bool bodyVisible)
{
    const std::optional<QColor> fill = style.swimlaneFillColor();
    const bool line = style.swimlaneLine();
    const bool head = style.flag("swimlaneHead", true);
    const bool body = style.flag("swimlaneBody", true);

    if (style.horizontal()) {
        c.begin();
        c.moveTo(w, start);
        c.lineTo(w, r);
        c.quadTo(w, 0, w - std::min(w / 2, r), 0);
        c.lineTo(std::min(w / 2, r), 0);
        c.quadTo(0, 0, 0, r);
        c.lineTo(0, start);
        finishHead(c, head);

        if (bodyVisible && start < h) {
            if (fill)
                c.setFillColor(fill);
            c.begin();
            c.moveTo(0, start);
            c.lineTo(0, h - r);
            c.quadTo(0, h, std::min(w / 2, r), h);
            c.lineTo(w - std::min(w / 2, r), h);
            c.quadTo(w, h, w, h - r);
            c.lineTo(w, start);
            finishBody(c, fill, body);
        }
    } else {
        c.begin();
        c.moveTo(start, 0);
        c.lineTo(r, 0);
        c.quadTo(0, 0, 0, std::min(h / 2, r));
        c.lineTo(0, h - std::min(h / 2, r));
        c.quadTo(0, h, r, h);
        c.lineTo(start, h);
        finishHead(c, head);

        if (bodyVisible && start < w) {
            if (fill)
                c.setFillColor(fill);
            c.begin();
            c.moveTo(start, h);
            c.lineTo(w - r, h);
            c.quadTo(w, h, w, h - std::min(h / 2, r));
            c.lineTo(w, std::min(h / 2, r));
            c.quadTo(w, 0, w - r, 0);
            c.lineTo(start, 0);
            finishBody(c, fill, body);
        }
    }

    if (line)
        paintDivider(c, style, w, h, start, !fill);
}

// mxSwimlane.paintSeparator (mxSwimlane.js:834)
void paintSeparator(Canvas& c, const ResolvedStyle& style, double w, double h, double start)
{
    const std::optional<QColor> color = style.color("separatorColor");
    if (!color)
        return;
    c.setStrokeColor(color);
    c.setDashed(true);
    c.begin();
    if (style.horizontal()) {
        c.moveTo(w, start);
        c.lineTo(w, h);
    } else {
        c.moveTo(start, 0);
        c.lineTo(w, 0);
    }
    c.stroke();
    c.setDashed(false);
}

// mxSwimlane.paintVertexShape (mxSwimlane.js:241). Leaves the canvas
// translated to (x, y) like upstream; the caller undoes it if it paints on.
void paintSwimlaneVertex(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h)
{
    const ResolvedStyle& style = context.style;
    double start = titleSize(style);
    if (start == 0 && !style.flag("fixedHeader", false))
        return;

    const bool horizontal = style.horizontal();
    start = std::min(start, horizontal ? h : w);

    // mxSwimlane.getGradientBounds: the gradient covers the title only.
    const std::optional<QColor> fill = style.fillColor();
    const std::optional<QColor> gradient = style.gradientColor();
    if (fill && gradient) {
        const Rect titleBounds = horizontal ? Rect{x, y, w, start} : Rect{x, y, start, h};
        c.setGradient(*fill, *gradient, titleBounds, parseGradientDirection(style.gradientDirection()));
    }

    c.translate(x, y);
    const bool bodyVisible = !context.collapsed;
    if (!style.rounded()) {
        paintSwimlane(c, style, w, h, start, bodyVisible);
    } else {
        double r = swimlaneArcSize(style, w, h, start);
        r = std::min((horizontal ? h : w) - start, std::min(start, r));
        paintRoundedSwimlane(c, style, w, h, start, r, bodyVisible);
    }
    paintSeparator(c, style, w, h, start);
}

// mxSwimlane.getLabelBounds (mxSwimlane.js:132): the title area.
Rect swimlaneLabelBounds(const ResolvedStyle& style, const Rect& rect, const VertexShape& fallback)
{
    const double start = titleSize(style);
    if (start == 0 && !style.flag("fixedHeader", false))
        return fallback.VertexShape::labelBounds(style, rect);

    const Direction d = style.direction();
    const bool flipH = style.flipH();
    const bool flipV = style.flipV();
    Rect bounds = rect;

    // East is default
    const bool shapeVertical = isVerticalDirection(d);
    const bool realHorizontal = style.horizontal() == !shapeVertical;
    const bool towardsEnd = d == Direction::South || d == Direction::West;
    const bool realFlipH = !realHorizontal && flipH != towardsEnd;
    const bool realFlipV = realHorizontal && flipV != towardsEnd;

    if (!shapeVertical) {
        const double size = std::min(bounds.height, start);
        if (realFlipH || realFlipV)
            bounds.y += bounds.height - size;
        bounds.height = size;
    } else {
        const double size = std::min(bounds.width, start);
        if (realFlipH || realFlipV)
            bounds.x += bounds.width - size;
        bounds.width = size;
    }
    return bounds;
}

// --- partial rectangle ------------------------------------------------------------

// PartialRectangleShape.paintVertexShape (Shapes.js:5787): fill without
// stroke, then only the enabled sides (top/right/bottom/left) as one stroked
// path.
void paintPartialRectangle(Canvas& c, const ResolvedStyle& style, double x, double y, double w, double h)
{
    c.setStrokeColor(std::nullopt);

    const bool top = style.flag("top", true);
    const bool left = style.flag("left", true);
    const bool right = style.flag("right", true);
    const bool bottom = style.flag("bottom", true);

    // drawHidden is true for this shape, so the sides are always painted.
    c.rect(x, y, w, h);
    c.fill();

    c.setStrokeColor(style.strokeColor());
    c.setLineCap(LineCap::Square);
    c.begin();
    c.moveTo(x, y);
    if (top)
        c.lineTo(x + w, y);
    else
        c.moveTo(x + w, y);
    if (right)
        c.lineTo(x + w, y + h);
    else
        c.moveTo(x + w, y + h);
    if (bottom)
        c.lineTo(x, y + h);
    else
        c.moveTo(x, y + h);
    if (left)
        c.lineTo(x, y);
    c.stroke();
    c.setLineCap(LineCap::Flat);
}

// --- table ----------------------------------------------------------------------------

// A polyline of the table grid; a gap (nullopt) breaks the line (rowspan or
// colspan cells).
using TableLine = std::vector<std::optional<Point>>;

// Graph.getTableLines (Graph.js:24795): the row and column lines of the table
// from the grid of its rows (children) and their cells, merged cells included.
std::vector<TableLine> tableLines(const ResolvedStyle& tableStyle, const std::vector<ShapeChild>& rows,
                                  bool rowLines, bool columnLines)
{
    std::vector<TableLine> horizontal;
    std::vector<TableLine> vertical;
    if (!rowLines && !columnLines)
        return {};

    for (const TableGridCell& iter : visitTableCells(tableStyle, rows)) {
        // Constructs horizontal lines.
        if (rowLines && iter.rowIndex < iter.rowCount - 1) {
            if (horizontal.size() <= static_cast<std::size_t>(iter.rowIndex))
                horizontal.resize(static_cast<std::size_t>(iter.rowIndex) + 1);
            TableLine& line = horizontal[static_cast<std::size_t>(iter.rowIndex)];
            if (line.empty())
                line.push_back(Point{iter.origin.x, iter.point.y});
            if (iter.rowspan > 1)
                line.push_back(std::nullopt);
            line.push_back(iter.point);
        }

        // Constructs vertical lines.
        if (columnLines && iter.colIndex < iter.colCount - 1) {
            if (vertical.size() <= static_cast<std::size_t>(iter.colIndex))
                vertical.resize(static_cast<std::size_t>(iter.colIndex) + 1);
            TableLine& line = vertical[static_cast<std::size_t>(iter.colIndex)];
            if (line.empty())
                line.push_back(Point{iter.point.x, iter.origin.y});
            if (iter.colspan > 1)
                line.push_back(std::nullopt);
            line.push_back(iter.point);
        }
    }

    horizontal.insert(horizontal.end(), vertical.begin(), vertical.end());
    return horizontal;
}

// TableLineShape.paintTableLine (Shapes.js:126)
void paintTableLine(Canvas& c, const TableLine& line, double dx, double dy)
{
    if (line.empty())
        return;
    bool previous = false;
    c.begin();
    for (const std::optional<Point>& point : line) {
        if (point) {
            if (!previous)
                c.moveTo(point->x + dx, point->y + dy);
            else
                c.lineTo(point->x + dx, point->y + dy);
        }
        previous = point.has_value();
    }
    c.stroke();
}

// TableShape (Shapes.js:207) and TableRowShape (:923). A table paints as a
// swimlane (title + body), or as a partial rectangle without title, and then
// its grid lines. A row is the same without the grid.
class TableShape : public VertexShape {
public:
    explicit TableShape(bool paintsGrid) : paintsGrid_(paintsGrid) {}

    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        const bool horizontal = style.horizontal();
        const double start = titleSize(style);
        const bool fixedHeader = style.flag("fixedHeader", false);

        // tableRender=collapsed: the table paints its rows and cells too.
        if (paintsGrid_ && !context.collapsed && isCollapsedTable(style)) {
            const Rect frame = enterTableFrame(c, style, x, y, w, h);
            const std::vector<ShapeChild> rows = context.children ? context.children() : std::vector<ShapeChild>{};
            paintCollapsedTable(c, style, rows, frame.x, frame.y, frame.width, frame.height);
            return;
        }

        if (start == 0 && style.rounded()) {
            // Headerless rounded table: both the swimlane and the partial
            // rectangle would paint square corners, so paint the rounded
            // background with the rectangle arc.
            const double r = rectangleArcSize(style, w, h);
            c.begin();
            c.roundrect(x, y, w, h, r, r);
            c.fillAndStroke();
        } else if (start == 0 && !fixedHeader) {
            paintPartialRectangle(c, style, x, y, w, h);
        } else {
            paintSwimlaneVertex(c, context, x, y, w, h);
            c.translate(-x, -y);
        }

        if (paintsGrid_ && !context.collapsed && ((horizontal && start < h) || (!horizontal && start < w)))
            paintGrid(c, context, x, y, w, h);
    }

    [[nodiscard]] Rect labelBounds(const ResolvedStyle& style, const Rect& rect) const override
    {
        return swimlaneLabelBounds(style, rect, *this);
    }

private:
    // The canvas in the unrotated coordinates of the table, as
    // TableShape.paintForeground and paintCollapsedTable use them: the
    // transform of paintVertex() is undone, and the state bounds are recovered
    // from the (possibly inverted) paint bounds. Returns the table's own
    // rectangle.
    static Rect enterTableFrame(Canvas& c, const ResolvedStyle& style, double x, double y, double w, double h)
    {
        const Direction direction = style.direction();
        bool flipH = style.flipH();
        bool flipV = style.flipV();
        Rect frame{x, y, w, h};
        if (isVerticalDirection(direction)) {
            std::swap(flipH, flipV);
            const double t = (h - w) / 2;
            frame = {x - t, y + t, h, w};
        }
        const double rotation = style.rotation() + directionAngle(direction);
        if (rotation != 0 || flipH || flipV)
            c.rotate(-rotation, flipH, flipV, x + w / 2, y + h / 2);
        return frame;
    }

    // TableShape.paintForeground/paintTableForeground (Shapes.js:253, :380).
    void paintGrid(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const
    {
        if (!context.children)
            return;
        const std::vector<ShapeChild> rows = context.children();
        if (rows.empty())
            return;

        const ResolvedStyle& style = context.style;
        const Rect frame = enterTableFrame(c, style, x, y, w, h);

        // Upstream paints through the shape's shadow; a shadow under every
        // grid line is not intended, so it is off here.
        c.setShadow(false);
        const std::vector<TableLine> lines =
            tableLines(style, rows, style.flag("rowLines", true), style.flag("columnLines", true));
        for (const TableLine& line : lines)
            paintTableLine(c, line, frame.x, frame.y);
    }

    bool paintsGrid_;
};

class SwimlaneShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        paintSwimlaneVertex(c, context, x, y, w, h);
    }
    [[nodiscard]] Rect labelBounds(const ResolvedStyle& style, const Rect& rect) const override
    {
        return swimlaneLabelBounds(style, rect, *this);
    }
};

class PartialRectangleShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        paintPartialRectangle(c, context.style, x, y, w, h);
    }
};

} // namespace

void registerContainerShapes(ShapeRegistry& registry)
{
    registry.add("swimlane", std::make_unique<SwimlaneShape>());
    registry.add("table", std::make_unique<TableShape>(true));
    registry.add("tableRow", std::make_unique<TableShape>(false));
    registry.add("partialRectangle", std::make_unique<PartialRectangleShape>());
}

} // namespace qraw::render::detail
