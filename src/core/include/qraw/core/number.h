#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace qraw::core {

// Parses a decimal number as written in draw.io attributes and styles
// ("10", "-2.5", "1e3"). Leading/trailing blanks are ignored; anything else
// that is not part of the number makes the parse fail.
[[nodiscard]] std::optional<double> parseNumber(std::string_view text);
[[nodiscard]] double parseNumber(std::string_view text, double fallback);

// Formats a number the way draw.io writes it: integers without a fraction,
// other values in the shortest fixed notation that round-trips.
[[nodiscard]] std::string formatNumber(double value);

} // namespace qraw::core
