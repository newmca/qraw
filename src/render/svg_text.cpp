// SvgCanvas::text: labels as draw.io writes them (DECISIONS.md D-023).
//
// Port of mxgraph/src/util/mxSvgCanvas2D.js (draw.io, Apache-2.0), modified:
// text(), addForeignObject(), updateTextNodes(), createCss(), getTextCss()
// and createAlternateContent(), without any measuring. Differences:
//  - no vertical writing modes and no HTML to native-SVG conversion;
//  - the <text> fallback uses RichText::lines as they are (no wrapping);
//  - plain labels use the same foreignObject structure (white-space:
//    pre-wrap) and a <text> fallback;
//  - an unbounded box (width 0) never wraps; upstream's "+ 2" padding is only
//    added to real widths.
//
//   <g transform opacity>
//     <switch>
//       <foreignObject ...>
//         <div xmlns=xhtml style="display: flex; ...">         flex container
//           <div style="box-sizing: border-box; ...">          item
//             <div style="display: inline-block; ...">LABEL</div>
//       <text ...>FALLBACK</text>
//     </switch>
//   </g>

#include "qraw/render/resolved_style.h"

#include "svg_canvas_impl.h"

#include <cctype>
#include <cmath>

namespace qraw::render {

using svg::number;
using svg::Tag;

namespace {

constexpr double kForeignObjectPadding = 2; // mxSvgCanvas2D.foreignObjectPadding

std::string px(double value) { return number(std::round(value)) + "px"; }

std::string cssColor(const QColor& c)
{
    if (c.alpha() == 255)
        return svg::hexColor(c);
    return "rgba(" + std::to_string(c.red()) + ", " + std::to_string(c.green()) + ", " + std::to_string(c.blue()) +
           ", " + number(c.alphaF()) + ")";
}

// CSS inside a double-quoted attribute: quotes in family names become '.
std::string cssFontFamily(const QString& family)
{
    std::string out = family.trimmed().toStdString();
    for (char& c : out) {
        if (c == '"')
            c = '\'';
    }
    return out.empty() ? "Helvetica" : out;
}

const char* hAlignCss(geometry::HAlign a)
{
    return a == geometry::HAlign::Left ? "left" : a == geometry::HAlign::Right ? "right" : "center";
}

// mxSvgCanvas2D.getTextCss
std::string textCss(const TextBlock& b)
{
    std::string css = "display: inline-block; font-size: " + number(b.fontSize) + "px; font-family: " +
                      cssFontFamily(b.fontFamily) + "; color: " + cssColor(b.fontColor) +
                      "; line-height: " + number(b.lineHeight) + "; pointer-events: all; ";
    if (b.fontStyle & FontBold)
        css += "font-weight: bold; ";
    if (b.fontStyle & FontItalic)
        css += "font-style: italic; ";
    std::string decoration;
    if (b.fontStyle & FontUnderline)
        decoration = "underline";
    if (b.fontStyle & FontStrikethrough)
        decoration += (decoration.empty() ? "" : " ") + std::string("line-through");
    if (!decoration.empty())
        css += "text-decoration: " + decoration + "; ";
    return css;
}

struct TextCss {
    double dx = 0;
    double dy = 0;
    std::string flex;  // width/height of the flex container (appended to the base flex css)
    std::string item;  // the middle div
    std::string block; // the inner div
};

// mxSvgCanvas2D.createCss (horizontal writing only). `w` includes the
// foreignObject padding, 0 when the box is unbounded.
TextCss createCss(const TextBlock& b, double w, double h, bool plain)
{
    TextCss css;
    std::string block = textCss(b);
    std::string item = std::string("box-sizing: border-box; font-size: 0px; text-align: ") + hAlignCss(b.align) + "; ";

    const double ptX = b.align == geometry::HAlign::Left ? 0 : b.align == geometry::HAlign::Right ? -1 : -0.5;
    const double ptY = b.verticalAlign == geometry::VAlign::Top ? 0 : b.verticalAlign == geometry::VAlign::Bottom ? -1 : -0.5;
    std::string overflowCss = "overflow: hidden; ";
    std::string fw = "width: 1px; ";
    std::string fh = "height: 1px; ";
    double dx = ptX * w;
    double dy = ptY * h;

    if (b.clip) {
        fw = "width: " + px(w) + "; ";
        item += "max-height: " + px(h) + "; ";
        dy = 0;
    } else if (b.overflow == geometry::Overflow::Fill) {
        fw = "width: " + px(w) + "; ";
        fh = "height: " + px(h) + "; ";
        block += "width: 100%; height: 100%; ";
        item += "width: " + px(w - 2) + "; " + fh;
    } else if (b.overflow == geometry::Overflow::Width) {
        fw = "width: " + px(w - 2) + "; ";
        block += "width: 100%; ";
        item += fw;
        dy = 0;
        if (h > 0)
            item += "max-height: " + px(h) + "; ";
    } else if (b.overflow == geometry::Overflow::Block) {
        fw = "width: " + px(w - 2) + "; ";
        block += "width: 100%; ";
        overflowCss.clear();
        dy = 0;
        item += fw;
        if (b.verticalAlign == geometry::VAlign::Middle)
            item += "max-height: " + px(h) + "; ";
    } else {
        overflowCss.clear();
        dy = 0;
    }

    // Background and border go around the text (and the padding).
    std::string decoration;
    if (b.backgroundColor)
        decoration += "background-color: " + cssColor(*b.backgroundColor) + "; ";
    if (b.borderColor)
        decoration += "border: 1px solid " + cssColor(*b.borderColor) + "; ";
    if (!decoration.empty() && b.padding != 0) {
        const std::string p = number(b.padding) + "px";
        decoration += "padding: " + p + "; ";
        if (overflowCss.empty() || b.clip)
            decoration += "margin: " + number(-b.padding) + "px; ";
    }
    if (overflowCss.empty() || b.clip)
        block += decoration;
    else
        item += decoration;

    if (b.wrap && w > 0) {
        block += plain ? "white-space: pre-wrap; word-wrap: normal; " : "white-space: normal; word-wrap: normal; ";
        fw = "width: " + px(w) + "; ";
        if (!overflowCss.empty() && b.overflow != geometry::Overflow::Fill)
            dy = 0;
    } else {
        block += plain ? "white-space: pre; " : "white-space: nowrap; ";
        if (overflowCss.empty() && b.overflow != geometry::Overflow::Block)
            dx = 0;
    }

    css.dx = dx;
    css.dy = dy;
    css.flex = fw + fh;
    css.item = item + overflowCss;
    css.block = block;
    return css;
}

} // namespace

void SvgCanvas::Impl::text(const TextBlock& b)
{
    if (b.text.empty() || b.fontSize <= 0)
        return;
    const double values[] = {b.x, b.y, b.width, b.height, b.rotation, b.fontSize, b.opacity, b.lineHeight, b.padding};
    for (const double v : values) {
        if (!std::isfinite(v))
            return;
    }

    const State& s = state;
    const double w = b.width > 0 ? b.width + kForeignObjectPadding : 0;
    const double h = b.height;
    const double x = b.x + s.dx;
    const double y = b.y + s.dy;
    const bool plain = !b.text.html;

    const char* alignItems = b.verticalAlign == geometry::VAlign::Top      ? "flex-start"
                             : b.verticalAlign == geometry::VAlign::Bottom ? "flex-end"
                                                                           : "center";
    const char* justifyContent = b.align == geometry::HAlign::Left    ? "flex-start"
                                 : b.align == geometry::HAlign::Right ? "flex-end"
                                                                      : "center";
    const TextCss css = createCss(b, w, h, plain);
    const double xp = std::round(x + css.dx);
    const double yp = std::round(y + css.dy);

    // <g>: the canvas transform, the zoom and the text rotation.
    std::string transform = s.transform;
    if (s.scale != 1)
        transform += "scale(" + number(s.scale) + ")";
    if (b.rotation != 0)
        transform += "rotate(" + number(b.rotation) + " " + number(x) + " " + number(y) + ")";
    Tag group("g");
    if (!transform.empty())
        group.attr("transform", transform);
    const double opacity = s.alpha * b.opacity;
    if (opacity < 1)
        group.attr("opacity", opacity);
    emitLine(group.open());

    // Fallback first (decided here, written after the foreignObject).
    const std::vector<std::string>& lines = b.text.lines;
    const bool hasFallback = !lines.empty();
    if (hasFallback)
        emitLine("<switch>");

    // <foreignObject>
    const std::string size = number(std::ceil(1 / std::min(1.0, s.scale) * 100)) + "%";
    Tag fo("foreignObject");
    if (hasFallback)
        fo.attr("requiredFeatures", "http://www.w3.org/TR/SVG11/feature#Extensibility");
    fo.attr("pointer-events", "none").attr("width", size).attr("height", size);
    // A negative top cannot be a padding: the foreignObject itself moves up.
    std::string paddingTop = "padding-top: " + number(yp) + "px; ";
    if (yp < 0) {
        fo.attr("y", yp);
        paddingTop = "padding-top: 0; ";
    }
    fo.attr("style", "overflow: visible; text-align: left;");
    emitLine(fo.open());

    const std::string flexStyle = std::string("display: flex; align-items: unsafe ") + alignItems +
                                  "; justify-content: unsafe " + justifyContent + "; " + css.flex + paddingTop +
                                  "margin-left: " + number(xp) + "px;";
    const std::string itemStyle = css.item + "color: " + cssColor(b.fontColor) + "; ";
    const std::string label = plain ? svg::escapeText(b.text.content) : b.text.content;
    emitLine(Tag("div").attr("xmlns", "http://www.w3.org/1999/xhtml").attr("style", flexStyle).open() +
             Tag("div").attr("style", itemStyle).open() + Tag("div").attr("style", css.block).open() + label +
             "</div></div></div>");
    emitLine("</foreignObject>");

    if (hasFallback) {
        // mxSvgCanvas2D.createAlternateContent without measuring.
        const auto n = static_cast<double>(lines.size());
        const double lineStep = std::round(b.lineHeight * b.fontSize);
        const double baseline = b.verticalAlign == geometry::VAlign::Top      ? 1
                                : b.verticalAlign == geometry::VAlign::Bottom ? 0
                                                                              : 0.3;
        const double x0 = std::round(x);
        const double blockOffset = b.verticalAlign == geometry::VAlign::Top      ? 0
                                   : b.verticalAlign == geometry::VAlign::Bottom ? (n - 1) * lineStep
                                                                                 : (n - 1) * lineStep / 2;
        const double y0 = y + baseline * b.fontSize - blockOffset;

        Tag alt("text");
        alt.attr("x", x0).attr("y", std::round(y0));
        alt.attr("fill", b.fontColor.alpha() == 0 ? "none" : svg::hexColor(b.fontColor));
        alt.attr("font-family", cssFontFamily(b.fontFamily)).attr("font-size", number(std::round(b.fontSize)) + "px");
        alt.attr("text-anchor", b.align == geometry::HAlign::Right  ? "end"
                                : b.align == geometry::HAlign::Left ? "start"
                                                                    : "middle");
        if (b.fontStyle & FontBold)
            alt.attr("font-weight", "bold");
        if (b.fontStyle & FontItalic)
            alt.attr("font-style", "italic");
        std::string decoration;
        if (b.fontStyle & FontUnderline)
            decoration = "underline";
        if (b.fontStyle & FontStrikethrough)
            decoration += (decoration.empty() ? "" : " ") + std::string("line-through");
        if (!decoration.empty())
            alt.attr("text-decoration", decoration);
        if (b.clip && b.width > 0 && b.height > 0) {
            // plainText: the clip is the label box, 2px larger on each side.
            const double left = x - (b.align == geometry::HAlign::Center  ? b.width / 2
                                     : b.align == geometry::HAlign::Right ? b.width
                                                                          : 0);
            const double top = y - (b.verticalAlign == geometry::VAlign::Middle  ? b.height / 2
                                    : b.verticalAlign == geometry::VAlign::Bottom ? b.height
                                                                                  : 0);
            alt.attr("clip-path", "url(#" + clipPathId(left - 2, top - 2, b.width + 4, b.height + 4) + ")");
        }

        std::string textLine = alt.open();
        if (lines.size() == 1) {
            textLine += svg::escapeText(lines[0]);
        } else {
            for (std::size_t i = 0; i < lines.size(); ++i) {
                textLine += Tag("tspan")
                                .attr("x", x0)
                                .attr("y", std::round(y0 + static_cast<double>(i) * lineStep))
                                .open() +
                            svg::escapeText(lines[i]) + "</tspan>";
            }
        }
        textLine += "</text>";
        emitLine(textLine);
        emitLine("</switch>");
    }
    emitLine("</g>");
}

// ------------------------------------------------------------ text along a path

namespace {

// An XML id made of the characters [A-Za-z0-9-]; everything else (and the
// underscore itself) becomes _xx, so different cell ids never collide.
std::string idFragment(std::string_view cellId)
{
    static constexpr char hex[] = "0123456789abcdef";
    std::string out;
    for (const char ch : cellId) {
        const auto c = static_cast<unsigned char>(ch);
        if (std::isalnum(c) != 0 && c < 0x80) {
            out += ch;
        } else if (c == '-') {
            out += ch;
        } else {
            out += '_';
            out += hex[c >> 4];
            out += hex[c & 15];
        }
    }
    return out;
}

} // namespace

// CurvedTextShape.paintForeground (Shapes.js), the SVG branch: a group with
// the canvas transform, the path in <defs>, and <text><textPath>.
void SvgCanvas::Impl::textOnPath(const PathText& t)
{
    if (t.text.empty() || t.path.empty() || t.fontSize <= 0 || !std::isfinite(t.fontSize) ||
        !std::isfinite(t.textLength) || !std::isfinite(t.opacity))
        return;

    std::string d;
    const auto add = [&](char op, std::initializer_list<geometry::Point> points) {
        if (!d.empty())
            d += ' ';
        d += op;
        for (const geometry::Point& p : points)
            d += " " + number(tx(p.x)) + " " + number(ty(p.y));
    };
    using Kind = geometry::PathElement::Kind;
    for (const geometry::PathElement& e : t.path.elements()) {
        switch (e.kind) {
        case Kind::MoveTo: add('M', {e.p[0]}); break;
        case Kind::LineTo: add('L', {e.p[0]}); break;
        case Kind::QuadTo: add('Q', {e.p[0], e.p[1]}); break;
        case Kind::CubicTo: add('C', {e.p[0], e.p[1], e.p[2]}); break;
        case Kind::Close: d += " Z"; break;
        }
    }

    const State& s = state;
    const std::string id =
        options.idPrefix + "-ctp-" + (cellIds.empty() ? std::to_string(++idCounter) : idFragment(cellIds.back()));

    Tag group("g");
    if (!s.transform.empty())
        group.attr("transform", s.transform);
    const double opacity = s.alpha * t.opacity;
    if (opacity < 1)
        group.attr("opacity", opacity);
    emitLine(group.open());
    emitLine("<defs>" + Tag("path").attr("id", id).attr("d", d).selfClosed() + "</defs>");

    Tag text("text");
    text.attr("font-size", number(t.fontSize * s.scale) + "px");
    text.attr("font-family", cssFontFamily(t.fontFamily));
    text.attr("fill", svg::hexColor(t.fontColor));
    if (t.fontStyle & FontBold)
        text.attr("font-weight", "bold");
    if (t.fontStyle & FontItalic)
        text.attr("font-style", "italic");
    std::string decoration;
    if (t.fontStyle & FontUnderline)
        decoration = "underline";
    if (t.fontStyle & FontStrikethrough)
        decoration += (decoration.empty() ? "" : " ") + std::string("line-through");
    if (!decoration.empty())
        text.attr("text-decoration", decoration);
    const bool left = t.align == geometry::HAlign::Left;
    const bool right = t.align == geometry::HAlign::Right;
    text.attr("text-anchor", left ? "start" : right ? "end" : "middle");
    text.attr("dominant-baseline", "central");

    Tag textPath("textPath");
    textPath.attr("startOffset", left ? "0%" : right ? "100%" : "50%");
    textPath.attr("href", "#" + id).attr("xlink:href", "#" + id);
    if (t.textLength > 0)
        textPath.attr("textLength", t.textLength * s.scale).attr("lengthAdjust", "spacing");
    emitLine(text.open() + textPath.open() + svg::escapeText(t.text) + "</textPath></text>");
    emitLine("</g>");
}

} // namespace qraw::render
