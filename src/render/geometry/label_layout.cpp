// Port of mxgraph/src/view/mxCellRenderer.js (getLabelBounds, rotateLabelBounds),
// mxgraph/src/view/mxGraphView.js (updateVertexLabelOffset), mxgraph/src/shape/mxText.js
// (getSpacing, isPaintBoundsInverted) and mxgraph/src/util/mxUtils.js
// (getAlignmentAsPoint) (draw.io, Apache-2.0), modified. View scale 1,
// translate 0; no DOM text measuring.

#include "qraw/geometry/label_layout.h"

#include <algorithm>
#include <cmath>

namespace qraw::geometry {

namespace {

// draw.io's mxText.prototype.baseSpacing* (js/grapheditor/Graph.js:212).
constexpr double kBaseSpacingTop = 5;
constexpr double kBaseSpacingBottom = 1;
constexpr double kBaseSpacingLeft = 0;
constexpr double kBaseSpacingRight = 0;

// mxText.verticalTextRotation
constexpr double kVerticalTextRotation = -90;

// The mxText spacing fields: the mxText constructor adds `spacing` to every
// directional spacing.
struct TextSpacing {
    double top, right, bottom, left;
};

TextSpacing textSpacing(const LabelParams& p)
{
    return {p.spacing + p.spacingTop, p.spacing + p.spacingRight, p.spacing + p.spacingBottom,
            p.spacing + p.spacingLeft};
}

// mxUtils.getAlignmentAsPoint: the fraction of the text box (negated) that
// the anchor sits away from its top-left corner.
Point alignmentAsPoint(HAlign align, VAlign valign)
{
    return {align == HAlign::Left ? 0.0 : (align == HAlign::Right ? -1.0 : -0.5),
            valign == VAlign::Top ? 0.0 : (valign == VAlign::Bottom ? -1.0 : -0.5)};
}

// mxText.getSpacing(noBase, margin): the shift of the anchor caused by the
// text spacing. The margin is derived from align/valign, so the branches of
// upstream's `margin.x == -0.5` and `align == center` checks coincide.
Point spacingOffset(const LabelParams& p, bool noBase)
{
    const TextSpacing s = textSpacing(p);
    double dx = 0;
    double dy = 0;

    if (p.align == HAlign::Center)
        dx = (s.left - s.right) / 2;
    else if (p.align == HAlign::Right)
        dx = -s.right - (noBase ? 0 : kBaseSpacingRight);
    else
        dx = s.left + (noBase ? 0 : kBaseSpacingLeft);

    if (p.verticalAlign == VAlign::Middle)
        dy = (s.top - s.bottom) / 2;
    else if (p.verticalAlign == VAlign::Bottom)
        dy = -s.bottom - (noBase ? 0 : kBaseSpacingBottom);
    else
        dy = s.top + (noBase ? 0 : kBaseSpacingTop);

    return {dx, dy};
}

// mxShape.getTextRotation: the shape rotation, plus -90 for vertical text.
double textRotation(const LabelParams& p)
{
    return p.rotation + (p.horizontal ? 0.0 : kVerticalTextRotation);
}

} // namespace

Rect LabelBox::rect() const
{
    const double left = align == HAlign::Left ? x : (align == HAlign::Center ? x - width / 2 : x - width);
    const double top = verticalAlign == VAlign::Top ? y : (verticalAlign == VAlign::Middle ? y - height / 2 : y - height);
    return {left, top, width, height};
}

LabelBox layoutVertexLabel(const Rect& cell, const LabelParams& params, const ShapeLabelBoundsFn& shapeLabelBounds)
{
    const bool hCentered = params.labelPosition == HAlign::Center;
    const bool vCentered = params.verticalLabelPosition == VAlign::Middle;

    // mxGraphView.updateCellState / updateVertexLabelOffset: the absolute
    // offset starts with the geometry offset and is moved by label position.
    double offX = params.offset.x;
    double offY = params.offset.y;

    if (params.labelPosition == HAlign::Left) {
        offX -= params.labelWidth.value_or(cell.width);
    } else if (params.labelPosition == HAlign::Right) {
        offX += cell.width;
    } else if (params.labelWidth) {
        // Aligns the text block of the given width inside the vertex width.
        const double dx = params.align == HAlign::Center ? 0.5 : (params.align == HAlign::Right ? 1.0 : 0.0);
        if (dx != 0)
            offX -= (*params.labelWidth - cell.width) * dx;
    }

    if (params.verticalLabelPosition == VAlign::Top)
        offY -= cell.height;
    else if (params.verticalLabelPosition == VAlign::Bottom)
        offY += cell.height;

    // mxCellRenderer.getLabelBounds, vertex branch.
    const bool inverted = !params.horizontal; // mxText.isPaintBoundsInverted
    Rect bounds{offX, offY, 0, 0};
    if (inverted)
        std::swap(bounds.x, bounds.y);
    bounds.x += cell.x;
    bounds.y += cell.y;
    // Minimum of 1 fixes an alignment bug in HTML labels (as upstream).
    bounds.width = std::max(1.0, cell.width);
    bounds.height = std::max(1.0, cell.height);

    if (inverted) {
        // Rotates around the center of the cell.
        const double t = (cell.width - cell.height) / 2;
        bounds.x += t;
        bounds.y -= t;
        std::swap(bounds.width, bounds.height);
    }

    // The shape may reserve a part of its area for the label.
    if (shapeLabelBounds && hCentered && vCentered)
        bounds = shapeLabelBounds(bounds);

    // The labelWidth style overrides the label width.
    if (params.labelWidth)
        bounds.width = *params.labelWidth;

    // rotateLabelBounds: alignment, spacing and rotation.
    const Point m = alignmentAsPoint(params.align, params.verticalAlign);
    bounds.y -= m.y * bounds.height;
    bounds.x -= m.x * bounds.width;

    // The renderer's legacySpacing is on: overflow fill and width, and block
    // without blockSpacing, ignore the spacing.
    const bool ignoreSpacing = params.overflow == Overflow::Fill || params.overflow == Overflow::Width ||
                               (params.overflow == Overflow::Block && !params.blockSpacing);
    if (!ignoreSpacing) {
        const Point spacing = spacingOffset(params, params.blockSpacing);
        const TextSpacing s = textSpacing(params);
        bounds.x += spacing.x;
        bounds.y += spacing.y;
        bounds.width = std::max(0.0, bounds.width - ((hCentered && !params.labelWidth) ? s.left + s.right : 0));
        bounds.height = std::max(0.0, bounds.height - (vCentered ? s.top + s.bottom : 0));
    }

    LabelBox box;
    box.rotation = textRotation(params);

    // Only needed if rotated around another center than the anchor.
    const Point center{cell.x + cell.width / 2, cell.y + cell.height / 2};
    if (box.rotation != 0 && (bounds.x != center.x || bounds.y != center.y)) {
        const Point rotated = rotatePoint({bounds.x, bounds.y}, box.rotation, center);
        bounds.x = rotated.x;
        bounds.y = rotated.y;
    }

    box.x = bounds.x;
    box.y = bounds.y;
    box.width = bounds.width;
    box.height = bounds.height;
    box.align = params.align;
    box.verticalAlign = params.verticalAlign;
    return box;
}

// Edge labels (mxCellRenderer.getLabelBounds, edge branch). Upstream starts at
// the edge's absoluteOffset (our `anchor`), adds getSpacing(null, margin)
// (the base spacing applies: noBase is null) and takes the size from the
// edge geometry, clamped at 0. rotateLabelBounds is only called for vertices:
// there is no alignment shift of the bounds and no rotation around a center.
// The anchor stays the alignment point and mxText applies the margin when it
// paints, which is why LabelBox keeps (x, y) as the anchor plus spacing. The
// shape label hook is not applied (the connector shape has none), the
// labelWidth style overrides the width as upstream, and the text rotation is
// -90 for horizontal=0 (the edge has no rotation of its own).
LabelBox layoutEdgeLabel(Point anchor, double width, double height, const LabelParams& params)
{
    const Point spacing = spacingOffset(params, false);

    LabelBox box;
    box.x = anchor.x + spacing.x;
    box.y = anchor.y + spacing.y;
    box.width = std::max(0.0, width);
    box.height = std::max(0.0, height);
    if (params.labelWidth)
        box.width = *params.labelWidth;
    box.align = params.align;
    box.verticalAlign = params.verticalAlign;
    box.rotation = params.horizontal ? 0.0 : kVerticalTextRotation;
    return box;
}

Rect estimateTextExtent(const LabelBox& box, const TextExtentInput& text)
{
    const double charWidth = (text.bold ? 0.65 : 0.6) * text.fontSize;
    const double lineHeight = 1.2 * text.fontSize;

    double textWidth = text.longestLine * charWidth;
    double lines = std::max(1, text.lineCount);

    // Wraps the longest line at the box width.
    if (text.wrap && box.width > 0 && textWidth > box.width) {
        lines = lines - 1 + std::ceil(textWidth / box.width);
        textWidth = box.width;
    }
    const double textHeight = lines * lineHeight;

    // An unbounded direction (size 0) is laid out around the anchor, a bounded
    // one inside the box rectangle; both by the box alignment.
    const Rect area = box.rect();
    double left = 0;
    if (box.width > 0) {
        left = box.align == HAlign::Left ? area.x
                                         : (box.align == HAlign::Center ? area.x + (area.width - textWidth) / 2
                                                                        : area.x + area.width - textWidth);
    } else {
        left = box.align == HAlign::Left ? box.x : (box.align == HAlign::Center ? box.x - textWidth / 2 : box.x - textWidth);
    }
    double top = 0;
    if (box.height > 0) {
        top = box.verticalAlign == VAlign::Top
                  ? area.y
                  : (box.verticalAlign == VAlign::Middle ? area.y + (area.height - textHeight) / 2
                                                         : area.y + area.height - textHeight);
    } else {
        top = box.verticalAlign == VAlign::Top
                  ? box.y
                  : (box.verticalAlign == VAlign::Middle ? box.y - textHeight / 2 : box.y - textHeight);
    }

    const Rect result{left, top, textWidth, textHeight};
    if (box.rotation == 0)
        return result;

    // Rotates around the anchor.
    const Point anchor{box.x, box.y};
    const Point corners[4] = {
        rotatePoint({result.x, result.y}, box.rotation, anchor),
        rotatePoint({result.x + result.width, result.y}, box.rotation, anchor),
        rotatePoint({result.x + result.width, result.y + result.height}, box.rotation, anchor),
        rotatePoint({result.x, result.y + result.height}, box.rotation, anchor),
    };
    Rect bounds{corners[0].x, corners[0].y, 0, 0};
    for (int i = 1; i < 4; ++i)
        bounds = unite(bounds, {corners[i].x, corners[i].y, 0, 0});
    return bounds;
}

} // namespace qraw::geometry
