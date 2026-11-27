#include "qraw/ui/edit_geometry_dialog.h"

#include "format_controls.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>
#include <functional>
#include <optional>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::EditGeometryDialog)
};

// The value every bound shares, or nullopt when they differ (the mixed sentinel).
std::optional<double> commonValue(const std::vector<core::Rect>& bounds,
                                  const std::function<double(const core::Rect&)>& pick)
{
    const double first = pick(bounds.front());
    const bool same = std::all_of(bounds.begin(), bounds.end(),
                                  [&](const core::Rect& r) { return pick(r) == first; });
    return same ? std::optional<double>(first) : std::nullopt;
}

} // namespace

EditGeometryDialog::EditGeometryDialog(const std::vector<core::Rect>& bounds, QWidget* parent) : QDialog(parent)
{
    setWindowTitle(Text::tr("Edit Geometry"));
    auto* form = new QFormLayout;

    // The panel's precision and ranges, in the unit in use.
    const auto addField = [&](Field& field, const char* name, const QString& label, double minimum,
                              std::function<double(const core::Rect&)> pick) {
        field.spin = new LengthSpinBox(minimum, 100000, this);
        field.spin->setObjectName(QString::fromLatin1(name));
        field.spin->setPixels(commonValue(bounds, pick));
        QObject::connect(field.spin, &QDoubleSpinBox::valueChanged, this, [&field] { field.changed = true; });
        form->addRow(label, field.spin);
    };
    addField(x_, "geometry.left", Text::tr("Left"), -100000, [](const core::Rect& r) { return r.x; });
    addField(y_, "geometry.top", Text::tr("Top"), -100000, [](const core::Rect& r) { return r.y; });
    addField(width_, "geometry.width", Text::tr("Width"), 1, [](const core::Rect& r) { return r.width; });
    addField(height_, "geometry.height", Text::tr("Height"), 1, [](const core::Rect& r) { return r.height; });

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addLayout(form);
    layout->addWidget(buttons);
    x_.spin->setFocus();
}

actions::GeometryEdit EditGeometryDialog::edit() const
{
    const auto value = [](const Field& field) -> std::optional<double> {
        return field.changed ? field.spin->pixels() : std::nullopt;
    };
    return {value(x_), value(y_), value(width_), value(height_)};
}

} // namespace qraw::ui
