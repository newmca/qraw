// mxgraph.arrows2.arrow and mxgraph.arrows2.twoWayArrow, the block arrows of
// the Arrows palette that draw.io implements as JavaScript classes (the
// other arrows2 shapes are not ported, see KNOWN_ISSUES).
//
// Port of shapes/mxArrows.js (draw.io, Apache-2.0), modified.

#include "shapes_common.h"

namespace qraw::render::detail {

namespace {

using geometry::Direction;

// mxShapeArrows2Arrow (:11): dx = head length, dy = shaft width (fraction),
// notch = tail notch; headCrossline and tailCrossline draw the joints.
class Arrow2Shape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        c.translate(x, y);
        const double dy = h * 0.5 * clampTo(style.number("dy", 0.5), 0, 1);
        const double dx = clampTo(style.number("dx", 0.5), 0, w);
        const double notch = clampTo(style.number("notch", 0), 0, w);

        c.begin();
        c.moveTo(0, dy);
        c.lineTo(w - dx, dy);
        c.lineTo(w - dx, 0);
        c.lineTo(w, h * 0.5);
        c.lineTo(w - dx, h);
        c.lineTo(w - dx, h - dy);
        c.lineTo(0, h - dy);
        c.lineTo(notch, h * 0.5);
        c.close();
        c.fillAndStroke();

        c.setShadow(false);
        if (style.flag("headCrossline", false)) {
            c.begin();
            c.moveTo(w - dx, dy);
            c.lineTo(w - dx, h - dy);
            c.stroke();
        }
        if (style.flag("tailCrossline", false)) {
            c.begin();
            c.moveTo(notch, dy);
            c.lineTo(notch, h - dy);
            c.stroke();
        }
    }

    // mxShapeArrows2Arrow.getLabelBounds (:106): the label stays in the shaft.
    [[nodiscard]] Rect labelBounds(const ResolvedStyle& style, const Rect& rect) const override
    {
        if (!boundedLabel(style))
            return rect;

        const double w = rect.width;
        const double h = rect.height;
        Direction direction = style.direction();
        if (style.flipH()) {
            if (direction == Direction::West)
                direction = Direction::East;
            else if (direction == Direction::East)
                direction = Direction::West;
        }
        if (style.flipV()) {
            if (direction == Direction::North)
                direction = Direction::South;
            else if (direction == Direction::South)
                direction = Direction::North;
        }

        const bool vertical = direction == Direction::North || direction == Direction::South;
        const double dy = (vertical ? w : h) * 0.5 * clampTo(style.number("dy", 0.5), 0, 1);
        const double dx = clampTo(style.number("dx", 0.5), 0, vertical ? h : w);
        switch (direction) {
        case Direction::East:
            return {rect.x, rect.y + dy, w - dx, h - 2 * dy};
        case Direction::West:
            return {rect.x + dx, rect.y + dy, w - dx, h - 2 * dy};
        case Direction::North:
            return {rect.x + dy, rect.y + dx, w - 2 * dy, h - dx};
        case Direction::South:
            break;
        }
        return {rect.x + dy, rect.y, w - 2 * dy, h - dx};
    }
};

// mxShapeArrows2TwoWayArrow (:207)
class TwoWayArrowShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        c.translate(x, y);
        const double dy = h * 0.5 * clampTo(style.number("dy", 0.5), 0, 1);
        const double dx = clampTo(style.number("dx", 0.5), 0, w);

        c.begin();
        c.moveTo(dx, dy);
        c.lineTo(w - dx, dy);
        c.lineTo(w - dx, 0);
        c.lineTo(w, h * 0.5);
        c.lineTo(w - dx, h);
        c.lineTo(w - dx, h - dy);
        c.lineTo(dx, h - dy);
        c.lineTo(dx, h);
        c.lineTo(0, h * 0.5);
        c.lineTo(dx, 0);
        c.close();
        c.fillAndStroke();
    }

    // mxShapeArrows2TwoWayArrow.getLabelBounds (:253)
    [[nodiscard]] Rect labelBounds(const ResolvedStyle& style, const Rect& rect) const override
    {
        if (!boundedLabel(style))
            return rect;
        const double w = rect.width;
        const double h = rect.height;
        const Direction direction = style.direction();
        const bool vertical = direction == Direction::North || direction == Direction::South;
        const double dy = (vertical ? w : h) * 0.5 * clampTo(style.number("dy", 0.5), 0, 1);
        const double dx = clampTo(style.number("dx", 0.5), 0, vertical ? h : w);
        if (vertical)
            return {rect.x + dy, rect.y + dx, w - 2 * dy, h - 2 * dx};
        return {rect.x + dx, rect.y + dy, w - 2 * dx, h - 2 * dy};
    }
};

} // namespace

void registerArrowShapes(ShapeRegistry& registry)
{
    registry.add("mxgraph.arrows2.arrow", std::make_unique<Arrow2Shape>());
    registry.add("mxgraph.arrows2.twoWayArrow", std::make_unique<TwoWayArrowShape>());
}

} // namespace qraw::render::detail
