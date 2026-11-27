#pragma once

// Label text: the sanitized HTML subset and plain text, and the parameters
// of one text drawing call (mxAbstractCanvas2D.text).

#include "qraw/geometry/label_layout.h"
#include "qraw/geometry/path.h"

#include <QColor>
#include <QString>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::render {

// A label value prepared for drawing.
struct RichText {
    // true: `content` is a sanitized, well-formed XHTML fragment (no root
    // element, every tag closed, entities limited to &amp; &lt; &gt; &quot;
    // &#39; and numeric references). It is safe to embed in an SVG
    // <foreignObject> and to pass to QTextDocument::setHtml.
    // false: `content` is the literal text (newlines are line breaks).
    bool html = false;
    std::string content;
    // The visible text split into lines: explicit breaks only (\n in plain
    // text; <br>, and the end of <div>, <p>, <li>, <h1>-<h6>, <tr> in HTML),
    // entities decoded, tags removed, no wrapping. Used for the SVG <text>
    // fallback and for size estimates.
    std::vector<std::string> lines;

    [[nodiscard]] bool empty() const { return content.empty(); }
    friend bool operator==(const RichText&, const RichText&) = default;
};

// Builds the drawable text of a cell value.
//  html == false: content = value; lines split at '\n'.
//  html == true:  content = sanitizeHtml(value), lines from that markup.
// Like mxText.paint, a plain value keeps its newlines and an HTML value
// turns literal '\n' into <br/> (mxText.replaceLinefeeds).
[[nodiscard]] RichText makeRichText(std::string_view value, bool html);

// Sanitizes draw.io label HTML (which draw.io itself sanitizes with
// DOMPurify, js/grapheditor/Graph.js Graph.sanitizeHtml) into a
// well-formed XHTML fragment. Parsing is lenient (HTML-style: unclosed and
// mis-nested tags, unquoted attributes, void elements, named entities from
// the HTML5 set that real labels use such as &nbsp;).
//
// Allowed elements: b, strong, i, em, u, s, strike, del, ins, sub, sup,
// font, span, div, p, br, ul, ol, li, a, h1-h6, pre, code, blockquote, hr,
// table, thead, tbody, tr, td, th, img (data: URIs only).
// Allowed attributes: style (filtered), font@color/size/face, a@href
// (http, https, mailto, data:-free, "#..." links only), img@src (data:
// URIs only)/width/height/alt, td/th@colspan/rowspan, align.
// Allowed CSS properties in style: color, background-color, font-size,
// font-family, font-weight, font-style, text-decoration, text-align,
// vertical-align, line-height, white-space, margin*, padding*, border*,
// width, height, display (none|block|inline|inline-block only).
// Values containing "url(", "expression(", "javascript:" or "@import" are
// dropped.
// Everything else is removed: unknown elements keep their text content;
// script, style, iframe, object, embed, svg, math, template and comments are
// removed with their content; event handlers and remote image loads never
// survive.
[[nodiscard]] std::string sanitizeHtml(std::string_view html);

// Text drawing parameters (mxAbstractCanvas2D.text plus the font state of
// mxSvgCanvas2D). Geometry comes from geometry::LabelBox.
struct TextBlock {
    // Anchor and box, see geometry::LabelBox (width/height 0 = unbounded).
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
    geometry::HAlign align = geometry::HAlign::Center;
    geometry::VAlign verticalAlign = geometry::VAlign::Middle;
    double rotation = 0; // degrees around (x, y)

    RichText text;
    bool wrap = false;                               // whiteSpace=wrap
    geometry::Overflow overflow = geometry::Overflow::Visible;
    bool clip = false;                               // overflow=hidden clips to the box

    QString fontFamily = QStringLiteral("Helvetica"); // style fontFamily (may be a CSS list)
    double fontSize = 12;                            // px
    int fontStyle = 0;                               // FontStyleBit mask
    QColor fontColor = QColor(0, 0, 0);
    std::optional<QColor> backgroundColor;           // labelBackgroundColor
    std::optional<QColor> borderColor;               // labelBorderColor
    double padding = 0;                              // labelPadding
    double opacity = 1;                              // textOpacity / 100 * opacity / 100
    double lineHeight = 1.2;                         // mxConstants.LINE_HEIGHT

    friend bool operator==(const TextBlock&, const TextBlock&) = default;
};

// Text set along a curve (Qraw extension of the canvas interface: there is
// no counterpart in mxAbstractCanvas2D; upstream's CurvedTextShape writes an
// SVG <textPath> by hand). Used by shape=curvedText.
struct PathText {
    // The baseline curve in the current coordinate space (like addPath).
    // The glyphs are centered on it (dominant-baseline: central).
    geometry::Path path;
    // Plain text, a single line.
    std::string text;
    // Where the text starts along the path: left 0, center 50 % and right
    // 100 % (SVG startOffset with the matching text-anchor).
    geometry::HAlign align = geometry::HAlign::Center;
    // The length the text is spread over (SVG textLength with
    // lengthAdjust=spacing: only the gaps between glyphs change). 0: the
    // natural length of the text.
    double textLength = 0;

    QString fontFamily = QStringLiteral("Helvetica"); // style fontFamily (may be a CSS list)
    double fontSize = 12;                            // px
    int fontStyle = 0;                               // FontStyleBit mask
    QColor fontColor = QColor(0, 0, 0);
    double opacity = 1;                              // textOpacity / 100

    friend bool operator==(const PathText&, const PathText&) = default;
};

} // namespace qraw::render
