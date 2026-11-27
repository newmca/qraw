// Flowchart and general-purpose native shapes: process, document,
// parallelogram, trapezoid, step, card, tape, note, internalStorage,
// manualInput, cross, plus, callout, cylinder, cylinder3, actor, cloud.
//
// Port of mxgraph/src/shape/mxActor.js, mxCylinder.js, mxCloud.js and
// js/grapheditor/Shapes.js (draw.io, Apache-2.0), modified. Sizes are read
// the way each upstream class does (`size` with the prototype default, the
// `fixedSize` switch). Rounded variants go through geometry::polylinePath
// (mxShape.addPoints).

#include "shapes_common.h"

#include <vector>

namespace qraw::render::detail {

namespace {

// --- mxActor family ---------------------------------------------------------

// mxActor.redrawPath (mxActor.js:77)
class ActorFigure final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle&, double w, double h) const override
    {
        const double width = w / 3;
        c.moveTo(0, h);
        c.curveTo(0, 3 * h / 5, 0, 2 * h / 5, w / 2, 2 * h / 5);
        c.curveTo(w / 2 - width, 2 * h / 5, w / 2 - width, 0, w / 2, 0);
        c.curveTo(w / 2 + width, 0, w / 2 + width, 2 * h / 5, w / 2, 2 * h / 5);
        c.curveTo(w, 2 * h / 5, w, 3 * h / 5, w, h);
        c.close();
    }
};

// mxCloud.redrawPath (mxCloud.js:45)
class CloudShape final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle&, double w, double h) const override
    {
        c.moveTo(0.25 * w, 0.25 * h);
        c.curveTo(0.05 * w, 0.25 * h, 0, 0.5 * h, 0.16 * w, 0.55 * h);
        c.curveTo(0, 0.66 * h, 0.18 * w, 0.9 * h, 0.31 * w, 0.8 * h);
        c.curveTo(0.4 * w, h, 0.7 * w, h, 0.8 * w, 0.8 * h);
        c.curveTo(w, 0.8 * h, w, 0.6 * h, 0.875 * w, 0.5 * h);
        c.curveTo(w, 0.3 * h, 0.8 * w, 0.1 * h, 0.625 * w, 0.2 * h);
        c.curveTo(0.5 * w, 0.05 * h, 0.3 * w, 0.05 * h, 0.25 * w, 0.25 * h);
        c.close();
    }
};

// ParallelogramShape (Shapes.js:2397)
class ParallelogramShape final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const double dx = style.fixedSize() ? clampTo(style.size(20), 0, w) : w * clampTo(style.size(0.2), 0, 1);
        const Point points[] = {{0, h}, {dx, 0}, {w, 0}, {w - dx, h}};
        addPoints(c, style, points);
    }
};

// TrapezoidShape (Shapes.js:2427)
class TrapezoidShape final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const double dx = style.fixedSize() ? clampTo(style.size(20), 0, w * 0.5) : w * clampTo(style.size(0.2), 0, 0.5);
        const Point points[] = {{0, h}, {dx, 0}, {w - dx, 0}, {w, h}};
        addPoints(c, style, points);
    }
};

// StepShape (Shapes.js:3111)
class StepShape final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const double s = style.fixedSize() ? clampTo(style.size(20), 0, w) : w * clampTo(style.size(0.2), 0, 1);
        const Point points[] = {{0, 0}, {w - s, 0}, {w, h / 2}, {w - s, h}, {0, h}, {s, h / 2}};
        addPoints(c, style, points);
    }
};

// CardShape (Shapes.js:2024)
class CardShape final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const double s = clampTo(style.size(30), 0, std::min(w, h));
        const Point points[] = {{s, 0}, {w, 0}, {w, h}, {0, h}, {0, s}};
        addPoints(c, style, points);
    }
};

// ManualInputShape (Shapes.js:5255)
class ManualInputShape final : public ActorShape {
public:
    [[nodiscard]] std::optional<geometry::Insets> labelMargins(const ResolvedStyle& style, const Rect&) const override
    {
        if (!boundedLabel(style))
            return std::nullopt;
        return margins(0, style.size(30), 0, 0);
    }

protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const double s = std::min(h, style.size(30));
        const Point points[] = {{0, h}, {0, s}, {w, 0}, {w, h}};
        addPoints(c, style, points);
    }
};

// CrossShape (Shapes.js:5935)
class CrossShape final : public ActorShape {
protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const double m = std::min(h, w);
        const double size = clampTo(m * style.size(0.2), 0, m);
        const double t = (h - size) / 2;
        const double b = t + size;
        const double l = (w - size) / 2;
        const double r = l + size;
        c.moveTo(0, t);
        c.lineTo(l, t);
        c.lineTo(l, 0);
        c.lineTo(r, 0);
        c.lineTo(r, t);
        c.lineTo(w, t);
        c.lineTo(w, b);
        c.lineTo(r, b);
        c.lineTo(r, h);
        c.lineTo(l, h);
        c.lineTo(l, b);
        c.lineTo(0, b);
        c.close();
    }
};

// CalloutShape (Shapes.js:2919); the bottom margin is unconditional upstream.
class CalloutShape final : public ActorShape {
public:
    [[nodiscard]] std::optional<geometry::Insets> labelMargins(const ResolvedStyle& style, const Rect&) const override
    {
        return margins(0, 0, 0, style.size(30));
    }

protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const double s = clampTo(style.size(30), 0, h);
        const double dx = w * clampTo(style.number("position", 0.5), 0, 1);
        const double dx2 = w * clampTo(style.number("position2", 0.5), 0, 1);
        const double base = clampTo(style.number("base", 20), 0, w);
        const Point points[] = {{0, 0}, {w, 0}, {w, h - s}, {std::min(w, dx + base), h - s},
                                {dx2, h}, {std::max(0.0, dx), h - s}, {0, h - s}};
        const int exclude[] = {4};
        addPoints(c, style, points, true, exclude);
    }
};

// --- curved outlines (document, tape) -----------------------------------------

// One segment of addCurvedPath: a line to (x, y) or a quadratic curve with
// control point (cx, cy).
struct CurveSegment {
    bool line = true;
    double cx = 0;
    double cy = 0;
    double x = 0;
    double y = 0;
};

CurveSegment lineTo(double x, double y) { return {true, 0, 0, x, y}; }
CurveSegment quadTo(double cx, double cy, double x, double y) { return {false, cx, cy, x, y}; }

struct Quad {
    Point p0;
    Point p1;
    Point c;
    bool line = false;
    bool corner = false;
    double t0 = 0;
    double t1 = 0;
};

// getQuadPolar (Shapes.js:2057): the polar form of a quadratic curve.
Point quadPolar(const Quad& q, double u, double v)
{
    const double a = (1 - u) * (1 - v);
    const double b = u * (1 - v) + v * (1 - u);
    return {a * q.p0.x + b * q.c.x + u * v * q.p1.x, a * q.p0.y + b * q.c.y + u * v * q.p1.y};
}

// addCurvedPath (Shapes.js:2070): a closed path starting at (x0, y0). With
// rounding, corners between segments whose tangents differ are replaced by
// cubic curves that keep the tangents.
void addCurvedPath(Canvas& c, double x0, double y0, std::vector<CurveSegment> segs, bool rounded, double arcSize)
{
    if (!rounded || !(arcSize > 0)) {
        c.moveTo(x0, y0);
        for (const CurveSegment& s : segs) {
            if (s.line)
                c.lineTo(s.x, s.y);
            else
                c.quadTo(s.cx, s.cy, s.x, s.y);
        }
        c.close();
        return;
    }

    const CurveSegment& last = segs.back();
    if (last.x != x0 || last.y != y0)
        segs.push_back(lineTo(x0, y0));

    // Quadratic curves, where lines get the control point in the middle.
    std::vector<Quad> curves;
    Point p{x0, y0};
    for (const CurveSegment& s : segs) {
        const Point pe{s.x, s.y};
        Quad q;
        q.p0 = p;
        q.p1 = pe;
        q.line = s.line;
        q.c = s.line ? Point{(p.x + pe.x) / 2, (p.y + pe.y) / 2} : Point{s.cx, s.cy};
        curves.push_back(q);
        p = pe;
    }

    // Finds the corners and the parameters where they are cut off.
    const std::size_t count = curves.size();
    for (std::size_t i = 0; i < count; ++i) {
        Quad& q0 = curves[i];
        Quad& q1 = curves[(i + 1) % count];
        const double dx0 = q0.p1.x - q0.c.x;
        const double dy0 = q0.p1.y - q0.c.y;
        const double dx1 = q1.c.x - q1.p0.x;
        const double dy1 = q1.c.y - q1.p0.y;
        const double d0 = std::sqrt(dx0 * dx0 + dy0 * dy0);
        const double d1 = std::sqrt(dx1 * dx1 + dy1 * dy1);
        if (std::abs(dx0 * dy1 - dy0 * dx1) > 1e-6 * d0 * d1) {
            q0.corner = true;
            q0.t1 = std::min(0.5, arcSize / std::max(1.0, 2 * d0));
            q1.t0 = std::min(0.5, arcSize / std::max(1.0, 2 * d1));
        }
    }

    p = quadPolar(curves[0], curves[0].t0, curves[0].t0);
    c.moveTo(p.x, p.y);
    for (std::size_t i = 0; i < count; ++i) {
        const Quad& q0 = curves[i];
        const double t = 1 - q0.t1;
        const Point pe = quadPolar(q0, t, t);
        if (q0.line) {
            c.lineTo(pe.x, pe.y);
        } else {
            const Point pc = quadPolar(q0, q0.t0, t);
            c.quadTo(pc.x, pc.y, pe.x, pe.y);
        }

        // Replaces the parts that were cut off with a cubic curve along the
        // tangents of the remaining parts.
        if (q0.corner) {
            const Quad& q1 = curves[(i + 1) % count];
            const Point pn = quadPolar(q1, q1.t0, q1.t0);
            const Point c0 = quadPolar(q0, t, 1);
            const Point c1 = quadPolar(q1, 0, q1.t0);
            c.curveTo(pe.x + (c0.x - pe.x) * 4 / 3, pe.y + (c0.y - pe.y) * 4 / 3, pn.x + (c1.x - pn.x) * 4 / 3,
                      pn.y + (c1.y - pn.y) * 4 / 3, pn.x, pn.y);
        }
    }
    c.close();
}

constexpr double kWaveFactor = 1.4; // `fy` of the document/tape wave

// DocumentShape (Shapes.js:2247)
class DocumentShape final : public ActorShape {
public:
    [[nodiscard]] std::optional<geometry::Insets> labelMargins(const ResolvedStyle& style,
                                                               const Rect& rect) const override
    {
        if (!boundedLabel(style))
            return std::nullopt;
        return margins(0, 0, 0, style.size(0.3) * rect.height);
    }

protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const double dy = h * clampTo(style.size(0.3), 0, 1);
        const double fy = kWaveFactor;
        const bool rounded = style.rounded();
        // The left side ends at the corner if rounded and at dy / 2 otherwise.
        addCurvedPath(c, 0, 0,
                      {lineTo(w, 0), lineTo(w, h - dy / 2), quadTo(w * 3 / 4, h - dy * fy, w / 2, h - dy / 2),
                       quadTo(w / 4, h - dy * (1 - fy), 0, h - dy / 2), lineTo(0, rounded ? 0 : dy / 2)},
                      rounded, polygonArcSize(style));
    }
};

// TapeShape (Shapes.js:2178)
class TapeShape final : public ActorShape {
public:
    [[nodiscard]] Rect labelBounds(const ResolvedStyle& style, const Rect& rect) const override
    {
        if (!boundedLabel(style))
            return rect;
        const double size = style.size(0.4);
        const geometry::Direction d = style.direction();
        if (d == geometry::Direction::East || d == geometry::Direction::West) {
            const double dy = rect.height * size;
            return {rect.x, rect.y + dy, rect.width, rect.height - 2 * dy};
        }
        const double dx = rect.width * size;
        return {rect.x + dx, rect.y, rect.width - 2 * dx, rect.height};
    }

protected:
    void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h) const override
    {
        const double dy = h * clampTo(style.size(0.4), 0, 1);
        const double fy = kWaveFactor;
        addCurvedPath(c, 0, dy / 2,
                      {quadTo(w / 4, dy * fy, w / 2, dy / 2), quadTo(w * 3 / 4, dy * (1 - fy), w, dy / 2),
                       lineTo(w, h - dy / 2), quadTo(w * 3 / 4, h - dy * fy, w / 2, h - dy / 2),
                       quadTo(w / 4, h - dy * (1 - fy), 0, h - dy / 2), lineTo(0, dy / 2)},
                      style.rounded(), polygonArcSize(style));
    }
};

// --- mxRectangleShape family --------------------------------------------------

// mxRectangleShape inner offset shared by process and internalStorage when
// rounded: the inner lines keep clear of the corner arcs.
double cornerInset(const ResolvedStyle& style, double w, double h)
{
    const double f = style.number("arcSize", kRectangleRoundingFactor * 100) / 100;
    return std::min(w * f, h * f);
}

// ProcessShape (Shapes.js:2776)
class ProcessShape final : public RectangleShape {
public:
    [[nodiscard]] Rect labelBounds(const ResolvedStyle& style, const Rect& rect) const override
    {
        const geometry::Direction d = style.direction();
        const bool horizontalDirection = d == geometry::Direction::East || d == geometry::Direction::West;
        if (style.horizontal() != horizontalDirection)
            return rect;

        const double w = rect.width;
        double inset = style.size(0.1);
        if (style.fixedSize()) {
            inset = clampTo(inset, 0, w);
        } else {
            inset = w * clampTo(inset, 0, 1);
            if (style.rounded())
                inset = std::max(inset, cornerInset(style, w, rect.height));
        }
        return {rect.x + jsRound(inset), rect.y, rect.width - jsRound(2 * inset), rect.height};
    }

protected:
    void paintForeground(Canvas& c, const ShapePaintContext& context, double x, double y, double w,
                         double h) const override
    {
        const ResolvedStyle& style = context.style;
        double inset = style.size(0.1);
        inset = style.fixedSize() ? clampTo(inset, 0, w) : w * clampTo(inset, 0, 1);
        if (style.rounded())
            inset = std::max(inset, cornerInset(style, w, h));
        inset = jsRound(inset); // crisp rendering of the inner lines

        c.begin();
        c.moveTo(x + inset, y);
        c.lineTo(x + inset, y + h);
        c.moveTo(x + w - inset, y);
        c.lineTo(x + w - inset, y + h);
        c.stroke();
    }
};

// PlusShape (Shapes.js:3153)
class PlusShape final : public RectangleShape {
protected:
    void paintForeground(Canvas& c, const ShapePaintContext&, double x, double y, double w, double h) const override
    {
        const double border = std::min(w / 5, h / 5) + 1;
        c.begin();
        c.moveTo(x + w / 2, y + border);
        c.lineTo(x + w / 2, y + h - border);
        c.moveTo(x + border, y + h / 2);
        c.lineTo(x + w - border, y + h / 2);
        c.stroke();
    }
};

// InternalStorageShape (Shapes.js:5267)
class InternalStorageShape final : public RectangleShape {
protected:
    void paintForeground(Canvas& c, const ShapePaintContext& context, double x, double y, double w,
                         double h) const override
    {
        const ResolvedStyle& style = context.style;
        const double inset = style.rounded() ? cornerInset(style, w, h) : 0;
        const double dx = std::max(inset, std::min(w, style.number("dx", 20)));
        const double dy = std::max(inset, std::min(h, style.number("dy", 20)));

        c.begin();
        c.moveTo(x, y + dy);
        c.lineTo(x + w, y + dy);
        c.stroke();

        c.begin();
        c.moveTo(x + dx, y);
        c.lineTo(x + dx, y + h);
        c.stroke();
    }
};

// --- mxCylinder family --------------------------------------------------------

// mxCylinder (mxCylinder.js:54, :86)
class CylinderShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        c.translate(x, y);
        c.begin();
        redrawPath(c, context.style, w, h, false);
        c.fillAndStroke();

        c.setShadow(false);
        c.begin();
        redrawPath(c, context.style, w, h, true);
        c.stroke();
    }

private:
    static constexpr double kMaxHeight = 40; // mxCylinder.prototype.maxHeight

    static void redrawPath(Canvas& c, const ResolvedStyle& style, double w, double h, bool isForeground)
    {
        const double dy = std::min(kMaxHeight, jsRound(h / 5)); // getCylinderSize
        const bool filled = style.fillColor().has_value();

        if ((isForeground && filled) || (!isForeground && !filled)) {
            c.moveTo(0, dy);
            c.curveTo(0, 2 * dy, w, 2 * dy, w, dy);
            // Needs separate shapes for correct hit-detection upstream.
            if (!isForeground) {
                c.stroke();
                c.begin();
            }
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

// CylinderShape3 (Shapes.js:1645)
class Cylinder3Shape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        const double size = clampTo(style.size(15), 0, h * 0.5);
        const bool lid = style.flag("lid", true);

        c.translate(x, y);
        if (size == 0) {
            c.rect(0, 0, w, h);
            c.fillAndStroke();
            return;
        }

        c.begin();
        if (lid) {
            c.moveTo(0, size);
            c.arcTo(w * 0.5, size, 0, false, true, w * 0.5, 0);
            c.arcTo(w * 0.5, size, 0, false, true, w, size);
        } else {
            c.moveTo(0, 0);
            c.arcTo(w * 0.5, size, 0, false, false, w * 0.5, size);
            c.arcTo(w * 0.5, size, 0, false, false, w, 0);
        }
        c.lineTo(w, h - size);
        c.arcTo(w * 0.5, size, 0, false, true, w * 0.5, h);
        c.arcTo(w * 0.5, size, 0, false, true, 0, h - size);
        c.close();
        c.fillAndStroke();

        c.setShadow(false);
        if (lid) {
            c.begin();
            c.moveTo(w, size);
            c.arcTo(w * 0.5, size, 0, false, true, w * 0.5, 2 * size);
            c.arcTo(w * 0.5, size, 0, false, true, 0, size);
            c.stroke();
        }
    }

    // Shapes.js:2292
    [[nodiscard]] std::optional<geometry::Insets> labelMargins(const ResolvedStyle& style,
                                                               const Rect& rect) const override
    {
        if (!boundedLabel(style))
            return std::nullopt;
        double size = style.size(15);
        if (!style.flag("lid", true))
            size /= 2;
        return margins(0, std::min(rect.height, size * 2), 0, std::max(0.0, size * 0.3));
    }
};

// NoteShape (Shapes.js:1481)
class NoteShape : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        const double s = clampTo(style.size(30), 0, std::min(w, h));
        const double op = clampTo(style.number("darkOpacity", 0), -1, 1);

        c.translate(x, y);
        c.begin();
        c.moveTo(0, 0);
        c.lineTo(w - s, 0);
        c.lineTo(w, s);
        c.lineTo(w, h);
        c.lineTo(0, h);
        c.lineTo(0, 0);
        c.close();
        c.fillAndStroke();

        c.setShadow(false);
        if (op != 0) {
            c.setFillAlpha(std::abs(op));
            c.setFillColor(op < 0 ? QColor(255, 255, 255) : QColor(0, 0, 0));
            c.begin();
            c.moveTo(w - s, 0);
            c.lineTo(w - s, s);
            c.lineTo(w, s);
            c.close();
            c.fill();
        }

        c.begin();
        c.moveTo(w - s, 0);
        c.lineTo(w - s, s);
        c.lineTo(w, s);
        c.stroke();
    }
};

// NoteShape2 (Shapes.js:2368): the note with a different bounded label.
class Note2Shape final : public NoteShape {
public:
    [[nodiscard]] std::optional<geometry::Insets> labelMargins(const ResolvedStyle& style,
                                                               const Rect& rect) const override
    {
        if (!boundedLabel(style))
            return std::nullopt;
        const double size = style.size(15);
        return margins(0, std::min(rect.height, size), 0, std::max(0.0, size));
    }
};

} // namespace

void registerFlowchartShapes(ShapeRegistry& registry)
{
    registry.add("process", std::make_unique<ProcessShape>());
    registry.add("process2", std::make_unique<ProcessShape>()); // Shapes.js:2866, backwards compatibility
    registry.add("document", std::make_unique<DocumentShape>());
    registry.add("parallelogram", std::make_unique<ParallelogramShape>());
    registry.add("trapezoid", std::make_unique<TrapezoidShape>());
    registry.add("step", std::make_unique<StepShape>());
    registry.add("card", std::make_unique<CardShape>());
    registry.add("tape", std::make_unique<TapeShape>());
    registry.add("note", std::make_unique<NoteShape>());
    registry.add("note2", std::make_unique<Note2Shape>());
    registry.add("internalStorage", std::make_unique<InternalStorageShape>());
    registry.add("manualInput", std::make_unique<ManualInputShape>());
    registry.add("cross", std::make_unique<CrossShape>());
    registry.add("plus", std::make_unique<PlusShape>());
    registry.add("callout", std::make_unique<CalloutShape>());
    registry.add("cylinder", std::make_unique<CylinderShape>());
    registry.add("cylinder3", std::make_unique<Cylinder3Shape>());
    registry.add("actor", std::make_unique<ActorFigure>());
    registry.add("cloud", std::make_unique<CloudShape>());
}

} // namespace qraw::render::detail
