// shape=curvedText ("Curved Text" in the Misc palette): a rectangle that also
// writes the cell value along a curve.
//
// Port of CurvedTextShape in js/grapheditor/Shapes.js (draw.io, Apache-2.0),
// modified: the curve and the text are handed to Canvas::textOnPath instead of
// being written into the SVG DOM, so every backend draws it. The curve is
// computed in the unscaled coordinates of the shape (upstream works in screen
// pixels, which only matters for the "nearly straight" threshold of 0.5).
// Not ported: the arcStartY / arcMidY / arcEndY handles (KNOWN_ISSUES.md) and
// the autosize that Graph.getPreferredSizeForCell adds for the shape.

#include "shapes_common.h"

#include <cmath>
#include <numbers>

namespace qraw::render::detail {

namespace {

constexpr double kDefaultArcStartY = 25; // CurvedTextShape.prototype.arcStartY
constexpr double kDefaultArcMidY = -25;  // arcMidY: offset of the middle point from the chord
constexpr double kDefaultArcEndY = 25;
constexpr double kStraightSagitta = 0.5; // below this the "round" curve is a line

struct Curve {
    geometry::Path path;
    double length = 0;
};

[[nodiscard]] double distance(geometry::Point a, geometry::Point b)
{
    return std::hypot(b.x - a.x, b.y - a.y);
}

// The curve through p0, p1 (the middle control point) and p2.
// curveType=round: the circular arc through the three points, a line when the
// sagitta is below kStraightSagitta; any other value: a quadratic Bezier with
// p1 as its control point.
[[nodiscard]] Curve buildCurve(bool round, geometry::Point p0, geometry::Point p1, geometry::Point p2)
{
    Curve curve;
    curve.path.moveTo(p0);
    if (!round) {
        curve.path.quadTo(p1, p2);
        // Upstream's length estimate: half of the control polygon plus the chord.
        curve.length = (distance(p0, p1) + distance(p1, p2) + distance(p0, p2)) / 2;
        return curve;
    }

    const double chord = distance(p0, p2);
    const geometry::Point chordMid{(p0.x + p2.x) / 2, (p0.y + p2.y) / 2};
    const double sagitta = distance(p1, chordMid);
    if (sagitta < kStraightSagitta) {
        curve.path.lineTo(p2);
        curve.length = chord;
        return curve;
    }

    const double halfChord = chord / 2;
    const double radius = halfChord * halfChord / (2 * sagitta) + sagitta / 2;
    // The arc bends towards the middle point: the side of the chord it is on.
    const double cross = (p2.x - p0.x) * (p1.y - p0.y) - (p2.y - p0.y) * (p1.x - p0.x);
    const bool sweep = cross <= 0;
    // A sagitta beyond half the chord means more than a half circle.
    const bool largeArc = sagitta > halfChord;
    curve.path.arcTo(radius, radius, 0, largeArc, sweep, p2);

    double theta = 2 * std::asin(std::min(1.0, halfChord / radius));
    if (largeArc)
        theta = 2 * std::numbers::pi - theta;
    curve.length = radius * theta;
    return curve;
}

class CurvedTextShape final : public RectangleShape {
public:
    [[nodiscard]] bool paintsValue() const override { return true; }

protected:
    // CurvedTextShape.paintForeground: the rectangle's own foreground (none),
    // then the value along the curve unless it is being edited.
    void paintForeground(Canvas& c, const ShapePaintContext& context, double x, double y, double w,
                         double h) const override
    {
        if (context.valueHidden || context.valueText.empty())
            return;
        const ResolvedStyle& style = context.style;
        const double fontSize = style.fontSize();
        const double inset = fontSize / 2; // keeps the text inside the bounds

        const double startY = style.number("arcStartY", kDefaultArcStartY);
        const double endY = style.number("arcEndY", kDefaultArcEndY);
        // The curve has its natural shape whatever the height; the middle is
        // clamped to the default range, or a little more when the cell has
        // room on both sides of the chord.
        const double chordMidY = (startY + endY) / 2;
        const double maxMid = std::max(std::abs(kDefaultArcMidY), std::min(chordMidY, h - chordMidY));
        const double midOffset = std::clamp(style.number("arcMidY", kDefaultArcMidY), -maxMid, maxMid);
        const double midY = chordMidY + midOffset;

        // verticalAlign places the curve's bounding box in the cell.
        const double curveTop = std::min({startY, endY, midY});
        const double curveBottom = std::max({startY, endY, midY});
        const std::string_view verticalAlign = style.string("verticalAlign", "middle");
        double yOffset = (h - (curveBottom - curveTop)) / 2 - curveTop;
        if (verticalAlign == "top")
            yOffset = inset - curveTop;
        else if (verticalAlign == "bottom")
            yOffset = (h - inset) - curveBottom;

        const double left = x + inset;
        const double right = x + w - inset;
        const Curve curve = buildCurve(style.string("curveType", "round") == "round", {left, y + startY + yOffset},
                                       {(left + right) / 2, y + midY + yOffset}, {right, y + endY + yOffset});

        PathText text;
        text.path = curve.path;
        text.text = std::string(context.valueText);
        text.align = geometry::parseHAlign(style.string("align"), geometry::HAlign::Center);
        text.textLength = curve.length; // upstream always spreads the text over the whole curve
        text.fontFamily = QString::fromUtf8(style.fontFamily().data(), static_cast<qsizetype>(style.fontFamily().size()));
        text.fontSize = fontSize;
        text.fontStyle = style.fontStyle();
        text.fontColor = style.fontColor().value_or(QColor(0, 0, 0));
        text.opacity = style.textOpacity() / 100;
        c.textOnPath(text);
    }
};

} // namespace

void registerCurvedTextShape(ShapeRegistry& registry)
{
    registry.add("curvedText", std::make_unique<CurvedTextShape>());
}

} // namespace qraw::render::detail
