#pragma once

// Vertex shapes: the painter interface, the shared paint pipeline
// (mxShape.paint) and the registry of native shapes.

#include "qraw/geometry/path.h"
#include "qraw/geometry/routing.h"
#include "qraw/render/canvas.h"
#include "qraw/render/image_source.h"
#include "qraw/render/resolved_style.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace qraw::render {

class StencilRegistry;

// A child cell as some container shapes need it (table, tableRow draw
// their grid from the rows and cells).
struct ShapeChild {
    std::string id;
    Rect bounds;                       // relative to the parent's top-left corner
    const ResolvedStyle* style = nullptr;
    bool visible = true;              // false: not shown (a table cell that a merged cell covers)
    // The geometry's alternate bounds: the size of a table cell before a
    // merged cell grew over its neighbours.
    std::optional<Rect> alternateBounds;
    std::vector<ShapeChild> children;  // the next level (table cells of a row)
};

struct ShapePaintContext {
    const ResolvedStyle& style;
    // Image lookup for shape=image / shape=label (`image` style). nullptr:
    // images are not drawn (placeholder frame only).
    const ImageResolveContext* images = nullptr;
    // Lazily lists the cell's children (only table/tableRow call it).
    // Empty function: no children.
    std::function<std::vector<ShapeChild>()> children;
    // The cell is collapsed (swimlane/table paint only the title).
    bool collapsed = false;
    // Stencil lookup for shapes that draw stencil markers
    // (mxStencilRegistry.getStencil, e.g. the BPMN task markers). nullptr:
    // StencilRegistry::builtin().
    StencilRegistry* stencils = nullptr;
    // The cell value as plain text on one line, for shapes that paint it
    // themselves (VertexShape::paintsValue()); empty otherwise.
    std::string_view valueText{};
    // The value is being edited on screen: shapes that paint it skip it.
    bool valueHidden = false;
};

// One painter for a draw.io shape name.
class VertexShape {
public:
    virtual ~VertexShape() = default;

    // mxShape.paintVertexShape(c, x, y, w, h) (or mxStencil.drawShape): the
    // canvas is already transformed and configured by paintVertex(). The
    // painter issues fill/stroke calls and may change the state (e.g.
    // setShadow(false) after the background), within the save/restore
    // bracket of paintVertex().
    virtual void paint(Canvas& canvas, const ShapePaintContext& context, double x, double y, double w,
                       double h) const = 0;

    // True for shapes that paint the cell value themselves instead of
    // leaving it to the label (curvedText, whose style has noLabel=1). The
    // page view then provides CellState::valueText and the paint pipeline
    // passes it in ShapePaintContext::valueText.
    [[nodiscard]] virtual bool paintsValue() const { return false; }

    // True for shapes that implement `direction` themselves (stencils, via
    // computeAspect). Others get the bounds swapped for north/south
    // (mxShape.isPaintBoundsInverted) plus the direction rotation.
    [[nodiscard]] virtual bool handlesDirection() const { return false; }

    // mxShape.getLabelMargins(rect): insets of the label area as fractions
    // or absolute values exactly as the upstream shape returns them
    // (x = left, y = top, width = right, height = bottom). nullopt: none.
    [[nodiscard]] virtual std::optional<geometry::Insets> labelMargins(const ResolvedStyle& style,
                                                                       const Rect& rect) const;

    // mxShape.getLabelBounds(rect) (mxgraph/src/shape/mxShape.js:643):
    // applies labelMargins() with mxUtils.getDirectedBounds, honoring
    // direction and flips. Swimlane/table override it (title area,
    // mxSwimlane.getLabelBounds). Implemented in shape_pipeline.cpp.
    [[nodiscard]] virtual Rect labelBounds(const ResolvedStyle& style, const Rect& rect) const;

    // Fixed connection points of the shape (stencil <connections>, the
    // `constraints` of Shapes.js classes). Empty: the default points
    // (M4 decides). Fractions of the unrotated bounds.
    [[nodiscard]] virtual std::vector<geometry::ConnectionConstraint>
    connectionConstraints(const ResolvedStyle& /*style*/, double /*w*/, double /*h*/) const
    {
        return {};
    }
};

// mxShape.configureCanvas (mxgraph/src/shape/mxShape.js:1274) for `style`:
// alpha = opacity/100, fill/stroke alphas, shadow, dashed/fixDash and
// dashPattern, fill color or gradient (gradientColor over
// `gradientBounds` in gradientDirection), linecap/linejoin/miterlimit,
// stroke color, and strokeWidth. Shared by vertices and edges.
void configureCanvas(Canvas& canvas, const ResolvedStyle& style, const Rect& gradientBounds);

// mxShape.paint for a vertex (mxShape.js:1094):
//  save(); if !shape.handlesDirection() and direction is north/south, swap
//  w/h around the center; rotate(rotation + direction angle (north 270,
//  west 180, south 90), flipH, flipV, center); configureCanvas(); then
//  shape.paint(); restore().
// Zero or negative width/height paint nothing.
void paintVertex(Canvas& canvas, const VertexShape& shape, const ShapePaintContext& context, const Rect& bounds);

// Native shapes by draw.io name (the `shape` style value).
class ShapeRegistry {
public:
    // Every native painter (see docs/design/render.md, "Native shapes").
    // Built once, thread-safe, immutable.
    [[nodiscard]] static const ShapeRegistry& builtin();

    ShapeRegistry();
    ~ShapeRegistry();
    ShapeRegistry(ShapeRegistry&&) noexcept;
    ShapeRegistry& operator=(ShapeRegistry&&) noexcept;

    // nullptr when the name is not a native shape (the scene then tries
    // stencils, then fallback()).
    [[nodiscard]] const VertexShape* find(std::string_view name) const;
    // Painter for unknown shapes: the rectangle (fill + stroke from the
    // style) plus a small marker in the top-right corner (an 8x8 folded
    // corner outline at 50% stroke alpha) so unsupported shapes stay
    // visible and recognizable. Never fails.
    [[nodiscard]] const VertexShape& fallback() const;

    // True for a shape name draw.io can draw but Qraw cannot yet (a stencil
    // name or a painter that is not ported): such a vertex gets fallback().
    // Any other unregistered name is drawn by draw.io as the default
    // rectangle (mxCellRenderer.getShapeConstructor), so it gets "rectangle".
    [[nodiscard]] static bool isUpstreamOnly(std::string_view name);

    void add(std::string name, std::unique_ptr<VertexShape> shape);
    [[nodiscard]] std::vector<std::string> names() const; // sorted

private:
    std::unordered_map<std::string, std::unique_ptr<VertexShape>> shapes_;
    std::unique_ptr<VertexShape> fallback_;
};

} // namespace qraw::render
