#pragma once

#include <QColor>

#include <optional>
#include <string>
#include <string_view>

namespace qraw::render {

// The per-diagram dark mode setting (mxGraphModel@adaptiveColors, Graph.js
// getAdaptiveColors). In a dark scheme: Auto inverts every explicit color,
// Simple keeps them and adapts only the default colors and light-dark()
// values, None keeps the diagram in the light scheme. An absent attribute
// means Auto (Graph.defaultAdaptiveColors).
enum class AdaptiveColors { Auto, Simple, None };

// "auto", "simple", "none"; anything else (including "default") yields nullopt.
[[nodiscard]] std::optional<AdaptiveColors> parseAdaptiveColors(std::string_view value);
[[nodiscard]] std::string_view adaptiveColorsName(AdaptiveColors value);

// mxUtils.getInverseColor: the dark-scheme counterpart of a light color. The
// RGB values are inverted by `percentage` (93: not fully, so a light gray
// stays visibly gray) and the hue is rotated by `hueRotation` degrees
// (180: a red becomes a cyan-ish dark red), alpha is kept. "transparent" and
// invalid colors are returned unchanged.
[[nodiscard]] QColor inverseColor(const QColor& color, double percentage = 93, double hueRotation = 180);

// "light-dark(a, b)" with two non-empty arguments picks a (light) or b (dark);
// a comma inside parentheses, as in rgb(0, 0, 0), does not separate them.
// Anything else (including a single argument) yields nullopt.
[[nodiscard]] std::optional<std::string> pickLightDark(std::string_view value, bool dark);

// The style text of a color: "#rrggbb", or "#rrggbbaa" when not opaque (draw.io's order).
[[nodiscard]] std::string styleColorText(const QColor& color);

// Parses a draw.io style color value: "#rgb", "#rrggbb", "#rrggbbaa", CSS
// color names and "rgb(...)" forms understood by QColor, and the light color of
// "light-dark(a, b)". "none" and an empty
// value yield a transparent color; "default" and unparsable values yield
// `fallback`.
[[nodiscard]] QColor parseStyleColor(std::string_view value, const QColor& fallback);

} // namespace qraw::render
