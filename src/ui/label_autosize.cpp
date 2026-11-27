// Port of mxgraph/src/view/mxGraph.js cellSizeUpdated and
// getPreferredSizeForCell, and js/grapheditor/Graph.js
// Graph.prototype.getPreferredSizeForCell (the +10 / +4 buffer).
#include "qraw/ui/label_autosize.h"

#include "qraw/render/text_metrics.h"

#include <algorithm>
#include <cmath>
#include <utility>

namespace qraw::ui::label {

namespace {

double snap(double value, const GridSnap& grid)
{
    return grid.enabled && grid.size > 0 ? std::round(value / grid.size) * grid.size : value;
}

// mxGraph.js:5864: `a == 1` also holds for "1.0"; style flags use the same rule.
bool resizes(const render::ResolvedStyle& style, std::string_view key)
{
    return style.number(key, 1) != 0;
}

} // namespace

bool isAutoSize(const render::ResolvedStyle& style)
{
    return style.number("autosize", 0) == 1;
}

std::optional<core::Rect> autosizedBounds(const render::ResolvedStyle& style, const core::Rect& bounds,
                                          std::string_view value, const GridSnap& grid)
{
    if (!isAutoSize(style) || style.shape() == "swimlane")
        return std::nullopt;
    return preferredBounds(style, bounds, value, grid);
}

std::optional<core::Rect> preferredBounds(const render::ResolvedStyle& style, const core::Rect& bounds,
                                          std::string_view value, const GridSnap& grid)
{
    const bool wrap = style.wrap();
    const bool keepWidth = wrap || !resizes(style, "resizeWidth");
    const bool keepHeight = !resizes(style, "resizeHeight");
    const bool gridOn = style.string("autosizeGrid", grid.enabled ? "1" : "0") == "1";
    const GridSnap snapping{gridOn, grid.size};

    // getPreferredSizeForCell: spacings use upstream's default of 2 on every side.
    const double spacing = style.number("spacing", 2);
    const double dx = 2 * spacing + style.number("spacingLeft", 2) + style.number("spacingRight", 2);
    const double dy = 2 * spacing + style.number("spacingTop", 2) + style.number("spacingBottom", 2);

    double width = 0;
    double height = 0;
    if (value.empty()) {
        width = height = 4 * grid.size;
    } else {
        std::optional<double> wrapWidth;
        if (wrap)
            wrapWidth = std::max(1.0, bounds.width - (2 * spacing + style.number("spacingLeft", 0) +
                                                       style.number("spacingRight", 0)));
        const QSizeF text = render::measureText(render::textBlockForValue(style, value), wrapWidth);
        width = text.width() + dx;
        height = text.height() + dy;
        if (!style.horizontal())
            std::swap(width, height);
        if (snapping.enabled) {
            width = snap(width + snapping.size / 2, snapping);
            height = snap(height + snapping.size / 2, snapping);
        }
        // Graph.getPreferredSizeForCell buffer.
        width += 10;
        height += 4;
        if (snapping.enabled) {
            width = snap(width, snapping);
            height = snap(height, snapping);
        }
    }
    if (style.string("aspect") == "fixed" && bounds.width > 0)
        height = std::round(bounds.height * width * 100 / bounds.width) / 100;

    core::Rect result = bounds;
    if (!keepHeight) {
        const auto valign = style.labelParams().verticalAlign;
        if (valign == geometry::VAlign::Bottom)
            result.y += bounds.height - height;
        else if (valign == geometry::VAlign::Middle)
            result.y += std::round((bounds.height - height) / 2);
        result.height = height;
    }
    if (!keepWidth) {
        const auto align = style.labelParams().align;
        if (align == geometry::HAlign::Right)
            result.x += bounds.width - width;
        else if (align == geometry::HAlign::Center)
            result.x += std::round((bounds.width - width) / 2);
        result.width = width;
    }
    if (result == bounds)
        return std::nullopt;
    return result;
}

} // namespace qraw::ui::label
