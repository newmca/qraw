#pragma once

// The colors inside an HTML label for a color scheme (Graph.addLightDarkColors).

#include "qraw/render/stylesheet.h"

#include <string>
#include <string_view>

namespace qraw::render {

// Re-colors the `color` and `background-color` declarations and the font
// `color` attribute of sanitized label HTML (RichText::content): a
// light-dark(a, b) value becomes the side that `theme` shows, and in a dark
// Auto theme every other color is inverted (Theme::adapt). The markup is
// otherwise unchanged; the result is `content` itself when nothing changes.
[[nodiscard]] std::string adaptLabelColors(std::string_view content, const Theme& theme);

} // namespace qraw::render
