// Port of mxgraph/src/shape/mxConnector.js (draw.io, Apache-2.0), modified.
// Port of mxgraph/src/shape/mxPolyline.js (draw.io, Apache-2.0), modified.
//
// Paints CellStates onto a Canvas. Vertices go through the shape pipeline
// (paintVertex); edges are drawn here as mxConnector does: the line, shortened
// by the markers, then the markers.

#include "qraw/render/scene_renderer.h"

#include "shapes_table.h"

#include "qraw/core/number.h"
#include "qraw/geometry/polyline.h"
#include "qraw/render/markers.h"
#include "qraw/render/perf_scope.h"
#include "qraw/render/shape.h"
#include "qraw/render/sketch_canvas.h"
#include "qraw/render/stats.h"
#include "qraw/render/stencil.h"

#include <cmath>
#include <functional>

namespace qraw::render {

namespace {

// The view being painted, set by paintPage so that container shapes (tables)
// can list the children of the cell they paint. paintShape() takes the view
// explicitly; without one (and outside paintPage) containers see no children.
thread_local const PageView* currentView = nullptr;

class ViewScope {
public:
    explicit ViewScope(const PageView* view) : previous_(currentView) { currentView = view; }
    ~ViewScope() { currentView = previous_; }
    ViewScope(const ViewScope&) = delete;
    ViewScope& operator=(const ViewScope&) = delete;

private:
    const PageView* previous_;
};

// ShapeChild tree of the vertex children of states[index]: the cells that are
// shown, and the ones a table row keeps that are not (they are no state).
std::vector<ShapeChild> childrenOf(const PageView& view, std::size_t index)
{
    const auto& states = view.states();
    const CellState& parent = states[index];
    std::vector<ShapeChild> result;
    const auto hidden = parent.hiddenChildren.begin();
    auto nextHidden = hidden;
    const auto addHidden = [&] {
        ShapeChild entry;
        entry.id = nextHidden->id;
        entry.bounds = nextHidden->bounds;
        entry.style = &nextHidden->style;
        entry.visible = false;
        entry.alternateBounds = nextHidden->alternateBounds;
        result.push_back(std::move(entry));
        ++nextHidden;
    };
    // Descendants follow their ancestor directly in paint order.
    for (std::size_t j = index + 1; j < states.size() && states[j].depth > parent.depth; ++j) {
        const CellState& child = states[j];
        if (child.parent != index || child.kind != CellState::Kind::Vertex)
            continue;
        while (nextHidden != parent.hiddenChildren.end() && nextHidden->position <= result.size())
            addHidden();
        ShapeChild entry;
        entry.id = child.id;
        entry.bounds = {child.bounds.x - parent.bounds.x, child.bounds.y - parent.bounds.y, child.bounds.width,
                        child.bounds.height};
        entry.style = &child.style;
        entry.alternateBounds = child.alternateBounds;
        entry.children = childrenOf(view, j);
        result.push_back(std::move(entry));
    }
    while (nextHidden != parent.hiddenChildren.end())
        addHidden();
    return result;
}

// Graph.getCollapsedTableForCell, the cell being a row or a cell of a table
// with tableRender=collapsed: the table paints its fill and borders.
bool paintedByCollapsedTable(const PageView& view, std::size_t index)
{
    const auto& states = view.states();
    const CellState& state = states[index];
    if (state.kind != CellState::Kind::Vertex || state.parent == CellState::npos
        || !detail::isCollapsedTableChildShape(state.style))
        return false;
    const CellState& parent = states[state.parent];
    if (parent.kind != CellState::Kind::Vertex)
        return false;
    if (detail::isCollapsedTable(parent.style))
        return true;
    return parent.parent != CellState::npos && detail::isCollapsedTable(states[parent.parent].style);
}

// --- edges (mxConnector.paintEdgeShape) ---------------------------------------

// A marker filled unless the style value is numerically 0 (upstream compares
// `style[startFill] != 0`).
bool isFilled(const ResolvedStyle& style, std::string_view key)
{
    const std::string* value = style.map().find(key);
    if (!value)
        return true;
    const auto number = core::parseNumber(*value);
    return !number || *number != 0;
}

// mxConnector.createMarker: prepares the marker at one end and moves that end
// of `points` back so the line stops at the marker.
MarkerPainter prepareMarker(Canvas& canvas, const ResolvedStyle& style, std::vector<Point>& points, bool source)
{
    const std::string_view type = source ? style.startArrow() : style.endArrow();
    if (type.empty() || type == "none" || points.size() < 2)
        return {};
    const Point previous = source ? points[1] : points[points.size() - 2];
    Point& end = source ? points[0] : points.back();
    const double dx = end.x - previous.x;
    const double dy = end.y - previous.y;
    const double distance = std::sqrt(dx * dx + dy * dy);
    if (!(distance > 0))
        return {}; // zero-length end segment: no direction, upstream would produce NaN
    const double size = source ? style.startMarkerSize() : style.endMarkerSize();
    const bool filled = isFilled(style, source ? "startFill" : "endFill");
    return createMarker(canvas, style, type, end, dx / distance, dy / distance, size, source, style.strokeWidth(),
                        filled);
}

// mxConnector.paintLine with line jumps (Graph.js, mxConnector.prototype.paintLine):
// the polyline through the routed points, cut at each jump by `jumpSize`
// on both sides and bridged in the jump style. `ends` are the marker-shortened
// first and last points of the line.
geometry::Path jumpedPath(const ResolvedStyle& style, const std::vector<geometry::RoutedPoint>& routed,
                          const std::vector<Point>& ends)
{
    const bool rounded = style.rounded();
    const double arcSize = style.arcSize() / 2;
    const double size = (style.jumpSize() - 2) / 2 + style.strokeWidth();
    const std::string_view jumpStyle = style.jumpStyle();

    geometry::Path path;
    bool moveTo = true;
    std::optional<Point> last;
    std::optional<Point> n;
    double len = 0;
    std::vector<Point> pts;
    const auto flush = [&] {
        path.append(geometry::polylinePath(pts, rounded, arcSize, false, {}, moveTo));
    };

    for (std::size_t i = 0; i < routed.size(); ++i) {
        Point pt = routed[i].point;
        if (i == 0)
            pt = ends.front();
        else if (i + 1 == routed.size())
            pt = ends.back();

        bool done = false;
        if (last && routed[i].jump && i + 1 < routed.size()) {
            // Skips the jump when the next or the previous point is too close.
            const Point next = routed[i + 1].point;
            const double distNext = (next.x - pt.x) * (next.x - pt.x) + (next.y - pt.y) * (next.y - pt.y);
            if (!n) {
                n = Point{pt.x - last->x, pt.y - last->y};
                len = std::sqrt(n->x * n->x + n->y * n->y);
                if (len > 0) {
                    n->x = n->x * size / len;
                    n->y = n->y * size / len;
                } else {
                    n.reset();
                }
            }
            const double distLast = (last->x - pt.x) * (last->x - pt.x) + (last->y - pt.y) * (last->y - pt.y);
            if (distNext > size * size && len > 0 && distLast > size * size) {
                const Point p0{pt.x - n->x, pt.y - n->y};
                const Point p1{pt.x + n->x, pt.y + n->y};
                pts.push_back(p0);
                flush();
                // Which side the jump bulges to (Math.round in upstream).
                double f = (std::round(n->x) < 0 || (std::round(n->x) == 0 && std::round(n->y) <= 0)) ? 1 : -1;
                moveTo = false;
                if (jumpStyle == "sharp") {
                    path.lineTo({p0.x - n->y * f, p0.y + n->x * f});
                    path.lineTo({p1.x - n->y * f, p1.y + n->x * f});
                    path.lineTo(p1);
                } else if (jumpStyle == "line") {
                    path.moveTo({p0.x + n->y * f, p0.y - n->x * f});
                    path.lineTo({p0.x - n->y * f, p0.y + n->x * f});
                    path.moveTo({p1.x - n->y * f, p1.y + n->x * f});
                    path.lineTo({p1.x + n->y * f, p1.y - n->x * f});
                    path.moveTo(p1);
                } else if (jumpStyle == "arc") {
                    f *= 1.3;
                    path.cubicTo({p0.x - n->y * f, p0.y + n->x * f}, {p1.x - n->y * f, p1.y + n->x * f}, p1);
                } else { // gap
                    path.moveTo(p1);
                    moveTo = true;
                }
                pts = {p1};
                done = true;
            }
        } else {
            n.reset();
        }
        if (!done) {
            pts.push_back(pt);
            last = pt;
        }
    }
    flush();
    return path;
}

// mxPolyline.paintEdgeShape: curved, or straight/rounded through addPoints;
// with the jumps of a straight or rounded line.
void paintLine(Canvas& canvas, const CellState& state, const std::vector<Point>& points)
{
    const ResolvedStyle& style = state.style;
    canvas.begin();
    if (style.curved()) {
        canvas.addPath(geometry::curvedPath(points));
    } else if (!state.routedPoints.empty()) {
        canvas.addPath(jumpedPath(style, state.routedPoints, {points.front(), points.back()}));
    } else {
        // mxPolyline.paintLine passes arcSize / 2 to addPoints.
        canvas.addPath(geometry::polylinePath(points, style.rounded(), style.arcSize() / 2));
    }
    canvas.stroke();
}

void paintEdge(Canvas& canvas, const CellState& state)
{
    if (state.points.size() < 2)
        return;
    const ResolvedStyle& style = state.style;
    canvas.save();
    configureCanvas(canvas, style, state.bounds);

    std::vector<Point> points = state.points;
    // Both markers are prepared before the line is painted (this shortens the
    // ends of `points`) and painted after it.
    const MarkerPainter startMarker = prepareMarker(canvas, style, points, true);
    const MarkerPainter endMarker = prepareMarker(canvas, style, points, false);
    paintLine(canvas, state, points);

    // Markers: no shadow, no dashes, filled with the stroke color unless
    // startFillColor/endFillColor say otherwise.
    canvas.setShadow(false);
    canvas.setDashed(false);
    const std::optional<QColor> stroke = style.strokeColor();
    const auto markerFill = [&](std::string_view key) {
        return style.has(key) ? style.color(key) : stroke;
    };
    const std::optional<QColor> startStroke = style.color("startStrokeColor");
    if (startMarker) {
        canvas.setFillColor(markerFill("startFillColor"));
        if (startStroke)
            canvas.setStrokeColor(startStroke);
        startMarker();
    }
    if (endMarker) {
        canvas.setFillColor(markerFill("endFillColor"));
        if (const auto endStroke = style.color("endStrokeColor"))
            canvas.setStrokeColor(endStroke);
        else if (startStroke)
            canvas.setStrokeColor(stroke);
        endMarker();
    }
    canvas.restore();
}

} // namespace

SceneRenderer::SceneRenderer(RenderContext context) : context_(std::move(context))
{
    if (!context_.stylesheet)
        context_.stylesheet = &Stylesheet::drawioDefault();
    if (!context_.shapes)
        context_.shapes = &ShapeRegistry::builtin();
    if (!context_.stencils)
        context_.stencils = &StencilRegistry::builtin();
}

void SceneRenderer::paintShape(Canvas& target, const CellState& state, const PageView* view, bool valueHidden) const
{
    QRAW_PERF_SCOPE(ShapePaint);
    stats::add(stats::Counter::cellsPainted);
    // sketch=1 / comic=1: everything the shape or edge paints goes through
    // the hand-drawn decorator; labels (paintLabel) never do.
    const std::optional<SketchSettings> sketch = SketchSettings::fromStyle(state.style);
    std::optional<SketchCanvas> sketched;
    if (sketch) {
        target.save();
        sketched.emplace(target, *sketch, SketchCanvas::seedFromId(state.id));
    }
    Canvas& canvas = sketched ? static_cast<Canvas&>(*sketched) : target;
    paintShapeOn(canvas, state, view, valueHidden);
    if (sketch)
        target.restore();
}

void SceneRenderer::paintShapeOn(Canvas& canvas, const CellState& state, const PageView* view,
                                 bool valueHidden) const
{
    if (state.kind == CellState::Kind::Edge) {
        paintEdge(canvas, state);
        return;
    }
    if (!state.shape)
        return;
    std::function<std::vector<ShapeChild>()> children;
    const PageView* owner = view ? view : currentView;
    if (owner) {
        const auto& states = owner->states();
        if (!states.empty() && &state >= states.data() && &state < states.data() + states.size()) {
            const std::size_t index = static_cast<std::size_t>(&state - states.data());
            // The fill and borders of the rows and cells are the table's to paint.
            if (paintedByCollapsedTable(*owner, index))
                return;
            children = [owner, index] { return childrenOf(*owner, index); };
        }
    }
    const ShapePaintContext shapeContext{state.style, &context_.images, std::move(children), state.collapsed,
                                         context_.stencils, state.valueText, valueHidden};
    paintVertex(canvas, *state.shape, shapeContext, state.bounds);
}

void SceneRenderer::paintLabel(Canvas& canvas, const CellState& state) const
{
    if (!state.label || state.labelText.empty())
        return;
    QRAW_PERF_SCOPE(LabelPaint);
    stats::add(stats::Counter::labelsPainted);
    const ResolvedStyle& style = state.style;
    const geometry::LabelBox& box = *state.label;

    TextBlock block;
    block.x = box.x;
    block.y = box.y;
    block.width = box.width;
    block.height = box.height;
    block.align = box.align;
    block.verticalAlign = box.verticalAlign;
    block.rotation = box.rotation;
    block.text = state.labelText;
    block.wrap = style.wrap();
    block.overflow = style.overflow();
    block.clip = block.overflow == geometry::Overflow::Hidden;
    block.fontFamily = QString::fromUtf8(style.fontFamily().data(), static_cast<qsizetype>(style.fontFamily().size()));
    block.fontSize = style.fontSize();
    block.fontStyle = style.fontStyle();
    block.fontColor = style.fontColor().value_or(QColor(0, 0, 0));
    block.backgroundColor = style.labelBackgroundColor();
    block.borderColor = style.labelBorderColor();
    block.padding = style.labelPadding();
    // Not textOpacity/100 * opacity/100:
    // upstream applies only textOpacity to labels (mxText.js:497,
    // mxCellRenderer.js:599); the shape's opacity is not inherited by its text.
    block.opacity = style.textOpacity() / 100;
    canvas.text(block);
}

void SceneRenderer::paintCell(Canvas& canvas, const CellState& state, const PageView* view) const
{
    paintShape(canvas, state, view);
    paintLabel(canvas, state);
}

void SceneRenderer::paintPage(Canvas& canvas, const PageView& view, const PaintOptions& options) const
{
    const ViewScope scope(&view);
    const auto& states = view.states();

    std::vector<std::size_t> order;
    if (options.cellIds.empty()) {
        order.resize(states.size());
        for (std::size_t i = 0; i < order.size(); ++i)
            order[i] = i;
    } else {
        order = view.selection(options.cellIds);
    }

    // Open groups, innermost last. A cell's group nests in its parent's group;
    // when the parent is not painted (selection export) it starts at the top.
    std::vector<std::size_t> open;
    const auto closeTo = [&](std::size_t parent) {
        while (!open.empty() && open.back() != parent) {
            canvas.endGroup();
            open.pop_back();
        }
    };

    for (const std::size_t index : order) {
        const CellState& state = states[index];
        if (options.groups) {
            closeTo(state.parent);
            canvas.beginGroup(state.id);
            open.push_back(index);
        }
        try {
            if (options.labels)
                paintCell(canvas, state);
            else
                paintShape(canvas, state);
        } catch (...) {
            // Painting never fails: a broken cell must not abort the page.
        }
    }
    if (options.groups)
        closeTo(CellState::npos); // no state has this parent: closes everything
}

} // namespace qraw::render
