// mxgraph.basic.rect (mxShapeBasicRect2, js/grapheditor/Shapes.js:5996-7495):
// a rectangle whose four corners are square, rounded, snipped, inverted
// round or folded, with optional double or frame outline, per-side lines, and
// an inner fill. The corner and edge helpers below are the upstream
// prototype methods; the combination code in strictDrawRect2 is upstream's
// 16 cases for the visible sides. The helpers pass the corner style the way
// upstream's call sites do (some calls hand a neighbour's style), so
// behavior matches draw.io including its quirks.
//
// Port of js/grapheditor/Shapes.js (draw.io, Apache-2.0), modified: the
// function bodies are translated one to one, with `x`, `y` dropped (the
// canvas is translated to the shape origin before drawing).

#include "shapes_rect2.h"

#include <QColor>

namespace qraw::render::detail {

namespace {

constexpr QColor kClear(0, 0, 0, 0);

void moveNW(Canvas& c, double /*w*/, double /*h*/, std::string_view rectStyle, std::string_view topLeftStyle,
        double size, bool left)
{
    if ((topLeftStyle == "square" || (topLeftStyle == "default" && rectStyle == "square")) || !left) {
        c.moveTo(0, 0);
    } else {
        c.moveTo(0, size);
    }
}

void moveNE(Canvas& c, double w, double /*h*/, std::string_view rectStyle, std::string_view topRightStyle, double size,
        bool top)
{
    if ((topRightStyle == "square" || (topRightStyle == "default" && rectStyle == "square")) || !top) {
        c.moveTo(w, 0);
    } else {
        c.moveTo(w - size, 0);
    }
}

void moveSE(Canvas& c, double w, double h, std::string_view rectStyle, std::string_view bottomRightStyle, double size,
        bool right)
{
    if ((bottomRightStyle == "square" || (bottomRightStyle == "default" && rectStyle == "square")) || !right) {
        c.moveTo(w, h);
    } else {
        c.moveTo(w, h - size);
    }
}

void moveSW(Canvas& c, double /*w*/, double h, std::string_view rectStyle, std::string_view bottomLeftStyle,
        double size, bool bottom)
{
    if ((bottomLeftStyle == "square" || (bottomLeftStyle == "default" && rectStyle == "square")) || !bottom) {
        c.moveTo(0, h);
    } else {
        c.moveTo(size, h);
    }
}

void paintNW(Canvas& c, double /*w*/, double /*h*/, std::string_view rectStyle, std::string_view topLeftStyle,
        double size, bool left)
{
    if (!left) {
        c.lineTo(0, 0);
    } else if ((topLeftStyle == "rounded" || (topLeftStyle == "default" && rectStyle == "rounded")) ||
            (topLeftStyle == "invRound" || (topLeftStyle == "default" && rectStyle == "invRound"))) {
        bool inv = false;

        if (topLeftStyle == "rounded" || (topLeftStyle == "default" && rectStyle == "rounded")) {
            inv = true;
        }

        c.arcTo(size, size, 0, false, inv, size, 0);
    } else if ((topLeftStyle == "snip" || (topLeftStyle == "default" && rectStyle == "snip")) ||
            (topLeftStyle == "fold" || (topLeftStyle == "default" && rectStyle == "fold"))) {
        c.lineTo(size, 0);
    }
}

void paintTop(Canvas& c, double w, double /*h*/, std::string_view rectStyle, std::string_view topRightStyle,
        double size, bool right)
{
    if ((topRightStyle == "square" || (topRightStyle == "default" && rectStyle == "square")) || !right) {
        c.lineTo(w, 0);
    } else {
        c.lineTo(w - size, 0);
    }
}

void paintNE(Canvas& c, double w, double /*h*/, std::string_view rectStyle, std::string_view topRightStyle,
        double size, bool top)
{
    if (!top) {
        c.lineTo(w, 0);
    } else if ((topRightStyle == "rounded" || (topRightStyle == "default" && rectStyle == "rounded")) ||
            (topRightStyle == "invRound" || (topRightStyle == "default" && rectStyle == "invRound"))) {
        bool inv = false;

        if (topRightStyle == "rounded" || (topRightStyle == "default" && rectStyle == "rounded")) {
            inv = true;
        }

        c.arcTo(size, size, 0, false, inv, w, size);
    } else if ((topRightStyle == "snip" || (topRightStyle == "default" && rectStyle == "snip")) ||
            (topRightStyle == "fold" || (topRightStyle == "default" && rectStyle == "fold"))) {
        c.lineTo(w, size);
    }
}

void paintRight(Canvas& c, double w, double h, std::string_view rectStyle, std::string_view bottomRightStyle,
        double size, bool bottom)
{
    if ((bottomRightStyle == "square" || (bottomRightStyle == "default" && rectStyle == "square")) || !bottom) {
        c.lineTo(w, h);
    } else {
        c.lineTo(w, h - size);
    }
}

void paintLeft(Canvas& c, double /*w*/, double /*h*/, std::string_view rectStyle, std::string_view topLeftStyle,
        double size, bool top)
{
    if ((topLeftStyle == "square" || (topLeftStyle == "default" && rectStyle == "square")) || !top) {
        c.lineTo(0, 0);
    } else {
        c.lineTo(0, size);
    }
}

void paintSE(Canvas& c, double w, double h, std::string_view rectStyle, std::string_view bottomRightStyle, double size,
        bool right)
{
    if (!right) {
        c.lineTo(w, h);
    } else if ((bottomRightStyle == "rounded" || (bottomRightStyle == "default" && rectStyle == "rounded")) ||
            (bottomRightStyle == "invRound" || (bottomRightStyle == "default" && rectStyle == "invRound"))) {
        bool inv = false;

        if (bottomRightStyle == "rounded" || (bottomRightStyle == "default" && rectStyle == "rounded")) {
            inv = true;
        }

        c.arcTo(size, size, 0, false, inv, w - size, h);
    } else if ((bottomRightStyle == "snip" || (bottomRightStyle == "default" && rectStyle == "snip")) ||
            (bottomRightStyle == "fold" || (bottomRightStyle == "default" && rectStyle == "fold"))) {
        c.lineTo(w - size, h);
    }
}

void paintBottom(Canvas& c, double /*w*/, double h, std::string_view rectStyle, std::string_view bottomLeftStyle,
        double size, bool left)
{
    if ((bottomLeftStyle == "square" || (bottomLeftStyle == "default" && rectStyle == "square")) || !left) {
        c.lineTo(0, h);
    } else {
        c.lineTo(size, h);
    }
}

void paintSW(Canvas& c, double /*w*/, double h, std::string_view rectStyle, std::string_view bottomLeftStyle,
        double size, bool bottom)
{
    if (!bottom) {
        c.lineTo(0, h);
    } else if ((bottomLeftStyle == "rounded" || (bottomLeftStyle == "default" && rectStyle == "rounded")) ||
            (bottomLeftStyle == "invRound" || (bottomLeftStyle == "default" && rectStyle == "invRound"))) {
        bool inv = false;

        if (bottomLeftStyle == "rounded" || (bottomLeftStyle == "default" && rectStyle == "rounded")) {
            inv = true;
        }

        c.arcTo(size, size, 0, false, inv, 0, h - size);
    } else if ((bottomLeftStyle == "snip" || (bottomLeftStyle == "default" && rectStyle == "snip")) ||
            (bottomLeftStyle == "fold" || (bottomLeftStyle == "default" && rectStyle == "fold"))) {
        c.lineTo(0, h - size);
    }
}

void paintNWInner(Canvas& c, double /*w*/, double /*h*/, std::string_view rectStyle, std::string_view topLeftStyle,
        double size, double indent)
{
    if (topLeftStyle == "rounded" || (topLeftStyle == "default" && rectStyle == "rounded")) {
        c.arcTo(size - indent * 0.5, size - indent * 0.5, 0, false, false, indent, indent * 0.5 + size);
    } else if (topLeftStyle == "invRound" || (topLeftStyle == "default" && rectStyle == "invRound")) {
        c.arcTo(size + indent, size + indent, 0, false, true, indent, indent + size);
    } else if (topLeftStyle == "snip" || (topLeftStyle == "default" && rectStyle == "snip")) {
        c.lineTo(indent, indent * 0.5 + size);
    } else if (topLeftStyle == "fold" || (topLeftStyle == "default" && rectStyle == "fold")) {
        c.lineTo(indent + size, indent + size);
        c.lineTo(indent, indent + size);
    }
}

void paintTopInner(Canvas& c, double /*w*/, double /*h*/, std::string_view rectStyle, std::string_view topLeftStyle,
        double size, double indent, bool left, bool top)
{
    if (!left && !top) {
        c.lineTo(0, 0);
    } else if (!left && top) {
        c.lineTo(0, indent);
    } else if (left && !top) {
        c.lineTo(indent, 0);
    } else if (!left) {
        c.lineTo(0, indent);
    } else if (topLeftStyle == "square" || (topLeftStyle == "default" && rectStyle == "square")) {
        c.lineTo(indent, indent);
    } else if ((topLeftStyle == "rounded" || (topLeftStyle == "default" && rectStyle == "rounded")) ||
            (topLeftStyle == "snip" || (topLeftStyle == "default" && rectStyle == "snip"))) {
        c.lineTo(size + indent * 0.5, indent);
    } else {
        c.lineTo(size + indent, indent);
    }
}

void paintNEInner(Canvas& c, double w, double /*h*/, std::string_view rectStyle, std::string_view topRightStyle,
        double size, double indent)
{
    if (topRightStyle == "rounded" || (topRightStyle == "default" && rectStyle == "rounded")) {
        c.arcTo(size - indent * 0.5, size - indent * 0.5, 0, false, false, w - size - indent * 0.5, indent);
    } else if (topRightStyle == "invRound" || (topRightStyle == "default" && rectStyle == "invRound")) {
        c.arcTo(size + indent, size + indent, 0, false, true, w - size - indent, indent);
    } else if (topRightStyle == "snip" || (topRightStyle == "default" && rectStyle == "snip")) {
        c.lineTo(w - size - indent * 0.5, indent);
    } else if (topRightStyle == "fold" || (topRightStyle == "default" && rectStyle == "fold")) {
        c.lineTo(w - size - indent, size + indent);
        c.lineTo(w - size - indent, indent);
    }
}

void paintRightInner(Canvas& c, double w, double /*h*/, std::string_view rectStyle, std::string_view topRightStyle,
        double size, double indent, bool top, bool right)
{
    if (!top && !right) {
        c.lineTo(w, 0);
    } else if (!top && right) {
        c.lineTo(w - indent, 0);
    } else if (top && !right) {
        c.lineTo(w, indent);
    } else if (!top) {
        c.lineTo(w - indent, 0);
    } else if (topRightStyle == "square" || (topRightStyle == "default" && rectStyle == "square")) {
        c.lineTo(w - indent, indent);
    } else if ((topRightStyle == "rounded" || (topRightStyle == "default" && rectStyle == "rounded")) ||
            (topRightStyle == "snip" || (topRightStyle == "default" && rectStyle == "snip"))) {
        c.lineTo(w - indent, size + indent * 0.5);
    } else {
        c.lineTo(w - indent, size + indent);
    }
}

void paintLeftInner(Canvas& c, double /*w*/, double h, std::string_view rectStyle, std::string_view bottomLeftStyle,
        double size, double indent, bool bottom, bool left)
{
    if (!bottom && !left) {
        c.lineTo(0, h);
    } else if (!bottom && left) {
        c.lineTo(indent, h);
    } else if (bottom && !left) {
        c.lineTo(0, h - indent);
    } else if (!bottom) {
        c.lineTo(indent, h);
    } else if (bottomLeftStyle == "square" || (bottomLeftStyle == "default" && rectStyle == "square")) {
        c.lineTo(indent, h - indent);
    } else if ((bottomLeftStyle == "rounded" || (bottomLeftStyle == "default" && rectStyle == "rounded")) ||
            (bottomLeftStyle == "snip" || (bottomLeftStyle == "default" && rectStyle == "snip"))) {
        c.lineTo(indent, h - size - indent * 0.5);
    } else {
        c.lineTo(indent, h - size - indent);
    }
}

void paintSEInner(Canvas& c, double w, double h, std::string_view rectStyle, std::string_view bottomRightStyle,
        double size, double indent)
{
    if (bottomRightStyle == "rounded" || (bottomRightStyle == "default" && rectStyle == "rounded")) {
        c.arcTo(size - indent * 0.5, size - indent * 0.5, 0, false, false, w - indent, h - size - indent * 0.5);
    } else if (bottomRightStyle == "invRound" || (bottomRightStyle == "default" && rectStyle == "invRound")) {
        c.arcTo(size + indent, size + indent, 0, false, true, w - indent, h - size - indent);
    } else if (bottomRightStyle == "snip" || (bottomRightStyle == "default" && rectStyle == "snip")) {
        c.lineTo(w - indent, h - size - indent * 0.5);
    } else if (bottomRightStyle == "fold" || (bottomRightStyle == "default" && rectStyle == "fold")) {
        c.lineTo(w - size - indent, h - size - indent);
        c.lineTo(w - indent, h - size - indent);
    }
}

void paintBottomInner(Canvas& c, double w, double h, std::string_view rectStyle, std::string_view bottomRightStyle,
        double size, double indent, bool right, bool bottom)
{
    if (!right && !bottom) {
        c.lineTo(w, h);
    } else if (!right && bottom) {
        c.lineTo(w, h - indent);
    } else if (right && !bottom) {
        c.lineTo(w - indent, h);
    } else if ((bottomRightStyle == "square" || (bottomRightStyle == "default" && rectStyle == "square")) || !right) {
        c.lineTo(w - indent, h - indent);
    } else if ((bottomRightStyle == "rounded" || (bottomRightStyle == "default" && rectStyle == "rounded")) ||
            (bottomRightStyle == "snip" || (bottomRightStyle == "default" && rectStyle == "snip"))) {
        c.lineTo(w - size - indent * 0.5, h - indent);
    } else {
        c.lineTo(w - size - indent, h - indent);
    }
}

void paintSWInner(Canvas& c, double /*w*/, double h, std::string_view rectStyle, std::string_view bottomLeftStyle,
        double size, double indent, bool bottom)
{
    if (!bottom) {
        c.lineTo(indent, h);
    } else if (bottomLeftStyle == "square" || (bottomLeftStyle == "default" && rectStyle == "square")) {
        c.lineTo(indent, h - indent);
    } else if (bottomLeftStyle == "rounded" || (bottomLeftStyle == "default" && rectStyle == "rounded")) {
        c.arcTo(size - indent * 0.5, size - indent * 0.5, 0, false, false, size + indent * 0.5, h - indent);
    } else if (bottomLeftStyle == "invRound" || (bottomLeftStyle == "default" && rectStyle == "invRound")) {
        c.arcTo(size + indent, size + indent, 0, false, true, size + indent, h - indent);
    } else if (bottomLeftStyle == "snip" || (bottomLeftStyle == "default" && rectStyle == "snip")) {
        c.lineTo(size + indent * 0.5, h - indent);
    } else if (bottomLeftStyle == "fold" || (bottomLeftStyle == "default" && rectStyle == "fold")) {
        c.lineTo(indent + size, h - size - indent);
        c.lineTo(indent + size, h - indent);
    }
}

void moveSWInner(Canvas& c, double /*w*/, double h, std::string_view rectStyle, std::string_view bottomLeftStyle,
        double size, double indent, bool left)
{
    if (!left) {
        c.moveTo(0, h - indent);
    } else if (bottomLeftStyle == "square" || (bottomLeftStyle == "default" && rectStyle == "square")) {
        c.moveTo(indent, h - indent);
    } else if ((bottomLeftStyle == "rounded" || (bottomLeftStyle == "default" && rectStyle == "rounded")) ||
            (bottomLeftStyle == "snip" || (bottomLeftStyle == "default" && rectStyle == "snip"))) {
        c.moveTo(indent, h - size - indent * 0.5);
    } else if ((bottomLeftStyle == "invRound" || (bottomLeftStyle == "default" && rectStyle == "invRound")) ||
            (bottomLeftStyle == "fold" || (bottomLeftStyle == "default" && rectStyle == "fold"))) {
        c.moveTo(indent, h - size - indent);
    }
}

void lineSWInner(Canvas& c, double /*w*/, double h, std::string_view rectStyle, std::string_view bottomLeftStyle,
        double size, double indent, bool left)
{
    if (!left) {
        c.lineTo(0, h - indent);
    } else if (bottomLeftStyle == "square" || (bottomLeftStyle == "default" && rectStyle == "square")) {
        c.lineTo(indent, h - indent);
    } else if ((bottomLeftStyle == "rounded" || (bottomLeftStyle == "default" && rectStyle == "rounded")) ||
            (bottomLeftStyle == "snip" || (bottomLeftStyle == "default" && rectStyle == "snip"))) {
        c.lineTo(indent, h - size - indent * 0.5);
    } else if ((bottomLeftStyle == "invRound" || (bottomLeftStyle == "default" && rectStyle == "invRound")) ||
            (bottomLeftStyle == "fold" || (bottomLeftStyle == "default" && rectStyle == "fold"))) {
            c.lineTo(indent, h - size - indent);
    }
}

void moveSEInner(Canvas& c, double w, double h, std::string_view rectStyle, std::string_view bottomRightStyle,
        double size, double indent, bool bottom)
{
    if (!bottom) {
        c.moveTo(w - indent, h);
    } else if (bottomRightStyle == "square" || (bottomRightStyle == "default" && rectStyle == "square")) {
        c.moveTo(w - indent, h - indent);
    } else if ((bottomRightStyle == "rounded" || (bottomRightStyle == "default" && rectStyle == "rounded")) ||
            (bottomRightStyle == "snip" || (bottomRightStyle == "default" && rectStyle == "snip"))) {
        c.moveTo(w - indent, h - size - indent * 0.5);
    } else if ((bottomRightStyle == "invRound" || (bottomRightStyle == "default" && rectStyle == "invRound")) ||
            (bottomRightStyle == "fold" || (bottomRightStyle == "default" && rectStyle == "fold"))) {
        c.moveTo(w - indent, h - size - indent);
    }
}

void lineSEInner(Canvas& c, double w, double h, std::string_view rectStyle, std::string_view bottomRightStyle,
        double size, double indent, bool bottom)
{
    if (!bottom) {
        c.lineTo(w - indent, h);
    } else if (bottomRightStyle == "square" || (bottomRightStyle == "default" && rectStyle == "square")) {
        c.lineTo(w - indent, h - indent);
    } else if ((bottomRightStyle == "rounded" || (bottomRightStyle == "default" && rectStyle == "rounded")) ||
            (bottomRightStyle == "snip" || (bottomRightStyle == "default" && rectStyle == "snip"))) {
        c.lineTo(w - indent, h - size - indent * 0.5);
    } else if ((bottomRightStyle == "invRound" || (bottomRightStyle == "default" && rectStyle == "invRound")) ||
            (bottomRightStyle == "fold" || (bottomRightStyle == "default" && rectStyle == "fold"))) {
        c.lineTo(w - indent, h - size - indent);
    }
}

void moveNEInner(Canvas& c, double w, double /*h*/, std::string_view rectStyle, std::string_view topRightStyle,
        double size, double indent, bool right)
{
    if (!right) {
        c.moveTo(w, indent);
    } else if ((topRightStyle == "square" || (topRightStyle == "default" && rectStyle == "square")) || right) {
        c.moveTo(w - indent, indent);
    } else if ((topRightStyle == "rounded" || (topRightStyle == "default" && rectStyle == "rounded")) ||
            (topRightStyle == "snip" || (topRightStyle == "default" && rectStyle == "snip"))) {
        c.moveTo(w - indent, size + indent * 0.5);
    } else if ((topRightStyle == "invRound" || (topRightStyle == "default" && rectStyle == "invRound")) ||
            (topRightStyle == "fold" || (topRightStyle == "default" && rectStyle == "fold"))) {
        c.moveTo(w - indent, size + indent);
    }
}

void lineNEInner(Canvas& c, double w, double /*h*/, std::string_view rectStyle, std::string_view topRightStyle,
        double size, double indent, bool right)
{
    if (!right) {
        c.lineTo(w, indent);
    } else if ((topRightStyle == "square" || (topRightStyle == "default" && rectStyle == "square")) || right) {
        c.lineTo(w - indent, indent);
    } else if ((topRightStyle == "rounded" || (topRightStyle == "default" && rectStyle == "rounded")) ||
            (topRightStyle == "snip" || (topRightStyle == "default" && rectStyle == "snip"))) {
        c.lineTo(w - indent, size + indent * 0.5);
    } else if ((topRightStyle == "invRound" || (topRightStyle == "default" && rectStyle == "invRound")) ||
            (topRightStyle == "fold" || (topRightStyle == "default" && rectStyle == "fold"))) {
        c.lineTo(w - indent, size + indent);
    }
}

void moveNWInner(Canvas& c, double /*w*/, double /*h*/, std::string_view rectStyle, std::string_view topLeftStyle,
        double size, double indent, bool top, bool left)
{
    if (!top && !left) {
        c.moveTo(0, 0);
    } else if (!top && left) {
        c.moveTo(indent, 0);
    } else if (top && !left) {
        c.moveTo(0, indent);
    } else if (topLeftStyle == "square" || (topLeftStyle == "default" && rectStyle == "square")) {
        c.moveTo(indent, indent);
    } else if ((topLeftStyle == "rounded" || (topLeftStyle == "default" && rectStyle == "rounded")) ||
            (topLeftStyle == "snip" || (topLeftStyle == "default" && rectStyle == "snip"))) {
        c.moveTo(indent, size + indent * 0.5);
    } else if ((topLeftStyle == "invRound" || (topLeftStyle == "default" && rectStyle == "invRound")) ||
            (topLeftStyle == "fold" || (topLeftStyle == "default" && rectStyle == "fold"))) {
        c.moveTo(indent, size + indent);
    }
}

void lineNWInner(Canvas& c, double /*w*/, double /*h*/, std::string_view rectStyle, std::string_view topLeftStyle,
        double size, double indent, bool top, bool left)
{
    if (!top && !left) {
        c.lineTo(0, 0);
    } else if (!top && left) {
        c.lineTo(indent, 0);
    } else if (top && !left) {
        c.lineTo(0, indent);
    } else if (topLeftStyle == "square" || (topLeftStyle == "default" && rectStyle == "square")) {
        c.lineTo(indent, indent);
    } else if ((topLeftStyle == "rounded" || (topLeftStyle == "default" && rectStyle == "rounded")) ||
            (topLeftStyle == "snip" || (topLeftStyle == "default" && rectStyle == "snip"))) {
        c.lineTo(indent, size + indent * 0.5);
    } else if ((topLeftStyle == "invRound" || (topLeftStyle == "default" && rectStyle == "invRound")) ||
            (topLeftStyle == "fold" || (topLeftStyle == "default" && rectStyle == "fold"))) {
        c.lineTo(indent, size + indent);
    }
}

void paintFolds(Canvas& c, double w, double h, std::string_view rectStyle, std::string_view topLeftStyle,
        std::string_view topRightStyle, std::string_view bottomRightStyle, std::string_view bottomLeftStyle,
        double size, bool top, bool right, bool bottom, bool left)
{
    if (rectStyle == "fold" || topLeftStyle == "fold" || topRightStyle == "fold" || bottomRightStyle == "fold" ||
        bottomLeftStyle == "fold") {
        if ((topLeftStyle == "fold" || (topLeftStyle == "default" && rectStyle == "fold")) && (top && left)) {
            c.moveTo(0, size);
            c.lineTo(size, size);
            c.lineTo(size, 0);
        }

        if ((topRightStyle == "fold" || (topRightStyle == "default" && rectStyle == "fold")) && (top && right)) {
            c.moveTo(w - size, 0);
            c.lineTo(w - size, size);
            c.lineTo(w, size);
        }

        if ((bottomRightStyle == "fold" || (bottomRightStyle == "default" && rectStyle == "fold")) &&
            (bottom && right)) {
            c.moveTo(w - size, h);
            c.lineTo(w - size, h - size);
            c.lineTo(w, h - size);
        }

        if ((bottomLeftStyle == "fold" || (bottomLeftStyle == "default" && rectStyle == "fold")) && (bottom && left)) {
            c.moveTo(0, h - size);
            c.lineTo(size, h - size);
            c.lineTo(size, h);
        }
    }
}


} // namespace

// mxShapeBasicRect2.strictDrawShape (Shapes.js:6106). `os` replaces style
// values where upstream callers pass them (BPMN tasks).
void strictDrawRect2(Canvas& c, const ResolvedStyle& style, double w, double h, const Rect2Overrides& os)
{
    const std::string_view rectStyle = style.string("rectStyle", "square");
    const bool absoluteCornerSize = style.flag("absoluteCornerSize", true);
    double size = clampTo(style.number("size", 10), 0, w);
    const std::string_view rectOutline = os.rectOutline ? *os.rectOutline : style.string("rectOutline", "single");
    double indent = os.indent ? *os.indent : clampTo(style.number("indent", 2), 0, w);
    const bool dashed = os.dashed ? *os.dashed : style.dashed();
    const std::string_view dashPattern = style.string("dashPattern");
    const double relIndent = clampTo(indent, 0, 50);
    const bool top = style.flag("top", true);
    const bool right = style.flag("right", true);
    const bool bottom = style.flag("bottom", true);
    const bool left = style.flag("left", true);
    const std::string_view topLeftStyle = style.string("topLeftStyle", "default");
    const std::string_view topRightStyle = style.string("topRightStyle", "default");
    const std::string_view bottomRightStyle = style.string("bottomRightStyle", "default");
    const std::string_view bottomLeftStyle = style.string("bottomLeftStyle", "default");
    const bool fillIsNone = style.string("fillColor", "#ffffff") == "none";
    const double strokeWidth = os.strokeWidth ? *os.strokeWidth : style.strokeWidth();
    const std::optional<QColor> fillColor2 = style.color("fillColor2");
    const std::optional<QColor> gradientColor2 = style.color("gradientColor2");
    const std::string_view gdir2 = style.string("gradientDirection2", "south");
    // Upstream hands the percentage `opacity` to setGradient as an alpha, so
    // anything from 1 up is fully opaque there.
    const double innerAlpha = clampTo(style.opacity(), 0, 1);

    const double relSize = clampTo(size, 0, 50);

    c.setDashed(dashed);
    if (!dashPattern.empty())
        c.setDashPattern(dashPattern);
    c.setStrokeWidth(strokeWidth);

    size = std::min({h * 0.5, w * 0.5, size});
    if (!absoluteCornerSize)
        size = relSize * std::min(w, h) / 100;
    size = std::min(size, std::min(w, h) * 0.5);
    if (!absoluteCornerSize)
        indent = relIndent * std::min(w, h) / 100;
    indent = std::min(indent, std::min(w, h) * 0.5 - size);


    if ((top || right || bottom || left) && rectOutline != "frame") {
        //outline fill
        c.begin();
        if (!top) {
            c.moveTo(0, 0);
        } else {
            moveNW(c, w, h, rectStyle, topLeftStyle, size, left);
        }

        if (top) {
            paintNW(c, w, h, rectStyle, topLeftStyle, size, left);
        }

        paintTop(c, w, h, rectStyle, topRightStyle, size, right);

        if (right) {
            paintNE(c, w, h, rectStyle, topRightStyle, size, top);
        }

        paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);

        if (bottom) {
            paintSE(c, w, h, rectStyle, bottomRightStyle, size, right);
        }

        paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);

        if (left) {
            paintSW(c, w, h, rectStyle, bottomLeftStyle, size, bottom);
        }

        paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);
        c.close();
        c.fill();

        c.setShadow(false);

        //inner fill
        c.setFillColor(fillColor2);
        const double alpha1 = fillColor2 ? innerAlpha : 0;
        const double alpha2 = gradientColor2 ? innerAlpha : 0;
        c.setGradient(fillColor2.value_or(kClear), gradientColor2.value_or(kClear), {0, 0, w, h},
                      parseGradientDirection(gdir2), alpha1, alpha2);

        c.begin();

        if (!top) {
            c.moveTo(indent, 0);
        } else {
            moveNWInner(c, w, h, rectStyle, topLeftStyle, size, indent, top, left);
        }

        paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);

        if (left && bottom) {
            paintSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom);
        }

        paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);

        if (bottom && right) {
            paintSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent);
        }

        paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);

        if (right && top) {
            paintNEInner(c, w, h, rectStyle, topRightStyle, size, indent);
        }

        paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);

        if (top && left) {
            paintNWInner(c, w, h, rectStyle, topLeftStyle, size, indent);
        }

        c.fill();

        if (fillIsNone) {
            c.begin();
            paintFolds(c, w, h, rectStyle, topLeftStyle, topRightStyle, bottomRightStyle, bottomLeftStyle, size, top,
                    right, bottom, left);
            c.stroke();
        }
    }

    //draw all the combinations
    if (!top && !right && !bottom && left) {
        if (rectOutline != "frame") {
            c.begin();
            moveSW(c, w, h, rectStyle, topLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);

            if (rectOutline == "double") {
                moveNWInner(c, w, h, rectStyle, topLeftStyle, size, indent, top, left);
                paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
            }

            c.stroke();
        } else {
            c.begin();
            moveSW(c, w, h, rectStyle, topLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);
            lineNWInner(c, w, h, rectStyle, topLeftStyle, size, indent, top, left);
            paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
            c.close();
            c.fillAndStroke();
        }
    } else if (!top && !right && bottom && !left) {
        if (rectOutline != "frame") {
            c.begin();
            moveSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);

            if (rectOutline == "double") {
                moveSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, left);
                paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
            }

            c.stroke();
        } else {
            c.begin();
            moveSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);
            lineSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, left);
            paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
            c.close();
            c.fillAndStroke();
        }
    } else if (!top && !right && bottom && left) {
        if (rectOutline != "frame") {
            c.begin();
            moveSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);
            paintSW(c, w, h, rectStyle, bottomLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);

            if (rectOutline == "double") {
                moveNWInner(c, w, h, rectStyle, topLeftStyle, size, indent, top, left);
                paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
                paintSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom);
                paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
            }

            c.stroke();
        } else {
            c.begin();
            moveSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);
            paintSW(c, w, h, rectStyle, bottomLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);
            lineNWInner(c, w, h, rectStyle, topLeftStyle, size, indent, top, left);
            paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
            paintSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom);
            paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
            c.close();
            c.fillAndStroke();
        }
    } else if (!top && right && !bottom && !left) {
        if (rectOutline != "frame") {
            c.begin();
            moveNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);

            if (rectOutline == "double") {
                moveSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent, bottom);
                paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
            }

            c.stroke();
        } else {
            c.begin();
            moveNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);
            lineSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent, bottom);
            paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
            c.close();
            c.fillAndStroke();
        }
    } else if (!top && right && !bottom && left) {
        if (rectOutline != "frame") {
            c.begin();
            moveSW(c, w, h, rectStyle, topLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);

            if (rectOutline == "double") {
                moveNWInner(c, w, h, rectStyle, topLeftStyle, size, indent, top, left);
                paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
            }

            c.stroke();

            c.begin();
            moveNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);

            if (rectOutline == "double") {
                moveSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent, bottom);
                paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
            }

            c.stroke();
        } else {
            c.begin();
            moveSW(c, w, h, rectStyle, topLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);
            lineNWInner(c, w, h, rectStyle, topLeftStyle, size, indent, top, left);
            paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
            c.close();
            c.fillAndStroke();

            c.begin();
            moveNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);
            lineSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent, bottom);
            paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
            c.close();
            c.fillAndStroke();
        }
    } else if (!top && right && bottom && !left) {
        if (rectOutline != "frame") {
            c.begin();
            moveNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);
            paintSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);

            if (rectOutline == "double") {
                moveSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, left);
                paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
                paintSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent);
                paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
            }

            c.stroke();
        } else {
            c.begin();
            moveNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);
            paintSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);
            lineSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, left);
            paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
            paintSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent);
            paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
            c.close();
            c.fillAndStroke();
        }
    } else if (!top && right && bottom && left) {
        if (rectOutline != "frame") {
            c.begin();
            moveNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);
            paintSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);
            paintSW(c, w, h, rectStyle, bottomLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);

            if (rectOutline == "double") {
                moveNWInner(c, w, h, rectStyle, topLeftStyle, size, indent, top, left);
                paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
                paintSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom);
                paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
                paintSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent);
                paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
            }

            c.stroke();
        } else {
            c.begin();
            moveNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);
            paintSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);
            paintSW(c, w, h, rectStyle, bottomLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);
            lineNWInner(c, w, h, rectStyle, topLeftStyle, size, indent, top, left);
            paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
            paintSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom);
            paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
            paintSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent);
            paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
            c.close();
            c.fillAndStroke();
        }
    } else if (top && !right && !bottom && !left) {
        if (rectOutline != "frame") {
            c.begin();
            moveNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);

            if (rectOutline == "double") {
                moveNEInner(c, w, h, rectStyle, topRightStyle, size, indent, right);
                paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
            }

            c.stroke();
        } else {
            c.begin();
            moveNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);
            lineNEInner(c, w, h, rectStyle, topRightStyle, size, indent, right);
            paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
            c.close();
            c.fillAndStroke();
        }
    } else if (top && !right && !bottom && left) {
        if (rectOutline != "frame") {
            c.begin();
            moveSW(c, w, h, rectStyle, bottomLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);
            paintNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);

            if (rectOutline == "double") {
                moveNEInner(c, w, h, rectStyle, topRightStyle, size, indent, right);
                paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
                paintNWInner(c, w, h, rectStyle, topLeftStyle, size, indent);
                paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
            }

            c.stroke();
        } else {
            c.begin();
            moveSW(c, w, h, rectStyle, bottomLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);
            paintNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);
            lineNEInner(c, w, h, rectStyle, topRightStyle, size, indent, right);
            paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
            paintNWInner(c, w, h, rectStyle, topLeftStyle, size, indent);
            paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
            c.close();
            c.fillAndStroke();
        }
    } else if (top && !right && bottom && !left) {
        if (rectOutline != "frame") {
            c.begin();
            moveNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);

            if (rectOutline == "double") {
                moveNEInner(c, w, h, rectStyle, topRightStyle, size, indent, right);
                paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
            }

            c.stroke();

            c.begin();
            moveSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);

            if (rectOutline == "double") {
                moveSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, left);
                paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
            }

            c.stroke();
        } else {
            c.begin();
            moveNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);
            lineNEInner(c, w, h, rectStyle, topRightStyle, size, indent, right);
            paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
            c.close();
            c.fillAndStroke();

            c.begin();
            moveSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);
            lineSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, left);
            paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
            c.close();
            c.fillAndStroke();
        }
    } else if (top && !right && bottom && left) {
        if (rectOutline != "frame") {
            c.begin();
            moveSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);
            paintSW(c, w, h, rectStyle, bottomLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);
            paintNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);

            if (rectOutline == "double") {
                moveNEInner(c, w, h, rectStyle, topRightStyle, size, indent, right);
                paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
                paintNWInner(c, w, h, rectStyle, topLeftStyle, size, indent);
                paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
                paintSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom);
                paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
            }

            c.stroke();
        } else {
            c.begin();
            moveSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);
            paintSW(c, w, h, rectStyle, bottomLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);
            paintNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);
            lineNEInner(c, w, h, rectStyle, topRightStyle, size, indent, right);
            paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
            paintNWInner(c, w, h, rectStyle, topLeftStyle, size, indent);
            paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
            paintSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom);
            paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
            c.close();
            c.fillAndStroke();
        }
    } else if (top && right && !bottom && !left) {
        if (rectOutline != "frame") {
            c.begin();
            moveNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);
            paintNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);

            if (rectOutline == "double") {
                moveSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent, bottom);
                paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
                paintNEInner(c, w, h, rectStyle, topRightStyle, size, indent);
                paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
            }

            c.stroke();
        } else {
            c.begin();
            moveNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);
            paintNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);
            lineSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent, bottom);
            paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
            paintNEInner(c, w, h, rectStyle, topRightStyle, size, indent);
            paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
            c.close();
            c.fillAndStroke();
        }
    } else if (top && right && !bottom && left) {
        if (rectOutline != "frame") {
            c.begin();
            moveSW(c, w, h, rectStyle, bottomLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);
            paintNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);
            paintNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);

            if (rectOutline == "double") {
                moveSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent, bottom);
                paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
                paintNEInner(c, w, h, rectStyle, topRightStyle, size, indent);
                paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
                paintNWInner(c, w, h, rectStyle, topLeftStyle, size, indent);
                paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
            }

            c.stroke();
        } else {
            c.begin();
            moveSW(c, w, h, rectStyle, bottomLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);
            paintNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);
            paintNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);
            lineSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent, bottom);
            paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
            paintNEInner(c, w, h, rectStyle, topRightStyle, size, indent);
            paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
            paintNWInner(c, w, h, rectStyle, topLeftStyle, size, indent);
            paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
            c.close();
            c.fillAndStroke();
        }
    } else if (top && right && bottom && !left) {
        if (rectOutline != "frame") {
            c.begin();
            moveNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);
            paintNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);
            paintSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);

            if (rectOutline == "double") {
                moveSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, left);
                paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
                paintSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent);
                paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
                paintNEInner(c, w, h, rectStyle, topRightStyle, size, indent);
                paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
            }

            c.stroke();
        } else {
            c.begin();
            moveNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);
            paintNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);
            paintSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);
            lineSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, left);
            paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
            paintSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent);
            paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
            paintNEInner(c, w, h, rectStyle, topRightStyle, size, indent);
            paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
            c.close();
            c.fillAndStroke();
        }
    } else if (top && right && bottom && left) {
        if (rectOutline != "frame") {
            c.begin();
            moveNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);
            paintNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);
            paintSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);
            paintSW(c, w, h, rectStyle, bottomLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);
            c.close();

            if (rectOutline == "double") {
                moveSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, left);
                paintSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom);
                paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
                paintSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent);
                paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
                paintNEInner(c, w, h, rectStyle, topRightStyle, size, indent);
                paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
                paintNWInner(c, w, h, rectStyle, topLeftStyle, size, indent);
                paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
                c.close();
            }

            c.stroke();
        } else {
            c.begin();
            moveNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintNW(c, w, h, rectStyle, topLeftStyle, size, left);
            paintTop(c, w, h, rectStyle, topRightStyle, size, right);
            paintNE(c, w, h, rectStyle, topRightStyle, size, top);
            paintRight(c, w, h, rectStyle, bottomRightStyle, size, bottom);
            paintSE(c, w, h, rectStyle, bottomRightStyle, size, right);
            paintBottom(c, w, h, rectStyle, bottomLeftStyle, size, left);
            paintSW(c, w, h, rectStyle, bottomLeftStyle, size, bottom);
            paintLeft(c, w, h, rectStyle, topLeftStyle, size, top);
            c.close();
            moveSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, left);
            paintSWInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom);
            paintBottomInner(c, w, h, rectStyle, bottomRightStyle, size, indent, right, bottom);
            paintSEInner(c, w, h, rectStyle, bottomRightStyle, size, indent);
            paintRightInner(c, w, h, rectStyle, topRightStyle, size, indent, top, right);
            paintNEInner(c, w, h, rectStyle, topRightStyle, size, indent);
            paintTopInner(c, w, h, rectStyle, topLeftStyle, size, indent, left, top);
            paintNWInner(c, w, h, rectStyle, topLeftStyle, size, indent);
            paintLeftInner(c, w, h, rectStyle, bottomLeftStyle, size, indent, bottom, left);
            c.close();
            c.fillAndStroke();
        }
    }

    c.begin();
    paintFolds(c, w, h, rectStyle, topLeftStyle, topRightStyle, bottomRightStyle, bottomLeftStyle, size, top, right,
            bottom, left);
    c.stroke();
}

namespace {

// The `mxgraph.basic.rect` painter (Shapes.js:6099, paintVertexShape).
class Rect2Shape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        c.translate(x, y);
        strictDrawRect2(c, context.style, w, h, {});
    }
};

} // namespace

void registerRect2Shapes(ShapeRegistry& registry)
{
    registry.add("mxgraph.basic.rect", std::make_unique<Rect2Shape>());
}

} // namespace qraw::render::detail
