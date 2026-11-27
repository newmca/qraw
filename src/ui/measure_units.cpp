#include "qraw/ui/measure_units.h"

#include "qraw/ui/app_settings.h"

#include <QCoreApplication>

#include <cmath>

namespace qraw::ui {

namespace units {

namespace {

// Round to `decimals` decimal places like Math.round(x * 10^d) / 10^d.
double roundTo(double value, int decimals)
{
    const double scale = std::pow(10.0, decimals);
    return std::round(value * scale) / scale;
}

// Pixels in one unit (Editor.fromUnit).
double pixelsPer(MeasureUnit unit)
{
    switch (unit) {
    case MeasureUnit::Points:
        return 1;
    case MeasureUnit::Inches:
        return kPixelsPerInch;
    case MeasureUnit::Millimeters:
        return kPixelsPerMm;
    case MeasureUnit::Centimeters:
        return kPixelsPerMm * 10;
    case MeasureUnit::Meters:
        return kPixelsPerMm * 1000;
    }
    return 1;
}

} // namespace

double toUnit(double pixels, MeasureUnit unit)
{
    if (unit == MeasureUnit::Points)
        return pixels;
    return roundTo(pixels / pixelsPer(unit), decimals(unit));
}

double fromUnit(double value, MeasureUnit unit, bool exact)
{
    const double pixels = value * pixelsPer(unit);
    return exact || unit == MeasureUnit::Points ? pixels : roundTo(pixels, 1);
}

int decimals(MeasureUnit unit)
{
    switch (unit) {
    case MeasureUnit::Points:
        return 2;
    case MeasureUnit::Inches:
    case MeasureUnit::Millimeters:
        return 3;
    case MeasureUnit::Centimeters:
        return 4;
    case MeasureUnit::Meters:
        return 5;
    }
    return 2;
}

double step(MeasureUnit unit)
{
    switch (unit) {
    case MeasureUnit::Points:
        return 1;
    case MeasureUnit::Inches:
    case MeasureUnit::Centimeters:
        return 0.1;
    case MeasureUnit::Millimeters:
        return 0.5;
    case MeasureUnit::Meters:
        return 0.001;
    }
    return 1;
}

QString suffix(MeasureUnit unit)
{
    switch (unit) {
    case MeasureUnit::Points:
        return QStringLiteral(" pt");
    case MeasureUnit::Inches:
        return QStringLiteral(" in");
    case MeasureUnit::Millimeters:
        return QStringLiteral(" mm");
    case MeasureUnit::Centimeters:
        return QStringLiteral(" cm");
    case MeasureUnit::Meters:
        return QStringLiteral(" m");
    }
    return {};
}

QString label(MeasureUnit unit)
{
    constexpr const char* context = "qraw::ui::MeasureUnits";
    switch (unit) {
    case MeasureUnit::Points:
        return QCoreApplication::translate(context, "Points");
    case MeasureUnit::Inches:
        return QCoreApplication::translate(context, "Inches");
    case MeasureUnit::Millimeters:
        return QCoreApplication::translate(context, "Millimeters");
    case MeasureUnit::Centimeters:
        return QCoreApplication::translate(context, "Centimeters");
    case MeasureUnit::Meters:
        return QCoreApplication::translate(context, "Meters");
    }
    return {};
}

QString key(MeasureUnit unit)
{
    switch (unit) {
    case MeasureUnit::Points:
        return QStringLiteral("points");
    case MeasureUnit::Inches:
        return QStringLiteral("inches");
    case MeasureUnit::Millimeters:
        return QStringLiteral("millimeters");
    case MeasureUnit::Centimeters:
        return QStringLiteral("centimeters");
    case MeasureUnit::Meters:
        return QStringLiteral("meters");
    }
    return {};
}

std::optional<MeasureUnit> fromKey(const QString& name)
{
    for (const MeasureUnit unit : kAllMeasureUnits) {
        if (key(unit) == name)
            return unit;
    }
    return std::nullopt;
}

} // namespace units

MeasureUnits& MeasureUnits::instance()
{
    static MeasureUnits units;
    return units;
}

MeasureUnit MeasureUnits::unit() const
{
    return AppSettings().measureUnit();
}

void MeasureUnits::setUnit(MeasureUnit unit)
{
    if (AppSettings().measureUnit() == unit)
        return;
    AppSettings().setMeasureUnit(unit);
    emit unitChanged(unit);
}

} // namespace qraw::ui
