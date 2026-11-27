#pragma once

// mxgraph.basic.rect (mxShapeBasicRect2) as other painters use it: the BPMN
// tasks draw their frame with strictDrawShape and style overrides. Private
// to the shape painters.

#include "shapes_common.h"

#include <optional>
#include <string_view>

namespace qraw::render::detail {

// The `os` argument of mxShapeBasicRect2.strictDrawShape: a set value takes
// the place of the style's.
struct Rect2Overrides {
    std::optional<std::string_view> rectOutline;
    std::optional<double> indent;
    std::optional<bool> dashed;
    std::optional<double> strokeWidth;
};

// mxShapeBasicRect2.strictDrawShape(c, 0, 0, w, h, os): draws at the canvas
// origin (the caller translates), reading rectStyle, size, indent, the
// per-corner styles, side flags and the inner fill from `style`.
void strictDrawRect2(Canvas& c, const ResolvedStyle& style, double w, double h, const Rect2Overrides& os = {});

} // namespace qraw::render::detail
