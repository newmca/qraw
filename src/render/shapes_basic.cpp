// Basic native shapes: rectangle, label, ellipse, doubleEllipse, rhombus,
// triangle, hexagon, line, plus the paint-nothing shapes (text, connector).
//
// Port of mxgraph/src/shape/mxRectangleShape.js, mxLabel.js, mxEllipse.js,
// mxDoubleEllipse.js, mxRhombus.js, mxTriangle.js, mxLine.js and
// js/grapheditor/Shapes.js (HexagonShape, rhombus/ellipse overrides)
// (draw.io, Apache-2.0), modified.

#include "shapes_common.h"

namespace qraw::render::detail {

namespace {

constexpr double kDefaultImageSize = 24; // mxConstants.DEFAULT_IMAGESIZE

// Shapes whose label is drawn by the scene and that paint nothing themselves
// (`shape=text`, `shape=connector`).
class NothingShape final : public VertexShape {
public:
    void paint(Canvas&, const ShapePaintContext&, double, double, double, double) const override {}
};

// mxLabel (mxLabel.js): a rectangle with an optional image. Indicator
// shapes and indicator images (`indicatorShape`, `indicatorImage`) are set
// from the graph by the cell renderer upstream and are not painted.
class LabelShape final : public RectangleShape {
protected:
    void paintForeground(Canvas& c, const ShapePaintContext& context, double x, double y, double w,
                         double h) const override
    {
        paintImage(c, context, x, y, w, h); // mxLabel.js:118
    }

private:
    // mxLabel.paintImage (:131)
    static void paintImage(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h)
    {
        const std::string_view source = context.style.image();
        if (source.empty() || !context.images)
            return;
        const std::optional<ImageData> image = cachedStyleImage(context.style, *context.images);
        if (!image)
            return;
        const Rect b = imageBounds(context.style, x, y, w, h);
        c.image(b.x, b.y, b.width, b.height, *image, false, false, false);
    }

    // mxLabel.getImageBounds (:146)
    static Rect imageBounds(const ResolvedStyle& style, double x, double y, double w, double h)
    {
        const std::string_view align = style.string("imageAlign", "left");
        const std::string_view valign = style.string("imageVerticalAlign", "middle");
        const double width = style.imageWidth(kDefaultImageSize);
        const double height = style.imageHeight(kDefaultImageSize);
        const double spacing = style.number("spacing", 2) + 5;

        if (align == "center")
            x += (w - width) / 2;
        else if (align == "right")
            x += w - width - spacing;
        else
            x += spacing;

        if (valign == "top")
            y += spacing;
        else if (valign == "bottom")
            y += h - height - spacing;
        else
            y += (h - height) / 2;
        return {x, y, width, height};
    }
};

// mxEllipse.paintVertexShape (mxEllipse.js:44) with paintCenter. The glass
// override of Shapes.js:3292 is not painted.
class EllipseShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        c.ellipse(x, y, w, h);
        c.fillAndStroke();
        paintCenter(c, context.style, x, y, w, h);
    }

private:
    // The filled circle in the middle (`centerRadius`, UML final state).
    static void paintCenter(Canvas& c, const ResolvedStyle& style, double x, double y, double w, double h)
    {
        const double r = std::min({std::max(0.0, style.number("centerRadius", 0)), w / 2, h / 2});
        if (!(r > 0))
            return;
        const std::optional<QColor> color = style.has("centerColor") ? style.color("centerColor") : style.strokeColor();
        if (!color)
            return;
        c.setShadow(false);
        c.setFillColor(color);
        c.ellipse(x + w / 2 - r, y + h / 2 - r, 2 * r, 2 * r);
        const std::optional<QColor> stroke = style.strokeColor();
        if (style.flag("centerStroke", false) && stroke) {
            c.setStrokeColor(stroke);
            c.fillAndStroke();
        } else {
            c.fill();
        }
    }
};

// mxDoubleEllipse (mxDoubleEllipse.js:65, :76, getLabelBounds).
class DoubleEllipseShape final : public BackgroundForegroundShape {
public:
    [[nodiscard]] Rect labelBounds(const ResolvedStyle& style, const Rect& rect) const override
    {
        const double margin = innerMargin(style, rect.width, rect.height);
        return {rect.x + margin, rect.y + margin, rect.width - 2 * margin, rect.height - 2 * margin};
    }

protected:
    void paintBackground(Canvas& c, const ShapePaintContext&, double x, double y, double w, double h) const override
    {
        c.ellipse(x, y, w, h);
        c.fillAndStroke();
    }
    void paintForeground(Canvas& c, const ShapePaintContext& context, double x, double y, double w,
                         double h) const override
    {
        const double margin = innerMargin(context.style, w, h);
        x += margin;
        y += margin;
        w -= 2 * margin;
        h -= 2 * margin;
        if (w > 0 && h > 0)
            c.ellipse(x, y, w, h);
        c.stroke();
    }

private:
    static double innerMargin(const ResolvedStyle& style, double w, double h)
    {
        return style.number("margin", std::min(3 + style.strokeWidth(), std::min(w / 5, h / 5)));
    }
};

// mxRhombus (mxRhombus.js:54) with the `double=1` variant of Shapes.js:3196.
class RhombusShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        paintRhombus(c, style, x, y, w, h);
        if (!style.flag("double", false))
            return;
        const double margin = doubleMargin(style);
        x += margin;
        y += margin;
        w -= 2 * margin;
        h -= 2 * margin;
        if (w > 0 && h > 0) {
            c.setShadow(false);
            paintRhombus(c, style, x, y, w, h);
        }
    }

    [[nodiscard]] Rect labelBounds(const ResolvedStyle& style, const Rect& rect) const override
    {
        if (!style.flag("double", false))
            return rect;
        const double margin = doubleMargin(style);
        return {rect.x + margin, rect.y + margin, rect.width - 2 * margin, rect.height - 2 * margin};
    }

private:
    static double doubleMargin(const ResolvedStyle& style)
    {
        return std::max(2.0, style.strokeWidth() + 1) * 2 + style.number("margin", 0);
    }

    static void paintRhombus(Canvas& c, const ResolvedStyle& style, double x, double y, double w, double h)
    {
        const double hw = w / 2;
        const double hh = h / 2;
        const Point points[] = {{x + hw, y}, {x + w, y + hh}, {x + hw, y + h}, {x, y + hh}};
        c.begin();
        addPoints(c, style, points);
        c.fillAndStroke();
    }
};

// mxTriangle.redrawPath (mxTriangle.js:39)
class TriangleShape final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const Point points[] = {{0, 0}, {w, 0.5 * h}, {0, h}};
        addPoints(c, style, points);
    }
};

// HexagonShape (Shapes.js:3140)
class HexagonShape final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const double s = style.fixedSize() ? clampTo(style.size(20), 0, w * 0.5) : w * clampTo(style.size(0.25), 0, 1);
        const Point points[] = {{s, 0}, {w - s, 0}, {w, 0.5 * h}, {w - s, h}, {s, h}, {0, 0.5 * h}};
        addPoints(c, style, points);
    }
};

// mxLine.paintVertexShape (mxLine.js:51). Upstream paints a vertical line
// only when `vertical` is set in code, which the cell renderer never does:
// direction=north/south turns the horizontal line through the paint pipeline.
class LineShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext&, double x, double y, double w, double h) const override
    {
        const double mid = y + h / 2;
        c.begin();
        c.moveTo(x, mid);
        c.lineTo(x + w, mid);
        c.stroke();
    }
};

} // namespace

void RectangleShape::paintBackground(Canvas& c, const ShapePaintContext& context, double x, double y, double w,
                                     double h) const
{
    if (context.style.rounded()) {
        const double r = rectangleArcSize(context.style, w, h);
        c.roundrect(x, y, w, h, r, r);
    } else {
        c.rect(x, y, w, h);
    }
    c.fillAndStroke();
}

void registerBasicShapes(ShapeRegistry& registry)
{
    registry.add("rectangle", std::make_unique<RectangleShape>());
    registry.add("label", std::make_unique<LabelShape>());
    registry.add("ellipse", std::make_unique<EllipseShape>());
    registry.add("doubleEllipse", std::make_unique<DoubleEllipseShape>());
    registry.add("rhombus", std::make_unique<RhombusShape>());
    registry.add("triangle", std::make_unique<TriangleShape>());
    registry.add("hexagon", std::make_unique<HexagonShape>());
    registry.add("line", std::make_unique<LineShape>());
    registry.add("text", std::make_unique<NothingShape>());
    registry.add("connector", std::make_unique<NothingShape>());
}

} // namespace qraw::render::detail
