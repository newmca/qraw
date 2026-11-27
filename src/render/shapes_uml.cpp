// Native painters for the shapes that draw.io implements only in
// js/grapheditor/Shapes.js (no stencil exists for them): cube, folder,
// umlActor, umlLifeline, umlFrame, component, module, message, startState,
// endState, singleArrow, doubleArrow, datastore, offPageConnector,
// curlyBracket, crossbar, lineEllipse, ext, waypoint and dimension.
// (note2 and process2 live next to note and process in shapes_flowchart.cpp.)
//
// Port of js/grapheditor/Shapes.js (draw.io, Apache-2.0), modified. Each
// class names the upstream constructor and line. Connection constraints
// (the `constraints` of these classes) are not ported: the editor's default
// connection points apply.

#include "shapes_common.h"

#include <QColor>

namespace qraw::render::detail {

namespace {

using geometry::Insets;

// --- cube, folder, datastore ---------------------------------------------------

// CubeShape (Shapes.js:1134)
class CubeShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        const double s = clampTo(style.size(20), 0, std::min(w, h));
        const double op = clampTo(style.number("darkOpacity", 0), -1, 1);
        const double op2 = clampTo(style.number("darkOpacity2", 0), -1, 1);

        c.translate(x, y);
        c.begin();
        c.moveTo(0, 0);
        c.lineTo(w - s, 0);
        c.lineTo(w, s);
        c.lineTo(w, h);
        c.lineTo(s, h);
        c.lineTo(0, h - s);
        c.lineTo(0, 0);
        c.close();
        c.fillAndStroke();

        c.setShadow(false);
        if (op != 0) {
            c.setFillAlpha(std::abs(op));
            c.setFillColor(op < 0 ? QColor(255, 255, 255) : QColor(0, 0, 0));
            c.begin();
            c.moveTo(0, 0);
            c.lineTo(w - s, 0);
            c.lineTo(w, s);
            c.lineTo(s, s);
            c.close();
            c.fill();
        }
        if (op2 != 0) {
            c.setFillAlpha(std::abs(op2));
            c.setFillColor(op2 < 0 ? QColor(255, 255, 255) : QColor(0, 0, 0));
            c.begin();
            c.moveTo(0, 0);
            c.lineTo(s, s);
            c.lineTo(s, h);
            c.lineTo(0, h - s);
            c.close();
            c.fill();
        }

        c.begin();
        c.moveTo(s, h);
        c.lineTo(s, s);
        c.lineTo(0, 0);
        c.moveTo(s, s);
        c.lineTo(w, s);
        c.stroke();
    }

    [[nodiscard]] std::optional<Insets> labelMargins(const ResolvedStyle& style, const Rect&) const override
    {
        if (!boundedLabel(style))
            return std::nullopt;
        const double s = style.size(20);
        return margins(s, s, 0, 0);
    }
};

// FolderShape (Shapes.js:1717) and its label margins (Shapes.js:2309).
class FolderShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        c.translate(x, y);

        double dx = clampTo(style.number("tabWidth", 60), 0, w);
        const double dy = clampTo(style.number("tabHeight", 20), 0, h);
        const bool left = style.string("tabPosition", "right") == "left";
        const bool rounded = style.flag("rounded", false);
        const double arcSize = effectiveArcSize(style, w, h, dy);
        dx = std::max(dx, arcSize);
        dx = std::min(w - arcSize, dx);
        const double arc = rounded ? arcSize : 0; // arcSize itself drives the tab inset below

        c.begin();
        if (left) {
            c.moveTo(std::max(arc, 0.0), dy);
            c.lineTo(std::max(arc, 0.0), 0);
            c.lineTo(dx, 0);
            c.lineTo(dx, dy);
        } else {
            c.moveTo(w - dx, dy);
            c.lineTo(w - dx, 0);
            c.lineTo(w - std::max(arc, 0.0), 0);
            c.lineTo(w - std::max(arc, 0.0), dy);
        }

        if (rounded) {
            c.moveTo(0, arc + dy);
            c.arcTo(arc, arc, 0, false, true, arc, dy);
            c.lineTo(w - arc, dy);
            c.arcTo(arc, arc, 0, false, true, w, arc + dy);
            c.lineTo(w, h - arc);
            c.arcTo(arc, arc, 0, false, true, w - arc, h);
            c.lineTo(arc, h);
            c.arcTo(arc, arc, 0, false, true, 0, h - arc);
        } else {
            c.moveTo(0, dy);
            c.lineTo(w, dy);
            c.lineTo(w, h);
            c.lineTo(0, h);
        }
        c.close();
        c.fillAndStroke();

        c.setShadow(false);
        if (style.string("folderSymbol") == "triangle") {
            c.begin();
            c.moveTo(w - 30, dy + 20);
            c.lineTo(w - 20, dy + 10);
            c.lineTo(w - 10, dy + 20);
            c.close();
            c.stroke();
        }
    }

    [[nodiscard]] std::optional<Insets> labelMargins(const ResolvedStyle& style, const Rect& rect) const override
    {
        if (!boundedLabel(style))
            return std::nullopt;
        const double sizeY = style.number("tabHeight", 15);
        if (!style.flag("labelInHeader", false))
            return margins(0, std::min(rect.height, sizeY), 0, 0);

        const double sizeX = style.number("tabWidth", 15);
        const double arc = style.flag("rounded", false) ? effectiveArcSize(style, rect.width, rect.height, sizeY) : 0;
        if (style.string("tabPosition", "right") == "left")
            return margins(arc, 0, std::min(rect.width, rect.width - sizeX), std::min(rect.height, rect.height - sizeY));
        return margins(std::min(rect.width, rect.width - sizeX), 0, arc, std::min(rect.height, rect.height - sizeY));
    }

private:
    // The arc size before the `rounded` switch: `arcSize` (default 0.1) is a
    // fraction of the shorter side unless absoluteArcSize, capped by half the
    // width and half the body height.
    static double effectiveArcSize(const ResolvedStyle& style, double w, double h, double tabHeight)
    {
        double arcSize = style.number("arcSize", 0.1);
        if (!style.flag("absoluteArcSize", false))
            arcSize = std::min(w, h) * arcSize;
        return std::min({arcSize, w * 0.5, (h - tabHeight) * 0.5});
    }
};

// DataStoreShape (Shapes.js:1403)
class DataStoreShape final : public CylinderBase {
public:
    [[nodiscard]] std::optional<Insets> labelMargins(const ResolvedStyle& style, const Rect& rect) const override
    {
        return margins(0, 2.5 * std::min(rect.height / 2, jsRound(rect.height / 8) + style.strokeWidth() - 1), 0, 0);
    }

protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h, bool isForeground) const override
    {
        const double dy = std::min(h / 2, jsRound(h / 8) + style.strokeWidth() - 1);
        const bool filled = style.fillColor().has_value();

        if ((isForeground && filled) || (!isForeground && !filled)) {
            c.moveTo(0, dy);
            c.curveTo(0, 2 * dy, w, 2 * dy, w, dy);
            // Separate shapes for hit detection upstream: stroke each curve.
            if (!isForeground) {
                c.stroke();
                c.begin();
            }

            c.translate(0, dy / 2);
            c.moveTo(0, dy);
            c.curveTo(0, 2 * dy, w, 2 * dy, w, dy);
            if (!isForeground) {
                c.stroke();
                c.begin();
            }

            c.translate(0, dy / 2);
            c.moveTo(0, dy);
            c.curveTo(0, 2 * dy, w, 2 * dy, w, dy);
            if (!isForeground) {
                c.stroke();
                c.begin();
            }

            c.translate(0, -dy);
        }

        if (!isForeground) {
            c.moveTo(0, dy);
            c.curveTo(0, -dy / 3, w, -dy / 3, w, dy);
            c.lineTo(w, h - dy);
            c.curveTo(w, h + dy / 3, 0, h + dy / 3, 0, h - dy);
            c.close();
        }
    }
};

// --- UML ------------------------------------------------------------------------

// UmlActorShape (Shapes.js:3466)
class UmlActorShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext&, double x, double y, double w, double h) const override
    {
        c.translate(x, y);

        // Head
        c.ellipse(w / 4, 0, w / 2, h / 4);
        c.fillAndStroke();

        c.begin();
        c.moveTo(w / 2, h / 4);
        c.lineTo(w / 2, 2 * h / 3);
        // Arms
        c.moveTo(w / 2, h / 3);
        c.lineTo(0, h / 3);
        c.moveTo(w / 2, h / 3);
        c.lineTo(w, h / 3);
        // Legs
        c.moveTo(w / 2, 2 * h / 3);
        c.lineTo(0, h);
        c.moveTo(w / 2, 2 * h / 3);
        c.lineTo(w, h);
        c.stroke();
    }
};

// UmlLifeline (Shapes.js:3858): a head rectangle (or the shape named by
// `participant`) above a dashed line. lifelineMirror=1 repeats the head at
// the foot; the copy of the label that upstream paints into the foot box
// (paintMirrorLabel) is not painted here: the cell value is not available
// to a shape painter (KNOWN_ISSUES).
class UmlLifeline final : public RectangleShape {
public:
    [[nodiscard]] Rect labelBounds(const ResolvedStyle& style, const Rect& rect) const override
    {
        const double size = clampTo(style.size(kDefaultSize), 0, mirrored(style) ? rect.height / 2 : rect.height);
        return {rect.x, rect.y, rect.width, size};
    }

    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        const double size = headSize(style, h);
        const bool mirror = mirrored(style);
        const std::string_view participant = style.string("participant");

        // paintBackground
        const VertexShape* custom = nullptr;
        if (!participant.empty()) {
            custom = ShapeRegistry::builtin().find(participant);
            if (custom == this)
                custom = nullptr; // `ctor != UmlLifeline`
        }
        if (participant.empty()) {
            RectangleShape::paintBackground(c, context, x, y, w, size);
            if (mirror)
                RectangleShape::paintBackground(c, context, x, y + h - size, w, size);
        } else if (custom) {
            c.save();
            custom->paint(c, context, x, y, w, size);
            c.restore();
            if (mirror) {
                c.save();
                custom->paint(c, context, x, y + h - size, w, size);
                c.restore();
            }
        }

        const double lineEnd = mirror ? h - size : h;
        if (size < lineEnd) {
            c.setDashed(style.string("lifelineDashed", "1") == "1");
            if (const std::optional<QColor> color = style.color("lifelineColor"))
                c.setStrokeColor(*color);
            c.begin();
            c.moveTo(x + w / 2, y + size);
            c.lineTo(x + w / 2, y + lineEnd);
            c.stroke();
        }

        // paintForeground: the rectangle outline is the participant's job
        // when a participant shape is set; mxRectangleShape.paintForeground
        // itself only paints glass, which is not drawn.
        c.setShadow(false);
    }

private:
    static constexpr double kDefaultSize = 40; // UmlLifeline.prototype.size

    static bool mirrored(const ResolvedStyle& style) { return style.string("lifelineMirror", "0") == "1"; }
    static double headSize(const ResolvedStyle& style, double h)
    {
        return std::max(0.0, std::min(mirrored(style) ? h / 2 : h, style.size(kDefaultSize)));
    }
};

// UmlFrame (Shapes.js:4060)
class UmlFrame final : public VertexShape {
public:
    [[nodiscard]] std::optional<Insets> labelMargins(const ResolvedStyle& style, const Rect& rect) const override
    {
        return margins(0, 0, rect.width - style.number("width", kWidth), rect.height - style.number("height", kHeight));
    }

    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        const double w0 = std::min(w, std::max(kCorner, style.number("width", kWidth)));
        const double h0 = std::min(h, std::max(kCorner * 1.5, style.number("height", kHeight)));

        if (const std::optional<QColor> bg = style.color("swimlaneFillColor")) {
            c.setFillColor(bg);
            c.rect(x, y, w, h);
            c.fill();
        }

        const std::optional<QColor> fill = style.fillColor();
        const std::optional<QColor> gradient = style.gradientColor();
        if (fill && gradient)
            c.setGradient(*fill, *gradient, {x, y, w, h}, parseGradientDirection(style.gradientDirection()));
        else
            c.setFillColor(fill);

        c.begin();
        c.moveTo(x, y);
        c.lineTo(x + w0, y);
        c.lineTo(x + w0, y + std::max(0.0, h0 - kCorner * 1.5));
        c.lineTo(x + std::max(0.0, w0 - kCorner), y + h0);
        c.lineTo(x, y + h0);
        c.close();
        c.fillAndStroke();

        c.begin();
        c.moveTo(x + w0, y);
        c.lineTo(x + w, y);
        c.lineTo(x + w, y + h);
        c.lineTo(x, y + h);
        c.lineTo(x, y + h0);
        c.stroke();
        c.setShadow(false); // paintForeground: nothing
    }

private:
    static constexpr double kWidth = 60;
    static constexpr double kHeight = 30;
    static constexpr double kCorner = 10;
};

// ModuleShape (Shapes.js:4995)
class ModuleShape final : public CylinderBase {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h, bool isForeground) const override
    {
        const double dx = style.number("jettyWidth", 20);
        const double dy = style.number("jettyHeight", 10);
        const double x0 = dx / 2;
        const double x1 = x0 + dx / 2;
        const double y0 = std::min(dy, h - dy);
        const double y1 = std::min(y0 + 2 * dy, h - dy);
        drawJetties(c, w, h, x0, x1, y0, y1, dy, isForeground);
    }

public:
    // The outline with two jetty notches (background) or the two jetty
    // boxes (foreground); shared with ComponentShape.
    static void drawJetties(Canvas& c, double w, double h, double x0, double x1, double y0, double y1, double dy,
                            bool isForeground)
    {
        if (isForeground) {
            c.moveTo(x0, y0);
            c.lineTo(x1, y0);
            c.lineTo(x1, y0 + dy);
            c.lineTo(x0, y0 + dy);
            c.moveTo(x0, y1);
            c.lineTo(x1, y1);
            c.lineTo(x1, y1 + dy);
            c.lineTo(x0, y1 + dy);
        } else {
            c.moveTo(x0, 0);
            c.lineTo(w, 0);
            c.lineTo(w, h);
            c.lineTo(x0, h);
            c.lineTo(x0, y1 + dy);
            c.lineTo(0, y1 + dy);
            c.lineTo(0, y1);
            c.lineTo(x0, y1);
            c.lineTo(x0, y0 + dy);
            c.lineTo(0, y0 + dy);
            c.lineTo(0, y0);
            c.lineTo(x0, y0);
            c.close();
        }
    }
};

// ComponentShape (Shapes.js:5049)
class ComponentShape final : public CylinderBase {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h, bool isForeground) const override
    {
        const double dx = style.number("jettyWidth", 32);
        const double dy = style.number("jettyHeight", 12);
        const double x0 = dx / 2;
        const double x1 = x0 + dx / 2;
        const double y0 = 0.3 * h - dy / 2;
        const double y1 = 0.7 * h - dy / 2;
        ModuleShape::drawJetties(c, w, h, x0, x1, y0, y1, dy, isForeground);
    }
};

// MessageShape (Shapes.js:3437)
class MessageShape final : public CylinderBase {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle&, double w, double h, bool isForeground) const override
    {
        if (isForeground) {
            c.moveTo(0, 0);
            c.lineTo(w / 2, h / 2);
            c.lineTo(w, 0);
        } else {
            c.moveTo(0, 0);
            c.lineTo(w, 0);
            c.lineTo(w, h);
            c.lineTo(0, h);
            c.close();
        }
    }
};

// StateShape (Shapes.js:5126, an mxDoubleEllipse) and StartStateShape
// (:5157, the same without the outer ring).
class StateShape final : public VertexShape {
public:
    explicit StateShape(bool outerStroke) : outerStroke_(outerStroke) {}

    void paint(Canvas& c, const ShapePaintContext&, double x, double y, double w, double h) const override
    {
        const double inset = std::min(4.0, std::min(w / 5, h / 5));
        if (w > 0 && h > 0) {
            c.ellipse(x + inset, y + inset, w - 2 * inset, h - 2 * inset);
            c.fillAndStroke();
        }
        c.setShadow(false);
        if (outerStroke_) {
            c.ellipse(x, y, w, h);
            c.stroke();
        }
    }

    // mxDoubleEllipse.getLabelBounds
    [[nodiscard]] Rect labelBounds(const ResolvedStyle& style, const Rect& rect) const override
    {
        const double margin =
            style.number("margin", std::min(3 + style.strokeWidth(), std::min(rect.width / 5, rect.height / 5)));
        return {rect.x + margin, rect.y + margin, rect.width - 2 * margin, rect.height - 2 * margin};
    }

private:
    bool outerStroke_;
};

// --- arrows, brackets, misc -------------------------------------------------------

// CurlyBracketShape (Shapes.js:2441): an open path, never filled.
class CurlyBracketShape final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        c.setFillColor(std::nullopt);
        const double s = w * clampTo(style.size(0.5), 0, 1);
        const Point points[] = {{w, 0}, {s, 0}, {s, h / 2}, {0, h / 2}, {s, h / 2}, {s, h}, {w, h}};
        addPoints(c, style, points, false);
    }
};

// CrossbarShape (Shapes.js:5346): three lines.
class CrossbarShape final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle&, double w, double h) const override
    {
        c.moveTo(0, 0);
        c.lineTo(0, h);
        c.moveTo(w, 0);
        c.lineTo(w, h);
        c.moveTo(0, h / 2);
        c.lineTo(w, h / 2);
    }
};

// SingleArrowShape (Shapes.js:5405) and DoubleArrowShape (:5438) share the
// arrowWidth/arrowSize styles and their defaults.
constexpr double kArrowWidth = 0.3;
constexpr double kArrowSize = 0.2;

class SingleArrowShape final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const double aw = h * clampTo(style.number("arrowWidth", kArrowWidth), 0, 1);
        const double as = w * clampTo(style.number("arrowSize", kArrowSize), 0, 1);
        const double at = (h - aw) / 2;
        const double ab = at + aw;
        const Point points[] = {{0, at}, {w - as, at}, {w - as, 0}, {w, h / 2}, {w - as, h}, {w - as, ab}, {0, ab}};
        addPoints(c, style, points);
    }
};

class DoubleArrowShape final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const double aw = h * clampTo(style.number("arrowWidth", kArrowWidth), 0, 1);
        const double as = w * clampTo(style.number("arrowSize", kArrowSize), 0, 1);
        const double at = (h - aw) / 2;
        const double ab = at + aw;
        const Point points[] = {{0, h / 2}, {as, 0},     {as, at},     {w - as, at}, {w - as, 0},
                                {w, h / 2}, {w - as, h}, {w - as, ab}, {as, ab},     {as, h}};
        addPoints(c, style, points);
    }
};

// OffPageConnectorShape (Shapes.js:5573)
class OffPageConnectorShape final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const double s = h * clampTo(style.size(3.0 / 8), 0, 1);
        const Point points[] = {{0, 0}, {w, 0}, {w, h - s}, {w / 2, h}, {0, h - s}};
        addPoints(c, style, points);
    }
};

// mxEllipse.paintVertexShape plus the center line (LineEllipseShape,
// Shapes.js:5881): `line=vertical` or horizontal (default).
class LineEllipseShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        c.ellipse(x, y, w, h);
        c.fillAndStroke();

        c.setShadow(false);
        c.begin();
        if (context.style.string("line") == "vertical") {
            c.moveTo(x + w / 2, y);
            c.lineTo(x + w / 2, y + h);
        } else {
            c.moveTo(x, y + h / 2);
            c.lineTo(x + w, y + h / 2);
        }
        c.stroke();
    }
};

// DimensionShape (Shapes.js:5730): a dimension line with arrowheads and two
// extension lines; the stroke width is the canvas' own.
class DimensionShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const double sw = context.style.strokeWidth() / 2;
        const double al = 10 + 2 * sw; // arrow size
        const double cy = y + h - al / 2;

        c.begin();
        c.moveTo(x, y);
        c.lineTo(x, y + h);
        c.moveTo(x + sw, cy);
        c.lineTo(x + sw + al, cy - al / 2);
        c.moveTo(x + sw, cy);
        c.lineTo(x + sw + al, cy + al / 2);
        c.moveTo(x + sw, cy);
        c.lineTo(x + w - sw, cy);

        // Opposite side
        c.moveTo(x + w, y);
        c.lineTo(x + w, y + h);
        c.moveTo(x + w - sw, cy);
        c.lineTo(x + w - al - sw, cy - al / 2);
        c.moveTo(x + w - sw, cy);
        c.lineTo(x + w - al - sw, cy + al / 2);
        c.stroke();
    }
};

// WaypointShape (Shapes.js:1277): a dot in the stroke color; upstream adds an
// unfilled rectangle over the bounds for hit detection, which paints nothing.
class WaypointShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        c.setFillColor(style.strokeColor());
        const double s = std::max(0.0, style.size(6) - 2) + 2 * style.strokeWidth();
        c.ellipse(x + (w - s) * 0.5, y + (h - s) * 0.5, s, s);
        c.fill();
    }
};

// ExtendedShape (Shapes.js:3314, `ext`): a rectangle with an optional inner
// rectangle (`double=1`) and symbols drawn by other shapes (`symbol0`,
// `symbol1`, ... with `symbolNAlign`, `symbolNVerticalAlign`,
// `symbolNWidth`, `symbolNHeight`, `symbolNSpacing`, `symbolNVSpacing`,
// `symbolNArcSpacing`).
class ExtendedShape final : public RectangleShape {
public:
    [[nodiscard]] Rect labelBounds(const ResolvedStyle& style, const Rect& rect) const override
    {
        if (!style.flag("double", false))
            return rect;
        const double margin = doubleMargin(style);
        return {rect.x + margin, rect.y + margin, rect.width - 2 * margin, rect.height - 2 * margin};
    }

protected:
    void paintForeground(Canvas& c, const ShapePaintContext& context, double x, double y, double w,
                         double h) const override
    {
        const ResolvedStyle& style = context.style;
        if (style.flag("double", false)) {
            const double margin = doubleMargin(style);
            x += margin;
            y += margin;
            w -= 2 * margin;
            h -= 2 * margin;
            if (w > 0 && h > 0)
                RectangleShape::paintBackground(c, context, x, y, w, h);
        }

        c.setDashed(false);
        for (int counter = 0;; ++counter) {
            const std::string prefix = "symbol" + std::to_string(counter);
            const VertexShape* symbol = ShapeRegistry::builtin().find(style.string(prefix));
            if (!symbol)
                break;
            paintSymbol(c, context, *symbol, prefix, x, y, w, h);
        }
    }

private:
    static double doubleMargin(const ResolvedStyle& style)
    {
        return std::max(2.0, style.strokeWidth() + 1) + style.number("margin", 0);
    }

    static void paintSymbol(Canvas& c, const ShapePaintContext& context, const VertexShape& symbol,
                            const std::string& prefix, double x, double y, double w, double h)
    {
        const ResolvedStyle& style = context.style;
        const std::string_view align = style.string(prefix + "Align");
        const std::string_view valign = style.string(prefix + "VerticalAlign");
        const double width = style.number(prefix + "Width", std::nan(""));
        const double height = style.number(prefix + "Height", std::nan(""));
        double spacing = style.number(prefix + "Spacing", 0);
        double vspacing = style.has(prefix + "VSpacing") ? style.number(prefix + "VSpacing", 0) : spacing;

        if (style.has(prefix + "ArcSpacing")) {
            const double arc = rectangleArcSize(style, w + style.strokeWidth(), h + style.strokeWidth()) *
                               style.number(prefix + "ArcSpacing", 0);
            spacing += arc;
            vspacing += arc;
        }

        double x2 = x;
        double y2 = y;
        if (align == "center")
            x2 += (w - width) / 2;
        else if (align == "right")
            x2 += w - width - spacing;
        else
            x2 += spacing;

        if (valign == "middle")
            y2 += (h - height) / 2;
        else if (valign == "bottom")
            y2 += h - height - vspacing;
        else
            y2 += vspacing;

        if (!std::isfinite(x2) || !std::isfinite(y2) || !std::isfinite(width) || !std::isfinite(height))
            return; // upstream paints NaN geometry, which shows nothing
        c.save();
        symbol.paint(c, context, x2, y2, width, height);
        c.restore();
    }
};

} // namespace

void registerUmlShapes(ShapeRegistry& registry)
{
    registry.add("cube", std::make_unique<CubeShape>());
    registry.add("folder", std::make_unique<FolderShape>());
    registry.add("datastore", std::make_unique<DataStoreShape>());
    registry.add("umlActor", std::make_unique<UmlActorShape>());
    registry.add("umlLifeline", std::make_unique<UmlLifeline>());
    registry.add("umlFrame", std::make_unique<UmlFrame>());
    registry.add("module", std::make_unique<ModuleShape>());
    registry.add("component", std::make_unique<ComponentShape>());
    registry.add("message", std::make_unique<MessageShape>());
    registry.add("endState", std::make_unique<StateShape>(true));
    registry.add("startState", std::make_unique<StateShape>(false));
    registry.add("curlyBracket", std::make_unique<CurlyBracketShape>());
    registry.add("crossbar", std::make_unique<CrossbarShape>());
    registry.add("singleArrow", std::make_unique<SingleArrowShape>());
    registry.add("doubleArrow", std::make_unique<DoubleArrowShape>());
    registry.add("offPageConnector", std::make_unique<OffPageConnectorShape>());
    registry.add("lineEllipse", std::make_unique<LineEllipseShape>());
    registry.add("dimension", std::make_unique<DimensionShape>());
    registry.add("waypoint", std::make_unique<WaypointShape>());
    registry.add("ext", std::make_unique<ExtendedShape>());
}

} // namespace qraw::render::detail
