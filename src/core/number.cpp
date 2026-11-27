#include "qraw/core/number.h"

#include <charconv>
#include <cmath>

namespace qraw::core {

namespace {
std::string_view trim(std::string_view s)
{
    while (!s.empty() && (s.front() == ' ' || s.front() == '\t'))
        s.remove_prefix(1);
    while (!s.empty() && (s.back() == ' ' || s.back() == '\t'))
        s.remove_suffix(1);
    return s;
}
} // namespace

std::optional<double> parseNumber(std::string_view text)
{
    text = trim(text);
    if (!text.empty() && text.front() == '+')
        text.remove_prefix(1);
    if (text.empty())
        return std::nullopt;
    double value = 0;
    const auto [ptr, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || ptr != text.data() + text.size() || !std::isfinite(value))
        return std::nullopt;
    return value;
}

double parseNumber(std::string_view text, double fallback)
{
    return parseNumber(text).value_or(fallback);
}

std::string formatNumber(double value)
{
    if (!std::isfinite(value))
        return "0";
    if (value == 0)
        return "0"; // also normalizes -0
    // Plain decimal for everyday magnitudes (shortest form that round-trips).
    // Fixed notation of 1e300 or 1e-300 needs hundreds of characters, so
    // extreme values use the shortest general form ("1e+64"), which
    // parseNumber() and draw.io both read.
    constexpr double kFixedMax = 1e21;
    constexpr double kFixedMin = 1e-6;
    char buffer[64];
    std::to_chars_result result{};
    const double magnitude = std::abs(value);
    if (std::trunc(value) == value && magnitude < 1e15)
        result = std::to_chars(buffer, buffer + sizeof buffer, static_cast<long long>(value));
    else if (magnitude >= kFixedMin && magnitude < kFixedMax)
        result = std::to_chars(buffer, buffer + sizeof buffer, value, std::chars_format::fixed);
    else
        result = std::to_chars(buffer, buffer + sizeof buffer, value);
    if (result.ec != std::errc())
        return "0"; // unreachable with a 64-byte buffer; never return unwritten bytes
    return std::string(buffer, result.ptr);
}

} // namespace qraw::core
