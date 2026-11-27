// PainterCanvas: the Canvas interface on QPainter.
//
// Port of mxgraph/src/util/mxAbstractCanvas2D.js (draw.io, Apache-2.0),
// modified: same state model as SvgCanvas (dx/dy/scale, rotate() accumulating
// a transform in scaled coordinates), but drawn with QPainter. Path points are
// stored already scaled/translated, the accumulated rotation/flip transform
// and the painter's base transform are applied while drawing, so save() and
// restore() never touch the painter beyond the base state.

#include "qraw/render/painter_canvas.h"

#include "qraw/render/image_cache.h"
#include "qraw/render/perf_scope.h"
#include "qraw/render/resolved_style.h"
#include "qraw/render/stats.h"
#include "qraw/render/text_layout_cache.h"
#include "qraw/render/text_metrics.h"

#include <QAbstractTextDocumentLayout>
#include <QImage>
#include <QPainter>
#include <QPainterPath>
#include <QPalette>
#include <QSvgRenderer>
#include <QTextBlockFormat>
#include <QTextCursor>
#include <QTextDocument>
#include <QTextOption>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <vector>

namespace qraw::render {

namespace {

constexpr double kShadowAlpha = 0.25;
constexpr double kShadowDx = 2;
constexpr double kShadowDy = 3;
constexpr double kMinPenWidth = 0.01;

template <typename... T>
bool allFinite(T... values)
{
    return (std::isfinite(values) && ...);
}

QColor withAlpha(QColor color, double alpha)
{
    color.setAlphaF(std::clamp(color.alphaF() * alpha, 0.0, 1.0));
    return color;
}

std::vector<double> parseDashPattern(std::string_view pattern)
{
    std::vector<double> values;
    std::istringstream in{std::string(pattern)};
    double v = 0;
    while (in >> v) {
        if (v > 0 && std::isfinite(v))
            values.push_back(v);
    }
    if (values.size() % 2 == 1) // Qt wants dash/gap pairs, SVG repeats an odd list
        values.insert(values.end(), values.begin(), values.end());
    return values;
}

double hFraction(geometry::HAlign a)
{
    return a == geometry::HAlign::Left ? 0 : a == geometry::HAlign::Center ? 0.5 : 1;
}
double vFraction(geometry::VAlign a)
{
    return a == geometry::VAlign::Top ? 0 : a == geometry::VAlign::Middle ? 0.5 : 1;
}

// QTransform products apply the left operand first. `rotateAbout` is
// translate(-c) rotate(theta) translate(c) in that order.
QTransform rotateAbout(double degrees, double cx, double cy)
{
    return QTransform::fromTranslate(-cx, -cy) * QTransform().rotate(degrees) * QTransform::fromTranslate(cx, cy);
}

} // namespace

struct PainterCanvas::Impl {
    struct Gradient {
        QColor start;
        QColor end;
        double startAlpha = 1;
        double endAlpha = 1;
        Rect bounds;
        GradientDirection direction = GradientDirection::South;
    };
    struct State {
        double alpha = 1;
        double fillAlpha = 1;
        double strokeAlpha = 1;
        std::optional<QColor> fill;
        std::optional<Gradient> gradient;
        std::optional<QColor> stroke;
        double strokeWidth = 1;
        bool dashed = false;
        bool fixDash = false;
        std::vector<double> dashPattern{3, 3};
        LineCap lineCap = LineCap::Flat;
        LineJoin lineJoin = LineJoin::Miter;
        double miterLimit = 10;
        bool shadow = false;
        double scale = 1;
        double dx = 0;
        double dy = 0;
        QTransform rotation; // accumulated rotate()/flips, in scaled coordinates
    };

    explicit Impl(QPainter& p) : painter(p), base(p.worldTransform())
    {
        painter.setRenderHint(QPainter::Antialiasing, true);
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
    }

    [[nodiscard]] double tx(double x) const { return (x + state.dx) * state.scale; }
    [[nodiscard]] double ty(double y) const { return (y + state.dy) * state.scale; }
    [[nodiscard]] QPointF map(double x, double y) const { return {tx(x), ty(y)}; }

    // Transform for drawing path/image content (optionally the shadow offset,
    // which is applied last, outside the rotation, like SvgCanvas).
    [[nodiscard]] QTransform transform(bool shadow = false) const
    {
        QTransform t = state.rotation;
        if (shadow)
            t *= QTransform::fromTranslate(kShadowDx * state.scale, kShadowDy * state.scale);
        return t * base;
    }

    void startSubpath(double x, double y)
    {
        if (!hasCurrent) {
            path.moveTo(map(x, y));
            subpathStart = {x, y};
            hasCurrent = true;
        }
    }

    [[nodiscard]] QPen makePen(const QColor& color) const;
    [[nodiscard]] std::optional<QBrush> makeBrush() const;
    void paintPath(bool filled, bool stroked);
    void drawPath(bool filled, bool stroked, bool shadow);
    void text(const TextBlock& block);
    void textOnPath(const PathText& text);

    QPainter& painter;
    ImageQuality imageQuality = ImageQuality::Exact;
    QTransform base;
    State state;
    std::vector<State> stack;
    QPainterPath path;
    Point current{};
    Point subpathStart{};
    bool hasCurrent = false;
};

// ------------------------------------------------------------ pen and brush

QPen PainterCanvas::Impl::makePen(const QColor& color) const
{
    const State& s = state;
    const double width = std::max(kMinPenWidth, s.strokeWidth * s.scale);
    QPen pen(color);
    pen.setWidthF(width);
    pen.setCosmetic(false);
    pen.setCapStyle(s.lineCap == LineCap::Round    ? Qt::RoundCap
                    : s.lineCap == LineCap::Square ? Qt::SquareCap
                                                   : Qt::FlatCap);
    pen.setJoinStyle(s.lineJoin == LineJoin::Round ? Qt::RoundJoin
                     : s.lineJoin == LineJoin::Bevel ? Qt::BevelJoin
                                                     : Qt::MiterJoin);
    pen.setMiterLimit(s.miterLimit);
    if (s.dashed && !s.dashPattern.empty()) {
        // Qt dash lengths are multiples of the pen width; draw.io's are
        // multiples of the stroke width (unless fixDash: absolute px).
        QList<qreal> dashes;
        const double unit = (s.fixDash ? 1.0 : s.strokeWidth) * s.scale / width;
        for (double v : s.dashPattern)
            dashes.append(v * unit);
        pen.setDashPattern(dashes);
    }
    return pen;
}

std::optional<QBrush> PainterCanvas::Impl::makeBrush() const
{
    const State& s = state;
    if (s.gradient) {
        const Gradient& g = *s.gradient;
        const QRectF b(map(g.bounds.x, g.bounds.y), QSizeF(g.bounds.width * s.scale, g.bounds.height * s.scale));
        const QColor c1 = withAlpha(g.start, g.startAlpha * s.alpha * s.fillAlpha);
        const QColor c2 = withAlpha(g.end, g.endAlpha * s.alpha * s.fillAlpha);
        if (g.direction == GradientDirection::Radial) {
            QRadialGradient gradient(b.center(), std::max(b.width(), b.height()) / 2);
            gradient.setColorAt(0, c1);
            gradient.setColorAt(1, c2);
            return QBrush(gradient);
        }
        QPointF from = b.center();
        QPointF to = b.center();
        switch (g.direction) {
        case GradientDirection::North: from.setY(b.bottom()); to.setY(b.top()); break;
        case GradientDirection::East: from.setX(b.left()); to.setX(b.right()); break;
        case GradientDirection::West: from.setX(b.right()); to.setX(b.left()); break;
        default: from.setY(b.top()); to.setY(b.bottom()); break;
        }
        QLinearGradient gradient(from, to);
        gradient.setColorAt(0, c1);
        gradient.setColorAt(1, c2);
        return QBrush(gradient);
    }
    if (s.fill && s.fill->alpha() > 0)
        return QBrush(withAlpha(*s.fill, s.alpha * s.fillAlpha));
    return std::nullopt;
}

void PainterCanvas::Impl::drawPath(bool filled, bool stroked, bool shadow)
{
    const State& s = state;
    std::optional<QBrush> brush = filled ? makeBrush() : std::nullopt;
    const bool hasStroke = stroked && s.stroke && s.stroke->alpha() > 0;
    if (!brush && !hasStroke)
        return;

    painter.save();
    painter.setWorldTransform(transform(shadow));
    if (shadow) {
        const QColor black = withAlpha(QColor(0, 0, 0), kShadowAlpha * s.alpha);
        painter.setBrush(brush ? QBrush(black) : QBrush(Qt::NoBrush));
        painter.setPen(hasStroke ? makePen(black) : QPen(Qt::NoPen));
    } else {
        painter.setBrush(brush ? *brush : QBrush(Qt::NoBrush));
        painter.setPen(hasStroke ? makePen(withAlpha(*s.stroke, s.alpha * s.strokeAlpha)) : QPen(Qt::NoPen));
    }
    painter.drawPath(path);
    painter.restore();
}

void PainterCanvas::Impl::paintPath(bool filled, bool stroked)
{
    if (path.isEmpty())
        return;
    QRAW_PERF_SCOPE(Raster);
    if (state.shadow)
        drawPath(filled, stroked, true);
    drawPath(filled, stroked, false);
    // The path is consumed (canvas.h; upstream sets this.node = null). Shapes
    // that call rect()/ellipse() without begin() rely on this: otherwise each
    // fill would repaint every earlier shape and cover its label.
    path = QPainterPath();
    hasCurrent = false;
}

// ------------------------------------------------------------------ public

PainterCanvas::PainterCanvas(QPainter& painter) : d(std::make_unique<Impl>(painter)) {}
PainterCanvas::~PainterCanvas() = default;

void PainterCanvas::save() { d->stack.push_back(d->state); }
void PainterCanvas::restore()
{
    if (d->stack.empty())
        return;
    d->state = std::move(d->stack.back());
    d->stack.pop_back();
}

void PainterCanvas::scale(double s)
{
    if (std::isfinite(s))
        d->state.scale *= s;
}

void PainterCanvas::translate(double dx, double dy)
{
    if (allFinite(dx, dy)) {
        d->state.dx += dx;
        d->state.dy += dy;
    }
}

// mxSvgCanvas2D.rotate: flips (mirror about cx/cy) then rotation, both in
// scaled coordinates. The newest operation is applied to points first.
void PainterCanvas::rotate(double degrees, bool flipH, bool flipV, double cx, double cy)
{
    if (!allFinite(degrees, cx, cy) || (degrees == 0 && !flipH && !flipV))
        return;
    cx = d->tx(cx);
    cy = d->ty(cy);
    QTransform t = d->state.rotation;
    if (flipH && flipV) {
        degrees += 180;
    } else if (flipH != flipV) {
        const double mx = flipH ? cx : 0;
        const double my = flipV ? cy : 0;
        t = QTransform::fromTranslate(-mx, -my) * QTransform::fromScale(flipH ? -1 : 1, flipV ? -1 : 1) *
            QTransform::fromTranslate(mx, my) * t;
    }
    if (flipH ? !flipV : flipV)
        degrees = -degrees;
    if (degrees != 0)
        t = rotateAbout(degrees, cx, cy) * t;
    d->state.rotation = t;
}

void PainterCanvas::setAlpha(double a) { d->state.alpha = std::clamp(a, 0.0, 1.0); }
void PainterCanvas::setFillAlpha(double a) { d->state.fillAlpha = std::clamp(a, 0.0, 1.0); }
void PainterCanvas::setStrokeAlpha(double a) { d->state.strokeAlpha = std::clamp(a, 0.0, 1.0); }

void PainterCanvas::setFillColor(std::optional<QColor> color)
{
    d->state.fill = color;
    d->state.gradient.reset();
}

void PainterCanvas::setGradient(const QColor& c1, const QColor& c2, const Rect& bounds, GradientDirection direction,
                                double alpha1, double alpha2)
{
    d->state.gradient = Impl::Gradient{c1, c2, alpha1, alpha2, bounds, direction};
}

void PainterCanvas::setStrokeColor(std::optional<QColor> color) { d->state.stroke = color; }
void PainterCanvas::setStrokeWidth(double width)
{
    if (std::isfinite(width))
        d->state.strokeWidth = std::max(0.0, width);
}
void PainterCanvas::setDashed(bool dashed, bool fixDash)
{
    d->state.dashed = dashed;
    d->state.fixDash = fixDash;
}
void PainterCanvas::setDashPattern(std::string_view pattern)
{
    auto values = parseDashPattern(pattern);
    if (!values.empty())
        d->state.dashPattern = std::move(values);
}
void PainterCanvas::setLineCap(LineCap cap) { d->state.lineCap = cap; }
void PainterCanvas::setLineJoin(LineJoin join) { d->state.lineJoin = join; }
void PainterCanvas::setMiterLimit(double limit)
{
    if (std::isfinite(limit) && limit >= 1)
        d->state.miterLimit = limit;
}
void PainterCanvas::setShadow(bool enabled) { d->state.shadow = enabled; }

// ------------------------------------------------------------------- path

void PainterCanvas::begin()
{
    d->path = QPainterPath();
    d->hasCurrent = false;
}

void PainterCanvas::moveTo(double x, double y)
{
    if (!allFinite(x, y))
        return;
    d->path.moveTo(d->map(x, y));
    d->current = d->subpathStart = {x, y};
    d->hasCurrent = true;
}

void PainterCanvas::lineTo(double x, double y)
{
    if (!allFinite(x, y))
        return;
    if (!d->hasCurrent) {
        moveTo(x, y);
        return;
    }
    d->path.lineTo(d->map(x, y));
    d->current = {x, y};
}

void PainterCanvas::quadTo(double x1, double y1, double x2, double y2)
{
    if (!allFinite(x1, y1, x2, y2))
        return;
    d->startSubpath(x1, y1);
    d->path.quadTo(d->map(x1, y1), d->map(x2, y2));
    d->current = {x2, y2};
}

void PainterCanvas::curveTo(double x1, double y1, double x2, double y2, double x3, double y3)
{
    if (!allFinite(x1, y1, x2, y2, x3, y3))
        return;
    d->startSubpath(x1, y1);
    d->path.cubicTo(d->map(x1, y1), d->map(x2, y2), d->map(x3, y3));
    d->current = {x3, y3};
}

void PainterCanvas::addPath(const geometry::Path& path)
{
    using Kind = geometry::PathElement::Kind;
    for (const geometry::PathElement& e : path.elements()) {
        switch (e.kind) {
        case Kind::MoveTo: moveTo(e.p[0].x, e.p[0].y); break;
        case Kind::LineTo: lineTo(e.p[0].x, e.p[0].y); break;
        case Kind::QuadTo: quadTo(e.p[0].x, e.p[0].y, e.p[1].x, e.p[1].y); break;
        case Kind::CubicTo: curveTo(e.p[0].x, e.p[0].y, e.p[1].x, e.p[1].y, e.p[2].x, e.p[2].y); break;
        case Kind::Close: close(); break;
        }
    }
}

void PainterCanvas::arcTo(double rx, double ry, double angle, bool largeArc, bool sweep, double x, double y)
{
    if (!allFinite(rx, ry, angle, x, y))
        return;
    if (!d->hasCurrent) {
        moveTo(x, y);
        return;
    }
    // Single implementation of the arc maths (geometry::Path::arcTo).
    geometry::Path arc;
    arc.moveTo(d->current);
    arc.arcTo(rx, ry, angle, largeArc, sweep, {x, y});
    using Kind = geometry::PathElement::Kind;
    for (const geometry::PathElement& e : arc.elements()) {
        switch (e.kind) {
        case Kind::LineTo: d->path.lineTo(d->map(e.p[0].x, e.p[0].y)); break;
        case Kind::QuadTo: d->path.quadTo(d->map(e.p[0].x, e.p[0].y), d->map(e.p[1].x, e.p[1].y)); break;
        case Kind::CubicTo:
            d->path.cubicTo(d->map(e.p[0].x, e.p[0].y), d->map(e.p[1].x, e.p[1].y), d->map(e.p[2].x, e.p[2].y));
            break;
        default: break;
        }
    }
    d->current = {x, y};
}

void PainterCanvas::close()
{
    if (!d->hasCurrent)
        return;
    d->path.closeSubpath();
    d->current = d->subpathStart;
}

void PainterCanvas::rect(double x, double y, double w, double h)
{
    if (!allFinite(x, y, w, h) || w < 0 || h < 0)
        return;
    d->path.addRect(QRectF(d->map(x, y), QSizeF(w * d->state.scale, h * d->state.scale)));
    d->current = d->subpathStart = {x, y};
    d->hasCurrent = true;
}

// mxAbstractCanvas2D.roundrect: quadratic corners, radii clamped to half the size.
void PainterCanvas::roundrect(double x, double y, double w, double h, double dx, double dy)
{
    if (!allFinite(x, y, w, h, dx, dy) || w < 0 || h < 0)
        return;
    dx = std::clamp(dx, 0.0, w / 2);
    dy = std::clamp(dy, 0.0, h / 2);
    QPainterPath p;
    p.moveTo(d->map(x + dx, y));
    p.lineTo(d->map(x + w - dx, y));
    p.quadTo(d->map(x + w, y), d->map(x + w, y + dy));
    p.lineTo(d->map(x + w, y + h - dy));
    p.quadTo(d->map(x + w, y + h), d->map(x + w - dx, y + h));
    p.lineTo(d->map(x + dx, y + h));
    p.quadTo(d->map(x, y + h), d->map(x, y + h - dy));
    p.lineTo(d->map(x, y + dy));
    p.quadTo(d->map(x, y), d->map(x + dx, y));
    p.closeSubpath();
    d->path.addPath(p);
    d->current = d->subpathStart = {x + dx, y};
    d->hasCurrent = true;
}

void PainterCanvas::ellipse(double x, double y, double w, double h)
{
    if (!allFinite(x, y, w, h) || w < 0 || h < 0)
        return;
    d->path.addEllipse(QRectF(d->map(x, y), QSizeF(w * d->state.scale, h * d->state.scale)));
    d->current = d->subpathStart = {x + w, y + h / 2};
    d->hasCurrent = true;
}

void PainterCanvas::fill() { d->paintPath(true, false); }
void PainterCanvas::stroke() { d->paintPath(false, true); }
void PainterCanvas::fillAndStroke() { d->paintPath(true, true); }

// ------------------------------------------------------------------ image

void PainterCanvas::setImageQuality(ImageQuality quality) { d->imageQuality = quality; }
ImageQuality PainterCanvas::imageQuality() const { return d->imageQuality; }

void PainterCanvas::image(double x, double y, double w, double h, const ImageData& image, bool aspect, bool flipH,
                          bool flipV)
{
    Impl& s = *d;
    if (image.empty() || !allFinite(x, y, w, h) || w <= 0 || h <= 0)
        return;

    // Decoded once, whatever number of times and wherever this image is drawn.
    const std::shared_ptr<const CachedImage> cached =
        image.cached ? image.cached : ImageCache::instance().fromEncoded(image.bytes, image.mimeType);
    if (!cached || cached->decodeFailed())
        return;
    const QSizeF natural = cached->size();

    QRectF box(s.map(x, y), QSizeF(w * s.state.scale, h * s.state.scale));
    QRectF target = box;
    if (aspect && natural.width() > 0 && natural.height() > 0) {
        const double f = std::min(box.width() / natural.width(), box.height() / natural.height());
        target = QRectF(0, 0, natural.width() * f, natural.height() * f);
        target.moveCenter(box.center());
    }

    QTransform t = s.transform();
    if (flipH || flipV) {
        const QPointF c = box.center();
        t = QTransform::fromTranslate(-c.x(), -c.y()) * QTransform::fromScale(flipH ? -1 : 1, flipV ? -1 : 1) *
            QTransform::fromTranslate(c.x(), c.y()) * t;
    }
    QRAW_PERF_SCOPE(ImageDraw);
    s.painter.save();
    s.painter.setWorldTransform(t);
    s.painter.setOpacity(s.painter.opacity() * s.state.alpha * s.state.fillAlpha);

    // The destination in device pixels: the world transform's scale along each
    // axis (rotation does not change a length) and the target's pixel ratio.
    const double ratio = s.painter.device() != nullptr ? s.painter.device()->devicePixelRatioF() : 1.0;
    const QSizeF device(target.width() * std::hypot(t.m11(), t.m12()) * ratio,
                        target.height() * std::hypot(t.m21(), t.m22()) * ratio);
    const bool interactive = s.imageQuality == ImageQuality::Interactive;

    if (cached->isSvg()) {
        const QImage raster = interactive ? cached->svgRaster(device) : QImage();
        if (!raster.isNull())
            s.painter.drawImage(target, raster);
        else
            cached->paintSvg(s.painter, target);
    } else {
        const QImage raster = interactive ? cached->level(cached->levelFor(device)) : cached->exactImage(image.bytes);
        if (!raster.isNull()) {
            // Drawn at another size than its own (in device pixels): QPainter scales it.
            if (std::abs(device.width() - raster.width()) > 0.5 || std::abs(device.height() - raster.height()) > 0.5)
                stats::add(stats::Counter::imageScales);
            s.painter.drawImage(target, raster);
        }
    }
    s.painter.restore();
}

// ------------------------------------------------------------------- text

void PainterCanvas::Impl::text(const TextBlock& b)
{
    if (b.text.empty() || !allFinite(b.x, b.y, b.width, b.height, b.rotation, b.fontSize, b.padding, b.opacity,
                                     b.lineHeight) ||
        b.fontSize <= 0 || b.width < 0 || b.height < 0)
        return;
    const double sc = state.scale;
    const double ax = tx(b.x);
    const double ay = ty(b.y);
    const double boxW = b.width * sc;
    const double boxH = b.height * sc;
    const double pad = b.padding * sc;

    // Laid out once per (text, font, wrap width, ...) on this thread, then
    // drawn from the cache; the view zoom is only in the painter transform.
    const LaidOutLabel& label = TextLayoutCache::forThisThread().layout(b, sc, boxW);
    const double contentW = label.width;
    const double contentH = label.height;

    // The anchor (x, y) sits left/center/right and top/middle/bottom of the
    // laid-out text; for a bounded box the same fractions place the box.
    const double left = ax - contentW * hFraction(b.align);
    const double top = label.placement.contentTop(ay, vFraction(b.verticalAlign));

    QTransform t = rotateAbout(b.rotation, ax, ay) * transform();
    painter.save();
    painter.setWorldTransform(t);
    painter.setOpacity(painter.opacity() * state.alpha * std::clamp(b.opacity, 0.0, 1.0));
    if (b.clip && boxW > 0 && boxH > 0) {
        painter.setClipRect(QRectF(ax - boxW * hFraction(b.align), ay - boxH * vFraction(b.verticalAlign), boxW, boxH));
    }
    if (b.backgroundColor || b.borderColor) {
        const QRectF frame(left - pad, top - pad, contentW + 2 * pad, contentH + 2 * pad);
        painter.setBrush(b.backgroundColor ? QBrush(*b.backgroundColor) : QBrush(Qt::NoBrush));
        painter.setPen(b.borderColor ? QPen(*b.borderColor, 1) : QPen(Qt::NoPen));
        painter.drawRect(frame);
    }
    painter.translate(left, top);
    QAbstractTextDocumentLayout::PaintContext context;
    context.palette.setColor(QPalette::Text, b.fontColor);
    {
        QRAW_PERF_SCOPE(TextDraw);
        label.document->documentLayout()->draw(&painter, context);
    }
    painter.restore();
}

void PainterCanvas::text(const TextBlock& block) { d->text(block); }

void PainterCanvas::Impl::textOnPath(const PathText& t)
{
    if (t.text.empty() || t.path.empty() || !allFinite(t.fontSize, t.textLength, t.opacity) || t.fontSize <= 0 ||
        t.textLength < 0)
        return;

    // The curve in canvas units, before the painter transform.
    QPainterPath curve;
    using Kind = geometry::PathElement::Kind;
    for (const geometry::PathElement& e : t.path.elements()) {
        switch (e.kind) {
        case Kind::MoveTo: curve.moveTo(map(e.p[0].x, e.p[0].y)); break;
        case Kind::LineTo: curve.lineTo(map(e.p[0].x, e.p[0].y)); break;
        case Kind::QuadTo: curve.quadTo(map(e.p[0].x, e.p[0].y), map(e.p[1].x, e.p[1].y)); break;
        case Kind::CubicTo:
            curve.cubicTo(map(e.p[0].x, e.p[0].y), map(e.p[1].x, e.p[1].y), map(e.p[2].x, e.p[2].y));
            break;
        case Kind::Close: curve.closeSubpath(); break;
        }
    }
    const double length = curve.length();
    if (!(length > 0))
        return;

    const QFont font = diagramFont(firstFontFamily(t.fontFamily), int(std::lround(t.fontSize * state.scale)), t.fontStyle);
    const QFontMetricsF metrics(font);

    // One entry per code point; lengthAdjust=spacing only changes the gaps.
    const QString text = QString::fromUtf8(t.text.data(), qsizetype(t.text.size()));
    std::vector<QString> glyphs;
    std::vector<double> advances;
    double natural = 0;
    for (const char32_t cp : text.toUcs4()) {
        const QString glyph = QString::fromUcs4(&cp, 1);
        glyphs.push_back(glyph);
        advances.push_back(metrics.horizontalAdvance(glyph));
        natural += advances.back();
    }
    const double total = t.textLength > 0 ? t.textLength * state.scale : natural;
    const double gap = glyphs.size() > 1 ? (total - natural) / double(glyphs.size() - 1) : 0;
    double position = hFraction(t.align) * (length - total); // text-anchor at startOffset
    // dominant-baseline: central puts the middle of the line on the curve.
    const double baseline = (metrics.ascent() - metrics.descent()) / 2;

    painter.save();
    painter.setWorldTransform(transform());
    painter.setOpacity(painter.opacity() * state.alpha * std::clamp(t.opacity, 0.0, 1.0));
    painter.setFont(font);
    painter.setPen(t.fontColor);
    for (std::size_t i = 0; i < glyphs.size(); ++i) {
        const double centre = position + advances[i] / 2;
        position += advances[i] + gap;
        if (centre < 0 || centre > length || glyphs[i].trimmed().isEmpty())
            continue;
        const double percent = curve.percentAtLength(centre);
        painter.save();
        painter.translate(curve.pointAtPercent(percent));
        painter.rotate(-curve.angleAtPercent(percent)); // Qt measures counterclockwise
        painter.drawText(QPointF(-advances[i] / 2, baseline), glyphs[i]);
        painter.restore();
    }
    painter.restore();
}

void PainterCanvas::textOnPath(const PathText& text) { d->textOnPath(text); }

// ---------------------------------------------------------------- structure

void PainterCanvas::beginGroup(std::string_view) {}
void PainterCanvas::endGroup() {}

} // namespace qraw::render
