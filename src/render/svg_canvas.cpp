// SvgCanvas: our own SVG writer (DECISIONS.md D-021).
//
// Port of mxgraph/src/util/mxSvgCanvas2D.js and mxAbstractCanvas2D.js
// (draw.io, Apache-2.0), modified: it writes text instead of DOM nodes,
// numbers go through svg::number (mxSvgCanvas2D.format), ids are
// counter-based and the attribute order of every element is fixed:
//
//   fill, fill-opacity, stroke, stroke-opacity, stroke-width,
//   stroke-dasharray, stroke-linecap, stroke-linejoin, stroke-miterlimit,
//   opacity, transform, pointer-events
//
// Attributes that equal the SVG default are left out (stroke-width 1,
// butt caps, miter joins). `opacity` is only written for shadows (shapes use
// fill-opacity/stroke-opacity, like upstream). cap and miter limit are
// written for <path> only, as upstream does.

#include "qraw/render/svg_canvas.h"

#include "svg_canvas_impl.h"

#include <QByteArray>

#include <algorithm>
#include <cmath>
#include <sstream>

namespace qraw::render {

using svg::number;
using svg::Tag;

namespace {

template <typename... Ts>
bool allFinite(Ts... values)
{
    return (std::isfinite(values) && ...);
}

constexpr double kShadowAlpha = 0.25; // mxConstants.SHADOW_OPACITY as used by Graph.js
constexpr double kShadowDx = 2;       // mxConstants.SHADOW_OFFSET_X
constexpr double kShadowDy = 3;       // mxConstants.SHADOW_OFFSET_Y
constexpr double kMinStrokeWidth = 1; // mxSvgCanvas2D.minStrokeWidth

std::string directionName(GradientDirection d)
{
    switch (d) {
    case GradientDirection::South: return "south";
    case GradientDirection::North: return "north";
    case GradientDirection::East: return "east";
    case GradientDirection::West: return "west";
    case GradientDirection::Radial: return "radial";
    }
    return "south";
}

// mxSvgCanvas2D.createDashPattern: each number times `scale`, rounded to two
// decimals; entries that are not numbers are ignored.
std::string dashArray(const std::string& pattern, double scale)
{
    std::istringstream in(pattern);
    std::string token;
    std::string out;
    while (in >> token) {
        try {
            std::size_t used = 0;
            const double value = std::stod(token, &used);
            if (used != token.size() || !std::isfinite(value))
                continue;
            if (!out.empty())
                out += ' ';
            out += number(value * scale);
        } catch (...) {
            // not a number, e.g. "none" in stencils
        }
    }
    return out;
}

std::string sniffMimeType(const QByteArray& bytes)
{
    if (bytes.startsWith("\x89PNG"))
        return "image/png";
    if (bytes.startsWith("\xFF\xD8"))
        return "image/jpeg";
    if (bytes.startsWith("GIF8"))
        return "image/gif";
    if (bytes.startsWith("BM"))
        return "image/bmp";
    if (bytes.startsWith("RIFF") && bytes.mid(8, 4) == "WEBP")
        return "image/webp";
    const QByteArray head = bytes.left(512).trimmed();
    if (head.startsWith("<svg") || head.startsWith("<?xml"))
        return "image/svg+xml";
    return {};
}

} // namespace

// ------------------------------------------------------------------ state

void SvgCanvas::Impl::rotate(double theta, bool flipH, bool flipV, double cx, double cy)
{
    if (!allFinite(theta, cx, cy) || (theta == 0 && !flipH && !flipV))
        return;
    cx = tx(cx);
    cy = ty(cy);
    if (flipH && flipV) {
        theta += 180;
    } else if (flipH != flipV) {
        const double translateX = flipH ? cx : 0;
        const double translateY = flipV ? cy : 0;
        state.transform += "translate(" + number(translateX) + "," + number(translateY) + ")scale(" +
                           number(flipH ? -1 : 1) + "," + number(flipV ? -1 : 1) + ")translate(" +
                           number(-translateX) + "," + number(-translateY) + ")";
    }
    if (flipH ? !flipV : flipV)
        theta = -theta;
    if (theta != 0)
        state.transform += "rotate(" + number(theta) + "," + number(cx) + "," + number(cy) + ")";
}

// ------------------------------------------------------------------ path

void SvgCanvas::Impl::resetPath()
{
    primitive_ = Primitive::None;
    d_.clear();
    hasCurrent_ = false;
}

void SvgCanvas::Impl::begin() { resetPath(); }

void SvgCanvas::Impl::appendOp(const std::string& op)
{
    if (!d_.empty())
        d_ += ' ';
    d_ += op;
}

void SvgCanvas::Impl::moveTo(double x, double y)
{
    if (!allFinite(x, y))
        return;
    demotePrimitive();
    appendOp("M " + number(tx(x)) + " " + number(ty(y)));
    current_ = subpathStart_ = {x, y};
    hasCurrent_ = true;
}

void SvgCanvas::Impl::startSubpathAt(double x, double y) { moveTo(x, y); }

void SvgCanvas::Impl::lineTo(double x, double y)
{
    if (!allFinite(x, y))
        return;
    demotePrimitive();
    if (!hasCurrent_) {
        startSubpathAt(x, y); // geometry::Path: a drawing op without a current point starts a subpath
        return;
    }
    appendOp("L " + number(tx(x)) + " " + number(ty(y)));
    current_ = {x, y};
}

void SvgCanvas::Impl::quadTo(double x1, double y1, double x2, double y2)
{
    if (!allFinite(x1, y1, x2, y2))
        return;
    demotePrimitive();
    if (!hasCurrent_)
        startSubpathAt(x1, y1);
    appendOp("Q " + number(tx(x1)) + " " + number(ty(y1)) + " " + number(tx(x2)) + " " + number(ty(y2)));
    current_ = {x2, y2};
}

void SvgCanvas::Impl::curveTo(double x1, double y1, double x2, double y2, double x3, double y3)
{
    if (!allFinite(x1, y1, x2, y2, x3, y3))
        return;
    demotePrimitive();
    if (!hasCurrent_)
        startSubpathAt(x1, y1);
    appendOp("C " + number(tx(x1)) + " " + number(ty(y1)) + " " + number(tx(x2)) + " " + number(ty(y2)) + " " +
             number(tx(x3)) + " " + number(ty(y3)));
    current_ = {x3, y3};
}

void SvgCanvas::Impl::arcTo(double rx, double ry, double angle, bool largeArc, bool sweep, double x, double y)
{
    if (!allFinite(rx, ry, angle, x, y))
        return;
    if (!hasCurrent_ && primitive_ == Primitive::None) {
        moveTo(x, y);
        return;
    }
    demotePrimitive();
    // mxAbstractCanvas2D.arcTo: converted to curves from the last point.
    geometry::Path arc;
    arc.moveTo(current_);
    arc.arcTo(rx, ry, angle, largeArc, sweep, {x, y});
    const auto& elements = arc.elements();
    for (std::size_t i = 1; i < elements.size(); ++i) {
        const geometry::PathElement& e = elements[i];
        if (e.kind == geometry::PathElement::Kind::CubicTo)
            curveTo(e.p[0].x, e.p[0].y, e.p[1].x, e.p[1].y, e.p[2].x, e.p[2].y);
        else if (e.kind == geometry::PathElement::Kind::LineTo)
            lineTo(e.p[0].x, e.p[0].y);
    }
}

void SvgCanvas::Impl::close()
{
    demotePrimitive();
    if (d_.empty())
        return;
    appendOp("Z");
    current_ = subpathStart_;
}

void SvgCanvas::Impl::addPrimitive(Primitive kind, const PrimitiveBox& box)
{
    if (primitive_ == Primitive::None && d_.empty()) {
        primitive_ = kind;
        box_ = box;
        hasCurrent_ = true;
        return;
    }
    demotePrimitive();
    primitive_ = kind;
    box_ = box;
    demotePrimitive();
}

void SvgCanvas::Impl::demotePrimitive()
{
    if (primitive_ == Primitive::None)
        return;
    geometry::Path path;
    const geometry::Rect r{box_.x, box_.y, box_.w, box_.h};
    switch (primitive_) {
    case Primitive::Rect: path.addRect(r); break;
    case Primitive::RoundRect: path.addRoundedRect(r, box_.rx, box_.ry); break;
    case Primitive::Ellipse: path.addEllipse(r); break;
    case Primitive::None: break;
    }
    appendElements(path, false);
    primitive_ = Primitive::None;
}

void SvgCanvas::Impl::appendElements(const geometry::Path& path, bool transform)
{
    const auto px = [&](double v) { return number(transform ? tx(v) : v); };
    const auto py = [&](double v) { return number(transform ? ty(v) : v); };
    using Kind = geometry::PathElement::Kind;
    for (const geometry::PathElement& e : path.elements()) {
        switch (e.kind) {
        case Kind::MoveTo:
            appendOp("M " + px(e.p[0].x) + " " + py(e.p[0].y));
            current_ = subpathStart_ = e.p[0];
            break;
        case Kind::LineTo:
            appendOp("L " + px(e.p[0].x) + " " + py(e.p[0].y));
            current_ = e.p[0];
            break;
        case Kind::QuadTo:
            appendOp("Q " + px(e.p[0].x) + " " + py(e.p[0].y) + " " + px(e.p[1].x) + " " + py(e.p[1].y));
            current_ = e.p[1];
            break;
        case Kind::CubicTo:
            appendOp("C " + px(e.p[0].x) + " " + py(e.p[0].y) + " " + px(e.p[1].x) + " " + py(e.p[1].y) + " " +
                     px(e.p[2].x) + " " + py(e.p[2].y));
            current_ = e.p[2];
            break;
        case Kind::Close:
            appendOp("Z");
            current_ = subpathStart_;
            break;
        }
    }
    if (!path.elements().empty())
        hasCurrent_ = true;
}

void SvgCanvas::Impl::rect(double x, double y, double w, double h)
{
    if (!allFinite(x, y, w, h) || w < 0 || h < 0)
        return;
    PrimitiveBox box{tx(x), ty(y), w * state.scale, h * state.scale, 0, 0};
    addPrimitive(Primitive::Rect, box);
    current_ = subpathStart_ = {x, y};
}

void SvgCanvas::Impl::roundrect(double x, double y, double w, double h, double dx, double dy)
{
    if (!allFinite(x, y, w, h, dx, dy) || w < 0 || h < 0)
        return;
    // Like upstream, rounding is an SVG corner radius (rx/ry), not quads.
    PrimitiveBox box{tx(x), ty(y), w * state.scale, h * state.scale, std::max(0.0, dx) * state.scale,
                     std::max(0.0, dy) * state.scale};
    addPrimitive(Primitive::RoundRect, box);
    current_ = subpathStart_ = {x + dx, y};
}

void SvgCanvas::Impl::ellipse(double x, double y, double w, double h)
{
    if (!allFinite(x, y, w, h) || w < 0 || h < 0)
        return;
    PrimitiveBox box{tx(x), ty(y), w * state.scale, h * state.scale, 0, 0};
    addPrimitive(Primitive::Ellipse, box);
    current_ = subpathStart_ = {x + w, y + h / 2};
}

void SvgCanvas::Impl::addPath(const geometry::Path& path)
{
    demotePrimitive();
    appendElements(path, true);
}

// ------------------------------------------------------------------ painting

std::string SvgCanvas::Impl::clipPathId(double x, double y, double w, double h)
{
    const std::string key = number(x) + " " + number(y) + " " + number(w) + " " + number(h);
    auto it = clipIds.find(key);
    if (it != clipIds.end())
        return it->second;
    const std::string id = nextId();
    defs += Tag("clipPath").attr("id", id).open() + "\n";
    defs += Tag("rect").attr("x", x).attr("y", y).attr("width", w).attr("height", h).selfClosed() + "\n";
    defs += "</clipPath>\n";
    clipIds.emplace(key, id);
    return id;
}

// mxSvgCanvas2D.getSvgGradient / createSvgGradient. Identical gradients are
// written once.
std::string SvgCanvas::Impl::gradientId(const Gradient& g)
{
    const std::string key = svg::hexColor(g.start) + " " + number(g.startAlpha) + " " + svg::hexColor(g.end) + " " +
                            number(g.endAlpha) + " " + directionName(g.direction);
    auto it = gradientIds.find(key);
    if (it != gradientIds.end())
        return it->second;

    const std::string id = nextId();
    if (g.direction == GradientDirection::Radial) {
        defs += Tag("radialGradient").attr("id", id).attr("cx", "50%").attr("cy", "50%").attr("r", "50%").open();
    } else {
        const char* x1 = g.direction == GradientDirection::West ? "100%" : "0%";
        const char* y1 = g.direction == GradientDirection::North ? "100%" : "0%";
        const char* x2 = g.direction == GradientDirection::East ? "100%" : "0%";
        const char* y2 = g.direction == GradientDirection::South ? "100%" : "0%";
        defs += Tag("linearGradient").attr("id", id).attr("x1", x1).attr("y1", y1).attr("x2", x2).attr("y2", y2).open();
    }
    defs += '\n';
    defs += Tag("stop")
                .attr("offset", "0%")
                .attr("stop-color", svg::hexColor(g.start))
                .attr("stop-opacity", g.startAlpha)
                .selfClosed() +
            "\n";
    defs += Tag("stop")
                .attr("offset", "100%")
                .attr("stop-color", svg::hexColor(g.end))
                .attr("stop-opacity", g.endAlpha)
                .selfClosed() +
            "\n";
    defs += g.direction == GradientDirection::Radial ? "</radialGradient>\n" : "</linearGradient>\n";
    gradientIds.emplace(key, id);
    return id;
}

std::string SvgCanvas::Impl::buildElement(bool filled, bool stroked, bool shadow)
{
    const State& s = state;
    const bool hasFill = filled && (s.gradient || (s.fill && s.fill->alpha() > 0));
    const bool hasStroke = stroked && s.stroke && s.stroke->alpha() > 0;
    if (!hasFill && !hasStroke)
        return {};

    const bool isPath = primitive_ == Primitive::None;
    Tag tag(isPath                              ? "path"
            : primitive_ == Primitive::Ellipse ? "ellipse"
                                               : "rect");
    switch (primitive_) {
    case Primitive::None: tag.attr("d", d_); break;
    case Primitive::Rect:
        tag.attr("x", box_.x).attr("y", box_.y).attr("width", box_.w).attr("height", box_.h);
        break;
    case Primitive::RoundRect:
        tag.attr("x", box_.x).attr("y", box_.y).attr("width", box_.w).attr("height", box_.h);
        if (box_.rx > 0)
            tag.attr("rx", box_.rx);
        if (box_.ry > 0)
            tag.attr("ry", box_.ry);
        break;
    case Primitive::Ellipse:
        tag.attr("cx", box_.x + box_.w / 2).attr("cy", box_.y + box_.h / 2).attr("rx", box_.w / 2).attr("ry", box_.h / 2);
        break;
    }

    // fill, fill-opacity
    if (!hasFill) {
        tag.attr("fill", "none");
    } else if (shadow) {
        tag.attr("fill", "#000000");
    } else if (s.gradient) {
        tag.attr("fill", "url(#" + gradientId(*s.gradient) + ")");
        const double opacity = s.alpha * s.fillAlpha;
        if (opacity < 1)
            tag.attr("fill-opacity", opacity);
    } else {
        tag.attr("fill", svg::hexColor(*s.fill));
        const double opacity = s.alpha * s.fillAlpha * s.fill->alphaF();
        if (opacity < 1)
            tag.attr("fill-opacity", opacity);
    }

    // stroke, stroke-opacity, stroke-width, stroke-dasharray, stroke-linecap,
    // stroke-linejoin, stroke-miterlimit
    if (!hasStroke) {
        tag.attr("stroke", "none");
    } else {
        tag.attr("stroke", shadow ? "#000000" : svg::hexColor(*s.stroke));
        const double opacity = s.alpha * s.strokeAlpha * s.stroke->alphaF();
        if (!shadow && opacity < 1)
            tag.attr("stroke-opacity", opacity);
        const double width = std::max(kMinStrokeWidth, std::max(0.01, std::round(s.strokeWidth * s.scale * 100) / 100));
        if (width != 1)
            tag.attr("stroke-width", width);
        if (s.dashed) {
            const std::string pattern = dashArray(s.dashPattern, (s.fixDash ? 1 : s.strokeWidth) * s.scale);
            if (!pattern.empty())
                tag.attr("stroke-dasharray", pattern);
        }
        if (isPath && s.lineCap != LineCap::Flat)
            tag.attr("stroke-linecap", s.lineCap == LineCap::Round ? "round" : "square");
        if (s.lineJoin != LineJoin::Miter)
            tag.attr("stroke-linejoin", s.lineJoin == LineJoin::Round ? "round" : "bevel");
        else if (isPath)
            tag.attr("stroke-miterlimit", s.miterLimit);
    }

    // opacity (shadows only), transform, pointer-events
    if (shadow) {
        tag.attr("opacity", kShadowAlpha * s.alpha);
        tag.attr("transform", "translate(" + number(kShadowDx * s.scale) + "," + number(kShadowDy * s.scale) + ")" +
                                  s.transform);
        tag.attr("pointer-events", "none");
    } else {
        if (!s.transform.empty())
            tag.attr("transform", s.transform);
        tag.attr("pointer-events", "all");
    }
    return tag.selfClosed();
}

void SvgCanvas::Impl::paint(bool filled, bool stroked)
{
    if (primitive_ == Primitive::None && d_.empty())
        return;
    if (state.shadow) {
        const std::string shadow = buildElement(filled, stroked, true);
        if (!shadow.empty())
            emitLine(shadow);
    }
    const std::string element = buildElement(filled, stroked, false);
    if (!element.empty())
        emitLine(element);
    resetPath(); // the path is consumed, as upstream (this.node = null)
}

// ------------------------------------------------------------------ image

void SvgCanvas::Impl::image(double x, double y, double w, double h, const ImageData& img, bool aspect, bool flipH,
                            bool flipV)
{
    if (img.empty() || !allFinite(x, y, w, h) || w < 0 || h < 0)
        return;
    const QByteArray bytes = img.encodedBytes();
    if (bytes.isEmpty())
        return;
    std::string mime = img.mimeType.toStdString();
    if (mime.rfind("image/", 0) != 0)
        mime = sniffMimeType(bytes);
    if (mime.empty())
        return;

    const State& s = state;
    x += s.dx;
    y += s.dy;
    Tag tag("image");
    tag.attr("x", x * s.scale).attr("y", y * s.scale).attr("width", w * s.scale).attr("height", h * s.scale);
    tag.attr("xlink:href", "data:" + mime + ";base64," + bytes.toBase64().toStdString());
    tag.attr("preserveAspectRatio", aspect ? "xMidYMid meet" : "none");

    const double opacity = s.alpha * s.fillAlpha;
    if (opacity < 1)
        tag.attr("opacity", opacity);

    std::string transform = s.transform;
    if (flipH || flipV) {
        const double sx = flipH ? -1 : 1;
        const double sy = flipV ? -1 : 1;
        const double dx = flipH ? -w - 2 * x : 0;
        const double dy = flipV ? -h - 2 * y : 0;
        transform += "scale(" + number(sx) + "," + number(sy) + ")translate(" + number(dx * s.scale) + "," +
                     number(dy * s.scale) + ")";
    }
    if (!transform.empty())
        tag.attr("transform", transform);
    tag.attr("pointer-events", "none");
    emitLine(tag.selfClosed());
}

// ------------------------------------------------------------------ public class

SvgCanvas::SvgCanvas(SvgDocumentOptions options) : d(std::make_unique<Impl>(std::move(options))) {}
SvgCanvas::~SvgCanvas() = default;

void SvgCanvas::save() { d->stack.push_back(d->state); }
void SvgCanvas::restore()
{
    if (d->stack.empty())
        return;
    d->state = std::move(d->stack.back());
    d->stack.pop_back();
}
void SvgCanvas::scale(double s)
{
    if (std::isfinite(s))
        d->state.scale *= s;
}
void SvgCanvas::translate(double dx, double dy)
{
    if (std::isfinite(dx) && std::isfinite(dy)) {
        d->state.dx += dx;
        d->state.dy += dy;
    }
}
void SvgCanvas::rotate(double degrees, bool flipH, bool flipV, double cx, double cy)
{
    d->rotate(degrees, flipH, flipV, cx, cy);
}

void SvgCanvas::setAlpha(double alpha) { d->state.alpha = std::clamp(std::isfinite(alpha) ? alpha : 1, 0.0, 1.0); }
void SvgCanvas::setFillAlpha(double alpha) { d->state.fillAlpha = std::clamp(std::isfinite(alpha) ? alpha : 1, 0.0, 1.0); }
void SvgCanvas::setStrokeAlpha(double alpha)
{
    d->state.strokeAlpha = std::clamp(std::isfinite(alpha) ? alpha : 1, 0.0, 1.0);
}
void SvgCanvas::setFillColor(std::optional<QColor> color)
{
    d->state.fill = color;
    d->state.gradient.reset();
}
void SvgCanvas::setGradient(const QColor& c1, const QColor& c2, const Rect&, GradientDirection direction,
                            double alpha1, double alpha2)
{
    // Like upstream, the gradient spans the bounding box of the painted
    // shape (objectBoundingBox units), so the bounds are not needed.
    d->state.gradient = Impl::Gradient{c1, c2, std::clamp(alpha1, 0.0, 1.0), std::clamp(alpha2, 0.0, 1.0), direction};
}
void SvgCanvas::setStrokeColor(std::optional<QColor> color) { d->state.stroke = color; }
void SvgCanvas::setStrokeWidth(double width)
{
    if (std::isfinite(width))
        d->state.strokeWidth = width;
}
void SvgCanvas::setDashed(bool dashed, bool fixDash)
{
    d->state.dashed = dashed;
    d->state.fixDash = fixDash;
}
void SvgCanvas::setDashPattern(std::string_view pattern) { d->state.dashPattern = std::string(pattern); }
void SvgCanvas::setLineCap(LineCap cap) { d->state.lineCap = cap; }
void SvgCanvas::setLineJoin(LineJoin join) { d->state.lineJoin = join; }
void SvgCanvas::setMiterLimit(double limit)
{
    if (std::isfinite(limit))
        d->state.miterLimit = limit;
}
void SvgCanvas::setShadow(bool enabled) { d->state.shadow = enabled; }

void SvgCanvas::begin() { d->begin(); }
void SvgCanvas::moveTo(double x, double y) { d->moveTo(x, y); }
void SvgCanvas::lineTo(double x, double y) { d->lineTo(x, y); }
void SvgCanvas::quadTo(double x1, double y1, double x2, double y2) { d->quadTo(x1, y1, x2, y2); }
void SvgCanvas::curveTo(double x1, double y1, double x2, double y2, double x3, double y3)
{
    d->curveTo(x1, y1, x2, y2, x3, y3);
}
void SvgCanvas::arcTo(double rx, double ry, double angle, bool largeArc, bool sweep, double x, double y)
{
    d->arcTo(rx, ry, angle, largeArc, sweep, x, y);
}
void SvgCanvas::close() { d->close(); }
void SvgCanvas::rect(double x, double y, double w, double h) { d->rect(x, y, w, h); }
void SvgCanvas::roundrect(double x, double y, double w, double h, double dx, double dy)
{
    d->roundrect(x, y, w, h, dx, dy);
}
void SvgCanvas::ellipse(double x, double y, double w, double h) { d->ellipse(x, y, w, h); }
void SvgCanvas::addPath(const geometry::Path& path) { d->addPath(path); }
void SvgCanvas::fill() { d->paint(true, false); }
void SvgCanvas::stroke() { d->paint(false, true); }
void SvgCanvas::fillAndStroke() { d->paint(true, true); }

void SvgCanvas::image(double x, double y, double w, double h, const ImageData& image, bool aspect, bool flipH,
                      bool flipV)
{
    d->image(x, y, w, h, image, aspect, flipH, flipV);
}
void SvgCanvas::text(const TextBlock& block) { d->text(block); }
void SvgCanvas::textOnPath(const PathText& text) { d->textOnPath(text); }

void SvgCanvas::beginGroup(std::string_view cellId)
{
    d->emitLine(Tag("g").attr("data-cell-id", cellId).open());
    d->cellIds.emplace_back(cellId);
    ++d->groupDepth;
}
void SvgCanvas::endGroup()
{
    if (d->groupDepth == 0)
        return;
    d->emitLine("</g>");
    d->cellIds.pop_back();
    --d->groupDepth;
}

std::string SvgCanvas::toSvg() const
{
    const SvgDocumentOptions& o = d->options;
    std::string out =
        "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\n"
        "<!DOCTYPE svg PUBLIC \"-//W3C//DTD SVG 1.1//EN\" \"http://www.w3.org/Graphics/SVG/1.1/DTD/svg11.dtd\">\n";

    Tag root("svg");
    root.attr("xmlns", "http://www.w3.org/2000/svg")
        .attr("xmlns:xlink", "http://www.w3.org/1999/xlink")
        .attr("version", "1.1")
        .attr("width", number(o.width) + "px")
        .attr("height", number(o.height) + "px")
        .attr("viewBox", number(o.originX) + " " + number(o.originY) + " " + number(o.width) + " " + number(o.height));
    if (!o.content.empty())
        root.attr("content", o.content);
    const bool background = o.background && o.background->alpha() > 0;
    if (background)
        root.attr("style", "background-color: " + svg::hexColor(*o.background) + ";");
    out += root.open() + "\n";

    out += d->defs.empty() ? std::string("<defs/>\n") : "<defs>\n" + d->defs + "</defs>\n";
    if (background) {
        Tag rect("rect");
        rect.attr("fill", svg::hexColor(*o.background));
        if (o.background->alphaF() < 1)
            rect.attr("fill-opacity", o.background->alphaF());
        rect.attr("x", o.originX).attr("y", o.originY).attr("width", "100%").attr("height", "100%");
        out += rect.selfClosed() + "\n";
    }
    out += d->body;
    for (int i = 0; i < d->groupDepth; ++i)
        out += "</g>\n";
    out += "</svg>\n";
    return out;
}

} // namespace qraw::render
