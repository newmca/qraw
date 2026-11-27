#include "qraw/render/color.h"

#include <QString>

#include <algorithm>
#include <cmath>
#include <numbers>

namespace qraw::render {

QColor parseStyleColor(std::string_view value, const QColor& fallback)
{
    if (value.empty() || value == "none")
        return QColor(Qt::transparent);
    if (value == "default")
        return fallback;
    // A light-dark() pair that reaches here was not resolved for a scheme (style resolution does
    // that): the light color is the one a viewer without a dark scheme sees.
    if (const auto light = pickLightDark(value, false))
        return parseStyleColor(*light, fallback);
    const QString text = QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())).trimmed();
    // draw.io writes 8-digit hex as #rrggbbaa; QColor reads #aarrggbb.
    if (text.size() == 9 && text.startsWith(u'#')) {
        bool ok = false;
        const uint rgba = text.mid(1).toUInt(&ok, 16);
        if (ok)
            return QColor(int(rgba >> 24 & 0xFF), int(rgba >> 16 & 0xFF), int(rgba >> 8 & 0xFF), int(rgba & 0xFF));
    }
    const QColor color = QColor::fromString(text);
    return color.isValid() ? color : fallback;
}

namespace {
std::string_view trimmed(std::string_view text)
{
    const auto isSpace = [](char c) { return c == ' ' || c == '\t'; };
    while (!text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return text;
}

} // namespace

std::optional<std::string> pickLightDark(std::string_view value, bool dark)
{
    constexpr std::string_view prefix = "light-dark(";
    if (value.size() <= prefix.size() || value.substr(0, prefix.size()) != prefix || value.back() != ')')
        return std::nullopt;
    const std::string_view args = value.substr(prefix.size(), value.size() - prefix.size() - 1);
    std::size_t comma = std::string_view::npos;
    int depth = 0;
    for (std::size_t i = 0; i < args.size(); ++i) {
        if (args[i] == '(') {
            ++depth;
        } else if (args[i] == ')') {
            --depth;
        } else if (args[i] == ',' && depth == 0) {
            if (comma != std::string_view::npos)
                return std::nullopt;
            comma = i;
        }
    }
    if (comma == std::string_view::npos || depth != 0)
        return std::nullopt;
    const std::string_view light = trimmed(args.substr(0, comma));
    const std::string_view darkValue = trimmed(args.substr(comma + 1));
    if (light.empty() || darkValue.empty())
        return std::nullopt;
    return std::string(dark ? darkValue : light);
}

std::optional<AdaptiveColors> parseAdaptiveColors(std::string_view value)
{
    if (value == "auto")
        return AdaptiveColors::Auto;
    if (value == "simple")
        return AdaptiveColors::Simple;
    if (value == "none")
        return AdaptiveColors::None;
    return std::nullopt;
}

std::string_view adaptiveColorsName(AdaptiveColors value)
{
    switch (value) {
    case AdaptiveColors::Auto:
        return "auto";
    case AdaptiveColors::Simple:
        return "simple";
    case AdaptiveColors::None:
        return "none";
    }
    return "auto";
}

QColor inverseColor(const QColor& color, double percentage, double hueRotation)
{
    if (!color.isValid() || color.alpha() == 0)
        return color;
    const double amount = percentage / 100;
    const auto invert = [amount](int channel) { return std::round((255 - channel) * amount + channel * (1 - amount)); };
    const double r = invert(color.red());
    const double g = invert(color.green());
    const double b = invert(color.blue());

    // The CSS hue-rotate matrix of mxUtils.getInverseColor, with its coefficients.
    constexpr double lumR = 0.2126;
    constexpr double lumG = 0.7152;
    constexpr double lumB = 0.0722;
    constexpr double hueR = 0.143;
    constexpr double hueG = 0.140;
    constexpr double hueB = 0.283;
    const double radians = hueRotation * std::numbers::pi / 180;
    const double c = std::cos(radians);
    const double s = std::sin(radians);
    const double m[9] = {lumR + (1 - lumR) * c - lumR * s, lumG - lumG * c - lumG * s, lumB - lumB * c + (1 - lumB) * s,
                         lumR - lumR * c + hueR * s,       lumG + (1 - lumG) * c + hueG * s, lumB - lumB * c - hueB * s,
                         lumR - lumR * c - (1 - lumR) * s, lumG - lumG * c + lumG * s, lumB + (1 - lumB) * c + lumB * s};
    const auto clamp = [](double v) { return static_cast<int>(std::lround(std::clamp(v, 0.0, 255.0))); };
    QColor result(clamp(m[0] * r + m[1] * g + m[2] * b), clamp(m[3] * r + m[4] * g + m[5] * b),
                  clamp(m[6] * r + m[7] * g + m[8] * b));
    result.setAlpha(color.alpha());
    return result;
}

std::string styleColorText(const QColor& color)
{
    QString text = color.name(QColor::HexRgb);
    if (color.alpha() < 255)
        text += QStringLiteral("%1").arg(color.alpha(), 2, 16, QLatin1Char('0'));
    return text.toStdString();
}

} // namespace qraw::render
