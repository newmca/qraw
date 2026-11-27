#pragma once

// Edge end markers (arrowheads), ported from mxMarker
// (mxgraph/src/shape/mxMarker.js), the draw.io additions
// (js/grapheditor/Shapes.js:8280-8534) and the ER markers
// (shapes/er/mxER.js:1247-1357).

#include "qraw/render/canvas.h"
#include "qraw/render/resolved_style.h"

#include <functional>
#include <string_view>
#include <vector>

namespace qraw::render {

// Paints a prepared marker on the canvas it was created for.
using MarkerPainter = std::function<void()>;

// mxMarker.createMarker(canvas, shape, type, pe, unitX, unitY, size,
// source, sw, filled), same contract:
//  - `end` (pe) is the terminal point of the line; it is moved back in
//    place to where the line must stop so it does not poke through the
//    marker. The caller paints the line *after* this call, then the marker.
//  - (unitX, unitY) is the unit vector from the previous point towards `end`.
//  - The returned painter issues begin/moveTo/.../fill/stroke calls on
//    `canvas` with the canvas state the caller set (the caller sets the
//    fill color to the edge stroke color first, as
//    mxConnector.paintEdgeShape does; startFillColor/endFillColor override).
//  - `style` is the edge style (some ER markers read strokeColor).
//  - Unknown types and "none"/"" return an empty function and leave `end`
//    untouched.
// Supported: classic, classicThin, block, blockThin, open, openThin, oval,
// diamond, diamondThin, dash, cross, circle, circlePlus, halfCircle, async,
// openAsync, box, doubleBlock, baseDash, manyOptional, ERone, ERmandOne,
// ERmany, ERoneToMany, ERzeroToMany, ERzeroToOne.
[[nodiscard]] MarkerPainter createMarker(Canvas& canvas, const ResolvedStyle& style, std::string_view type,
                                         Point& end, double unitX, double unitY, double size, bool source,
                                         double strokeWidth, bool filled);

[[nodiscard]] bool isKnownMarker(std::string_view type);
// All supported marker names, sorted.
[[nodiscard]] std::vector<std::string_view> markerNames();

} // namespace qraw::render
