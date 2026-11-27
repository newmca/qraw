#pragma once

// Scene projection, part 1: the computed view state of every cell of a
// page (draw.io's mxCellState), independent of any canvas. Export paints
// from it; the canvas's DiagramScene records every state into a snapshot and
// hit tests against the states. build() is O(cells); update() patches a view
// after an edit that kept the tree in O(the changed cells and what depends on
// them) (D-037).

#include "qraw/core/diagnostic.h"
#include "qraw/core/page.h"
#include "qraw/geometry/jumps.h"
#include "qraw/geometry/label_layout.h"
#include "qraw/render/image_source.h"
#include "qraw/render/resolved_style.h"
#include "qraw/render/stylesheet.h"
#include "qraw/render/text.h"

#include <QDateTime>

#include <cstddef>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace qraw::render {

class ShapeRegistry;
class StencilRegistry;
class VertexShape;

// Values of the global placeholder variables (draw.io's
// Graph.getGlobalVariable and the page variables of its EditorUi). A token
// whose value is unset stays literal. `now` is read at render time when unset.
struct PlaceholderVariables {
    std::string page;             // %page%: the page name
    std::string fileName;         // %filename%
    int pageNumber = 0;           // %pagenumber%: 1-based; 0 = unset
    int pageCount = 0;            // %pagecount%; 0 = unset
    std::optional<QDateTime> now; // %date%, %time%, %timestamp%
};

// Graph.isReplacePlaceholders: the cell's own value object has placeholders="1"
// and no `placeholder` attribute.
[[nodiscard]] bool cellUsesPlaceholders(const core::Cell& cell);
// Graph.replacePlaceholders on `text` for `cell`: the label's rules, for the
// other texts of a cell (the tooltip). %label% and %tooltip% stay in place.
[[nodiscard]] std::string replaceCellPlaceholders(const core::Page& page, const core::Cell& cell,
                                                  const std::string& text, const PlaceholderVariables& variables);

// Everything rendering needs besides the page. Cheap to copy.
struct RenderContext {
    const Stylesheet* stylesheet = nullptr;  // nullptr = Stylesheet::drawioDefault()
    const ShapeRegistry* shapes = nullptr;   // nullptr = ShapeRegistry::builtin()
    StencilRegistry* stencils = nullptr;     // nullptr = StencilRegistry::builtin()
    Theme theme;
    ImageResolveContext images;
    PlaceholderVariables placeholders;

    // The context with all registries set to the built-in ones.
    [[nodiscard]] static RenderContext defaults();
};

// A vertex child of a table row that is not shown (visible=0), which is what a
// merged cell makes of the cells it covers. It has no state of its own; the
// row keeps what the table painter needs to count it in the grid.
struct HiddenChild {
    std::size_t position = 0; // among the vertex children of the row that have a geometry
    std::string id;
    Rect bounds;              // relative to the row
    std::optional<Rect> alternateBounds;
    ResolvedStyle style;
};

struct CellState {
    enum class Kind { Vertex, Edge };

    const core::Cell* cell = nullptr; // valid until the page changes structurally
    std::string id;
    Kind kind = Kind::Vertex;
    std::size_t parent = npos;        // index of the parent's state, npos for top level
    int depth = 0;                    // 0 = child of a layer

    ResolvedStyle style;
    // Vertex: absolute, unrotated bounds (parent origins added; relative
    // geometry resolved against the parent vertex or the parent edge path).
    // Edge: bounding box of `points`.
    Rect bounds;
    // Edge only: the routed absolute polyline (geometry::routeEdge).
    std::vector<Point> points;
    // Edge with a jumpStyle only: `points` with the jumps over the edges
    // painted before it (geometry::routeJumps). Empty when it crosses none.
    std::vector<geometry::RoutedPoint> routedPoints;
    // Resolved painter for vertices (native, stencil or fallback); nullptr
    // for edges and invisible shapes.
    const VertexShape* shape = nullptr;
    bool collapsed = false;
    // The geometry's alternate bounds of a child of a table row: a table
    // cell keeps its own size there while a merged cell is larger.
    std::optional<Rect> alternateBounds;
    // The children of a table row (shape=tableRow) that are not shown, in order.
    std::vector<HiddenChild> hiddenChildren;

    // The label, when the cell has a non-empty value and noLabel is off.
    std::optional<geometry::LabelBox> label;
    RichText labelText;
    // The value as one line of plain text (explicit breaks joined by a
    // space, placeholders replaced), only for shapes that paint the value
    // themselves (VertexShape::paintsValue(), e.g. curvedText); empty for
    // everything else. Independent of noLabel.
    std::string valueText;

    // Conservative bounds of everything painted for the cell: rotated shape
    // bounds grown by half the stroke width, the shadow offset, marker
    // sizes, and the estimated label extent (geometry::estimateTextExtent).
    Rect paintBounds;

    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
};

// One end of an edge in a what-if route (PageView::previewRoute).
struct RouteEnd {
    enum class Kind {
        Current, // as the edge is now
        Cell,    // attached to `cell`, with `constraint` (none: floating)
        Point,   // dangling at `point` (absolute)
    };
    Kind kind = Kind::Current;
    std::string cell;
    std::optional<geometry::ConnectionConstraint> constraint;
    Point point;
    // Current and Cell: the terminal is routed as if it were shifted by this
    // (the cell is being moved and the view still has it where it was)...
    Point shift;
    // ...or as if its unrotated bounds were these and its rotation this (the
    // cell is being resized or rotated). Applied before `shift`.
    std::optional<Rect> bounds;
    std::optional<double> rotation;
};

// What an edge handler changes while it drags: the ends and/or the waypoints.
struct RouteOverride {
    RouteEnd source;
    RouteEnd target;
    std::optional<std::vector<Point>> waypoints; // absolute; none = the stored ones
};

class PageView {
public:
    // Builds the states of all visible cells in paint order: depth-first,
    // pre-order over the cell tree in z-order (root -> layers -> children),
    // which is draw.io's rendering order. Skipped: the root cell and layers
    // themselves (they paint nothing), cells with visible="0", layers with
    // visible="0" and everything below them, descendants of collapsed cells,
    // cells without geometry (vertices) or without a route (edges).
    // Edges are routed after the states they depend on (terminal vertices
    // and terminal edges); cycles are broken by routing with floating ends.
    // Problems (dangling terminals, unknown shapes) become diagnostics; the
    // build never fails.
    [[nodiscard]] static PageView build(const core::Page& page, const RenderContext& context);

    // What update() rebuilt: the paint indices of the states (ascending) and
    // what they were before.
    struct Update {
        std::vector<std::size_t> indices;
        std::vector<CellState> before;
    };
    // Brings this view, built from `page` earlier, up to date after an edit
    // that changed only the content of the cells `changed` (ids) and left the
    // tree alone (nothing added, removed, reordered or re-parented, no page
    // property changed): rebuilds those states and the ones that depend on
    // them (their descendants and the edges attached to them or to their
    // descendants, repeatedly) and re-runs the line jumps, which is what
    // build() would produce (states, not diagnostics: those of the earlier
    // build are kept). O(rebuilt states), not O(cells). Returns nullopt and
    // leaves the view as it was when that is not possible: a changed cell
    // that is not in the view, was hidden, collapsed, expanded or turned into
    // an edge, can no longer be drawn, or when the edges form a cycle. The
    // caller then calls build().
    [[nodiscard]] std::optional<Update> update(const core::Page& page, const RenderContext& context,
                                               const std::set<std::string>& changed);

    [[nodiscard]] const std::vector<CellState>& states() const { return states_; }
    // The scheme the page was built in (the context's theme as the page's adaptiveColors allows).
    [[nodiscard]] const Theme& theme() const { return theme_; }
    [[nodiscard]] const CellState* find(std::string_view id) const;
    // Union of all paintBounds; {0,0,0,0} for an empty page.
    [[nodiscard]] Rect bounds() const;
    // Union of the paintBounds of the given cells and their descendants;
    // unknown ids are ignored.
    [[nodiscard]] Rect boundsOf(std::span<const std::string> ids) const;
    // Indices of the given cells and all their descendants, in paint order.
    [[nodiscard]] std::vector<std::size_t> selection(std::span<const std::string> ids) const;
    [[nodiscard]] const std::vector<core::Diagnostic>& diagnostics() const { return diagnostics_; }

    // The route `edge` would have with the given ends and waypoints, as
    // mxEdgeHandler.updatePreviewState re-runs the edge style for the
    // preview. Empty when the edge could not be routed. Nothing is stored.
    [[nodiscard]] std::vector<Point> previewRoute(const CellState& edge, const RouteOverride& change) const;
    // `edge` as it would be drawn with that route: points, bounds, label box
    // and paint bounds follow the route (the jumps over other edges are
    // dropped). Nullopt when the edge could not be routed. For the live preview
    // of an edge whose end is dragged along with a cell; nothing is stored.
    [[nodiscard]] std::optional<CellState> previewEdgeState(const CellState& edge, const RouteOverride& change) const;
    // `vertex` as it would be drawn at `bounds` (absolute, unrotated) and
    // rotated by `rotation` degrees (nullopt: as it is): label box and paint
    // bounds follow. Children and connected edges are not touched.
    [[nodiscard]] static CellState previewVertexState(const CellState& vertex, const Rect& bounds,
                                                     std::optional<double> rotation);
    // The absolute point of the fixed connection constraint (exitX/entryX...)
    // of one end of `edge`; none for a floating or dangling end.
    [[nodiscard]] std::optional<Point> fixedTerminalPoint(const CellState& edge, bool source) const;

private:
    std::vector<CellState> states_;
    std::unordered_map<std::string, std::size_t> index_;
    std::vector<core::Diagnostic> diagnostics_;
    double gridSize_ = 10;
    Theme theme_;
};

} // namespace qraw::render
