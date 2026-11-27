// Port of mxgraph/src/shape/mxStencil.js and the labelBounds hook of
// js/grapheditor/Shapes.js (draw.io, Apache-2.0), modified: painting goes
// through the Canvas interface, fonts and colors come from ResolvedStyle.

#include "qraw/core/number.h"
#include "qraw/geometry/polyline.h"
#include "qraw/render/image_cache.h"
#include "qraw/render/image_source.h"
#include "qraw/render/stencil.h"
#include "qraw/render/stylesheet.h"

#include <QColor>
#include <QString>

#include <algorithm>
#include <cmath>
#include <sstream>
#include <unordered_map>

namespace qraw::render {

namespace {

// include-shape cycles are cut here (upstream recurses without a limit).
constexpr int kMaxIncludeDepth = 8;
thread_local int includeDepth = 0;

// --- attribute access with JavaScript semantics ------------------------------

// Number(node.getAttribute(name)): a missing or unparsable attribute is 0
// (Number(null) is 0; NaN would only poison the coordinates).
double num(const core::XmlNode& node, std::string_view name)
{
    const std::string* value = node.attributes.find(name);
    return value ? core::parseNumber(*value, 0) : 0;
}

std::string_view str(const core::XmlNode& node, std::string_view name, std::string_view fallback = {})
{
    const std::string* value = node.attributes.find(name);
    return value ? std::string_view(*value) : fallback;
}

bool isOne(const core::XmlNode& node, std::string_view name) { return str(node, name) == "1"; }

// --- computeAspect ------------------------------------------------------------

// mxStencil.computeAspect: the origin and the scale of stencil coordinates.
struct Aspect {
    double x = 0;
    double y = 0;
    double sx = 1;
    double sy = 1;
    [[nodiscard]] double minScale() const { return std::min(sx, sy); }
};

Aspect computeAspect(double w0, double h0, bool fixedAspect, bool inverse, double x, double y, double w, double h)
{
    Aspect a{x, y, w / w0, h / h0};
    if (inverse) {
        a.sy = w / h0;
        a.sx = h / w0;
        const double delta = (w - h) / 2;
        a.x += delta;
        a.y -= delta;
    }
    if (fixedAspect) {
        a.sy = std::min(a.sx, a.sy);
        a.sx = a.sy;
        // Centers the shape inside the available space.
        if (inverse) {
            a.x += (h - w0 * a.sx) / 2;
            a.y += (w - h0 * a.sy) / 2;
        } else {
            a.x += (w - w0 * a.sx) / 2;
            a.y += (h - h0 * a.sy) / 2;
        }
    }
    return a;
}

bool isInverse(geometry::Direction d) { return d == geometry::Direction::North || d == geometry::Direction::South; }

// --- colors ---------------------------------------------------------------------

std::optional<QColor> toColor(std::string_view value)
{
    if (value.empty() || value == "none")
        return std::nullopt;
    const QColor color = parseStyleColor(value, QColor());
    if (!color.isValid())
        return std::nullopt;
    return color;
}

// mxUtils.isValidColor: hex, rgb()/hsl() functions, CSS color names, none.
bool isValidColor(std::string_view value)
{
    if (value == "none" || value.starts_with('#') || value.starts_with("rgb") || value.starts_with("hsl"))
        return true;
    return QColor::isValidColorName(QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())));
}

// --- the interpreter ------------------------------------------------------------

enum class Op {
    Unknown,
    Save,
    Restore,
    Path,
    Close,
    Move,
    Line,
    Quad,
    Curve,
    Arc,
    Rect,
    RoundRect,
    Ellipse,
    Image,
    Text,
    IncludeShape,
    FillStroke,
    Fill,
    Stroke,
    StrokeWidth,
    Dashed,
    DashPattern,
    StrokeColor,
    LineCap,
    LineJoin,
    MiterLimit,
    FillColor,
    Alpha, // alpha, fillalpha and strokealpha: upstream maps all three to setAlpha
    FontColor,
    FontStyle,
    FontFamily,
    FontSize,
};

Op opOf(std::string_view name)
{
    static const std::unordered_map<std::string_view, Op> ops = {
        {"save", Op::Save},
        {"restore", Op::Restore},
        {"path", Op::Path},
        {"close", Op::Close},
        {"move", Op::Move},
        {"line", Op::Line},
        {"quad", Op::Quad},
        {"curve", Op::Curve},
        {"arc", Op::Arc},
        {"rect", Op::Rect},
        {"roundrect", Op::RoundRect},
        {"ellipse", Op::Ellipse},
        {"image", Op::Image},
        {"text", Op::Text},
        {"include-shape", Op::IncludeShape},
        {"fillstroke", Op::FillStroke},
        {"fill", Op::Fill},
        {"stroke", Op::Stroke},
        {"strokewidth", Op::StrokeWidth},
        {"dashed", Op::Dashed},
        {"dashpattern", Op::DashPattern},
        {"strokecolor", Op::StrokeColor},
        {"linecap", Op::LineCap},
        {"linejoin", Op::LineJoin},
        {"miterlimit", Op::MiterLimit},
        {"fillcolor", Op::FillColor},
        {"alpha", Op::Alpha},
        {"fillalpha", Op::Alpha},
        {"strokealpha", Op::Alpha},
        {"fontcolor", Op::FontColor},
        {"fontstyle", Op::FontStyle},
        {"fontfamily", Op::FontFamily},
        {"fontsize", Op::FontSize},
    };
    const auto it = ops.find(name);
    return it == ops.end() ? Op::Unknown : it->second;
}

// The text state of the canvas (mxAbstractCanvas2D font*), saved and restored
// with save()/restore() like the rest of the canvas state.
struct FontState {
    std::optional<QColor> color;
    QString family;
    double size = kDefaultFontSize;
    int style = 0;
};

class Interpreter {
public:
    Interpreter(Canvas& canvas, const ShapePaintContext& context, const Aspect& aspect)
        : c_(canvas), context_(context), style_(context.style), aspect_(aspect)
    {
        FontState font;
        font.color = style_.fontColor().value_or(Theme{}.foreground);
        font.family = QString::fromUtf8(style_.fontFamily().data(), static_cast<qsizetype>(style_.fontFamily().size()));
        font.size = style_.fontSize();
        font.style = style_.fontStyle();
        fonts_.push_back(font);
    }

    // mxStencil.drawChildren
    void drawChildren(const core::XmlNode* parent, bool disableShadow)
    {
        if (!parent)
            return;
        for (const core::XmlNode& child : parent->children)
            if (child.isElement())
                drawNode(child, disableShadow);
    }

    // mxStencil.drawShape: "Restores stack for unequal count of save/restore calls".
    void restoreUnbalancedSaves()
    {
        while (fonts_.size() > 1)
            doRestore();
    }

private:
    void doSave()
    {
        c_.save();
        fonts_.push_back(fonts_.back());
    }

    void doRestore()
    {
        if (fonts_.size() <= 1)
            return; // more restores than saves must not pop the caller's state
        c_.restore();
        fonts_.pop_back();
    }

    [[nodiscard]] double px(const core::XmlNode& n, std::string_view attr) const
    {
        return aspect_.x + num(n, attr) * aspect_.sx;
    }
    [[nodiscard]] double py(const core::XmlNode& n, std::string_view attr) const
    {
        return aspect_.y + num(n, attr) * aspect_.sy;
    }

    // mxStencil.drawNode
    void drawNode(const core::XmlNode& node, bool disableShadow)
    {
        const double minScale = aspect_.minScale();
        const double sx = aspect_.sx;
        const double sy = aspect_.sy;

        switch (opOf(node.name)) {
        case Op::Save:
            doSave();
            break;
        case Op::Restore:
            doRestore();
            break;
        case Op::Path:
            drawPath(node, disableShadow);
            break;
        case Op::Close:
            c_.close();
            break;
        case Op::Move:
            c_.moveTo(px(node, "x"), py(node, "y"));
            break;
        case Op::Line:
            c_.lineTo(px(node, "x"), py(node, "y"));
            break;
        case Op::Quad:
            c_.quadTo(px(node, "x1"), py(node, "y1"), px(node, "x2"), py(node, "y2"));
            break;
        case Op::Curve:
            c_.curveTo(px(node, "x1"), py(node, "y1"), px(node, "x2"), py(node, "y2"), px(node, "x3"),
                       py(node, "y3"));
            break;
        case Op::Arc:
            // The x-axis rotation is not scaled.
            c_.arcTo(num(node, "rx") * sx, num(node, "ry") * sy, num(node, "x-axis-rotation"),
                     num(node, "large-arc-flag") != 0, num(node, "sweep-flag") != 0, px(node, "x"), py(node, "y"));
            break;
        case Op::Rect:
            c_.rect(px(node, "x"), py(node, "y"), num(node, "w") * sx, num(node, "h") * sy);
            break;
        case Op::RoundRect: {
            double arcsize = num(node, "arcsize");
            if (arcsize == 0)
                arcsize = kRectangleRoundingFactor * 100;
            const double w = num(node, "w") * sx;
            const double h = num(node, "h") * sy;
            const double factor = arcsize / 100;
            const double r = std::min(w * factor, h * factor);
            c_.roundrect(px(node, "x"), py(node, "y"), w, h, r, r);
            break;
        }
        case Op::Ellipse:
            c_.ellipse(px(node, "x"), py(node, "y"), num(node, "w") * sx, num(node, "h") * sy);
            break;
        case Op::Image:
            drawImage(node);
            break;
        case Op::Text:
            drawText(node);
            break;
        case Op::IncludeShape:
            drawIncludedShape(node);
            break;
        case Op::FillStroke:
            c_.fillAndStroke();
            break;
        case Op::Fill:
            c_.fill();
            break;
        case Op::Stroke:
            c_.stroke();
            break;
        case Op::StrokeWidth:
            c_.setStrokeWidth(num(node, "width") * (isOne(node, "fixed") ? 1 : minScale));
            break;
        case Op::Dashed:
            c_.setDashed(isOne(node, "dashed"));
            break;
        case Op::DashPattern:
            setDashPattern(node, minScale);
            break;
        case Op::StrokeColor:
            c_.setStrokeColor(parseColor(node, Theme{}.foreground));
            break;
        case Op::LineCap:
            c_.setLineCap(parseLineCap(str(node, "cap")));
            break;
        case Op::LineJoin:
            c_.setLineJoin(parseLineJoin(str(node, "join")));
            break;
        case Op::MiterLimit:
            c_.setMiterLimit(num(node, "limit"));
            break;
        case Op::FillColor:
            c_.setFillColor(parseColor(node, Theme{}.background));
            break;
        case Op::Alpha:
            c_.setAlpha(num(node, "alpha"));
            break;
        case Op::FontColor:
            fonts_.back().color = parseColor(node, Theme{}.foreground);
            break;
        case Op::FontStyle:
            fonts_.back().style = static_cast<int>(num(node, "style"));
            break;
        case Op::FontFamily:
            fonts_.back().family = QString::fromUtf8(str(node, "family").data(),
                                                     static_cast<qsizetype>(str(node, "family").size()));
            break;
        case Op::FontSize:
            fonts_.back().size = num(node, "size") * minScale;
            break;
        case Op::Unknown:
            break; // ignored, like upstream
        }

        if (disableShadow && (node.name == "fillstroke" || node.name == "fill" || node.name == "stroke"))
            c_.setShadow(false);
    }

    // <path>: with rounded="1" and only move/line children the outline is
    // rounded (mxShape.addPoints); anything else is interpreted regularly.
    void drawPath(const core::XmlNode& node, bool disableShadow)
    {
        c_.begin();
        if (!(isOne(node, "rounded") && drawRoundedPath(node)))
            drawChildren(&node, disableShadow);
    }

    bool drawRoundedPath(const core::XmlNode& node)
    {
        std::vector<std::vector<geometry::Point>> segments;
        std::size_t pointCount = 0;
        for (const core::XmlNode& child : node.children) {
            if (!child.isElement())
                continue;
            if (child.name != "move" && child.name != "line")
                return false; // only move and line are supported for rounded corners
            if (child.name == "move" || segments.empty())
                segments.emplace_back();
            segments.back().push_back({px(child, "x"), py(child, "y")});
            ++pointCount;
        }
        if (pointCount == 0)
            return false;

        const double arcSize = num(node, "arcSize");
        for (auto& points : segments) {
            bool close = false;
            if (points.front() == points.back()) {
                points.pop_back();
                close = true;
            }
            c_.addPath(geometry::polylinePath(points, true, arcSize, close));
        }
        return true;
    }

    void drawImage(const core::XmlNode& node)
    {
        const std::string* src = node.attributes.find("src");
        if (!src || !context_.images)
            return; // absent images are skipped
        const auto image = cachedImage(*src, *context_.images);
        if (!image)
            return;
        c_.image(px(node, "x"), py(node, "y"), num(node, "w") * aspect_.sx, num(node, "h") * aspect_.sy, *image, false,
                 isOne(node, "flipH"), isOne(node, "flipV"));
    }

    // dashpattern: the space separated numbers scaled by minScale. Without a
    // `pattern` attribute (5 shipped shapes use a wrong one) nothing happens.
    void setDashPattern(const core::XmlNode& node, double minScale)
    {
        const std::string* pattern = node.attributes.find("pattern");
        if (!pattern)
            return;
        std::string scaled;
        std::istringstream tokens(*pattern);
        for (std::string token; std::getline(tokens, token, ' ');) {
            if (token.empty())
                continue;
            if (!scaled.empty())
                scaled += ' ';
            scaled += core::formatNumber(core::parseNumber(token, 0) * minScale);
        }
        c_.setDashPattern(scaled);
    }

    // The text op: Canvas::text at the scaled anchor, with the current font
    // state, `vertical`, `rotation`, `align-shape` and `flip-shape`.
    void drawText(const core::XmlNode& node)
    {
        const std::string* text = node.attributes.find("str");
        const FontState& font = fonts_.back();
        if (!text || text->empty() || !font.color)
            return;

        double rotation = isOne(node, "vertical") ? -90 : 0;
        std::string alignValue(str(node, "align"));
        std::string valignValue(str(node, "valign"));
        if (alignValue.empty())
            alignValue = "left";
        if (valignValue.empty())
            valignValue = "top";

        // flip-shape="0" keeps the text unmirrored in flipped shapes.
        const bool unflip = str(node, "flip-shape") == "0";
        const bool unflipH = unflip && style_.flipH();
        const bool unflipV = unflip && style_.flipV();

        if (str(node, "align-shape") == "0") {
            const double dr = style_.rotation();
            const bool flipH = style_.flipH();
            const bool flipV = style_.flipV();
            if (unflipH || unflipV)
                rotation -= dr;
            else if (flipH && flipV)
                rotation -= dr;
            else if (flipH || flipV)
                rotation += dr;
            else
                rotation -= dr;
        }
        rotation -= num(node, "rotation");

        if ((unflipH || unflipV) && rotation == 0) {
            // Mirrors the alignment so that the text occupies the area of the flipped text.
            if (unflipH)
                alignValue = alignValue == "left" ? "right" : alignValue == "right" ? "left" : alignValue;
            if (unflipV)
                valignValue = valignValue == "top" ? "bottom" : valignValue == "bottom" ? "top" : valignValue;
        }

        TextBlock block;
        block.x = px(node, "x");
        block.y = py(node, "y");
        block.width = 0;
        block.height = 0;
        block.align = geometry::parseHAlign(alignValue, geometry::HAlign::Left);
        block.verticalAlign = geometry::parseVAlign(valignValue, geometry::VAlign::Top);
        block.rotation = rotation;
        block.text.html = false;
        block.text.content = *text;
        for (std::size_t start = 0;;) {
            const std::size_t end = text->find('\n', start);
            block.text.lines.push_back(text->substr(start, end == std::string::npos ? end : end - start));
            if (end == std::string::npos)
                break;
            start = end + 1;
        }
        block.fontFamily = font.family;
        block.fontSize = font.size;
        block.fontStyle = font.style;
        block.fontColor = *font.color;

        if (unflipH || unflipV) {
            c_.save();
            c_.rotate(0, unflipH, unflipV, block.x, block.y);
        }
        c_.text(block);
        if (unflipH || unflipV)
            c_.restore();
    }

    void drawIncludedShape(const core::XmlNode& node)
    {
        if (includeDepth >= kMaxIncludeDepth)
            return;
        const Stencil* stencil = StencilRegistry::builtin().find(str(node, "name"));
        if (!stencil)
            return;
        ++includeDepth;
        stencil->paint(c_, context_, px(node, "x"), py(node, "y"), num(node, "w") * aspect_.sx,
                       num(node, "h") * aspect_.sy);
        --includeDepth;
    }

    // mxStencil.parseColor + getColorValue + getDefaultColorValue.
    // `themeDefault` is what the token `default` resolves to.
    std::optional<QColor> parseColor(const core::XmlNode& node, const QColor& themeDefault) const
    {
        const std::string* attr = node.attributes.find("color");
        if (!attr)
            return std::nullopt;
        const std::string_view token = *attr;
        if (token == "stroke")
            return style_.strokeColor();
        if (token == "fill")
            return style_.fillColor();
        if (token == "font")
            return style_.fontColor().value_or(Theme{}.foreground);
        if (token == "default")
            return themeDefault;
        if (isValidColor(token))
            return toColor(token);

        // Not a color: a style key, falling back to the default attribute.
        const std::string* defaultAttr = node.attributes.find("default");
        const std::string* styleValue = style_.map().find(token);
        const std::string* result = styleValue ? styleValue : defaultAttr;
        if (styleValue && *styleValue == "default" && defaultAttr && *defaultAttr != "none")
            result = defaultAttr;
        if (!result)
            return std::nullopt;
        if (*result == "default")
            return themeDefault;
        return toColor(*result);
    }

    Canvas& c_;
    const ShapePaintContext& context_;
    const ResolvedStyle& style_;
    Aspect aspect_;
    std::vector<FontState> fonts_;
};

// --- labelBounds ------------------------------------------------------------------

// Math.round
double jsRound(double v) { return std::floor(v + 0.5); }

// mxUtils.getDirectedBounds: adds the margins (Insets left = x, top = y,
// right = width, bottom = height of upstream's rectangle) to `rect`,
// honoring direction and flips.
Rect directedBounds(const Rect& rect, geometry::Insets m, const ResolvedStyle& style)
{
    const auto clamp = [](double v, double max) { return jsRound(std::max(0.0, std::min(max, v))); };
    m.left = clamp(m.left, rect.width);
    m.top = clamp(m.top, rect.height);
    m.right = clamp(m.right, rect.width);
    m.bottom = clamp(m.bottom, rect.height);

    const geometry::Direction d = style.direction();
    const bool vertical = isInverse(d);
    const bool flipH = style.flipH();
    const bool flipV = style.flipV();
    if ((flipV && vertical) || (flipH && !vertical))
        std::swap(m.left, m.right);
    if ((flipH && vertical) || (flipV && !vertical))
        std::swap(m.top, m.bottom);

    geometry::Insets m2 = m; // east
    switch (d) {
    case geometry::Direction::South:
        m2.top = m.left;
        m2.left = m.bottom;
        m2.right = m.top;
        m2.bottom = m.right;
        break;
    case geometry::Direction::West:
        m2.top = m.bottom;
        m2.left = m.right;
        m2.right = m.left;
        m2.bottom = m.top;
        break;
    case geometry::Direction::North:
        m2.top = m.right;
        m2.left = m.top;
        m2.right = m.bottom;
        m2.bottom = m.left;
        break;
    case geometry::Direction::East:
        break;
    }
    return {rect.x + m2.left, rect.y + m2.top, rect.width - m2.right - m2.left, rect.height - m2.bottom - m2.top};
}

} // namespace

// --- Stencil ------------------------------------------------------------------------

std::optional<Stencil> Stencil::fromXml(const core::XmlNode& shapeElement, std::vector<core::Diagnostic>* diagnostics)
{
    if (!shapeElement.isElement("shape")) {
        if (diagnostics) {
            core::Diagnostic d;
            d.message = "stencil: expected a <shape> element, found <" + shapeElement.name + ">";
            d.line = shapeElement.sourceLine;
            diagnostics->push_back(std::move(d));
        }
        return std::nullopt;
    }

    Stencil stencil;
    stencil.shape_ = shapeElement;
    stencil.name_ = str(shapeElement, "name");
    // Number(getAttribute('w') || 100): empty or missing means 100. A zero
    // or invalid size would divide by zero in computeAspect (deviation: 100).
    const auto size = [&](std::string_view attribute) {
        const std::string* value = shapeElement.attributes.find(attribute);
        if (!value || value->empty())
            return 100.0;
        const double v = core::parseNumber(*value, 100);
        return v > 0 ? v : 100.0;
    };
    stencil.w0_ = size("w");
    stencil.h0_ = size("h");
    // Only "fixed" is not variable ("1", seen in 104 shipped shapes, is variable).
    stencil.fixedAspect_ = str(shapeElement, "aspect") == "fixed";
    stencil.strokeWidth_ = std::string(str(shapeElement, "strokewidth", "1"));
    return stencil;
}

void Stencil::paint(Canvas& canvas, const ShapePaintContext& context, double x, double y, double w, double h) const
{
    if (!(w > 0 && h > 0) || !std::isfinite(x) || !std::isfinite(y) || !std::isfinite(w) || !std::isfinite(h))
        return;
    const ResolvedStyle& style = context.style;
    const Aspect aspect = computeAspect(w0_, h0_, fixedAspect_, isInverse(style.direction()), x, y, w, h);

    // inherit: the cell's strokeWidth, unscaled. A number is scaled by minScale.
    const double strokeWidth =
        strokeWidth_ == "inherit" ? style.strokeWidth() : core::parseNumber(strokeWidth_, 1) * aspect.minScale();
    canvas.setStrokeWidth(strokeWidth);

    // Draws a transparent rectangle for catching events.
    if (style.string("pointerEvents", "0") == "1") {
        canvas.setStrokeColor(std::nullopt);
        canvas.rect(x, y, w, h);
        canvas.stroke();
        canvas.setStrokeColor(style.strokeColor());
    }

    // The background path is painted by the first op of the foreground, so
    // the shadow (enabled by the caller) applies to it and goes off with it.
    Interpreter interpreter(canvas, context, aspect);
    interpreter.drawChildren(shape_.firstChild("background"), false);
    interpreter.drawChildren(shape_.firstChild("foreground"), true);
    interpreter.restoreUnbalancedSaves();
}

Rect Stencil::labelBounds(const ResolvedStyle& style, const Rect& rect) const
{
    for (const core::XmlNode& node : shape_.children) {
        if (!node.isElement("labelBounds"))
            continue;
        const std::string* condition = node.attributes.find("if");
        if (condition && style.string(*condition, "0") != "1")
            continue;
        // Shapes.js:8-60: x, y default to 0, w, h to the stencil size.
        const auto value = [&](std::string_view attribute, double fallback) {
            const std::string* v = node.attributes.find(attribute);
            return v && !v->empty() ? core::parseNumber(*v, 0) : fallback;
        };
        const Aspect aspect = computeAspect(w0_, h0_, fixedAspect_, false, rect.x, rect.y, rect.width, rect.height);
        const double x0 = aspect.x - rect.x + value("x", 0) * aspect.sx;
        const double y0 = aspect.y - rect.y + value("y", 0) * aspect.sy;
        geometry::Insets margins;
        margins.left = x0;
        margins.top = y0;
        margins.right = rect.width - x0 - value("w", w0_) * aspect.sx;
        margins.bottom = rect.height - y0 - value("h", h0_) * aspect.sy;
        return directedBounds(rect, margins, style);
    }
    return VertexShape::labelBounds(style, rect);
}

std::vector<geometry::ConnectionConstraint> Stencil::connectionConstraints(const ResolvedStyle&, double, double) const
{
    std::vector<geometry::ConnectionConstraint> result;
    const core::XmlNode* connections = shape_.firstChild("connections");
    if (!connections)
        return result;
    for (const core::XmlNode& node : connections->children) {
        if (!node.isElement())
            continue;
        geometry::ConnectionConstraint constraint;
        constraint.point = {num(node, "x"), num(node, "y")};
        constraint.perimeter = isOne(node, "perimeter");
        constraint.name = std::string(str(node, "name"));
        constraint.dx = num(node, "dx");
        constraint.dy = num(node, "dy");
        result.push_back(std::move(constraint));
    }
    return result;
}

std::string stencilKey(std::string_view setName, std::string_view shapeName)
{
    const auto lower = [](std::string_view text) {
        return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size())).toLower().toStdString();
    };
    std::string shape = lower(shapeName);
    std::replace(shape.begin(), shape.end(), ' ', '_');
    return lower(setName) + '.' + shape;
}

} // namespace qraw::render
