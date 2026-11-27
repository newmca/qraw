// Tables with tableRender=collapsed: the table paints the fills and the
// borders of its rows and cells itself, each shared border once.
//
// Port of TableShape.paintCollapsedTable and its helpers in
// js/grapheditor/Shapes.js (addCollapsedRegionPath, fillCollapsedRegion,
// paintCollapsedOutline) and of Graph.getCollapsedTableRegions,
// getCollapsedTableBorders, getCollapsedTableBands and
// getCollapsedTableFillMode in js/grapheditor/Graph.js (draw.io, Apache-2.0),
// modified: the colours a row or cell takes from its parent (inherit,
// parentFillColor, parentStrokeColor) are resolved here, and the striping
// (evenRowColor, ...) and the glass effect of the title are not painted.
//
// A table cell's and a row's own shape paints nothing (the scene renderer
// skips it, see isCollapsedTableChildShape), so everything about the grid
// comes from the table's children listing: the visible rows and cells with
// their bounds and resolved styles, and the cells a merged cell covers.

#include "shapes_common.h"
#include "shapes_table.h"

#include <algorithm>
#include <cmath>
#include <map>
#include <string>

namespace qraw::render::detail {

namespace {

constexpr double kEps = 0.01;

// Math.round(value * 100) / 100
double round2(double value) { return jsRound(value * 100) / 100; }

// --- borders and colours ----------------------------------------------------------

// Graph.createCollapsedTableBorder: how a piece of the grid is stroked.
struct Border {
    std::optional<QColor> color;
    double width = 1;
    bool dashed = false;
    std::string dashPattern;
    bool fixDash = false;
    double opacity = 100;
    double strokeOpacity = 100;
    // Graph.getCollapsedTableBorderKey: what two borders must share to be
    // painted as one line; empty when the border is not visible.
    std::string key;
};

Border makeBorder(const std::optional<QColor>& stroke, const ResolvedStyle& style)
{
    Border border;
    border.color = stroke;
    border.width = style.strokeWidth();
    border.dashed = style.dashed();
    border.dashPattern = border.dashed ? std::string(style.dashPattern()) : std::string();
    border.fixDash = style.fixDash();
    border.opacity = style.opacity();
    border.strokeOpacity = style.strokeOpacity();
    if (border.color && border.width > 0) {
        border.key = border.color->name(QColor::HexArgb).toStdString() + '|' + std::to_string(border.width) + '|'
                     + (border.dashed ? '1' : '0') + '|' + border.dashPattern + '|' + (border.fixDash ? '1' : '0') + '|'
                     + std::to_string(border.opacity) + '|' + std::to_string(border.strokeOpacity);
    }
    return border;
}

[[nodiscard]] const std::string& keyOf(const Border* border)
{
    static const std::string none;
    return border != nullptr ? border->key : none;
}

// The colours a shape has once inherit and the parent colours are resolved
// (mxCellRenderer.resolveColor): a row takes them from the table and a cell
// from its row.
struct Colors {
    std::optional<QColor> fill;
    std::optional<QColor> laneFill;
    std::optional<QColor> stroke;
};

std::optional<QColor> pick(const ResolvedStyle& style, std::string_view key, const Colors& parent,
                           const std::optional<QColor>& sameOfParent)
{
    const std::string_view raw = style.string(key);
    if (raw == "inherit")
        return sameOfParent;
    if (raw == "parentFillColor")
        return parent.fill;
    if (raw == "parentStrokeColor")
        return parent.stroke;
    return style.color(key);
}

Colors resolveColors(const ResolvedStyle& style, const Colors& parent)
{
    Colors colors;
    colors.fill = pick(style, "fillColor", parent, parent.fill);
    colors.laneFill = pick(style, "swimlaneFillColor", parent, parent.laneFill);
    colors.stroke = pick(style, "strokeColor", parent, parent.stroke);
    return colors;
}

// --- regions -------------------------------------------------------------------------

// Graph.getCollapsedTableFillMode: how the shape of a table, row or cell
// fills itself.
enum class FillMode { Full, Lane, None };

FillMode fillMode(const ResolvedStyle& style, const StartInsets& start)
{
    const std::string_view shape = style.shape();
    if (shape == "table" || shape == "tableRow") {
        const bool fixed = style.flag("fixedHeader", false);
        // Headerless rounded tables without collapsed borders fill their rounded rectangle.
        const bool rounded = style.string("tableRender") != "collapsed" && style.rounded();
        return (start.empty() && (!fixed || rounded)) ? FillMode::Full : FillMode::Lane;
    }
    if (shape == "swimlane")
        return (start.empty() && !style.flag("fixedHeader", true)) ? FillMode::None : FillMode::Lane;
    return FillMode::Full;
}

// Graph.getCollapsedTableBands: a rectangle split into the strips of its
// title and what is left.
struct Bands {
    std::vector<Rect> bands;
    Rect body;
};

Bands splitBands(double x, double y, double w, double h, const StartInsets& start)
{
    const double top = std::min(start.top, h);
    const double bottom = std::min(start.bottom, h - top);
    const double left = std::min(start.left, w);
    const double right = std::min(start.right, w - left);
    const double bh = h - top - bottom;
    Bands result;
    if (top > 0)
        result.bands.push_back({x, y, w, top});
    if (bottom > 0)
        result.bands.push_back({x, y + h - bottom, w, bottom});
    if (left > 0 && bh > 0)
        result.bands.push_back({x, y + top, left, bh});
    if (right > 0 && bh > 0)
        result.bands.push_back({x + w - right, y + top, right, bh});
    result.body = {x + left, y + top, std::max(0.0, w - left - right), std::max(0.0, bh)};
    return result;
}

struct Sides {
    bool top = true, left = true, bottom = true, right = true;
};

// Sides as in PartialRectangleShape: a missing flag is on, 0 hides the side.
Sides sidesOf(const ResolvedStyle& style)
{
    return {style.string("top", "1") == "1", style.string("left", "1") == "1", style.string("bottom", "1") == "1",
            style.string("right", "1") == "1"};
}

// A row or a cell of the table, in table coordinates.
struct Part {
    bool takeover = false; // its own shape paints nothing, the table paints it
    Sides sides;
    bool line = true; // swimlaneLine
    Rect rect;
    FillMode mode = FillMode::Full;
    Bands split;
    Colors colors;
    Border border;
    double opacity = 100;
    double fillOpacity = 100;
    std::optional<QColor> gradient;
    std::string_view gradientDirection;
    const Part* row = nullptr; // for a cell
};

enum class RegionKind { Title, Strip, CellStrip, Cell };

// A rectangle whose sides are borders: a title strip of the table, a row or a
// cell, or a cell (a swimlane cell is split in title strips and the body).
struct Region {
    RegionKind kind = RegionKind::Cell;
    Rect rect;
    const Part* row = nullptr;
    const Part* cell = nullptr;
    bool line = true; // false hides the divider between a title strip and the body
};

struct Regions {
    StartInsets start;
    Bands title;
    std::vector<Part> rows;
    std::vector<Part> cells;
    std::vector<Region> regions;
};

void initPart(Part& part, const ShapeChild& child, const Colors& parentColors, const ResolvedStyle& fallback)
{
    const ResolvedStyle& style = child.style != nullptr ? *child.style : fallback;
    part.takeover = isCollapsedTableChildShape(style);
    part.sides = sidesOf(style);
    part.line = style.swimlaneLine();
    part.colors = resolveColors(style, parentColors);
    part.border = makeBorder(part.colors.stroke, style);
    part.opacity = style.opacity();
    part.fillOpacity = style.fillOpacity();
    part.gradient = style.gradientColor();
    part.gradientDirection = style.gradientDirection();
}

// Graph.getCollapsedTableRegions: the rows and cells of the table with the
// regions between whose borders are drawn. A merged cell is one region with the
// size the layout gave it, and the cells it covers have none.
Regions collectRegions(const ResolvedStyle& tableStyle, const std::vector<ShapeChild>& rows, double w, double h)
{
    Regions data;
    data.start = actualStartInsets(&tableStyle);
    data.title = splitBands(0, 0, w, h, data.start);
    const Colors tableColors{tableStyle.fillColor(), tableStyle.swimlaneFillColor(), tableStyle.strokeColor()};
    const auto addRegion = [&](RegionKind kind, const Rect& rect, const Part* row, const Part* cell, bool line) {
        if (rect.width > 0 && rect.height > 0)
            data.regions.push_back({kind, rect, row, cell, line});
    };

    data.rows.reserve(rows.size());
    std::vector<const ShapeChild*> rowSource;
    for (const ShapeChild& row : rows) {
        if (!row.visible)
            continue;
        Part part;
        initPart(part, row, tableColors, tableStyle);
        part.rect = row.bounds;
        const StartInsets rowStart = actualStartInsets(row.style);
        part.mode = fillMode(row.style != nullptr ? *row.style : tableStyle, rowStart);
        part.split = splitBands(part.rect.x, part.rect.y, part.rect.width, part.rect.height, rowStart);
        data.rows.push_back(std::move(part));
        rowSource.push_back(&row);
    }

    // Cells: the ones that are shown and no span covers.
    const std::vector<TableGridCell> grid = visitTableCells(tableStyle, rows);
    data.cells.reserve(grid.size());
    for (std::size_t i = 0; i < grid.size(); ++i) {
        const TableGridCell& iter = grid[i];
        const auto row = std::ranges::find(rowSource, iter.row);
        if (row == rowSource.end() || iter.actual != i || !iter.cell->visible)
            continue;
        const Part& rowPart = data.rows[static_cast<std::size_t>(row - rowSource.begin())];
        Part part;
        initPart(part, *iter.cell, rowPart.colors, tableStyle);
        part.row = &rowPart;
        const StartInsets cellStart = actualStartInsets(iter.cell->style);
        part.rect = {rowPart.rect.x + iter.cell->bounds.x, rowPart.rect.y + iter.cell->bounds.y,
                     iter.cell->bounds.width, iter.cell->bounds.height};
        part.mode = fillMode(iter.cell->style != nullptr ? *iter.cell->style : tableStyle, cellStart);
        part.split = splitBands(part.rect.x, part.rect.y, part.rect.width, part.rect.height, cellStart);
        data.cells.push_back(std::move(part));
    }

    // The regions, now that the parts have their final place.
    const bool titleLine = tableStyle.swimlaneLine();
    for (const Rect& band : data.title.bands)
        addRegion(RegionKind::Title, band, nullptr, nullptr, titleLine);
    for (const Part& row : data.rows) {
        for (const Rect& band : row.split.bands)
            addRegion(RegionKind::Strip, band, &row, nullptr, row.line);
    }
    for (const Part& cell : data.cells) {
        if (cell.mode == FillMode::Lane) {
            for (const Rect& band : cell.split.bands)
                addRegion(RegionKind::CellStrip, band, cell.row, &cell, cell.line);
            addRegion(RegionKind::Cell, cell.split.body, cell.row, &cell, cell.line);
        } else {
            addRegion(RegionKind::Cell, cell.rect, cell.row, &cell, cell.line);
        }
    }
    return data;
}

// --- borders ----------------------------------------------------------------------------

enum class Side { Top, Bottom, Left, Right };

// One side of a region (or of the table, without a region) lying on a line of the grid.
struct Edge {
    double a = 0, b = 0;
    const Region* region = nullptr;
    bool before = false; // the bottom or right side
    Side side = Side::Top;
    double pos = 0;
};

// A piece of the outline of the table: the interval [a, b] of one side.
struct OutlinePiece {
    double a = 0, b = 0;
    const Border* border = nullptr; // null: hidden
};

struct Line {
    bool horizontal = true;
    double a = 0, b = 0, pos = 0;
    const Border* border = nullptr;
};

struct Outline {
    std::vector<OutlinePiece> top, right, bottom, left;
};

struct Borders {
    std::vector<Line> lines;
    Outline outline;
};

// Graph.getCollapsedTableBorders: every piece of the edge grid between two
// regions is painted once. A flag of 0 on a row or cell hides a side of it, and
// so does the row or cell on the other side of a piece; a visible piece takes the
// most specific stroke: a cell whose stroke differs from its row's beats a row
// whose stroke differs from the table's, which beats the table; between two
// cells or rows the thicker wins, then the top or left one.
class BorderSolver {
public:
    BorderSolver(const ResolvedStyle& style, double w, double h, const Border& tableBorder, const Regions& data)
        : w_(w), h_(h), tableBorder_(tableBorder), data_(data), rowLines_(style.string("rowLines", "1") != "0"),
          columnLines_(style.string("columnLines", "1") != "0")
    {
    }

    Borders solve()
    {
        std::map<double, std::vector<Edge>> hor;
        std::map<double, std::vector<Edge>> ver;
        for (const Region& region : data_.regions) {
            const Rect& r = region.rect;
            add(hor, r.y, r.x, r.x + r.width, &region, false, Side::Top);
            add(hor, r.y + r.height, r.x, r.x + r.width, &region, true, Side::Bottom);
            add(ver, r.x, r.y, r.y + r.height, &region, false, Side::Left);
            add(ver, r.x + r.width, r.y, r.y + r.height, &region, true, Side::Right);
        }
        add(hor, 0, 0, w_, nullptr, false, Side::Top);
        add(hor, h_, 0, w_, nullptr, true, Side::Bottom);
        add(ver, 0, 0, h_, nullptr, false, Side::Left);
        add(ver, w_, 0, h_, nullptr, true, Side::Right);
        process(hor, true);
        process(ver, false);
        return std::move(result_);
    }

private:
    void add(std::map<double, std::vector<Edge>>& map, double pos, double a, double b, const Region* region,
             bool before, Side side)
    {
        pos = round2(pos);
        map[pos].push_back({round2(std::min(a, b)), round2(std::max(a, b)), region, before, side, pos});
    }

    // Is this side of the owner the one the region side lies on, as opposed to a divider?
    static bool isOwnerSide(const Rect& owner, Side side, double pos)
    {
        const double value = side == Side::Top      ? owner.y
                             : side == Side::Bottom ? owner.y + owner.height
                             : side == Side::Left   ? owner.x
                                                    : owner.x + owner.width;
        return std::abs(round2(value) - pos) < kEps;
    }

    // The region side divides a title strip from the body of the table, row or cell.
    bool isDivider(const Edge& entry) const
    {
        const Region& region = *entry.region;
        const Rect owner = region.kind == RegionKind::Title ? Rect{0, 0, w_, h_}
                           : region.kind == RegionKind::Strip ? region.row->rect
                                                              : region.cell->rect;
        return !isOwnerSide(owner, entry.side, entry.pos);
    }

    static bool sideOn(const Sides& sides, Side side)
    {
        switch (side) {
        case Side::Top:
            return sides.top;
        case Side::Bottom:
            return sides.bottom;
        case Side::Left:
            return sides.left;
        case Side::Right:
            return sides.right;
        }
        return true;
    }

    // A 0 flag of its cell or row, or swimlaneLine=0 for a divider.
    bool isHidden(const Edge& entry) const
    {
        const Region& region = *entry.region;
        if (isDivider(entry))
            return !region.line;
        return (region.cell != nullptr && isOwnerSide(region.cell->rect, entry.side, entry.pos)
                && !sideOn(region.cell->sides, entry.side))
               || (region.row != nullptr && isOwnerSide(region.row->rect, entry.side, entry.pos)
                   && !sideOn(region.row->sides, entry.side));
    }

    struct Claim {
        int level = 0;
        const Border* border = nullptr;
    };

    // The most specific stroke of the region side.
    Claim claimOf(const Edge& entry) const
    {
        const Part* cell = entry.region->cell;
        const Part* row = entry.region->row;
        const std::string& rowKey = row != nullptr ? row->border.key : tableBorder_.key;
        if (cell != nullptr && cell->takeover && cell->border.key != rowKey)
            return {2, &cell->border};
        if (row != nullptr && row->takeover && rowKey != tableBorder_.key)
            return {1, &row->border};
        return {0, &tableBorder_};
    }

    // The border of a piece that the sides in `claims` lie on, or null when it is hidden.
    const Border* resolve(const std::vector<const Edge*>& claims, bool horizontal, bool outer) const
    {
        bool divider = false;
        struct Best {
            int level;
            const Border* border;
            bool before;
        };
        std::optional<Best> best;
        for (const Edge* entry : claims) {
            if (isHidden(*entry))
                return nullptr;
            divider = divider || isDivider(*entry);
            const Claim claim = claimOf(*entry);
            if (claim.level > 0
                && (!best || claim.level > best->level
                    || (claim.level == best->level
                        && (claim.border->width > best->border->width
                            || (claim.border->width == best->border->width && entry->before && !best->before)))))
                best = Best{claim.level, claim.border, entry->before};
        }
        if (best)
            return best->border->key.empty() ? nullptr : best->border;
        if (!outer && !divider && (horizontal ? !rowLines_ : !columnLines_))
            return nullptr;
        return tableBorder_.key.empty() ? nullptr : &tableBorder_;
    }

    void process(std::map<double, std::vector<Edge>>& map, bool horizontal)
    {
        for (auto& [pos, sides] : map) {
            std::stable_sort(sides.begin(), sides.end(), [](const Edge& a, const Edge& b) { return a.a < b.a; });
            std::vector<double> points;
            for (const Edge& side : sides) {
                points.push_back(side.a);
                points.push_back(side.b);
            }
            std::sort(points.begin(), points.end());

            std::vector<OutlinePiece>* outerSide = nullptr;
            if (horizontal) {
                outerSide = std::abs(pos) < kEps ? &result_.outline.top
                            : std::abs(pos - round2(h_)) < kEps ? &result_.outline.bottom
                                                                 : nullptr;
            } else {
                outerSide = std::abs(pos) < kEps ? &result_.outline.left
                            : std::abs(pos - round2(w_)) < kEps ? &result_.outline.right
                                                                 : nullptr;
            }

            std::vector<const Edge*> active;
            std::size_t next = 0;
            std::optional<std::size_t> current; // index in result_.lines of the line being extended
            for (std::size_t i = 0; i + 1 < points.size(); ++i) {
                const double p = points[i];
                const double q = points[i + 1];
                if (q - p <= kEps)
                    continue;
                while (next < sides.size() && sides[next].a <= p + kEps)
                    active.push_back(&sides[next++]);

                std::vector<const Edge*> claims;
                bool outer = false;
                std::vector<const Edge*> still;
                for (const Edge* edge : active) {
                    if (edge->b > p + kEps) {
                        still.push_back(edge);
                        if (edge->b >= q - kEps) {
                            if (edge->region == nullptr)
                                outer = true;
                            else
                                claims.push_back(edge);
                        }
                    }
                }
                active = std::move(still);

                if (outer && outerSide != nullptr) {
                    const Border* border = resolve(claims, horizontal, true);
                    if (!outerSide->empty() && std::abs(outerSide->back().b - p) < kEps
                        && keyOf(outerSide->back().border) == keyOf(border))
                        outerSide->back().b = q;
                    else
                        outerSide->push_back({p, q, border});
                    current.reset();
                } else if (!claims.empty()) {
                    const Border* border = resolve(claims, horizontal, false);
                    if (border == nullptr || border->key.empty()) {
                        current.reset();
                    } else if (current && keyOf(result_.lines[*current].border) == border->key
                               && std::abs(result_.lines[*current].b - p) < kEps) {
                        result_.lines[*current].b = q;
                    } else {
                        result_.lines.push_back({horizontal, p, q, pos, border});
                        current = result_.lines.size() - 1;
                    }
                } else {
                    current.reset();
                }
            }
        }
    }

    double w_, h_;
    const Border& tableBorder_;
    const Regions& data_;
    bool rowLines_, columnLines_;
    Borders result_;
};

// --- painting -------------------------------------------------------------------------------

// Graph.getCollapsedArcSize: the corner radius of the table outline.
double arcSize(const ResolvedStyle& style, double w, double h, const StartInsets& start)
{
    double r = 0;
    if (style.rounded()) {
        const double size = std::max({start.left, start.top, start.right, start.bottom});
        if (size == 0) {
            r = rectangleArcSize(style, w, h);
        } else {
            r = swimlaneArcSize(style, w, h, size);
            r = std::min(((start.top > 0 || start.bottom > 0) ? h : w) - size, std::min(size, r));
        }
    }
    return std::max(0.0, std::min({r, w / 2, h / 2}));
}

// TableShape.addCollapsedRegionPath: the path of `rect` in the table at (x, y)
// whose corners that are corners of the table follow the arc of its outline (the
// canvas does not clip, so the fill of a corner cell has the shape of the outline).
void addRegionPath(Canvas& c, double x, double y, double w, double h, double r, const Rect& rect)
{
    double x0 = rect.x;
    double y0 = rect.y;
    double x1 = rect.x + rect.width;
    double y1 = rect.y + rect.height;
    const bool top = std::abs(y0) < kEps;
    const bool left = std::abs(x0) < kEps;
    const bool bottom = std::abs(y1 - h) < kEps;
    const bool right = std::abs(x1 - w) < kEps;
    const bool tl = r > 0 && top && left;
    const bool tr = r > 0 && top && right;
    const bool br = r > 0 && bottom && right;
    const bool bl = r > 0 && bottom && left;
    const double rr = std::min(r, std::min(rect.width / (((tl && tr) || (bl && br)) ? 2 : 1),
                                           rect.height / (((tl && bl) || (tr && br)) ? 2 : 1)));
    x0 += x;
    x1 += x;
    y0 += y;
    y1 += y;

    c.begin();
    if (tl) {
        c.moveTo(x0, y0 + rr);
        c.arcTo(rr, rr, 0, false, true, x0 + rr, y0);
    } else {
        c.moveTo(x0, y0);
    }
    if (tr) {
        c.lineTo(x1 - rr, y0);
        c.arcTo(rr, rr, 0, false, true, x1, y0 + rr);
    } else {
        c.lineTo(x1, y0);
    }
    if (br) {
        c.lineTo(x1, y1 - rr);
        c.arcTo(rr, rr, 0, false, true, x1 - rr, y1);
    } else {
        c.lineTo(x1, y1);
    }
    if (bl) {
        c.lineTo(x0 + rr, y1);
        c.arcTo(rr, rr, 0, false, true, x0, y1 - rr);
    } else {
        c.lineTo(x0, y1);
    }
    c.close();
}

struct Frame {
    Canvas& c;
    double x, y, w, h, r;
};

// TableShape.fillCollapsedRegion
void fillRegion(const Frame& f, const Rect& rect, const std::optional<QColor>& fill, const Part& part,
                const Rect* gradientBounds)
{
    if (!fill || rect.width <= 0 || rect.height <= 0)
        return;
    f.c.save();
    f.c.setAlpha(part.opacity / 100);
    f.c.setFillAlpha(part.fillOpacity / 100);
    if (part.gradient && gradientBounds != nullptr) {
        f.c.setGradient(*fill, *part.gradient,
                        {f.x + gradientBounds->x, f.y + gradientBounds->y, gradientBounds->width, gradientBounds->height},
                        parseGradientDirection(part.gradientDirection));
    } else {
        f.c.setFillColor(fill);
    }
    addRegionPath(f.c, f.x, f.y, f.w, f.h, f.r, rect);
    f.c.fill();
    f.c.restore();
}

// TableShape.fillCollapsedChild: the fill of a row or cell.
void fillPart(const Frame& f, const Part& part)
{
    if (!part.takeover)
        return;
    if (part.mode == FillMode::Full) {
        fillRegion(f, part.rect, part.colors.fill, part, &part.rect);
    } else if (part.mode == FillMode::Lane) {
        for (const Rect& band : part.split.bands)
            fillRegion(f, band, part.colors.fill, part, &band);
        fillRegion(f, part.split.body, part.colors.laneFill, part, nullptr);
    }
}

// TableShape.setCollapsedBorder
void setBorder(Canvas& c, const Border& border)
{
    c.setAlpha(border.opacity / 100);
    c.setStrokeAlpha(border.strokeOpacity / 100);
    c.setStrokeColor(border.color);
    c.setStrokeWidth(border.width);
    c.setDashed(border.dashed, border.fixDash);
    if (border.dashed)
        c.setDashPattern(border.dashPattern.empty() ? std::string_view("3 3") : std::string_view(border.dashPattern));
}

// One move of the outline: a straight piece or a corner arc.
struct OutlineOp {
    bool arc = false;
    Point from, to;
    const Border* border = nullptr;
    std::string key;
};

// TableShape.paintCollapsedOutline: the outline of the table, consecutive
// pieces with the same border painted as one path, corner arcs included.
void paintOutline(Canvas& c, double x, double y, double w, double h, double r, const Outline& outline)
{
    struct SideInfo {
        const std::vector<OutlinePiece>* pieces;
        double x0, y0, dx, dy, len;
        bool rev;
        const Border* first = nullptr;
        const Border* last = nullptr;
    };
    SideInfo sides[] = {{&outline.top, 0, 0, 1, 0, w, false},
                        {&outline.right, w, 0, 0, 1, h, false},
                        {&outline.bottom, w, h, -1, 0, w, true},
                        {&outline.left, 0, h, 0, -1, h, true}};
    const auto pointAt = [](const SideInfo& side, double t) {
        return Point{side.x0 + side.dx * t, side.y0 + side.dy * t};
    };

    std::vector<OutlineOp> ops;
    std::vector<std::size_t> corners; // indices in ops
    std::vector<int> cornerSide;
    for (int i = 0; i < 4; ++i) {
        SideInfo& side = sides[i];
        std::vector<OutlinePiece> pieces = *side.pieces;
        if (side.rev)
            std::reverse(pieces.begin(), pieces.end());
        side.first = pieces.empty() ? nullptr : pieces.front().border;
        side.last = pieces.empty() ? nullptr : pieces.back().border;
        for (const OutlinePiece& piece : pieces) {
            const double t0 = std::max(r, side.rev ? side.len - piece.b : piece.a);
            const double t1 = std::min(side.len - r, side.rev ? side.len - piece.a : piece.b);
            if (t1 - t0 > kEps)
                ops.push_back({false, pointAt(side, t0), pointAt(side, t1), piece.border, {}});
        }
        if (r > 0) {
            corners.push_back(ops.size());
            cornerSide.push_back(i);
            ops.push_back({true, pointAt(side, side.len - r), {}, nullptr, {}});
        }
    }

    // Corner arcs use the border of the incoming or else the outgoing side.
    for (std::size_t k = 0; k < corners.size(); ++k) {
        const SideInfo& side = sides[cornerSide[k]];
        const SideInfo& next = sides[(cornerSide[k] + 1) % 4];
        const Border* border = side.last;
        if (keyOf(border).empty())
            border = next.first;
        OutlineOp& op = ops[corners[k]];
        op.to = pointAt(next, r);
        op.border = border;
    }
    for (OutlineOp& op : ops)
        op.key = keyOf(op.border);

    const auto joined = [](const OutlineOp& a, const OutlineOp& b) {
        return a.key == b.key && std::abs(a.to.x - b.from.x) < kEps && std::abs(a.to.y - b.from.y) < kEps;
    };
    const auto paintRun = [&](const std::vector<const OutlineOp*>& run, bool close) {
        c.save();
        setBorder(c, *run.front()->border);
        c.begin();
        c.moveTo(x + run.front()->from.x, y + run.front()->from.y);
        for (const OutlineOp* op : run) {
            if (op->arc)
                c.arcTo(r, r, 0, false, true, x + op->to.x, y + op->to.y);
            else
                c.lineTo(x + op->to.x, y + op->to.y);
        }
        if (close)
            c.close();
        c.stroke();
        c.restore();
    };

    // Starts at the first break in the outline.
    const std::size_t n = ops.size();
    std::optional<std::size_t> start;
    for (std::size_t i = 0; i < n && !start; ++i) {
        if (!joined(ops[(i + n - 1) % n], ops[i]))
            start = i;
    }
    if (!start) {
        if (n > 0 && !ops[0].key.empty()) {
            std::vector<const OutlineOp*> all;
            for (const OutlineOp& op : ops)
                all.push_back(&op);
            paintRun(all, true);
        }
        return;
    }
    std::vector<const OutlineOp*> run;
    for (std::size_t i = 0; i < n; ++i) {
        const OutlineOp& op = ops[(*start + i) % n];
        if (!run.empty() && !joined(*run.back(), op)) {
            paintRun(run, false);
            run.clear();
        }
        if (!op.key.empty())
            run.push_back(&op);
    }
    if (!run.empty())
        paintRun(run, false);
}

} // namespace

void paintCollapsedTable(Canvas& c, const ResolvedStyle& style, const std::vector<ShapeChild>& rows, double x, double y,
                         double w, double h)
{
    const Border tableBorder = makeBorder(style.strokeColor(), style);
    const Regions data = collectRegions(style, rows, w, h);
    const Borders borders = BorderSolver(style, w, h, tableBorder, data).solve();
    const Frame frame{c, x, y, w, h, arcSize(style, w, h, data.start)};
    const Rect full{0, 0, w, h};

    // The table's own fill, then the rows and cells over it. The shadow of the
    // table belongs to its background and outline only. Upstream draws these in
    // a group with a drop shadow; here the outline casts its shadow first so
    // that the fills cover the part of it that lies inside the table.
    if (style.shadow() && !tableBorder.key.empty()) {
        addRegionPath(c, x, y, w, h, frame.r, full);
        c.stroke();
    }
    if (fillMode(style, data.start) == FillMode::Full) {
        addRegionPath(c, x, y, w, h, frame.r, full);
        c.fill();
    } else {
        for (const Rect& band : data.title.bands) {
            addRegionPath(c, x, y, w, h, frame.r, band);
            c.fill();
        }
        if (const auto laneFill = style.swimlaneFillColor()) {
            c.setFillColor(laneFill);
            addRegionPath(c, x, y, w, h, frame.r, data.title.body);
            c.fill();
        }
    }
    c.setShadow(false);

    for (const Part& row : data.rows)
        fillPart(frame, row);
    for (const Part& cell : data.cells)
        fillPart(frame, cell);

    // The inner borders, grouped by stroke with the thicker ones on top.
    std::vector<std::string> keys;
    std::map<std::string, std::vector<const Line*>> groups;
    for (const Line& line : borders.lines) {
        auto& group = groups[line.border->key];
        if (group.empty())
            keys.push_back(line.border->key);
        group.push_back(&line);
    }
    std::stable_sort(keys.begin(), keys.end(), [&](const std::string& a, const std::string& b) {
        return groups[a].front()->border->width < groups[b].front()->border->width;
    });
    c.setLineCap(LineCap::Square);
    for (const std::string& key : keys) {
        const auto& lines = groups[key];
        c.save();
        setBorder(c, *lines.front()->border);
        c.begin();
        for (const Line* line : lines) {
            if (line->horizontal) {
                c.moveTo(x + line->a, y + line->pos);
                c.lineTo(x + line->b, y + line->pos);
            } else {
                c.moveTo(x + line->pos, y + line->a);
                c.lineTo(x + line->pos, y + line->b);
            }
        }
        c.stroke();
        c.restore();
    }
    c.setLineCap(LineCap::Flat);
    paintOutline(c, x, y, w, h, frame.r, borders.outline);
}

} // namespace qraw::render::detail
