#pragma once
// Measurement units of the length fields (draw.io's graph.view.unit and
// Editor.toUnit / Editor.fromUnit, js/grapheditor/Editor.js:1496-1545). The
// model always stores pixels; these functions are the only place that
// converts, and every length field (Arrange tab, Edit Geometry, page size,
// grid size) goes through them via LengthSpinBox (format_controls.h).
//
// The unit is a user preference, as in draw.io (mxSettings.setUnit): it is
// kept in AppSettings, not in the file.

#include <QObject>
#include <QString>

#include <optional>

namespace qraw::ui {

// draw.io's mxConstants.POINTS, INCHES, MILLIMETERS, CENTIMETERS, METERS. A
// point is one model pixel; the other units use mxConstants.PIXELS_PER_MM
// (3.937) and PIXELS_PER_INCH (100).
enum class MeasureUnit { Points, Inches, Millimeters, Centimeters, Meters };

inline constexpr MeasureUnit kAllMeasureUnits[] = {MeasureUnit::Points, MeasureUnit::Inches, MeasureUnit::Millimeters,
                                                   MeasureUnit::Centimeters, MeasureUnit::Meters};

namespace units {

inline constexpr double kPixelsPerMm = 3.937;
inline constexpr double kPixelsPerInch = 100;

// Pixels to the unit, rounded to draw.io's precision (inches and millimeters 3
// decimals, centimeters 4, meters 5). Points are not rounded: the fields show
// two decimals of the pixel value, as before units existed.
[[nodiscard]] double toUnit(double pixels, MeasureUnit unit);
// The unit to pixels. Rounded to 0.1 px like draw.io, so that a value typed in
// one unit reads back the same; `exact` skips the rounding (the grid size, which
// has to be a whole multiple of the unit). Points are never rounded.
[[nodiscard]] double fromUnit(double value, MeasureUnit unit, bool exact = false);

// Decimals a field shows (the precision of toUnit), and the step of its arrows
// (BaseFormatPanel.getUnitStep).
[[nodiscard]] int decimals(MeasureUnit unit);
[[nodiscard]] double step(MeasureUnit unit);

// " pt", " in", " mm", " cm", " m": the suffix of a field.
[[nodiscard]] QString suffix(MeasureUnit unit);
// "Points", "Inches", ... for menus and combo boxes (translated).
[[nodiscard]] QString label(MeasureUnit unit);
// The name stored in the settings: "points", "inches", ...
[[nodiscard]] QString key(MeasureUnit unit);
[[nodiscard]] std::optional<MeasureUnit> fromKey(const QString& key);

} // namespace units

// The unit in use. Reads and writes AppSettings; announces a change so that the
// open fields convert. One per process (the preference is global).
class MeasureUnits : public QObject {
    Q_OBJECT

public:
    [[nodiscard]] static MeasureUnits& instance();

    [[nodiscard]] MeasureUnit unit() const;
    void setUnit(MeasureUnit unit);

signals:
    void unitChanged(qraw::ui::MeasureUnit unit);

private:
    MeasureUnits() = default;
};

} // namespace qraw::ui
