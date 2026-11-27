#pragma once
// Conversion between draw.io label markup and the inline editor's
// QTextDocument. Private to the editor UI.

#include <QColor>
#include <QFont>

#include <string>
#include <string_view>

class QTextDocument;

namespace qraw::ui::label {

// Loads an HTML label (draw.io stores the editor div's innerHTML):
// render::sanitizeHtml, then QTextDocument::setHtml. Explicit pixel/point
// sizes inside the markup are multiplied by `zoom` so that the document
// matches the zoomed canvas (the base font of the document must already be
// scaled by the caller).
void loadHtml(QTextDocument& document, std::string_view html, double zoom);

// Multiplies the explicit font sizes of the document by `factor` (zoom
// changed while editing).
void scaleExplicitSizes(QTextDocument& document, double factor);

// The label markup of `document` in draw.io's style, deterministic:
//  * the first paragraph is bare inline content, every further one is a
//    <div>; empty ones are <div><br></div>; a line break inside a paragraph
//    is <br>;
//  * character formats become <b>, <i>, <u>, <s>, <sub>, <sup>, <a href>,
//    <font color> and <span style="font-size/font-family"> (sizes divided by
//    `zoom`); formats equal to the base font (`document.defaultFont()`) and
//    `baseColor` are not written;
//  * lists become <ul>/<ol> with <li>;
//  * text is escaped; runs of spaces and a leading space use &nbsp;.
// Other markup (tables, headings, images) is not representable and is lost
// when the label is edited (documented gap).
[[nodiscard]] std::string exportHtml(const QTextDocument& document, double zoom, const QColor& baseColor);

} // namespace qraw::ui::label
