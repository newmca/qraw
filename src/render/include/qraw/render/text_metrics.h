#pragma once

// Label text layout shared by the raster canvas and by code that has to know
// how big a label is (autosize on commit, label editor). One implementation
// so that a measured size is the size PainterCanvas::text draws.

#include "qraw/render/resolved_style.h"
#include "qraw/render/text.h"

#include <QFont>
#include <QSizeF>
#include <QString>
#include <QTextDocument>

#include <optional>
#include <string_view>

namespace qraw::render {

// The first family of a CSS font list, without quotes ("'Open Sans', Arial"
// gives Open Sans); Helvetica for an empty list. Every canvas builds its QFont
// from it.
[[nodiscard]] QString firstFontFamily(const QString& cssList);

// The font of diagram text. draw.io font sizes are CSS pixels: the size is a pixel size, never a
// point size, so the UI font DPI (QT_FONT_DPI, Xft.dpi, "Force fonts DPI") cannot scale a label.
// Hinting is switched off: Qt's default depends on the screen (vertical hinting at a device pixel
// ratio of 1, none above it, and the user's fontconfig settings), which would make the glyph
// pixels of an export, of a thumbnail and of the canvas differ from machine to machine. The
// metrics are the same either way. `fontStyle` holds FontBold, FontItalic, ... bits.
[[nodiscard]] QFont diagramFont(const QString& family, int pixelSize, int fontStyle);

// The height Qt's text layout gives one line of `font` (an unscaled line: no CSS line height applied),
// measured on a probe document: the font's height rounded up to whole pixels plus its leading when the
// layout counts it. A proportional line height is a percentage of this number. Cached per font and thread.
[[nodiscard]] double naturalLineHeight(const QFont& font);

// Fills `doc` the way PainterCanvas::text lays out `block` at `scale`: font
// (first family of fontFamily, fontSize * scale px, fontStyle bits), text
// alignment, wrapping at `wrapWidth` px (none when <= 0 or !block.wrap),
// content (HTML or plain) and the CSS line height. Does not set the text
// width; the caller does (wrapWidth, or idealWidth for unwrapped text).
void configureTextDocument(QTextDocument& doc, const TextBlock& block, double scale, double wrapWidth);

// Whether configureTextDocument gave the lines of `doc` the exact pitch `line-height * font size` (fixed line
// heights: text of one size) instead of a percentage of what Qt measures. The label placement relies on it.
[[nodiscard]] bool hasExactLinePitch(const QTextDocument& doc);

// The size (page units) the text of `block` occupies: its explicit lines
// laid out with the block's font, wrapped at `wrapWidth` when given and
// block.wrap is set. Ignores the block geometry, padding and rotation.
[[nodiscard]] QSizeF measureText(const TextBlock& block, std::optional<double> wrapWidth = std::nullopt);

// The text parameters of a cell label drawn with `style` for `value`
// (html per style.html()); geometry fields are left at their defaults.
[[nodiscard]] TextBlock textBlockForValue(const ResolvedStyle& style, std::string_view value);

} // namespace qraw::render
