// DisplayList and RecordingCanvas (display_list.h).
//
// The bounds tracking mirrors PainterCanvas call for call (same scaled-
// coordinate state, same fill/stroke/shadow conditions) so that the area it
// reports is the area PainterCanvas can touch; tst_render_display_list renders
// every fixture cell alone and checks that no pixel falls outside it.

#include "qraw/render/display_list.h"

#include "qraw/render/image_cache.h"
#include "qraw/render/text_layout_cache.h"

#include <QTransform>

#include <algorithm>
#include <cmath>
#include <limits>

namespace qraw::render {

namespace {

constexpr double kShadowDx = 2; // as PainterCanvas
constexpr double kShadowDy = 3;
constexpr double kMinPenWidth = 0.01;

template <typename... T>
bool allFinite(T... values)
{
    return (std::isfinite(values) && ...);
}

double hFraction(geometry::HAlign a)
{
    return a == geometry::HAlign::Left ? 0 : a == geometry::HAlign::Center ? 0.5 : 1;
}
double vFraction(geometry::VAlign a)
{
    return a == geometry::VAlign::Top ? 0 : a == geometry::VAlign::Middle ? 0.5 : 1;
}

QTransform rotateAbout(double degrees, double cx, double cy)
{
    return QTransform::fromTranslate(-cx, -cy) * QTransform().rotate(degrees) * QTransform::fromTranslate(cx, cy);
}

// Union that treats a null rectangle as "nothing" (QRectF::united would
// count it as a point at the origin) and ignores non-finite ones.
void growBy(QRectF& into, const QRectF& rect)
{
    if (!std::isfinite(rect.left()) || !std::isfinite(rect.top()) || !std::isfinite(rect.right())
        || !std::isfinite(rect.bottom()))
        return;
    into = into.isNull() ? rect : into.united(rect);
}

} // namespace

// ------------------------------------------------------------- DisplayList

void DisplayList::replay(Canvas& canvas, std::size_t from, std::size_t to) const
{
    to = std::min(to, ops_.size());
    for (std::size_t i = from; i < to; ++i) {
        const Op& op = ops_[i];
        const double* v = op.v;
        switch (op.kind) {
        case OpKind::Save: canvas.save(); break;
        case OpKind::Restore: canvas.restore(); break;
        case OpKind::Scale: canvas.scale(v[0]); break;
        case OpKind::Translate: canvas.translate(v[0], v[1]); break;
        case OpKind::Rotate: canvas.rotate(v[0], op.flags & 1, op.flags & 2, v[1], v[2]); break;
        case OpKind::SetAlpha: canvas.setAlpha(v[0]); break;
        case OpKind::SetFillAlpha: canvas.setFillAlpha(v[0]); break;
        case OpKind::SetStrokeAlpha: canvas.setStrokeAlpha(v[0]); break;
        case OpKind::SetFillColor:
            canvas.setFillColor(op.index == 0 ? std::nullopt : std::optional<QColor>(colors_[op.index - 1]));
            break;
        case OpKind::SetGradient: {
            const Gradient& g = gradients_[op.index];
            canvas.setGradient(g.c1, g.c2, g.bounds, g.direction, g.alpha1, g.alpha2);
            break;
        }
        case OpKind::SetStrokeColor:
            canvas.setStrokeColor(op.index == 0 ? std::nullopt : std::optional<QColor>(colors_[op.index - 1]));
            break;
        case OpKind::SetStrokeWidth: canvas.setStrokeWidth(v[0]); break;
        case OpKind::SetDashed: canvas.setDashed(op.flags & 1, op.flags & 2); break;
        case OpKind::SetDashPattern: canvas.setDashPattern(strings_[op.index]); break;
        case OpKind::SetLineCap: canvas.setLineCap(static_cast<LineCap>(op.flags)); break;
        case OpKind::SetLineJoin: canvas.setLineJoin(static_cast<LineJoin>(op.flags)); break;
        case OpKind::SetMiterLimit: canvas.setMiterLimit(v[0]); break;
        case OpKind::SetShadow: canvas.setShadow(op.flags != 0); break;
        case OpKind::Begin: canvas.begin(); break;
        case OpKind::MoveTo: canvas.moveTo(v[0], v[1]); break;
        case OpKind::LineTo: canvas.lineTo(v[0], v[1]); break;
        case OpKind::QuadTo: canvas.quadTo(v[0], v[1], v[2], v[3]); break;
        case OpKind::CurveTo: canvas.curveTo(v[0], v[1], v[2], v[3], v[4], v[5]); break;
        case OpKind::ArcTo: canvas.arcTo(v[0], v[1], v[2], op.flags & 1, op.flags & 2, v[3], v[4]); break;
        case OpKind::Close: canvas.close(); break;
        case OpKind::Rect: canvas.rect(v[0], v[1], v[2], v[3]); break;
        case OpKind::RoundRect: canvas.roundrect(v[0], v[1], v[2], v[3], v[4], v[5]); break;
        case OpKind::Ellipse: canvas.ellipse(v[0], v[1], v[2], v[3]); break;
        case OpKind::AddPath: canvas.addPath(paths_[op.index]); break;
        case OpKind::Fill: canvas.fill(); break;
        case OpKind::Stroke: canvas.stroke(); break;
        case OpKind::FillAndStroke: canvas.fillAndStroke(); break;
        case OpKind::Image:
            canvas.image(v[0], v[1], v[2], v[3], images_[op.index], op.flags & 1, op.flags & 2, op.flags & 4);
            break;
        case OpKind::Text: canvas.text(texts_[op.index]); break;
        case OpKind::TextOnPath: canvas.textOnPath(pathTexts_[op.index]); break;
        case OpKind::BeginGroup: canvas.beginGroup(strings_[op.index]); break;
        case OpKind::EndGroup: canvas.endGroup(); break;
        }
    }
}

std::size_t DisplayList::memoryBytes() const
{
    std::size_t bytes = ops_.capacity() * sizeof(Op) + colors_.capacity() * sizeof(QColor)
        + gradients_.capacity() * sizeof(Gradient) + images_.capacity() * sizeof(ImageData)
        + texts_.capacity() * sizeof(TextBlock) + pathTexts_.capacity() * sizeof(PathText)
        + strings_.capacity() * sizeof(std::string);
    for (const geometry::Path& path : paths_)
        bytes += sizeof(path) + path.elements().capacity() * sizeof(geometry::PathElement);
    for (const TextBlock& text : texts_)
        bytes += text.text.content.capacity();
    return bytes;
}

// --------------------------------------------------------- RecordingCanvas

struct RecordingCanvas::Impl {
    // The state PainterCanvas keeps, reduced to what decides the painted area.
    struct State {
        double alpha = 1;
        double fillAlpha = 1;
        double strokeAlpha = 1;
        bool fill = false; // a fill colour with alpha > 0
        bool gradient = false;
        bool stroke = false; // a stroke colour with alpha > 0
        double strokeWidth = 1;
        LineCap cap = LineCap::Flat;
        LineJoin join = LineJoin::Miter;
        double miterLimit = 10;
        bool shadow = false;
        double scale = 1;
        double dx = 0;
        double dy = 0;
        QTransform rotation;
    };

    DisplayList list;
    State state;
    std::vector<State> stack;
    std::vector<QPointF> points; // the current path, in scaled coordinates (PainterCanvas::Impl::path)
    Point current{};
    Point subpathStart{};
    bool hasCurrent = false;
    bool inLabel = false;

    [[nodiscard]] double tx(double x) const { return (x + state.dx) * state.scale; }
    [[nodiscard]] double ty(double y) const { return (y + state.dy) * state.scale; }
    [[nodiscard]] QPointF map(double x, double y) const { return {tx(x), ty(y)}; }
    [[nodiscard]] QRectF& target() { return inLabel ? list.labelBounds_ : list.shapeBounds_; }

    Op& push(OpKind kind) { return pushIdx(kind, 0, 0); }
    Op& pushIdx(OpKind kind, std::uint8_t flags, std::uint32_t index)
    {
        Op& op = list.ops_.emplace_back();
        op.kind = kind;
        op.flags = flags;
        op.index = index;
        return op;
    }
    Op& push(OpKind kind, double a, double b = 0, double c = 0, double d = 0, double e = 0, double f = 0)
    {
        Op& op = push(kind);
        op.v[0] = a;
        op.v[1] = b;
        op.v[2] = c;
        op.v[3] = d;
        op.v[4] = e;
        op.v[5] = f;
        return op;
    }
    std::uint32_t addColor(const std::optional<QColor>& color)
    {
        if (!color)
            return 0;
        list.colors_.push_back(*color);
        return static_cast<std::uint32_t>(list.colors_.size());
    }
    std::uint32_t addString(std::string_view s)
    {
        list.strings_.emplace_back(s);
        return static_cast<std::uint32_t>(list.strings_.size() - 1);
    }

    void startSubpath(double x, double y)
    {
        if (!hasCurrent) {
            points.push_back(map(x, y));
            subpathStart = {x, y};
            hasCurrent = true;
        }
    }
    void addRectPoints(double x, double y, double w, double h)
    {
        points.push_back(map(x, y));
        points.push_back(map(x + w, y));
        points.push_back(map(x + w, y + h));
        points.push_back(map(x, y + h));
    }

    // How far a stroke reaches beyond the path, in scaled coordinates.
    [[nodiscard]] double strokeReach() const
    {
        const double half = std::max(kMinPenWidth, state.strokeWidth * state.scale) / 2;
        double factor = 1;
        if (state.cap == LineCap::Square)
            factor = std::sqrt(2.0);
        if (state.join == LineJoin::Miter)
            factor = std::max(factor, state.miterLimit);
        return half * factor;
    }

    void paintPath(bool filled, bool stroked)
    {
        std::vector<QPointF> pts;
        pts.swap(points);
        hasCurrent = false;
        if (pts.empty())
            return;
        const bool brush = filled && (state.gradient || state.fill);
        const bool pen = stroked && state.stroke;
        if (!brush && !pen)
            return;
        double minX = std::numeric_limits<double>::infinity();
        double minY = minX;
        double maxX = -minX;
        double maxY = -minX;
        for (const QPointF& p : pts) {
            const QPointF q = state.rotation.isIdentity() ? p : state.rotation.map(p);
            minX = std::min(minX, q.x());
            minY = std::min(minY, q.y());
            maxX = std::max(maxX, q.x());
            maxY = std::max(maxY, q.y());
        }
        QRectF area(QPointF(minX, minY), QPointF(maxX, maxY));
        if (pen) {
            const double reach = strokeReach();
            area.adjust(-reach, -reach, reach, reach);
        }
        growBy(target(), area);
        if (state.shadow)
            growBy(target(), area.translated(kShadowDx * state.scale, kShadowDy * state.scale));
    }

    void text(const TextBlock& b)
    {
        if (b.text.empty() || !allFinite(b.x, b.y, b.width, b.height, b.rotation, b.fontSize, b.padding, b.opacity,
                                         b.lineHeight)
            || b.fontSize <= 0 || b.width < 0 || b.height < 0)
            return;
        const double sc = state.scale;
        const double ax = tx(b.x);
        const double ay = ty(b.y);
        const double boxW = b.width * sc;
        const double boxH = b.height * sc;
        const double pad = b.padding * sc;
        const LabelMetrics metrics = labelMetrics(b, sc, boxW); // the size PainterCanvas::text lays out
        const double w = metrics.size.width();
        const double h = metrics.size.height();
        const double left = ax - w * hFraction(b.align);
        const double top = metrics.placement.contentTop(ay, vFraction(b.verticalAlign));

        QRectF frame(left, top, w, h);
        // Glyphs can overhang the layout box (italics, kerning); one em tenth
        // keeps them inside.
        const double overhang = 0.1 * b.fontSize * sc;
        frame.adjust(-overhang, 0, overhang, 0);
        if (b.backgroundColor || b.borderColor)
            frame = frame.united(QRectF(left - pad, top - pad, w + 2 * pad, h + 2 * pad).adjusted(-1, -1, 1, 1));
        if (b.clip && boxW > 0 && boxH > 0) {
            frame = frame.intersected(
                QRectF(ax - boxW * hFraction(b.align), ay - boxH * vFraction(b.verticalAlign), boxW, boxH)
                    .adjusted(-1, -1, 1, 1));
        }
        if (frame.isEmpty())
            return;
        const QTransform t = rotateAbout(b.rotation, ax, ay) * state.rotation;
        growBy(target(), t.mapRect(frame));
        list.cost_.glyphs += b.text.content.size();
        if (inLabel)
            list.cost_.labelFontPx = std::max(list.cost_.labelFontPx, b.fontSize * state.scale);
    }

    void textOnPath(const PathText& t)
    {
        if (t.text.empty() || t.path.empty() || !allFinite(t.fontSize, t.textLength, t.opacity) || t.fontSize <= 0
            || t.textLength < 0)
            return;
        double minX = std::numeric_limits<double>::infinity();
        double minY = minX;
        double maxX = -minX;
        double maxY = -minX;
        const auto add = [&](const geometry::Point& p) {
            const QPointF q = state.rotation.map(map(p.x, p.y));
            minX = std::min(minX, q.x());
            minY = std::min(minY, q.y());
            maxX = std::max(maxX, q.x());
            maxY = std::max(maxY, q.y());
        };
        for (const geometry::PathElement& e : t.path.elements()) {
            using Kind = geometry::PathElement::Kind;
            const int n = e.kind == Kind::CubicTo ? 3 : e.kind == Kind::QuadTo ? 2 : e.kind == Kind::Close ? 0 : 1;
            for (int i = 0; i < n; ++i)
                add(e.p[i]);
        }
        if (!(minX <= maxX))
            return;
        // Each glyph is centred on the curve and is at most one em tall and wide.
        const double em = t.fontSize * state.scale;
        growBy(target(), QRectF(QPointF(minX, minY), QPointF(maxX, maxY)).adjusted(-em, -em, em, em));
        list.cost_.glyphs += t.text.size();
    }
};

RecordingCanvas::RecordingCanvas() : d(std::make_unique<Impl>()) {}
RecordingCanvas::~RecordingCanvas() = default;

void RecordingCanvas::markLabelStart()
{
    d->list.labelBegin_ = d->list.ops_.size();
    d->inLabel = true;
}

DisplayList RecordingCanvas::finish()
{
    DisplayList result = std::move(d->list);
    if (!d->inLabel)
        result.labelBegin_ = result.ops_.size();
    result.cost_.ops = result.ops_.size();
    result.ops_.shrink_to_fit();
    d->list = DisplayList();
    d->state = {};
    d->stack.clear();
    d->points.clear();
    d->hasCurrent = false;
    d->inLabel = false;
    return result;
}

void RecordingCanvas::save()
{
    d->push(OpKind::Save);
    d->stack.push_back(d->state);
}

void RecordingCanvas::restore()
{
    d->push(OpKind::Restore);
    if (d->stack.empty())
        return;
    d->state = std::move(d->stack.back());
    d->stack.pop_back();
}

void RecordingCanvas::scale(double s)
{
    d->push(OpKind::Scale, s);
    if (std::isfinite(s))
        d->state.scale *= s;
}

void RecordingCanvas::translate(double dx, double dy)
{
    d->push(OpKind::Translate, dx, dy);
    if (allFinite(dx, dy)) {
        d->state.dx += dx;
        d->state.dy += dy;
    }
}

// Mirrors PainterCanvas::rotate.
void RecordingCanvas::rotate(double degrees, bool flipH, bool flipV, double cx, double cy)
{
    Op& op = d->push(OpKind::Rotate, degrees, cx, cy);
    op.flags = static_cast<std::uint8_t>((flipH ? 1 : 0) | (flipV ? 2 : 0));
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
        t = QTransform::fromTranslate(-mx, -my) * QTransform::fromScale(flipH ? -1 : 1, flipV ? -1 : 1)
            * QTransform::fromTranslate(mx, my) * t;
    }
    if (flipH ? !flipV : flipV)
        degrees = -degrees;
    if (degrees != 0)
        t = rotateAbout(degrees, cx, cy) * t;
    d->state.rotation = t;
}

void RecordingCanvas::setAlpha(double a)
{
    d->push(OpKind::SetAlpha, a);
    d->state.alpha = std::clamp(a, 0.0, 1.0);
}
void RecordingCanvas::setFillAlpha(double a)
{
    d->push(OpKind::SetFillAlpha, a);
    d->state.fillAlpha = std::clamp(a, 0.0, 1.0);
}
void RecordingCanvas::setStrokeAlpha(double a)
{
    d->push(OpKind::SetStrokeAlpha, a);
    d->state.strokeAlpha = std::clamp(a, 0.0, 1.0);
}

void RecordingCanvas::setFillColor(std::optional<QColor> color)
{
    d->pushIdx(OpKind::SetFillColor, 0, d->addColor(color));
    d->state.fill = color && color->alpha() > 0;
    d->state.gradient = false;
}

void RecordingCanvas::setGradient(const QColor& c1, const QColor& c2, const Rect& bounds, GradientDirection direction,
                                  double alpha1, double alpha2)
{
    d->list.gradients_.push_back({c1, c2, bounds, direction, alpha1, alpha2});
    d->pushIdx(OpKind::SetGradient, 0, static_cast<std::uint32_t>(d->list.gradients_.size() - 1));
    d->state.gradient = true;
}

void RecordingCanvas::setStrokeColor(std::optional<QColor> color)
{
    d->pushIdx(OpKind::SetStrokeColor, 0, d->addColor(color));
    d->state.stroke = color && color->alpha() > 0;
}

void RecordingCanvas::setStrokeWidth(double width)
{
    d->push(OpKind::SetStrokeWidth, width);
    if (std::isfinite(width))
        d->state.strokeWidth = std::max(0.0, width);
}

void RecordingCanvas::setDashed(bool dashed, bool fixDash)
{
    d->pushIdx(OpKind::SetDashed, static_cast<std::uint8_t>((dashed ? 1 : 0) | (fixDash ? 2 : 0)), 0);
}

void RecordingCanvas::setDashPattern(std::string_view pattern)
{
    d->pushIdx(OpKind::SetDashPattern, 0, d->addString(pattern));
}

void RecordingCanvas::setLineCap(LineCap cap)
{
    d->pushIdx(OpKind::SetLineCap, static_cast<std::uint8_t>(cap), 0);
    d->state.cap = cap;
}

void RecordingCanvas::setLineJoin(LineJoin join)
{
    d->pushIdx(OpKind::SetLineJoin, static_cast<std::uint8_t>(join), 0);
    d->state.join = join;
}

void RecordingCanvas::setMiterLimit(double limit)
{
    d->push(OpKind::SetMiterLimit, limit);
    if (std::isfinite(limit) && limit >= 1)
        d->state.miterLimit = limit;
}

void RecordingCanvas::setShadow(bool enabled)
{
    d->pushIdx(OpKind::SetShadow, static_cast<std::uint8_t>(enabled ? 1 : 0), 0);
    d->state.shadow = enabled;
}

void RecordingCanvas::begin()
{
    d->push(OpKind::Begin);
    d->points.clear();
    d->hasCurrent = false;
}

void RecordingCanvas::moveTo(double x, double y)
{
    d->push(OpKind::MoveTo, x, y);
    if (!allFinite(x, y))
        return;
    d->points.push_back(d->map(x, y));
    d->current = d->subpathStart = {x, y};
    d->hasCurrent = true;
}

void RecordingCanvas::lineTo(double x, double y)
{
    d->push(OpKind::LineTo, x, y);
    if (!allFinite(x, y))
        return;
    d->points.push_back(d->map(x, y));
    d->current = {x, y};
    d->hasCurrent = true;
}

void RecordingCanvas::quadTo(double x1, double y1, double x2, double y2)
{
    d->push(OpKind::QuadTo, x1, y1, x2, y2);
    if (!allFinite(x1, y1, x2, y2))
        return;
    d->startSubpath(x1, y1);
    d->points.push_back(d->map(x1, y1));
    d->points.push_back(d->map(x2, y2));
    d->current = {x2, y2};
}

void RecordingCanvas::curveTo(double x1, double y1, double x2, double y2, double x3, double y3)
{
    d->push(OpKind::CurveTo, x1, y1, x2, y2, x3, y3);
    if (!allFinite(x1, y1, x2, y2, x3, y3))
        return;
    d->startSubpath(x1, y1);
    d->points.push_back(d->map(x1, y1));
    d->points.push_back(d->map(x2, y2));
    d->points.push_back(d->map(x3, y3));
    d->current = {x3, y3};
}

void RecordingCanvas::arcTo(double rx, double ry, double angle, bool largeArc, bool sweep, double x, double y)
{
    Op& op = d->push(OpKind::ArcTo, rx, ry, angle, x, y);
    op.flags = static_cast<std::uint8_t>((largeArc ? 1 : 0) | (sweep ? 2 : 0));
    if (!allFinite(rx, ry, angle, x, y))
        return;
    if (!d->hasCurrent) {
        d->points.push_back(d->map(x, y));
        d->current = d->subpathStart = {x, y};
        d->hasCurrent = true;
        return;
    }
    geometry::Path arc;
    arc.moveTo(d->current);
    arc.arcTo(rx, ry, angle, largeArc, sweep, {x, y});
    using Kind = geometry::PathElement::Kind;
    for (const geometry::PathElement& e : arc.elements()) {
        const int n = e.kind == Kind::CubicTo ? 3 : e.kind == Kind::QuadTo ? 2 : e.kind == Kind::LineTo ? 1 : 0;
        for (int i = 0; i < n; ++i)
            d->points.push_back(d->map(e.p[i].x, e.p[i].y));
    }
    d->current = {x, y};
}

void RecordingCanvas::close()
{
    d->push(OpKind::Close);
    if (d->hasCurrent)
        d->current = d->subpathStart;
}

void RecordingCanvas::rect(double x, double y, double w, double h)
{
    d->push(OpKind::Rect, x, y, w, h);
    if (!allFinite(x, y, w, h) || w < 0 || h < 0)
        return;
    d->addRectPoints(x, y, w, h);
    d->current = d->subpathStart = {x, y};
    d->hasCurrent = true;
}

void RecordingCanvas::roundrect(double x, double y, double w, double h, double dx, double dy)
{
    d->push(OpKind::RoundRect, x, y, w, h, dx, dy);
    if (!allFinite(x, y, w, h, dx, dy) || w < 0 || h < 0)
        return;
    d->addRectPoints(x, y, w, h);
    d->current = d->subpathStart = {x + std::clamp(dx, 0.0, w / 2), y};
    d->hasCurrent = true;
}

void RecordingCanvas::ellipse(double x, double y, double w, double h)
{
    d->push(OpKind::Ellipse, x, y, w, h);
    if (!allFinite(x, y, w, h) || w < 0 || h < 0)
        return;
    d->addRectPoints(x, y, w, h);
    d->current = d->subpathStart = {x + w, y + h / 2};
    d->hasCurrent = true;
}

void RecordingCanvas::addPath(const geometry::Path& path)
{
    d->list.paths_.push_back(path);
    d->pushIdx(OpKind::AddPath, 0, static_cast<std::uint32_t>(d->list.paths_.size() - 1));
    // PainterCanvas::addPath replays the elements through moveTo/lineTo/...:
    // so does the bounds tracking, without recording them again.
    using Kind = geometry::PathElement::Kind;
    for (const geometry::PathElement& e : path.elements()) {
        const int n = e.kind == Kind::CubicTo ? 3 : e.kind == Kind::QuadTo ? 2 : e.kind == Kind::Close ? 0 : 1;
        bool finite = true;
        for (int i = 0; i < n; ++i)
            finite = finite && allFinite(e.p[i].x, e.p[i].y);
        if (!finite)
            continue;
        if (e.kind == Kind::MoveTo) {
            d->current = d->subpathStart = e.p[0];
            d->hasCurrent = true;
        } else if (e.kind == Kind::Close) {
            if (d->hasCurrent)
                d->current = d->subpathStart;
            continue;
        } else {
            d->startSubpath(e.p[0].x, e.p[0].y);
            d->current = e.p[n - 1];
            if (e.kind == Kind::LineTo) {
                d->points.push_back(d->map(e.p[0].x, e.p[0].y));
                continue;
            }
        }
        for (int i = 0; i < n; ++i)
            d->points.push_back(d->map(e.p[i].x, e.p[i].y));
    }
}

void RecordingCanvas::fill()
{
    d->push(OpKind::Fill);
    d->paintPath(true, false);
}
void RecordingCanvas::stroke()
{
    d->push(OpKind::Stroke);
    d->paintPath(false, true);
}
void RecordingCanvas::fillAndStroke()
{
    d->push(OpKind::FillAndStroke);
    d->paintPath(true, true);
}

void RecordingCanvas::image(double x, double y, double w, double h, const ImageData& image, bool aspect, bool flipH,
                            bool flipV)
{
    d->list.images_.push_back(image);
    Op& op = d->push(OpKind::Image, x, y, w, h);
    op.flags = static_cast<std::uint8_t>((aspect ? 1 : 0) | (flipH ? 2 : 0) | (flipV ? 4 : 0));
    op.index = static_cast<std::uint32_t>(d->list.images_.size() - 1);
    if (image.empty() || !allFinite(x, y, w, h) || w <= 0 || h <= 0)
        return;
    // The box (the picture sits inside it, flips mirror it onto itself).
    QRectF box(d->map(x, y), QSizeF(w * d->state.scale, h * d->state.scale));
    growBy(d->target(), d->state.rotation.mapRect(box));
    if (image.cached)
        d->list.cost_.imagePixels += double(image.cached->size().width()) * image.cached->size().height();
}

void RecordingCanvas::text(const TextBlock& block)
{
    d->list.texts_.push_back(block);
    d->pushIdx(OpKind::Text, 0, static_cast<std::uint32_t>(d->list.texts_.size() - 1));
    d->text(block);
}

void RecordingCanvas::textOnPath(const PathText& text)
{
    d->list.pathTexts_.push_back(text);
    d->pushIdx(OpKind::TextOnPath, 0, static_cast<std::uint32_t>(d->list.pathTexts_.size() - 1));
    d->textOnPath(text);
}

void RecordingCanvas::beginGroup(std::string_view cellId)
{
    d->pushIdx(OpKind::BeginGroup, 0, d->addString(cellId));
}

void RecordingCanvas::endGroup()
{
    d->push(OpKind::EndGroup);
}

} // namespace qraw::render
