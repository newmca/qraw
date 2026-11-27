// Port of mxgraph/src/shape/mxShape.js (draw.io, Apache-2.0), modified:
// paint (:1094), configureCanvas (:1274), updateTransform (:1372),
// getShapeRotation (:2011), getLabelBounds (:643) and
// mxUtils.getDirectedBounds (mxgraph/src/util/mxUtils.js:3320).

#include "qraw/render/shape.h"

#include <algorithm>
#include <cmath>

namespace qraw::render {

namespace {

using geometry::Direction;
using geometry::Insets;

bool isVertical(Direction d) { return d == Direction::North || d == Direction::South; }

// mxShape.getShapeRotation: the direction adds to the style rotation.
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

bool finite(const Rect& r)
{
    return std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.width) && std::isfinite(r.height);
}

// Math.round(Math.max(0, Math.min(limit, v))); NaN counts as 0.
double clampMargin(double v, double limit)
{
    if (std::isnan(v))
        return 0;
    return std::floor(std::max(0.0, std::min(limit, v)) + 0.5);
}

// mxUtils.getDirectedBounds(rect, m, style, flipH, flipV). `m` follows the
// upstream mxRectangle layout: x = left, y = top, width = right,
// height = bottom (geometry::Insets holds the same values by name).
Rect directedBounds(const Rect& rect, const Insets& margins, Direction d, bool flipH, bool flipV)
{
    double left = clampMargin(margins.left, rect.width);
    double top = clampMargin(margins.top, rect.height);
    double right = clampMargin(margins.right, rect.width);
    double bottom = clampMargin(margins.bottom, rect.height);

    const bool vertical = isVertical(d);
    if ((flipV && vertical) || (flipH && !vertical))
        std::swap(left, right);
    if ((flipH && vertical) || (flipV && !vertical))
        std::swap(top, bottom);

    // m2: the margins as seen from the unrotated shape.
    double m2Left = left;
    double m2Top = top;
    double m2Right = right;
    double m2Bottom = bottom;
    switch (d) {
    case Direction::South:
        m2Top = left;
        m2Left = bottom;
        m2Right = top;
        m2Bottom = right;
        break;
    case Direction::West:
        m2Top = bottom;
        m2Left = right;
        m2Right = left;
        m2Bottom = top;
        break;
    case Direction::North:
        m2Top = right;
        m2Left = top;
        m2Right = bottom;
        m2Bottom = left;
        break;
    case Direction::East:
        break;
    }
    return {rect.x + m2Left, rect.y + m2Top, rect.width - m2Right - m2Left, rect.height - m2Bottom - m2Top};
}

} // namespace

std::optional<geometry::Insets> VertexShape::labelMargins(const ResolvedStyle&, const Rect&) const
{
    return std::nullopt;
}

// mxShape.getLabelBounds. The branches for vertical text (state.text) are
// left out: label layout handles `horizontal=0` itself.
Rect VertexShape::labelBounds(const ResolvedStyle& style, const Rect& rect) const
{
    const std::optional<Insets> margins = labelMargins(style, rect);
    if (!margins)
        return rect;
    return directedBounds(rect, *margins, style.direction(), style.flipH(), style.flipV());
}

void configureCanvas(Canvas& c, const ResolvedStyle& style, const Rect& gradientBounds)
{
    c.setAlpha(style.opacity() / 100);
    c.setFillAlpha(style.fillOpacity() / 100);
    c.setStrokeAlpha(style.strokeOpacity() / 100);
    c.setShadow(style.shadow());

    c.setDashed(style.dashed(), style.fixDash());
    if (!style.dashPattern().empty())
        c.setDashPattern(style.dashPattern());

    const std::optional<QColor> fill = style.fillColor();
    const std::optional<QColor> gradient = style.gradientColor();
    if (fill && gradient)
        c.setGradient(*fill, *gradient, gradientBounds, parseGradientDirection(style.gradientDirection()));
    else
        c.setFillColor(fill);

    if (style.has("linecap"))
        c.setLineCap(parseLineCap(style.lineCap()));
    if (style.has("linejoin"))
        c.setLineJoin(parseLineJoin(style.lineJoin()));
    if (style.has("miterlimit"))
        c.setMiterLimit(style.number("miterlimit", 10));

    c.setStrokeColor(style.strokeColor());
    c.setStrokeWidth(style.strokeWidth());
}

void paintVertex(Canvas& c, const VertexShape& shape, const ShapePaintContext& context, const Rect& bounds)
{
    if (!finite(bounds) || bounds.width <= 0 || bounds.height <= 0)
        return;

    const ResolvedStyle& style = context.style;
    const Direction direction = style.direction();
    double x = bounds.x;
    double y = bounds.y;
    double w = bounds.width;
    double h = bounds.height;

    // mxShape.isPaintBoundsInverted: shapes without their own direction
    // handling are painted in swapped bounds and turned by the rotation.
    if (!shape.handlesDirection() && isVertical(direction)) {
        const double t = (w - h) / 2;
        x += t;
        y -= t;
        std::swap(w, h);
    }

    // mxShape.apply swaps the flips for north/south.
    bool flipH = style.flipH();
    bool flipV = style.flipV();
    if (isVertical(direction))
        std::swap(flipH, flipV);

    double rotation = style.rotation() + directionAngle(direction);
    if (!std::isfinite(rotation))
        rotation = 0;

    c.save();
    try {
        if (rotation != 0 || flipH || flipV)
            c.rotate(rotation, flipH, flipV, x + w / 2, y + h / 2);
        configureCanvas(c, style, {x, y, w, h});
        shape.paint(c, context, x, y, w, h);
    } catch (...) {
        // Painting never throws: a failing painter leaves the rest of the
        // drawing intact.
    }
    c.restore();
}

} // namespace qraw::render
