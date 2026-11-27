#pragma once
// Autosize of a vertex to its label (mxGraph.cellLabelChanged with autoSize ->
// cellSizeUpdated / getPreferredSizeForCell, with draw.io's
// Graph.getPreferredSizeForCell buffer). Used by the label editor and by the
// Autosize action (edit_actions.h).

#include "qraw/core/geometry.h"
#include "qraw/render/resolved_style.h"

#include <optional>
#include <string_view>

namespace qraw::ui::label {

struct GridSnap {
    bool enabled = false;
    double size = 10;
};

// True for style autosize=1 (mxGraph.isAutoSizeCell).
[[nodiscard]] bool isAutoSize(const render::ResolvedStyle& style);

// The bounds a vertex with `style` and `bounds` gets when its label becomes
// `value`, or nullopt when nothing changes (not autosize, a swimlane, a
// relative geometry, or the same bounds). Only the autosize=1 rule applies.
//
//  * Preferred size: the text measured with render::measureText (the layout
//    the raster canvas draws) plus 2 * spacing + spacingLeft/Right (default
//    2 each) horizontally and 2 * spacing + spacingTop/Bottom vertically,
//    plus draw.io's buffer of 10 x 4 px; snapped to the grid first
//    (with the half grid step upstream adds) and again after the buffer.
//    Empty labels give 4 * gridSize square.
//  * The cell keeps its centre (valign=middle / align=center, as upstream),
//    or its bottom / right edge for valign=bottom / align=right.
//  * whiteSpace=wrap keeps the width and wraps the text at it; style
//    resizeWidth=0 / resizeHeight=0 keep that dimension.
//  * Not ported: swimlane start size, growing to hold the children
//    (KNOWN_ISSUES.md).
[[nodiscard]] std::optional<core::Rect> autosizedBounds(const render::ResolvedStyle& style, const core::Rect& bounds,
                                                        std::string_view value, const GridSnap& grid);

// autosizedBounds without the autosize=1 gate: the bounds that fit `value`
// whatever the style says (Graph.updateCellSize, used by the Autosize action).
// Swimlanes are the caller's to skip.
[[nodiscard]] std::optional<core::Rect> preferredBounds(const render::ResolvedStyle& style, const core::Rect& bounds,
                                                        std::string_view value, const GridSnap& grid);

} // namespace qraw::ui::label
