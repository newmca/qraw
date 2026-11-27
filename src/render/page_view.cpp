// Port of mxgraph/src/view/mxGraphView.js (draw.io, Apache-2.0), modified.
// Port of js/grapheditor/Graph.js (replacePlaceholders) (draw.io, Apache-2.0), modified.
//
// PageView::build is validate()/updateCellState()/updateVertexState()/
// updateEdgeState() of mxGraphView at scale 1 and translate 0. The work is
// done in four steps:
//   1. collect(): walk the cell tree in z-order and create one skeleton state
//      per visible cell (style, shape), without geometry.
//   2. resolve(): compute geometry on demand. A vertex needs its parent, an
//      edge needs its parent and its terminals (which may be edges, or
//      children of edges), so the work is a memoized recursion with a cycle
//      guard instead of a fixed order.
//   3. finish(): label box and paint bounds, once the geometry is known.
//   4. compact(): drop the states that turned out to be unusable.

#include "qraw/render/page_view.h"

#include "qraw/render/label_colors.h"

#include "qraw/core/number.h"
#include "qraw/geometry/path.h"
#include "qraw/geometry/polyline.h"
#include "qraw/geometry/routing.h"
#include "qraw/render/perf_scope.h"
#include "qraw/render/shape.h"
#include "qraw/render/sketch_canvas.h"
#include "qraw/render/stats.h"
#include "qraw/render/stencil.h"

#include <QDateTime>
#include <QLocale>

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_set>

namespace qraw::render {

namespace {

using Severity = core::Diagnostic::Severity;
constexpr std::size_t npos = CellState::npos;

// Longest dependency chain followed while resolving terminals; deeper chains
// are treated like cycles (the end floats).
constexpr int kMaxResolveDepth = 256;
// Longest ancestor walk (guards against parent cycles in broken files).
constexpr int kMaxAncestors = 1024;

// --- small helpers -----------------------------------------------------------

bool isFinite(const Rect& r)
{
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height);
}

Rect grown(const Rect& r, double amount)
{
    return {r.x - amount, r.y - amount, r.width + 2 * amount, r.height + 2 * amount};
}

Point center(const Rect& r) { return {r.x + r.width / 2, r.y + r.height / 2}; }

Point plus(Point a, Point b) { return {a.x + b.x, a.y + b.y}; }

bool hasMarker(std::string_view arrow) { return !arrow.empty() && arrow != "none"; }

// The lines of a label as one line of text (shapes that paint the value along a path).
std::string joinLines(const std::vector<std::string>& lines)
{
    std::string joined;
    for (const std::string& line : lines) {
        if (!joined.empty())
            joined += ' ';
        joined += line;
    }
    return joined;
}

std::size_t codePoints(const std::string& s)
{
    return static_cast<std::size_t>(
        std::count_if(s.begin(), s.end(), [](char c) { return (static_cast<unsigned char>(c) & 0xC0) != 0x80; }));
}

std::string shortened(std::string_view text)
{
    constexpr std::size_t kMax = 48;
    return text.size() <= kMax ? std::string(text) : std::string(text.substr(0, kMax)) + "...";
}

const core::Cell* parentCell(const core::Page& page, const core::Cell& cell)
{
    if (cell.parent().empty() || cell.parent() == cell.id())
        return nullptr;
    return page.findCell(cell.parent());
}

// --- placeholders (Graph.replacePlaceholders) ----------------------------------

// Graph.placeholderPattern: '%' name '%' with the name made of characters
// other than % { } " ' = ;
bool isPlaceholderNameChar(char c)
{
    switch (c) {
    case '%': case '{': case '}': case '"': case '\'': case '=': case ';':
        return false;
    default:
        return true;
    }
}

// The attribute `name` on the cell's wrapper or the nearest ancestor that has
// it (Graph.getPlaceholderCell).
const std::string* findDataAttribute(const core::Page& page, const core::Cell& cell, std::string_view name)
{
    const core::Cell* current = &cell;
    for (int steps = 0; current && steps < kMaxAncestors; ++steps) {
        if (const core::CellWrapper* wrapper = current->wrapper()) {
            if (const std::string* value = wrapper->attributes.find(name))
                return value;
        }
        current = parentCell(page, *current);
    }
    return nullptr;
}

// Global variables (Graph.getGlobalVariable): the page variables and the
// date/time, with the short system formats. Not supported: date{fmt}, unit
// suffixes of %width%/%height% and %length%; they stay literal like every
// unknown name. nullopt when the name is not a global variable or its value
// is unset in `vars`.
std::optional<std::string> globalVariable(const PlaceholderVariables& vars, std::string_view name)
{
    if (name == "page" && !vars.page.empty())
        return vars.page;
    if (name == "filename" && !vars.fileName.empty())
        return vars.fileName;
    if (name == "pagenumber" && vars.pageNumber > 0)
        return std::to_string(vars.pageNumber);
    if (name == "pagecount" && vars.pageCount > 0)
        return std::to_string(vars.pageCount);
    if (name != "date" && name != "time" && name != "timestamp")
        return std::nullopt;
    const QDateTime now = vars.now.value_or(QDateTime::currentDateTime());
    const QLocale locale = QLocale::system();
    const QString text = name == "date"   ? locale.toString(now.date(), QLocale::ShortFormat)
                         : name == "time" ? locale.toString(now.time(), QLocale::ShortFormat)
                                          : locale.toString(now, QLocale::ShortFormat);
    return text.toStdString();
}

// Differences from upstream: see globalVariable. %label% and %tooltip% are
// left in place for the label to handle.
std::string replacePlaceholders(const core::Page& page, const core::Cell& cell, const std::string& text,
                                const PlaceholderVariables& vars)
{
    std::string result;
    std::size_t last = 0;
    std::size_t searchFrom = 0;
    while (searchFrom < text.size()) {
        const std::size_t start = text.find('%', searchFrom);
        if (start == std::string::npos)
            break;
        std::size_t end = start + 1;
        while (end < text.size() && isPlaceholderNameChar(text[end]))
            ++end;
        if (end == start + 1 || end >= text.size() || text[end] != '%') {
            searchFrom = start + 1; // no match here, the regex moves on by one character
            continue;
        }
        const std::string_view token(text.data() + start, end + 1 - start);
        searchFrom = end + 1;
        if (token == "%label%" || token == "%tooltip%")
            continue;

        std::optional<std::string> replacement;
        if (start > last && text[start - 1] == '%') {
            replacement = std::string(token.substr(1)); // "%%name%" keeps a literal percent
        } else {
            const std::string_view name = token.substr(1, token.size() - 2);
            const core::Geometry* geometry = cell.geometry();
            if (name == "id") {
                replacement = cell.id();
            } else if (name == "width" && cell.isVertex() && geometry) {
                replacement = core::formatNumber(geometry->width());
            } else if (name == "height" && cell.isVertex() && geometry) {
                replacement = core::formatNumber(geometry->height());
            } else if (name.find('{') == std::string_view::npos) {
                if (const std::string* value = findDataAttribute(page, cell, name))
                    replacement = *value;
                else
                    replacement = globalVariable(vars, name);
            }
        }
        result.append(text, last, start - last);
        if (replacement)
            result += *replacement;
        else
            result += token;
        last = end + 1;
    }
    result.append(text, last, std::string::npos);
    return result;
}

// Graph.isReplacePlaceholders: the cell's own value object has placeholders="1"
// (a style flag does not count), and it has no `placeholder` attribute.
bool usesPlaceholders(const core::Cell& cell)
{
    const core::CellWrapper* wrapper = cell.wrapper();
    if (wrapper == nullptr || wrapper->attributes.has("placeholder"))
        return false;
    const std::string* flag = wrapper->attributes.find("placeholders");
    return flag != nullptr && *flag == "1";
}

// --- edge routing input ------------------------------------------------------

// The terminal of one end of `edge`, from the state of the cell it attaches to.
geometry::Terminal makeTerminal(const CellState& edge, const CellState& terminal, bool source)
{
    geometry::Terminal t;
    t.bounds = terminal.bounds;
    if (terminal.kind == CellState::Kind::Edge) {
        t.edgePoints = terminal.points;
        return t;
    }
    const ResolvedStyle& ts = terminal.style;
    const ResolvedStyle& es = edge.style;
    t.rotation = ts.rotation();
    t.flipH = ts.flipH();
    t.flipV = ts.flipV();
    if (const auto kind = geometry::parsePerimeter(ts.perimeter())) {
        geometry::PerimeterShape shape;
        shape.kind = *kind;
        shape.direction = ts.direction();
        shape.size = ts.has("size") ? ts.size(-1) : -1;
        shape.fixedSize = ts.fixedSize();
        t.perimeter = shape;
    } else {
        t.perimeter.reset();
    }
    t.perimeterSpacing =
        ts.perimeterSpacing() + (source ? es.sourcePerimeterSpacing() : es.targetPerimeterSpacing());
    t.constraint = es.constraint(source);
    std::string_view portConstraint = es.string(source ? "sourcePortConstraint" : "targetPortConstraint");
    if (portConstraint.empty())
        portConstraint = ts.string("portConstraint");
    t.portConstraint = std::string(portConstraint);
    t.portConstraintRotation = ts.flag("portConstraintRotation", false);
    return t;
}


// The routing input of `edge`: its geometry (points relative to `base`),
// style and the given terminals.
geometry::EdgeRouteRequest makeRequest(const CellState& edge, Point base, double gridSize,
                                       std::optional<geometry::Terminal> source,
                                       std::optional<geometry::Terminal> target, bool selfLoop)
{
    const ResolvedStyle& style = edge.style;
    const core::Geometry* geometry = edge.cell->geometry();
    geometry::EdgeRouteRequest request;
    request.source = std::move(source);
    request.target = std::move(target);
    if (geometry) {
        if (const auto p = geometry->sourcePoint())
            request.sourcePoint = plus(*p, base);
        if (const auto p = geometry->targetPoint())
            request.targetPoint = plus(*p, base);
        for (const Point& p : geometry->points())
            request.waypoints.push_back(plus(p, base));
    }
    request.style = style.edgeStyle();
    request.noEdgeStyle = style.flag("noEdgeStyle", false);
    if (style.has("orthogonal"))
        request.orthogonal = style.flag("orthogonal", false);
    request.elbow = style.elbow();
    request.jettySize = style.jettySize();
    request.sourceJettySize = style.has("sourceJettySize") ? style.jettySize("sourceJettySize") : style.jettySize();
    request.targetJettySize = style.has("targetJettySize") ? style.jettySize("targetJettySize") : style.jettySize();
    request.segment = style.number("segment", 30);
    request.gridSize = gridSize;
    request.orthogonalLoop = style.flag("orthogonalLoop", false);
    request.isSelfLoop = selfLoop;
    request.startMarkerSize = hasMarker(style.startArrow()) ? style.startMarkerSize() : 0;
    request.endMarkerSize = hasMarker(style.endArrow()) ? style.endMarkerSize() : 0;
    request.strokeWidth = style.strokeWidth();
    return request;
}

// Everything painted for the cell, conservatively (mxShape.augmentBoundingBox,
// mxConnector.augmentBoundingBox, plus the estimated text extent).
Rect paintBoundsOf(const CellState& state)
{
    const ResolvedStyle& style = state.style;
    Rect bounds;
    if (state.kind == CellState::Kind::Vertex) {
        bounds = style.rotation() != 0 ? geometry::rotatedBounds(state.bounds, style.rotation()) : state.bounds;
        bounds = grown(bounds, style.strokeWidth() / 2);
        if (style.shadow()) {
            bounds.width += kShadowOffsetX;
            bounds.height += kShadowOffsetY;
        }
    } else {
        // Markers only add their size when present,
        // as upstream: the size is 0 without arrows.
        double marker = 0;
        if (hasMarker(style.startArrow()))
            marker = style.startMarkerSize() + 1;
        if (hasMarker(style.endArrow()))
            marker = std::max(marker, style.endMarkerSize() + 1);
        bounds = grown(geometry::pointsBounds(state.points), marker + style.strokeWidth() / 2);
    }
    // Hand-drawn strokes stray beyond the geometry.
    if (const auto sketch = SketchSettings::fromStyle(style))
        bounds = grown(bounds, sketch->paintMargin());
    if (state.label) {
        geometry::TextExtentInput input;
        const auto& lines = state.labelText.lines;
        input.lineCount = static_cast<int>(std::max<std::size_t>(1, lines.size()));
        std::size_t longest = 0;
        for (const std::string& line : lines)
            longest = std::max(longest, codePoints(line));
        input.longestLine = static_cast<int>(longest);
        input.fontSize = style.fontSize();
        input.bold = (style.fontStyle() & FontBold) != 0;
        input.wrap = style.wrap();
        bounds = geometry::unite(bounds, geometry::estimateTextExtent(*state.label, input));
    }
    return bounds;
}

// The box of the label of `state` from its geometry, style, bounds and (edge)
// route; the text itself (labelText) is set by the caller.
void layoutLabel(CellState& state, const core::Geometry* geometry)
{
    const ResolvedStyle& style = state.style;
    geometry::LabelParams params = style.labelParams();
    if (state.kind == CellState::Kind::Vertex) {
        // The geometry offset of a non-relative vertex moves only its label.
        if (geometry && !geometry->relative())
            params.offset = geometry->offset().value_or(Point{});
        const VertexShape* shape = state.shape;
        const geometry::ShapeLabelBoundsFn hook = [shape, &style](const Rect& r) {
            return shape ? shape->labelBounds(style, r) : r;
        };
        state.label = geometry::layoutVertexLabel(state.bounds, params, hook);
    } else {
        const bool relative = geometry ? geometry->relative() : true;
        const Point anchor = geometry::edgeLabelAnchor(
            state.points, relative, geometry ? geometry->x() : 0, geometry ? geometry->y() : 0,
            geometry ? geometry->offset() : std::nullopt, center(state.bounds));
        state.label = geometry::layoutEdgeLabel(anchor, geometry ? geometry->width() : 0,
                                                geometry ? geometry->height() : 0, params);
    }
}

// --- the builder ---------------------------------------------------------------

Theme themeOf(const core::Page& page, const RenderContext& context)
{
    return context.theme.forPage(parseAdaptiveColors(page.adaptiveColors()));
}

class Builder {
public:
    using StateIndex = std::unordered_map<std::string, std::size_t>;

    // A build of the whole page into a vector of its own.
    Builder(const core::Page& page, const RenderContext& context)
        : page_(page), stylesheet_(*context.stylesheet), shapes_(*context.shapes), stencils_(*context.stencils),
          theme_(themeOf(page, context)), placeholders_(context.placeholders), states_(owned_)
    {
    }

    // An update of `states` (the states of an earlier build of `page`, found
    // by id through `index`) in place.
    Builder(const core::Page& page, const RenderContext& context, std::vector<CellState>& states,
            const StateIndex& index)
        : page_(page), stylesheet_(*context.stylesheet), shapes_(*context.shapes), stencils_(*context.stencils),
          theme_(themeOf(page, context)), placeholders_(context.placeholders), states_(states), index_(&index)
    {
    }

    void run()
    {
        collect();
        for (std::size_t i = 0; i < states_.size(); ++i)
            resolve(i);
        compact();
        applyLineJumps();
    }

    // Rebuilds the states at `dirty` (ascending), whose cells changed without
    // changing the tree, and nothing else: the other states are taken as they
    // are (the caller has put every state that depends on a dirty one into
    // `dirty`). Returns false, with the dirty states in an unspecified state,
    // when one of them could not be resolved as before: the caller restores
    // them and builds the page from scratch.
    bool update(const std::vector<std::size_t>& dirty)
    {
        status_.assign(states_.size(), Status::Done);
        for (const std::size_t i : dirty)
            status_[i] = Status::Pending;
        // Styles first: the parent of an edge label is an edge, whose style
        // the label inherits from, and a terminal may come after its edge.
        for (const std::size_t i : dirty) {
            const CellState& old = states_[i];
            const core::Cell* cell = page_.findCell(old.id);
            if (cell != old.cell || !cell->isVisible() || cell->isCollapsed() != old.collapsed
                || (cell->isEdge() ? CellState::Kind::Edge : CellState::Kind::Vertex) != old.kind)
                return false;
            if (old.parent != npos && states_[old.parent].id != cell->parent())
                return false;
            states_[i] = makeState(*cell, old.parent, old.depth);
        }
        for (const std::size_t i : dirty) {
            if (resolve(i) != Outcome::Ok)
                return false;
        }
        // A cycle or a depth limit makes the result depend on the order of
        // resolution, which a partial update does not reproduce.
        if (unresolved_ != 0)
            return false;
        applyLineJumps();
        return true;
    }

    std::vector<CellState> takeStates() { return std::move(owned_); }
    std::vector<core::Diagnostic> takeDiagnostics() { return std::move(diagnostics_); }
    // The scheme the page is built in: the context's, as the page's adaptiveColors allows.
    const Theme& theme() const { return theme_; }

private:
    enum class Status : unsigned char { Pending, Active, Done, Dead };
    enum class Outcome { Ok, Dead, InProgress };

    struct End {
        std::optional<geometry::Terminal> terminal;
        std::size_t state = npos; // index of the terminal's state, when attached
        bool hidden = false;      // the terminal exists but is not shown: the edge is not shown either
    };

    void report(Severity severity, std::string message, std::string_view cellId = {})
    {
        diagnostics_.push_back({severity, std::move(message), page_.id(), std::string(cellId), 0, 0});
    }

    // ---- step 1: the skeleton ----------------------------------------------------

    void collect()
    {
        const core::Cell* root = page_.rootCell();
        if (!root) {
            report(Severity::Warning, "page has no root cell; nothing to draw");
            return;
        }
        std::unordered_map<std::string_view, std::vector<const core::Cell*>> children;
        for (const core::Cell* cell : page_.cells()) {
            if (!cell->parent().empty() && cell->parent() != cell->id())
                children[cell->parent()].push_back(cell);
        }
        const auto childrenOf = [&](const core::Cell& cell) -> const std::vector<const core::Cell*>* {
            const auto it = children.find(cell.id());
            return it == children.end() ? nullptr : &it->second;
        };

        // Depth-first with an explicit stack: pathological nesting must not
        // exhaust the call stack.
        struct Frame {
            const std::vector<const core::Cell*>* cells;
            std::size_t next;
            std::size_t parentState;
            int depth;
            bool layerLevel; // the cells are the children of the root cell
        };
        std::vector<Frame> stack;
        std::unordered_set<const core::Cell*> visited; // duplicate ids must not multiply subtrees
        if (const auto* top = childrenOf(*root))
            stack.push_back({top, 0, npos, 0, true});

        while (!stack.empty()) {
            Frame& frame = stack.back();
            if (frame.next >= frame.cells->size()) {
                stack.pop_back();
                continue;
            }
            const core::Cell& cell = *(*frame.cells)[frame.next++];
            const std::size_t parentState = frame.parentState;
            const int depth = frame.depth;
            const bool layerLevel = frame.layerLevel;
            if (!visited.insert(&cell).second || !cell.isVisible())
                continue;

            const bool paintable = cell.isVertex() || cell.isEdge();
            if (!paintable) {
                // A layer (child of the root) paints nothing itself.
                if (layerLevel) {
                    if (const auto* below = childrenOf(cell))
                        stack.push_back({below, 0, npos, 0, false});
                }
                continue;
            }
            const std::size_t index = addState(cell, parentState, depth);
            if (!cell.isCollapsed()) { // descendants of collapsed cells are not shown
                if (const auto* below = childrenOf(cell))
                    stack.push_back({below, 0, index, depth + 1, false});
            }
        }
    }

    std::size_t addState(const core::Cell& cell, std::size_t parent, int depth)
    {
        CellState state = makeState(cell, parent, depth);
        const std::size_t index = states_.size();
        byId_.emplace(std::string_view(cell.id()), index);
        states_.push_back(std::move(state));
        status_.push_back(Status::Pending);
        return index;
    }

    // The state of `cell` before its geometry is resolved: the resolved
    // style and the painter. The state of the parent index is final.
    CellState makeState(const core::Cell& cell, std::size_t parent, int depth)
    {
        CellState state;
        state.cell = &cell;
        state.id = cell.id();
        state.kind = cell.isEdge() ? CellState::Kind::Edge : CellState::Kind::Vertex;
        state.parent = parent;
        state.depth = depth;
        state.collapsed = cell.isCollapsed();

        ResolveOptions options;
        options.theme = theme_;
        if (state.kind == CellState::Kind::Vertex && parent != npos && states_[parent].kind == CellState::Kind::Edge)
            options.parentEdgeStyle = &states_[parent].style; // edge label cells inherit label styles
        state.style = stylesheet_.resolve(cell.style(),
                                          state.kind == CellState::Kind::Edge ? CellKind::Edge : CellKind::Vertex,
                                          options);
        if (state.kind == CellState::Kind::Vertex)
            state.shape = lookupShape(state);
        else
            noteEdgeShape(state);
        return state;
    }

    // The index of the state of `id`, or npos.
    std::size_t indexOf(std::string_view id) const
    {
        if (index_ != nullptr) {
            const auto it = index_->find(std::string(id));
            return it == index_->end() ? npos : it->second;
        }
        const auto it = byId_.find(id);
        return it == byId_.end() ? npos : it->second;
    }

    // mxCellRenderer.defaultVertexShape is the rectangle. Then native shapes,
    // stencils (exact, case-insensitive key; inline stencil(...) values), the
    // placeholder for shapes draw.io has and we lack, and the rectangle for names
    // draw.io does not know either.
    const VertexShape* lookupShape(const CellState& state)
    {
        const std::string_view name = state.style.shape();
        const VertexShape* shape = nullptr;
        if (name.empty()) {
            shape = shapes_.find("rectangle");
        } else if (name.starts_with("stencil(")) {
            shape = stencils_.findInline(name);
        } else {
            shape = shapes_.find(name);
            if (!shape)
                shape = stencils_.find(name);
        }
        if (!shape) {
            if (ShapeRegistry::isUpstreamOnly(name)) {
                report(Severity::Info, "unknown shape " + shortened(name), state.id);
                shape = &shapes_.fallback();
            } else {
                // draw.io has no painter for this name either and draws the default
                // rectangle (mxCellRenderer.getShapeConstructor).
                shape = shapes_.find("rectangle");
            }
        }
        return shape;
    }

    // Edges are painted by the connector painter whatever `shape` says
    // (arrow, flexArrow, link, ...). Said once per shape name.
    void noteEdgeShape(const CellState& state)
    {
        const std::string_view name = state.style.shape();
        if (name.empty() || name == "connector")
            return;
        if (reportedEdgeShapes_.insert(std::string(name)).second)
            report(Severity::Info, "edge shape " + shortened(name) + " is drawn as a connector", state.id);
    }

    // ---- step 2: geometry -----------------------------------------------------------

    Outcome resolve(std::size_t i)
    {
        switch (status_[i]) {
        case Status::Done: return Outcome::Ok;
        case Status::Dead: return Outcome::Dead;
        case Status::Active:
            ++unresolved_;
            return Outcome::InProgress;
        case Status::Pending: break;
        }
        if (depth_ >= kMaxResolveDepth) {
            ++unresolved_;
            return Outcome::InProgress;
        }
        ++depth_;
        status_[i] = Status::Active;
        const Outcome outcome = states_[i].kind == CellState::Kind::Edge ? resolveEdge(i) : resolveVertex(i);
        status_[i] = outcome == Outcome::Ok ? Status::Done : outcome == Outcome::Dead ? Status::Dead : Status::Pending;
        --depth_;
        return outcome;
    }

    Outcome resolveParent(const CellState& state)
    {
        return state.parent == npos ? Outcome::Ok : resolve(state.parent);
    }

    // updateCellState + updateVertexState.
    Outcome resolveVertex(std::size_t i)
    {
        CellState& state = states_[i];
        const core::Geometry* geometry = state.cell->geometry();
        if (!geometry) {
            report(Severity::Warning, "vertex without geometry is not drawn", state.id);
            return Outcome::Dead;
        }
        if (const Outcome parentOutcome = resolveParent(state); parentOutcome != Outcome::Ok)
            return parentOutcome;
        const CellState* parent = state.parent == npos ? nullptr : &states_[state.parent];
        state.bounds = vertexBounds(*geometry, parent);
        if (!isFinite(state.bounds)) {
            report(Severity::Warning, "vertex with non-finite geometry is not drawn", state.id);
            return Outcome::Dead;
        }
        finish(state, geometry);
        return Outcome::Ok;
    }

    // The unrotated absolute bounds of a vertex.
    Rect vertexBounds(const core::Geometry& geometry, const CellState* parent) const
    {
        Point origin{geometry.x(), geometry.y()};
        const Point offset = geometry.offset().value_or(Point{});
        if (parent && geometry.relative()) {
            if (parent->kind == CellState::Kind::Edge) {
                // The child's top-left sits on the point of the edge path.
                origin = geometry::relativePointOnPolyline(parent->points, geometry.x(), geometry.y(), offset);
            } else {
                origin = {parent->bounds.x + geometry.x() * parent->bounds.width + offset.x,
                          parent->bounds.y + geometry.y() * parent->bounds.height + offset.y};
            }
        } else if (parent) {
            const Point base = parent->kind == CellState::Kind::Vertex ? Point{parent->bounds.x, parent->bounds.y}
                                                                       : Point{};
            origin = plus(base, origin);
        }
        Rect bounds{origin.x, origin.y, geometry.width(), geometry.height()};

        // updateVertexState: relative children of a rotated vertex follow the rotation.
        if (parent && geometry.relative() && parent->kind == CellState::Kind::Vertex) {
            const double rotation = parent->style.rotation();
            if (rotation != 0) {
                const Point rotated = geometry::rotatePoint(center(bounds), rotation, center(parent->bounds));
                bounds.x = rotated.x - bounds.width / 2;
                bounds.y = rotated.y - bounds.height / 2;
            }
        }
        return bounds;
    }

    // The terminal of one end of an edge. mxGraphView.getVisibleTerminal: a
    // terminal inside a collapsed cell attaches to the outermost collapsed
    // ancestor. A terminal that exists but is hidden hides the edge.
    End resolveEnd(const CellState& edge, bool source)
    {
        End end;
        const core::Cell& cell = *edge.cell;
        const std::string& id = source ? cell.source() : cell.target();
        if (id.empty())
            return end;
        const core::Cell* terminal = page_.findCell(id);
        if (!terminal || !(terminal->isVertex() || terminal->isEdge())) {
            report(Severity::Warning,
                   std::string(source ? "source" : "target") + " terminal '" + id + "' does not exist; edge is dangling",
                   edge.id);
            return end;
        }
        const core::Cell* shown = terminal;
        const core::Cell* current = terminal;
        for (int steps = 0; current && steps < kMaxAncestors; ++steps) {
            if (current->isCollapsed())
                shown = current;
            current = parentCell(page_, *current);
        }
        const std::size_t terminalIndex = indexOf(shown->id());
        if (terminalIndex == npos || states_[terminalIndex].cell != shown) {
            end.hidden = true;
            return end;
        }
        switch (resolve(terminalIndex)) {
        case Outcome::Dead:
            end.hidden = true;
            return end;
        case Outcome::InProgress: // a cycle: this end floats
            return end;
        case Outcome::Ok:
            break;
        }
        end.state = terminalIndex;
        end.terminal = makeTerminal(edge, states_[terminalIndex], source);
        return end;
    }

    // updateEdgeState.
    Outcome resolveEdge(std::size_t i)
    {
        CellState& state = states_[i];
        if (const Outcome parentOutcome = resolveParent(state); parentOutcome != Outcome::Ok)
            return parentOutcome;
        const core::Geometry* geometry = state.cell->geometry();
        // Edge geometry is in the coordinates of the parent vertex.
        const Point base = state.parent != npos && states_[state.parent].kind == CellState::Kind::Vertex
                               ? Point{states_[state.parent].bounds.x, states_[state.parent].bounds.y}
                               : Point{};

        const End source = resolveEnd(state, true);
        const End target = resolveEnd(state, false);
        if (source.hidden || target.hidden)
            return Outcome::Dead; // upstream clears edges whose terminal is not visible

        const geometry::EdgeRouteRequest request =
            makeRequest(state, base, page_.gridSize(), source.terminal, target.terminal,
                        source.state != npos && source.state == target.state);

        geometry::EdgeRoute route = geometry::routeEdge(request);
        if (!route.valid()) {
            report(Severity::Warning, "edge has no usable terminals or points; not drawn", state.id);
            return Outcome::Dead;
        }
        state.points = std::move(route.points);
        state.bounds = geometry::pointsBounds(state.points);
        if (!isFinite(state.bounds)) {
            report(Severity::Warning, "edge with non-finite route is not drawn", state.id);
            return Outcome::Dead;
        }
        finish(state, geometry);
        return Outcome::Ok;
    }

    // ---- step 3: label and paint bounds ----------------------------------------------

    void finish(CellState& state, const core::Geometry* geometry)
    {
        if (state.kind == CellState::Kind::Vertex && geometry != nullptr) {
            // Table cells keep their own size in the alternate bounds.
            if (state.parent != npos && states_[state.parent].style.shape() == "tableRow")
                state.alternateBounds = geometry->alternateBounds();
            if (state.style.shape() == "tableRow")
                collectHiddenChildren(state);
        }
        buildLabel(state, geometry);
        state.paintBounds = paintBoundsOf(state);
    }

    // The cells of a table row that are not shown: a table paints its grid
    // from all the cells of its rows, shown or not (Graph.visitTableCells).
    void collectHiddenChildren(CellState& row) const
    {
        row.hiddenChildren.clear();
        std::size_t position = 0;
        for (const core::Cell* child : page_.children(row.id)) {
            if (!child->isVertex() || child->geometry() == nullptr)
                continue;
            if (!child->isVisible()) {
                HiddenChild hidden;
                hidden.position = position;
                hidden.id = child->id();
                const core::Geometry& g = *child->geometry();
                hidden.bounds = {g.x(), g.y(), g.width(), g.height()};
                hidden.alternateBounds = g.alternateBounds();
                ResolveOptions options;
                options.theme = theme_;
                hidden.style = stylesheet_.resolve(child->style(), CellKind::Vertex, options);
                row.hiddenChildren.push_back(std::move(hidden));
            }
            ++position;
        }
    }

    void buildLabel(CellState& state, const core::Geometry* geometry) const
    {
        const ResolvedStyle& style = state.style;
        const bool shapePaintsValue = state.shape != nullptr && state.shape->paintsValue();
        if (style.noLabel() && !shapePaintsValue)
            return;
        std::string value = state.cell->value();
        if (usesPlaceholders(*state.cell))
            value = replacePlaceholders(page_, *state.cell, value, placeholders_);
        if (value.empty())
            return;
        RichText text = makeRichText(value, style.html());
        if (text.empty())
            return;
        if (text.html)
            text.content = adaptLabelColors(text.content, theme_);
        if (shapePaintsValue)
            state.valueText = joinLines(text.lines);
        if (style.noLabel())
            return;

        layoutLabel(state, geometry);
        state.labelText = std::move(text);
    }

    // ---- step 5: line jumps ------------------------------------------------------------

    // mxGraphView.updateLineJumps: every edge with a jumpStyle jumps over the
    // edges before it in paint order (those that are not noJump, and in the
    // same layer with jumpLayers=0). Curved edges do not take part (documented gap).
    void applyLineJumps()
    {
        std::vector<std::optional<geometry::JumpTarget>> targets(states_.size());
        std::vector<const geometry::JumpTarget*> earlier;
        const auto layerOf = [&](std::size_t i) {
            while (states_[i].parent != npos)
                i = states_[i].parent;
            return states_[i].cell->parent();
        };
        for (std::size_t i = 0; i < states_.size(); ++i) {
            CellState& state = states_[i];
            if (state.kind != CellState::Kind::Edge || state.style.curved())
                continue;
            const std::string_view jumpStyle = state.style.jumpStyle();
            if (jumpStyle == "none" || jumpStyle.empty())
                continue;
            earlier.clear();
            const bool sameLayer = !state.style.jumpAcrossLayers();
            const std::string layer = sameLayer ? layerOf(i) : std::string();
            for (std::size_t e = 0; e < i; ++e) {
                const CellState& other = states_[e];
                if (other.kind != CellState::Kind::Edge || other.style.curved() || other.style.noJump() ||
                    other.points.size() < 2 || (sameLayer && layerOf(e) != layer))
                    continue;
                if (!targets[e])
                    targets[e] = geometry::makeJumpTarget(other.points);
                earlier.push_back(&*targets[e]);
            }
            state.routedPoints = geometry::routeJumps(state.points, earlier);
        }
    }

    // ---- step 4 ------------------------------------------------------------------------

    // Removes dead states. A dead state's descendants are dead too (they
    // could not resolve their parent), so only parent indices need remapping.
    void compact()
    {
        std::vector<std::size_t> remap(states_.size(), npos);
        std::size_t kept = 0;
        for (std::size_t i = 0; i < states_.size(); ++i) {
            if (status_[i] == Status::Done)
                remap[i] = kept++;
        }
        std::vector<CellState> result;
        result.reserve(kept);
        for (std::size_t i = 0; i < states_.size(); ++i) {
            if (remap[i] == npos)
                continue;
            CellState& state = states_[i];
            state.parent = state.parent == npos ? npos : remap[state.parent];
            result.push_back(std::move(state));
        }
        states_ = std::move(result);
    }

    const core::Page& page_;
    const Stylesheet& stylesheet_;
    const ShapeRegistry& shapes_;
    StencilRegistry& stencils_;
    Theme theme_;
    PlaceholderVariables placeholders_;

    std::vector<CellState> owned_;
    std::vector<CellState>& states_; // owned_, or the states of an update
    const StateIndex* index_ = nullptr; // the id index of an update's states; null: byId_
    std::vector<Status> status_;
    std::unordered_map<std::string_view, std::size_t> byId_;
    int unresolved_ = 0; // resolutions that found a cycle or hit the depth limit
    std::unordered_set<std::string> reportedEdgeShapes_;
    std::vector<core::Diagnostic> diagnostics_;
    int depth_ = 0;
};

} // namespace

std::string replaceCellPlaceholders(const core::Page& page, const core::Cell& cell, const std::string& text,
                                    const PlaceholderVariables& variables)
{
    return replacePlaceholders(page, cell, text, variables);
}

bool cellUsesPlaceholders(const core::Cell& cell)
{
    return usesPlaceholders(cell);
}

RenderContext RenderContext::defaults()
{
    RenderContext context;
    context.stylesheet = &Stylesheet::drawioDefault();
    context.shapes = &ShapeRegistry::builtin();
    context.stencils = &StencilRegistry::builtin();
    return context;
}

PageView PageView::build(const core::Page& page, const RenderContext& context)
{
    QRAW_PERF_SCOPE(PageViewBuild);
    stats::add(stats::Counter::pageViewBuilds);
    RenderContext resolved = context;
    if (!resolved.stylesheet)
        resolved.stylesheet = &Stylesheet::drawioDefault();
    if (!resolved.shapes)
        resolved.shapes = &ShapeRegistry::builtin();
    if (!resolved.stencils)
        resolved.stencils = &StencilRegistry::builtin();

    Builder builder(page, resolved);
    builder.run();

    PageView view;
    view.states_ = builder.takeStates();
    view.diagnostics_ = builder.takeDiagnostics();
    view.gridSize_ = page.gridSize();
    view.theme_ = builder.theme();
    view.index_.reserve(view.states_.size());
    for (std::size_t i = 0; i < view.states_.size(); ++i)
        view.index_.emplace(view.states_[i].id, i); // the first of duplicate ids wins
    return view;
}

namespace {

// The indices of the states that an update of the cells `changed` has to
// rebuild (ascending), or nullopt when a changed cell is not a state, or a cell
// that depends on one is not a state although it is not hidden: it could not be
// drawn before (an edge without a usable route, a vertex with no geometry) and
// the change may make it drawable, which a partial update cannot add.
std::optional<std::vector<std::size_t>> dirtyStates(const core::Page& page, const std::vector<CellState>& states,
                                                    const std::unordered_map<std::string, std::size_t>& index,
                                                    const std::set<std::string>& changed)
{
    std::vector<char> dirty(states.size(), 0);
    std::vector<std::size_t> pending;
    const auto mark = [&](std::size_t i) {
        if (!dirty[i]) {
            dirty[i] = 1;
            pending.push_back(i);
        }
    };
    // Marks the state of `cell`; false for a cell that is not one and is not hidden.
    const auto markCell = [&](const core::Cell& cell) {
        if (const auto it = index.find(cell.id()); it != index.end()) {
            mark(it->second);
            return true;
        }
        return !cell.isVisible();
    };
    const auto markEdgesOf = [&](const std::string& id) {
        const auto edges = page.connectedEdges(id);
        return std::ranges::all_of(edges, [&](const core::Cell* edge) { return markCell(*edge); });
    };
    for (const std::string& id : changed) {
        const auto it = index.find(id);
        if (it == index.end())
            return std::nullopt;
        mark(it->second);
    }
    while (!pending.empty()) {
        const std::size_t i = pending.back();
        pending.pop_back();
        const CellState& state = states[i];
        if (!markEdgesOf(state.id))
            return std::nullopt;
        if (state.collapsed) {
            // The edges of the cells inside attach to this one.
            for (const core::Cell* inside : page.descendants(state.id)) {
                if (!markEdgesOf(inside->id()))
                    return std::nullopt;
            }
            continue;
        }
        // The children (and with them the descendants, which follow their
        // ancestor in paint order, deeper) are rebuilt with their parent.
        for (const std::string& child : page.childIds(state.id)) {
            if (!markCell(*page.findCell(child)))
                return std::nullopt;
        }
        for (std::size_t j = i + 1; j < states.size() && states[j].depth > state.depth; ++j)
            mark(j);
    }
    std::vector<std::size_t> result;
    for (std::size_t i = 0; i < dirty.size(); ++i) {
        if (dirty[i])
            result.push_back(i);
    }
    return result;
}

} // namespace

std::optional<PageView::Update> PageView::update(const core::Page& page, const RenderContext& context,
                                                 const std::set<std::string>& changed)
{
    QRAW_PERF_SCOPE(PageViewUpdate);
    if (page.gridSize() != gridSize_)
        return std::nullopt;
    std::optional<std::vector<std::size_t>> dirty = dirtyStates(page, states_, index_, changed);
    if (!dirty)
        return std::nullopt;

    RenderContext resolved = context;
    if (!resolved.stylesheet)
        resolved.stylesheet = &Stylesheet::drawioDefault();
    if (!resolved.shapes)
        resolved.shapes = &ShapeRegistry::builtin();
    if (!resolved.stencils)
        resolved.stencils = &StencilRegistry::builtin();

    Update result;
    result.indices = std::move(*dirty);
    result.before.reserve(result.indices.size());
    for (const std::size_t i : result.indices)
        result.before.push_back(states_[i]);

    Builder builder(page, resolved, states_, index_);
    if (!builder.update(result.indices)) {
        for (std::size_t k = 0; k < result.indices.size(); ++k)
            states_[result.indices[k]] = std::move(result.before[k]);
        return std::nullopt;
    }
    stats::add(stats::Counter::pageViewUpdates);
    return result;
}

const CellState* PageView::find(std::string_view id) const
{
    // The index is keyed by std::string (no transparent hashing); ids are short.
    const auto it = index_.find(std::string(id));
    return it == index_.end() ? nullptr : &states_[it->second];
}

std::vector<Point> PageView::previewRoute(const CellState& edge, const RouteOverride& change) const
{
    if (edge.kind != CellState::Kind::Edge || edge.cell == nullptr)
        return {};
    const Point base = edge.parent != npos && states_[edge.parent].kind == CellState::Kind::Vertex
                           ? Point{states_[edge.parent].bounds.x, states_[edge.parent].bounds.y}
                           : Point{};

    const auto terminalOf = [&](const RouteEnd& end, bool source) -> std::optional<geometry::Terminal> {
        const std::string& current = source ? edge.cell->source() : edge.cell->target();
        const std::string& id = end.kind == RouteEnd::Kind::Cell ? end.cell : current;
        if (end.kind == RouteEnd::Kind::Point || id.empty())
            return std::nullopt;
        const CellState* state = find(id);
        if (state == nullptr)
            return std::nullopt;
        geometry::Terminal terminal = makeTerminal(edge, *state, source);
        if (end.kind == RouteEnd::Kind::Cell)
            terminal.constraint = end.constraint;
        if (end.bounds)
            terminal.bounds = *end.bounds;
        if (end.rotation)
            terminal.rotation = *end.rotation;
        if (end.shift.x != 0 || end.shift.y != 0) {
            terminal.bounds.x += end.shift.x;
            terminal.bounds.y += end.shift.y;
            for (Point& p : terminal.edgePoints) {
                p.x += end.shift.x;
                p.y += end.shift.y;
            }
        }
        return terminal;
    };

    const CellState* sourceState = find(edge.cell->source());
    const CellState* targetState = find(edge.cell->target());
    geometry::EdgeRouteRequest request = makeRequest(edge, base, gridSize_, terminalOf(change.source, true),
                                                     terminalOf(change.target, false),
                                                     sourceState != nullptr && sourceState == targetState);
    if (change.source.kind == RouteEnd::Kind::Point)
        request.sourcePoint = change.source.point;
    if (change.target.kind == RouteEnd::Kind::Point)
        request.targetPoint = change.target.point;
    // A dangling end that stays dangling keeps its drawn position.
    if (!request.source && !request.sourcePoint && !edge.points.empty())
        request.sourcePoint = edge.points.front();
    if (!request.target && !request.targetPoint && !edge.points.empty())
        request.targetPoint = edge.points.back();
    if (change.waypoints)
        request.waypoints = *change.waypoints;
    return geometry::routeEdge(request).points;
}

std::optional<CellState> PageView::previewEdgeState(const CellState& edge, const RouteOverride& change) const
{
    std::vector<Point> route = previewRoute(edge, change);
    if (route.empty())
        return std::nullopt;
    CellState state = edge;
    state.points = std::move(route);
    state.bounds = geometry::pointsBounds(state.points);
    if (!isFinite(state.bounds))
        return std::nullopt;
    state.routedPoints.clear();
    if (state.label)
        layoutLabel(state, state.cell != nullptr ? state.cell->geometry() : nullptr);
    state.paintBounds = paintBoundsOf(state);
    return state;
}

CellState PageView::previewVertexState(const CellState& vertex, const Rect& bounds, std::optional<double> rotation)
{
    CellState state = vertex;
    state.bounds = bounds;
    if (rotation) {
        const double normalized = std::fmod(*rotation, 360.0);
        state.style.map().set("rotation", core::formatNumber(normalized));
    }
    if (state.label)
        layoutLabel(state, state.cell != nullptr ? state.cell->geometry() : nullptr);
    state.paintBounds = paintBoundsOf(state);
    return state;
}

std::optional<Point> PageView::fixedTerminalPoint(const CellState& edge, bool source) const
{
    const CellState* terminal = edge.cell != nullptr ? find(source ? edge.cell->source() : edge.cell->target()) : nullptr;
    if (terminal == nullptr)
        return std::nullopt;
    const geometry::Terminal t = makeTerminal(edge, *terminal, source);
    if (!t.constraint)
        return std::nullopt;
    return geometry::constraintPoint(t, *t.constraint);
}

Rect PageView::bounds() const
{
    if (states_.empty())
        return {};
    Rect result = states_.front().paintBounds;
    for (std::size_t i = 1; i < states_.size(); ++i)
        result = geometry::unite(result, states_[i].paintBounds);
    return result;
}

Rect PageView::boundsOf(std::span<const std::string> ids) const
{
    const std::vector<std::size_t> picked = selection(ids);
    if (picked.empty())
        return {};
    Rect result = states_[picked.front()].paintBounds;
    for (std::size_t k = 1; k < picked.size(); ++k)
        result = geometry::unite(result, states_[picked[k]].paintBounds);
    return result;
}

std::vector<std::size_t> PageView::selection(std::span<const std::string> ids) const
{
    std::vector<char> picked(states_.size(), 0);
    // Descendants follow their ancestor directly in paint order, with a
    // greater depth.
    const auto pickSubtree = [&](std::size_t first) {
        picked[first] = 1;
        for (std::size_t j = first + 1; j < states_.size() && states_[j].depth > states_[first].depth; ++j)
            picked[j] = 1;
    };
    for (const std::string& id : ids) {
        if (const auto it = index_.find(id); it != index_.end()) {
            pickSubtree(it->second);
            continue;
        }
        // Layers have no state of their own: selecting one selects its cells.
        for (std::size_t k = 0; k < states_.size(); ++k) {
            if (states_[k].depth == 0 && states_[k].cell->parent() == id)
                pickSubtree(k);
        }
    }
    std::vector<std::size_t> result;
    for (std::size_t k = 0; k < picked.size(); ++k) {
        if (picked[k])
            result.push_back(k);
    }
    return result;
}

} // namespace qraw::render
