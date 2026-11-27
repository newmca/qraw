#pragma once

// Label placement: where a cell's text box goes, as draw.io computes
// mxText.bounds. The result is exactly what mxAbstractCanvas2D.text(x, y,
// w, h, ..., align, valign, ..., rotation) receives, so canvas backends can
// implement draw.io's text semantics on top of it (render/text.h).

#include "qraw/core/geometry.h"
#include "qraw/geometry/path.h"

#include <functional>
#include <optional>
#include <string_view>

namespace qraw::geometry {

enum class HAlign { Left, Center, Right };
enum class VAlign { Top, Middle, Bottom };

// "left"/"center"/"right"; anything else yields `fallback`.
[[nodiscard]] constexpr HAlign parseHAlign(std::string_view value, HAlign fallback)
{
    if (value == "left")
        return HAlign::Left;
    if (value == "center")
        return HAlign::Center;
    if (value == "right")
        return HAlign::Right;
    return fallback;
}

// "top"/"middle"/"bottom"; anything else yields `fallback`.
[[nodiscard]] constexpr VAlign parseVAlign(std::string_view value, VAlign fallback)
{
    if (value == "top")
        return VAlign::Top;
    if (value == "middle")
        return VAlign::Middle;
    if (value == "bottom")
        return VAlign::Bottom;
    return fallback;
}

// `overflow` style. Default Visible.
enum class Overflow { Visible, Hidden, Fill, Width, Block };

[[nodiscard]] constexpr Overflow parseOverflow(std::string_view value)
{
    if (value == "hidden")
        return Overflow::Hidden;
    if (value == "fill")
        return Overflow::Fill;
    if (value == "width")
        return Overflow::Width;
    if (value == "block")
        return Overflow::Block;
    return Overflow::Visible;
}

// The label-related style values, read by the caller from the resolved
// style (render::ResolvedStyle::labelParams() fills this).
struct LabelParams {
    HAlign align = HAlign::Center;                 // `align`
    VAlign verticalAlign = VAlign::Middle;         // `verticalAlign`
    HAlign labelPosition = HAlign::Center;         // `labelPosition`
    VAlign verticalLabelPosition = VAlign::Middle; // `verticalLabelPosition`
    // Raw style values; the mxText constructor combines them
    // (spacingTop = spacing + spacingTop, ...; mxgraph/src/shape/mxText.js
    // constructor) and draw.io adds baseSpacingTop = 5 and
    // baseSpacingBottom = 1 (js/grapheditor/Graph.js:212).
    double spacing = 2;
    double spacingTop = 0;
    double spacingLeft = 0;
    double spacingBottom = 0;
    double spacingRight = 0;
    std::optional<double> labelWidth;              // `labelWidth`
    bool horizontal = true;                        // `horizontal` (false: vertical text)
    double rotation = 0;                           // vertex `rotation` (degrees)
    Overflow overflow = Overflow::Visible;         // `overflow`
    bool blockSpacing = false;                     // `blockSpacing=1`
    // Not a style value: the vertex geometry <mxPoint as="offset"> of a
    // non-relative vertex, which shifts its label (state.absoluteOffset in
    // mxGraphView.updateCellState). Set by the caller; zero otherwise.
    Point offset;
};

// mxText.bounds plus the text placement attributes, in page coordinates.
struct LabelBox {
    // Anchor: the alignment point (for align=center/valign=middle the center
    // of the box, for left/top its top-left corner, ...).
    double x = 0;
    double y = 0;
    // Box size. 0 means unbounded in that direction (edge labels without
    // geometry size, or vertices with zero size): the text is laid out
    // around the anchor without wrapping.
    double width = 0;
    double height = 0;
    HAlign align = HAlign::Center;
    VAlign verticalAlign = VAlign::Middle;
    // Text rotation in degrees around the anchor (cell rotation, -90 extra
    // for horizontal=0, mxText.getTextRotation).
    double rotation = 0;

    // The box as a rectangle (anchor and alignment applied, unrotated).
    [[nodiscard]] Rect rect() const;
    friend bool operator==(const LabelBox&, const LabelBox&) = default;
};

// mxShape.getLabelBounds hook: given the cell's (possibly inverted) bounds
// returns the area the shape reserves for its label (label margins,
// swimlane title, ...). Empty function = identity.
using ShapeLabelBoundsFn = std::function<Rect(const Rect&)>;

// Vertex labels: mxGraphView.updateVertexLabelOffset
// (mxgraph/src/view/mxGraphView.js:2817), mxCellRenderer.getLabelBounds
// (mxgraph/src/view/mxCellRenderer.js:1313) and rotateLabelBounds (:1455),
// with mxText.getSpacing (mxgraph/src/shape/mxText.js:1284) and
// isPaintBoundsInverted (vertical text). `cell` is the vertex's absolute
// bounds. The shape hook applies only when labelPosition and
// verticalLabelPosition are both centered, as upstream.
[[nodiscard]] LabelBox layoutVertexLabel(const Rect& cell, const LabelParams& params,
                                         const ShapeLabelBoundsFn& shapeLabelBounds = {});

// Edge labels: `anchor` is edgeLabelAnchor() (polyline.h); `size` is the
// edge geometry width/height (normally 0x0, unbounded). Spacing is applied
// as in mxCellRenderer.getLabelBounds for edges.
[[nodiscard]] LabelBox layoutEdgeLabel(Point anchor, double width, double height, const LabelParams& params);

// Deterministic, font-independent estimate of the painted text extent, used
// for export bounds and cheap culling (never for drawing): every line is
// `0.6 * fontSize` per character wide (0.65 when bold), lines are
// `1.2 * fontSize` high. With a bounded box width and wrapping on, lines
// wrap at that width. The result is the rotated bounding box in page
// coordinates, positioned per box alignment.
struct TextExtentInput {
    int lineCount = 1;          // lines after explicit breaks (\n, <br>, block elements)
    int longestLine = 0;        // characters in the longest line
    double fontSize = 12;
    bool bold = false;
    bool wrap = false;          // whiteSpace=wrap
};
[[nodiscard]] Rect estimateTextExtent(const LabelBox& box, const TextExtentInput& text);

} // namespace qraw::geometry
