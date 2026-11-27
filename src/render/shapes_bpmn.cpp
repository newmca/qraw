// BPMN 2 shapes that draw.io implements as JavaScript classes:
// mxgraph.bpmn.event, mxgraph.bpmn.gateway2, mxgraph.bpmn.task2 and the
// older mxgraph.bpmn.shape.
//
// Port of shapes/bpmn/mxBpmnShape2.js (draw.io, Apache-2.0), modified. The
// upstream code reads `c.state.strokeColor`, `fillColor`, `dashed` and
// `strokeWidth` back from the canvas and flips them for inverse symbols;
// Canvas has no getters, so Pen mirrors those four values next to the
// canvas. Task markers (loop, compensation, service task, ...) are stencils
// looked up through ShapePaintContext::stencils.

#include "shapes_rect2.h"

#include "qraw/render/stencil.h"

#include <QColor>

#include <array>

namespace qraw::render::detail {

namespace {

// The canvas state that the upstream BPMN code reads back (c.state.*).
class Pen {
public:
    Pen(Canvas& canvas, const ResolvedStyle& style)
        : c(canvas), stroke_(style.strokeColor()), fill_(style.fillColor()), strokeWidth_(style.strokeWidth()),
          dashed_(style.dashed())
    {
    }

    Canvas& c;

    [[nodiscard]] const std::optional<QColor>& stroke() const { return stroke_; }
    [[nodiscard]] const std::optional<QColor>& fill() const { return fill_; }
    [[nodiscard]] double strokeWidth() const { return strokeWidth_; }
    [[nodiscard]] bool dashed() const { return dashed_; }

    void setStroke(std::optional<QColor> color)
    {
        stroke_ = color;
        c.setStrokeColor(color);
    }
    void setFill(std::optional<QColor> color)
    {
        fill_ = color;
        c.setFillColor(color);
    }
    // Task markers replace setStrokeWidth with a no-op while they draw
    // (mxShapeBpmn2Task2.paintVertexShape).
    void setStrokeWidth(double width)
    {
        if (strokeWidthLocked)
            return;
        strokeWidth_ = width;
        c.setStrokeWidth(width);
    }
    void setDashed(bool dashed)
    {
        dashed_ = dashed;
        c.setDashed(dashed);
    }

    bool strokeWidthLocked = false;

private:
    std::optional<QColor> stroke_;
    std::optional<QColor> fill_;
    double strokeWidth_;
    bool dashed_;
};

// mxUtils.getValue(style, key, '#xxxxxx') for a color: a present value
// (including none) wins over the fallback.
std::optional<QColor> colorOrDefault(const ResolvedStyle& style, std::string_view key, const QColor& fallback)
{
    return style.has(key) ? style.color(key) : std::optional<QColor>(fallback);
}

void fillOrFillAndStroke(Canvas& c, bool inverse)
{
    if (inverse)
        c.fill();
    else
        c.fillAndStroke();
}

// --- event symbols and outlines -------------------------------------------------

// Where a symbol sits inside the event: translate(w * tx, h * ty), then
// w *= sw and h *= sh (the if-chain in strictDrawShape / redrawPath).
struct SymbolBox {
    std::string_view name;
    double tx;
    double ty;
    double sw;
    double sh;
};

constexpr std::array<SymbolBox, 12> kEventSymbolBoxes{{
    {"message", 0.15, 0.3, 0.7, 0.4},
    {"timer", 0.11, 0.11, 0.78, 0.78},
    {"escalation", 0.19, 0.15, 0.62, 0.57},
    {"conditional", 0.3, 0.16, 0.4, 0.68},
    {"link", 0.27, 0.33, 0.46, 0.34},
    {"error", 0.212, 0.243, 0.58, 0.507},
    {"cancel", 0.22, 0.22, 0.56, 0.56},
    {"compensation", 0.28, 0.35, 0.44, 0.3},
    {"signal", 0.19, 0.15, 0.62, 0.57},
    {"multiple", 0.2, 0.19, 0.6, 0.565},
    {"parallelMultiple", 0.2, 0.2, 0.6, 0.6},
    {"terminate", 0.05, 0.05, 0.9, 0.9},
}};

// mxBpmnShape adds the exclusive gateway symbol to the table.
constexpr SymbolBox kExclusiveGatewayBox{"exclusiveGw", 0.12, 0, 0.76, 1};

// Applies the symbol's translate and size (the if-chain upstream runs before
// calling the symbol function).
void applySymbolBox(Canvas& c, std::string_view symbol, bool legacy, double& w, double& h)
{
    const SymbolBox* box = nullptr;
    for (const SymbolBox& candidate : kEventSymbolBoxes) {
        if (candidate.name == symbol)
            box = &candidate;
    }
    if (!box && legacy && symbol == kExclusiveGatewayBox.name)
        box = &kExclusiveGatewayBox;
    if (box) {
        c.translate(w * box->tx, h * box->ty);
        w *= box->sw;
        h *= box->sh;
    }
}

bool isEventSymbol(std::string_view symbol, bool legacy)
{
    static constexpr std::string_view kShared[] = {"general", "message",   "timer",     "escalation",
                                                   "conditional", "link", "error",     "cancel",
                                                   "compensation", "signal", "multiple", "parallelMultiple",
                                                   "terminate", "star"};
    for (const std::string_view name : kShared) {
        if (name == symbol)
            return true;
    }
    return legacy && (symbol == "exclusiveGw" || symbol == "parallelGw" || symbol == "complexGw");
}

// The gateway symbols of mxBpmnShape: drawn in the fill color on the stroke
// color, then the colors go back.
template <typename Path> void drawInverted(Pen& pen, Path&& path)
{
    Canvas& c = pen.c;
    const std::optional<QColor> strokeColor = pen.stroke();
    const std::optional<QColor> fillColor = pen.fill();
    pen.setStroke(fillColor);
    pen.setFill(strokeColor);
    c.begin();
    path();
    c.close();
    c.fillAndStroke();
    pen.setStroke(strokeColor);
    pen.setFill(fillColor);
}

void parallelCross(Canvas& c, double w, double h)
{
    c.moveTo(w * 0.38, 0);
    c.lineTo(w * 0.62, 0);
    c.lineTo(w * 0.62, h * 0.38);
    c.lineTo(w, h * 0.38);
    c.lineTo(w, h * 0.62);
    c.lineTo(w * 0.62, h * 0.62);
    c.lineTo(w * 0.62, h);
    c.lineTo(w * 0.38, h);
    c.lineTo(w * 0.38, h * 0.62);
    c.lineTo(0, h * 0.62);
    c.lineTo(0, h * 0.38);
    c.lineTo(w * 0.38, h * 0.38);
}

void exclusiveCross(Canvas& c, double w, double h)
{
    c.moveTo(w * 0.105, 0);
    c.lineTo(w * 0.5, h * 0.38);
    c.lineTo(w * 0.895, h * 0);
    c.lineTo(w, h * 0.11);
    c.lineTo(w * 0.6172, h * 0.5);
    c.lineTo(w, h * 0.89);
    c.lineTo(w * 0.895, h);
    c.lineTo(w * 0.5, h * 0.62);
    c.lineTo(w * 0.105, h);
    c.lineTo(0, h * 0.89);
    c.lineTo(w * 0.3808, h * 0.5);
    c.lineTo(0, h * 0.11);
}

void complexCross(Canvas& c, double w, double h)
{
    c.moveTo(0, h * 0.44);
    c.lineTo(w * 0.36, h * 0.44);
    c.lineTo(w * 0.1, h * 0.18);
    c.lineTo(w * 0.18, h * 0.1);
    c.lineTo(w * 0.44, h * 0.36);
    c.lineTo(w * 0.44, 0);
    c.lineTo(w * 0.56, 0);
    c.lineTo(w * 0.56, h * 0.36);
    c.lineTo(w * 0.82, h * 0.1);
    c.lineTo(w * 0.90, h * 0.18);
    c.lineTo(w * 0.64, h * 0.44);
    c.lineTo(w, h * 0.44);
    c.lineTo(w, h * 0.56);
    c.lineTo(w * 0.64, h * 0.56);
    c.lineTo(w * 0.9, h * 0.82);
    c.lineTo(w * 0.82, h * 0.9);
    c.lineTo(w * 0.56, h * 0.64);
    c.lineTo(w * 0.56, h);
    c.lineTo(w * 0.44, h);
    c.lineTo(w * 0.44, h * 0.64);
    c.lineTo(w * 0.18, h * 0.9);
    c.lineTo(w * 0.1, h * 0.82);
    c.lineTo(w * 0.36, h * 0.56);
    c.lineTo(0, h * 0.56);
}

// mxShapeBpmnEvent.prototype.symbols / mxBpmnShape.prototype.symbols. The
// two tables differ in the miter limits and in which symbols honor
// `isInverse` (legacy = mxBpmnShape).
void drawSymbol(Pen& pen, const ResolvedStyle& style, std::string_view symbol, double w, double h, bool inverse,
                bool legacy)
{
    Canvas& c = pen.c;
    // The legacy table only inverts the error symbol.
    const auto closeAndPaint = [&](bool invertible) { fillOrFillAndStroke(c, inverse && invertible); };

    if (symbol == "general") {
        return;
    }
    if (symbol == "message") {
        c.rect(0, 0, w, h);
        c.fillAndStroke();
        if (style.string("fillColor", "none") == "none" && inverse)
            c.setStrokeColor(QColor(255, 255, 255));
        c.begin();
        c.moveTo(0, 0);
        c.lineTo(w * 0.5, h * 0.5);
        c.lineTo(w, 0);
        c.stroke();
    } else if (symbol == "timer") {
        c.ellipse(0, 0, w, h);
        c.fillAndStroke();
        c.begin();
        c.moveTo(w * 0.5, 0);
        c.lineTo(w * 0.5, h * 0.0642);
        c.moveTo(w * 0.7484, h * 0.0654);
        c.lineTo(w * 0.7126, h * 0.1281);
        c.moveTo(w * 0.93, h * 0.2471);
        c.lineTo(w * 0.8673, h * 0.2854);
        c.moveTo(w, h * 0.5);
        c.lineTo(w * 0.9338, h * 0.5);
        c.moveTo(w * 0.93, h * 0.7509);
        c.lineTo(w * 0.8673, h * 0.7126);
        c.moveTo(w * 0.7484, h * 0.9326);
        c.lineTo(w * 0.7126, h * 0.8699);
        c.moveTo(w * 0.5, h * 0.9338);
        c.lineTo(w * 0.5, h);
        c.moveTo(w * 0.2496, h * 0.9325);
        c.lineTo(w * 0.2854, h * 0.8699);
        c.moveTo(w * 0.068, h * 0.7509);
        c.lineTo(w * 0.1307, h * 0.7126);
        c.moveTo(0, h * 0.5);
        c.lineTo(w * 0.0642, h * 0.5);
        c.moveTo(w * 0.068, h * 0.2471);
        c.lineTo(w * 0.1307, h * 0.2854);
        c.moveTo(w * 0.2496, h * 0.0654);
        c.lineTo(w * 0.2854, h * 0.1281);
        c.moveTo(w * 0.5246, h * 0.0706);
        c.lineTo(w * 0.5, h * 0.5);
        c.lineTo(w * 0.7804, h * 0.5118);
        c.stroke();
    } else if (symbol == "escalation") {
        if (!legacy)
            c.setMiterLimit(6);
        c.begin();
        c.moveTo(0, h);
        c.lineTo(w * 0.5, 0);
        c.lineTo(w, h);
        c.lineTo(w * 0.5, h * 0.5);
        c.close();
        closeAndPaint(!legacy);
    } else if (symbol == "conditional") {
        c.rect(0, 0, w, h);
        c.fillAndStroke();
        c.begin();
        for (const double y : {0.1027, 0.3669, 0.6311, 0.8953}) {
            c.moveTo(0, h * y);
            c.lineTo(w * 0.798, h * y);
        }
        c.stroke();
    } else if (symbol == "link") {
        c.begin();
        c.moveTo(0, h * 0.76);
        c.lineTo(0, h * 0.24);
        c.lineTo(w * 0.63, h * 0.24);
        c.lineTo(w * 0.63, 0);
        c.lineTo(w, h * 0.5);
        c.lineTo(w * 0.63, h);
        c.lineTo(w * 0.63, h * 0.76);
        c.close();
        closeAndPaint(!legacy);
    } else if (symbol == "error") {
        if (!legacy)
            c.setMiterLimit(7);
        c.begin();
        c.moveTo(0, h);
        c.lineTo(w * 0.3287, h * 0.123);
        c.lineTo(w * 0.6194, h * 0.6342);
        c.lineTo(w, 0);
        c.lineTo(w * 0.6625, h * 0.939);
        c.lineTo(w * 0.3717, h * 0.5064);
        c.close();
        closeAndPaint(true);
    } else if (symbol == "cancel") {
        c.begin();
        c.moveTo(w * 0.1051, 0);
        c.lineTo(w * 0.5, h * 0.3738);
        c.lineTo(w * 0.8909, 0);
        c.lineTo(w, h * 0.1054);
        c.lineTo(w * 0.623, h * 0.5);
        c.lineTo(w, h * 0.8926);
        c.lineTo(w * 0.8909, h);
        c.lineTo(w * 0.5, h * 0.6242);
        c.lineTo(w * 0.1051, h);
        c.lineTo(0, h * 0.8926);
        c.lineTo(w * 0.373, h * 0.5);
        c.lineTo(0, h * 0.1054);
        c.close();
        closeAndPaint(!legacy);
    } else if (symbol == "compensation") {
        if (!legacy)
            c.setMiterLimit(1);
        c.begin();
        c.moveTo(0, h * 0.5);
        c.lineTo(w * 0.5, 0);
        c.lineTo(w * 0.5, h);
        c.close();
        c.moveTo(w * 0.5, h * 0.5);
        c.lineTo(w, 0);
        c.lineTo(w, h);
        c.close();
        closeAndPaint(!legacy);
    } else if (symbol == "signal") {
        c.begin();
        c.moveTo(0, h);
        c.lineTo(w * 0.5, 0);
        c.lineTo(w, h);
        c.close();
        closeAndPaint(!legacy);
    } else if (symbol == "multiple") {
        c.begin();
        c.moveTo(0, h * 0.39);
        c.lineTo(w * 0.5, 0);
        c.lineTo(w, h * 0.39);
        c.lineTo(w * 0.815, h);
        c.lineTo(w * 0.185, h);
        c.close();
        closeAndPaint(!legacy);
    } else if (symbol == "parallelMultiple") {
        c.begin();
        parallelCross(c, w, h);
        c.close();
        closeAndPaint(!legacy);
    } else if (symbol == "terminate") {
        c.ellipse(0, 0, w, h);
        c.fillAndStroke();
    } else if (symbol == "exclusiveGw" && legacy) {
        drawInverted(pen, [&] { exclusiveCross(c, w, h); });
    } else if (symbol == "parallelGw" && legacy) {
        drawInverted(pen, [&] { parallelCross(c, w, h); });
    } else if (symbol == "complexGw" && legacy) {
        drawInverted(pen, [&] { complexCross(c, w, h); });
    } else if (symbol == "star") {
        c.translate(w / 5, h / 6);
        h *= 2.0 / 3;
        w *= 3.0 / 5;
        c.begin();
        c.moveTo(0, h / 4);
        c.lineTo(w / 3, h / 4);
        c.lineTo(w / 2, 0);
        c.lineTo(2 * w / 3, h / 4);
        c.lineTo(w, h / 4);
        c.lineTo(5 * w / 6, h / 2);
        c.lineTo(w, 3 * h / 4);
        c.lineTo(2 * w / 3, 3 * h / 4);
        c.lineTo(w / 2, h);
        c.lineTo(w / 3, 3 * h / 4);
        c.lineTo(0, 3 * h / 4);
        c.lineTo(w / 6, h / 2);
        c.close();
        c.fillAndStroke();
    }
}

bool isKnownOutline(std::string_view outline)
{
    static constexpr std::string_view kNames[] = {"none",     "standard", "eventInt", "eventNonint", "catching",
                                                  "boundInt", "boundNonint", "throwing", "end"};
    for (const std::string_view name : kNames) {
        if (name == outline)
            return true;
    }
    return false;
}

// mxShapeBpmnEvent.prototype.outlines / mxBpmnShape.prototype.outlines. In
// mxBpmnShape the shadow stays on for an outline inside a gateway.
void drawOutline(Pen& pen, std::string_view outline, double w, double h, bool insideGateway)
{
    Canvas& c = pen.c;
    const auto shadowOff = [&] {
        if (!insideGateway)
            c.setShadow(false);
    };
    constexpr double kInset = 2;

    if (outline == "none") {
        shadowOff();
    } else if (outline == "standard" || outline == "eventInt") {
        c.ellipse(0, 0, w, h);
        c.fillAndStroke();
        shadowOff();
    } else if (outline == "eventNonint") {
        const bool dashed = pen.dashed();
        pen.setDashed(true);
        c.ellipse(0, 0, w, h);
        c.fillAndStroke();
        pen.setDashed(dashed);
        shadowOff();
    } else if (outline == "catching" || outline == "boundInt") {
        c.ellipse(0, 0, w, h);
        c.fillAndStroke();
        shadowOff();
        c.ellipse(kInset, kInset, w - 2 * kInset, h - 2 * kInset);
        c.stroke();
    } else if (outline == "boundNonint") {
        const bool dashed = pen.dashed();
        pen.setDashed(true);
        c.ellipse(0, 0, w, h);
        c.fillAndStroke();
        shadowOff();
        c.ellipse(kInset, kInset, w - 2 * kInset, h - 2 * kInset);
        c.stroke();
        pen.setDashed(dashed);
    } else if (outline == "throwing") {
        c.ellipse(0, 0, w, h);
        c.fillAndStroke();
        shadowOff();
        c.ellipse(w * 0.02 + kInset, h * 0.02 + kInset, w * 0.96 - 2 * kInset, h * 0.96 - 2 * kInset);
        c.stroke();
    } else if (outline == "end") {
        const double sw = pen.strokeWidth();
        pen.setStrokeWidth(sw * 3);
        c.ellipse(0, 0, w, h);
        c.fillAndStroke();
        pen.setStrokeWidth(sw);
        shadowOff();
    }
}

// Colors for the symbol layer: the throwing and end events draw their symbol
// inverted (stroke and fill swapped), a star always fills with the stroke
// color. Returns whether the symbol is inverse; restore() undoes the swap.
struct SymbolColors {
    Pen& pen;
    std::optional<QColor> strokeColor;
    std::optional<QColor> fillColor;
    bool star;
    bool swapped = false;

    SymbolColors(Pen& p, std::string_view symbol, std::string_view outline)
        : pen(p), strokeColor(p.stroke()), fillColor(p.fill()), star(symbol == "star")
    {
        if (star) {
            pen.setFill(strokeColor);
        } else if (outline == "throwing" || outline == "end") {
            pen.setStroke(fillColor);
            pen.setFill(strokeColor);
            swapped = true;
        }
    }
    void restore()
    {
        if (star) {
            pen.setFill(fillColor);
        } else if (swapped) {
            pen.setStroke(strokeColor);
            pen.setFill(fillColor);
        }
    }
};

// mxShapeBpmnEvent.prototype.strictDrawShape (:903): the outline, then the
// symbol, at (x, y) with size w x h. Empty `outline` or `symbol` read the
// style (outline default none).
void strictDrawEvent(Pen& pen, const ResolvedStyle& style, double x, double y, double w, double h,
                     std::string_view outline, std::optional<std::string_view> symbol)
{
    Canvas& c = pen.c;
    if (outline.empty())
        outline = style.string("outline", "none");
    // An unknown outline name skips the translate as well.
    if (isKnownOutline(outline)) {
        c.translate(x, y);
        drawOutline(pen, outline, w, h, false);
    }

    const std::string_view name = symbol ? *symbol : style.string("symbol");
    if (name.empty() || !isEventSymbol(name, false))
        return;
    applySymbolBox(c, name, false, w, h);
    SymbolColors colors(pen, name, outline);
    drawSymbol(pen, style, name, w, h, colors.swapped, false);
    colors.restore();
}

// --- painters -----------------------------------------------------------------------

// mxShapeBpmnEvent (mxgraph.bpmn.event, :820)
class BpmnEventShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        c.translate(x, y);
        Pen pen(c, context.style);
        strictDrawEvent(pen, context.style, 0, 0, w, h, {}, std::nullopt);
    }
};

// mxBpmnShape (mxgraph.bpmn.shape, :26): background, outline and symbol
// layers drawn in turn; `background=gateway` shrinks the event into a
// diamond.
class BpmnLegacyShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        Pen pen(c, style);
        const std::string_view background = style.string("background", "none");
        const bool gateway = background == "gateway";

        // Layer "background"
        if (gateway || background == "none") {
            c.translate(x, y);
            if (gateway) {
                c.begin();
                c.moveTo(w / 2, 0);
                c.lineTo(w, h / 2);
                c.lineTo(w / 2, h);
                c.lineTo(0, h / 2);
                c.close();
                c.fillAndStroke();
                c.setShadow(false);
            }
        }

        // Layer "outline"
        double ow = w;
        double oh = h;
        if (gateway) {
            c.translate(w / 4, h / 4);
            oh /= 2;
            ow /= 2;
        }
        const std::string_view outline = style.string("outline", "none");
        if (isKnownOutline(outline))
            drawOutline(pen, outline, ow, oh, gateway);

        // Layer "symbol"
        double sw = w;
        double sh = h;
        if (gateway) {
            sh /= 2;
            sw /= 2;
        }
        const std::string_view symbol = style.string("symbol");
        if (symbol.empty() || !isEventSymbol(symbol, true))
            return;
        applySymbolBox(c, symbol, true, sw, sh);
        SymbolColors colors(pen, symbol, outline);
        drawSymbol(pen, style, symbol, sw, sh, colors.swapped, true);
        colors.restore();
    }
};

// mxShapeBpmnGateway (mxgraph.bpmn.gateway2, :1362)
class BpmnGatewayShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        Pen pen(c, style);
        c.translate(x, y);

        c.begin();
        c.moveTo(w * 0.5, 0);
        c.lineTo(w, h * 0.5);
        c.lineTo(w * 0.5, h);
        c.lineTo(0, h * 0.5);
        c.close();
        c.fillAndStroke();

        c.setShadow(false);

        const double symbolW = w * 0.6;
        const double symbolH = h * 0.6;
        const std::string_view gwType = style.string("gwType", "event");
        const std::string_view outline = style.string("outline", "none");
        const std::string_view symbol = style.string("symbol", "standard");
        double tmpW = w * 0.5;
        const double tmpH = h * 0.5;

        if (gwType == "event" && outline != "none") {
            strictDrawEvent(pen, style, (w - symbolW) * 0.5, (h - symbolH) * 0.5, symbolW, symbolH, outline, symbol);
        } else if (gwType == "exclusive") {
            c.translate(w * 0.31, h * 0.25);
            tmpW *= 0.76;
            drawFilledWithStroke(pen, [&] { exclusiveCross(c, tmpW, tmpH); });
        } else if (gwType == "parallel") {
            c.translate(w * 0.25, h * 0.25);
            drawFilledWithStroke(pen, [&] { parallelCross(c, tmpW, tmpH); });
        } else if (gwType == "complex") {
            c.translate(w * 0.25, h * 0.25);
            drawFilledWithStroke(pen, [&] { complexCross(c, tmpW, tmpH); });
        }
    }

private:
    // The marker is filled with the stroke color and the fill comes back.
    template <typename Path> static void drawFilledWithStroke(Pen& pen, Path&& path)
    {
        const std::optional<QColor> strokeColor = pen.stroke();
        const std::optional<QColor> fillColor = pen.fill();
        pen.setFill(strokeColor);
        pen.c.begin();
        path();
        pen.c.close();
        pen.c.fillAndStroke();
        pen.setFill(fillColor);
    }
};

// mxShapeBpmn2Task2 (mxgraph.bpmn.task2, :1821): a basic.rect frame, loop
// and ad-hoc markers, a task-type marker and an event in the top-left.
class BpmnTaskShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        Pen pen(c, style);

        const std::string_view type = style.string("bpmnShapeType", "task");
        const std::string_view taskMarker = style.string("taskMarker", "abstract");
        const double strokeWidth = style.strokeWidth();
        const double inset = style.number("indent", 3);
        double offsetY = 14 + strokeWidth * 0.5;
        if (type == "call")
            offsetY = 14 + strokeWidth * 2;

        c.translate(x, y);

        Rect2Overrides overrides;
        if (type == "transaction") {
            offsetY += inset;
            overrides.rectOutline = "double";
            overrides.indent = 3;
        } else if (type == "subprocess") {
            overrides.dashed = true;
        } else if (type == "call") {
            overrides.strokeWidth = strokeWidth * 4;
        }
        strictDrawRect2(c, style, w, h, overrides);

        // Back to the cell's stroke width (the frame may have used another)
        // and a solid line for the markers.
        pen.setStrokeWidth(strokeWidth);
        pen.setDashed(false);
        c.setShadow(false);

        paintLoopMarkers(pen, context, w, h, offsetY);

        const double inlet = type == "call" ? strokeWidth * 2 : strokeWidth * 0.5;
        paintTaskMarker(pen, context, taskMarker, inlet);

        // The event in the corner, with the stroke width frozen.
        pen.strokeWidthLocked = true;
        strictDrawEvent(pen, style, inlet, inlet, 20, 20, style.string("outline", "none"),
                        style.string("symbol", "standard"));
        pen.strokeWidthLocked = false;
    }

private:
    static constexpr double kIconSpaceX = 14;

    // The marker stencils are drawn at stroke width 1 (upstream replaces
    // setStrokeWidth with a no-op after setting it to 1).
    static void drawStencil(Canvas& c, const ShapePaintContext& context, std::string_view name, double x, double y,
                            double w, double h)
    {
        StencilRegistry& registry = context.stencils ? *context.stencils : StencilRegistry::builtin();
        const Stencil* stencil = registry.find(name);
        if (!stencil)
            return;
        ResolvedStyle markerStyle = context.style;
        markerStyle.map().set("strokeWidth", "1");
        const ShapePaintContext markerContext{markerStyle, context.images, {}, false, context.stencils};
        stencil->paint(c, markerContext, x, y, w, h);
    }

    static void paintLoopMarkers(Pen& pen, const ShapePaintContext& context, double w, double h, double offsetY)
    {
        Canvas& c = pen.c;
        const ResolvedStyle& style = context.style;
        const bool isLoopSub = style.flag("isLoopSub", false);
        const bool isLoopStandard = style.flag("isLoopStandard", false);
        const bool isLoopMultiParallel = style.flag("isLoopMultiParallel", false);
        const bool isLoopMultiSeq = style.flag("isLoopMultiSeq", false);
        const bool isLoopComp = style.flag("isLoopComp", false);
        const bool isAdHoc = style.flag("isAdHoc", false);

        const int count = int(isLoopStandard) + int(isLoopMultiParallel) + int(isLoopMultiSeq) + int(isLoopComp) +
                          int(isLoopSub) + int(isAdHoc);
        double currXOffset = -kIconSpaceX * count * 0.5;

        pen.setStrokeWidth(1);

        if (isLoopStandard) {
            drawStencil(c, context, "mxgraph.bpmn.loop", w * 0.5 + currXOffset + 1, h - offsetY + 1, 12, 12);
            currXOffset += kIconSpaceX;
        }

        if (isLoopMultiParallel) {
            c.translate(w * 0.5 + currXOffset + 1, h - offsetY + 1);
            c.begin();
            c.moveTo(2.4, 0);
            c.lineTo(2.4, 12);
            c.moveTo(6, 0);
            c.lineTo(6, 12);
            c.moveTo(9.6, 0);
            c.lineTo(9.6, 12);
            c.stroke();
            c.translate(-w * 0.5 - currXOffset - 1, offsetY - 1 - h);
            currXOffset += kIconSpaceX;
        }

        if (isLoopMultiSeq) {
            c.translate(w * 0.5 + currXOffset + 1, h - offsetY + 1);
            c.begin();
            c.moveTo(0, 2.4);
            c.lineTo(12, 2.4);
            c.moveTo(0, 6);
            c.lineTo(12, 6);
            c.moveTo(0, 9.6);
            c.lineTo(12, 9.6);
            c.stroke();
            c.translate(-w * 0.5 - currXOffset - 1, offsetY - 1 - h);
            currXOffset += kIconSpaceX;
        }

        if (isLoopComp) {
            drawStencil(c, context, "mxgraph.bpmn.compensation", w * 0.5 + currXOffset, h - offsetY + 1, 14, 12);
            currXOffset += kIconSpaceX;
        }

        if (isLoopSub) {
            c.translate(w * 0.5 + currXOffset, h - offsetY);
            c.rect(0, 0, 14, 14);
            c.stroke();
            c.begin();
            c.moveTo(4, 7);
            c.lineTo(10, 7);
            c.moveTo(7, 4);
            c.lineTo(7, 10);
            c.stroke();
            c.translate(-w * 0.5 - currXOffset, offsetY - h);
            currXOffset += kIconSpaceX;
        }

        if (isAdHoc) {
            const std::optional<QColor> strokeColor = colorOrDefault(style, "strokeColor", QColor(0, 0, 0));
            const std::optional<QColor> fillColor = colorOrDefault(style, "fillColor", QColor(255, 255, 255));
            pen.setStroke(std::nullopt);
            pen.setFill(strokeColor);
            drawStencil(c, context, "mxgraph.bpmn.ad_hoc", w * 0.5 + currXOffset + 1, h - offsetY + 4, 12, 6);
            currXOffset += kIconSpaceX;
            pen.setStroke(strokeColor);
            pen.setFill(fillColor);
            pen.setStrokeWidth(1);
        }
    }

    // mxShapeBpmn2SendMarker.paintVertexShape (:800): an envelope. The
    // translate is not undone, as upstream.
    static void paintEnvelope(Canvas& c, double x, double y, double w, double h)
    {
        c.translate(x, y);
        c.rect(0, 0, w, h);
        c.fillAndStroke();
        c.setShadow(false);
        c.begin();
        c.moveTo(0, 0);
        c.lineTo(w * 0.5, h * 0.5);
        c.lineTo(w, 0);
        c.stroke();
    }

    static void paintTaskMarker(Pen& pen, const ShapePaintContext& context, std::string_view marker, double inlet)
    {
        Canvas& c = pen.c;
        const ResolvedStyle& style = context.style;
        const std::optional<QColor> fillColor = colorOrDefault(style, "fillColor", QColor(255, 255, 255));

        if (marker == "service") {
            pen.setFill(fillColor);
            drawStencil(c, context, "mxgraph.bpmn.service_task", inlet + 2, inlet + 2, 16, 16);
        } else if (marker == "send") {
            const std::optional<QColor> strokeColor = colorOrDefault(style, "strokeColor", QColor(0, 0, 0));
            pen.setStroke(fillColor);
            pen.setFill(strokeColor);
            paintEnvelope(c, inlet + 4, inlet + 4, 18, 13);
        } else if (marker == "receive") {
            paintEnvelope(c, inlet + 4, inlet + 4, 18, 13);
        } else if (marker == "user") {
            drawStencil(c, context, "mxgraph.bpmn.user_task", inlet + 2, inlet + 2, 16, 16);
        } else if (marker == "manual") {
            drawStencil(c, context, "mxgraph.bpmn.manual_task", inlet + 3, inlet + 3, 18, 14);
        } else if (marker == "businessRule") {
            drawStencil(c, context, "mxgraph.bpmn.business_rule_task", inlet + 4, inlet + 4, 18, 14);
        } else if (marker == "script") {
            drawStencil(c, context, "mxgraph.bpmn.script_task", inlet + 3, inlet + 3, 19, 18);
        }
    }
};

} // namespace

void registerBpmnShapes(ShapeRegistry& registry)
{
    registry.add("mxgraph.bpmn.event", std::make_unique<BpmnEventShape>());
    registry.add("mxgraph.bpmn.shape", std::make_unique<BpmnLegacyShape>());
    registry.add("mxgraph.bpmn.gateway2", std::make_unique<BpmnGatewayShape>());
    registry.add("mxgraph.bpmn.task2", std::make_unique<BpmnTaskShape>());
}

} // namespace qraw::render::detail
