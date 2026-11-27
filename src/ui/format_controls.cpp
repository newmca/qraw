// Format panel widgets (see format_controls.h).
#include "format_controls.h"

#include "qraw/ui/document_session.h"

#include <QColor>
#include <QColorDialog>
#include <QAction>
#include <QCoreApplication>
#include <QFormLayout>
#include <QGridLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QLineEdit>
#include <QPaintEvent>
#include <QPainter>
#include <QPushButton>
#include <QSignalBlocker>
#include <QToolButton>
#include <QVBoxLayout>

#include <cmath>

namespace qraw::ui {

QString formatTr(const char* text)
{
    return QCoreApplication::translate("qraw::ui::FormatPanel", text);
}

namespace {

// The color swatch: a filled square, a red slash for "none", a hatch for
// "mixed". A QToolButton with its own paint; it declares no signals.
class SwatchButton : public QToolButton {
public:
    enum class State { Color, None, Mixed };

    explicit SwatchButton(QWidget* parent) : QToolButton(parent)
    {
        setToolTip(formatTr("Choose color"));
        setFixedSize(24, 24);
    }

    void setState(State state, QColor color)
    {
        state_ = state;
        color_ = color;
        update();
    }

protected:
    void paintEvent(QPaintEvent* event) override
    {
        QToolButton::paintEvent(event);
        QPainter painter(this);
        const QRect area = rect().adjusted(4, 4, -5, -5);
        switch (state_) {
        case State::Color:
            painter.fillRect(area, color_);
            break;
        case State::None:
            painter.fillRect(area, Qt::white);
            painter.setPen(QPen(Qt::red, 1));
            painter.drawLine(area.topLeft(), area.bottomRight());
            break;
        case State::Mixed:
            painter.fillRect(area, Qt::white);
            painter.fillRect(area, QBrush(Qt::gray, Qt::BDiagPattern));
            break;
        }
        painter.setPen(palette().color(QPalette::Mid));
        painter.drawRect(area);
    }

private:
    State state_ = State::None;
    QColor color_;
};

} // namespace

struct ColorField::Impl {
    SwatchButton* swatch = nullptr;
    QLineEdit* text = nullptr;
    QCheckBox* none = nullptr;
    std::optional<std::string> current; // the color shown; nullopt when none or mixed
    std::string lastColor = "#000000";  // restored when "None" is unticked
    bool mixed = false;
};

ColorField::ColorField(QWidget* parent) : QWidget(parent), d(std::make_unique<Impl>())
{
    auto* layout = new QHBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);

    d->swatch = new SwatchButton(this);
    d->text = new QLineEdit(this);
    d->text->setMaxLength(64);
    d->none = new QCheckBox(formatTr("None"), this);
    layout->addWidget(d->swatch);
    layout->addWidget(d->text, 1);
    layout->addWidget(d->none);

    connect(d->swatch, &QToolButton::clicked, this, [this] {
        const QColor initial = d->current ? QColor(QString::fromStdString(*d->current)) : QColor();
        const QColor chosen = QColorDialog::getColor(initial.isValid() ? initial : QColor(Qt::white), this,
                                                     formatTr("Choose Color"));
        if (chosen.isValid())
            emit edited(chosen.name(QColor::HexRgb).toStdString());
    });
    connect(d->text, &QLineEdit::editingFinished, this, [this] {
        const QString typed = d->text->text().trimmed();
        const bool unchanged = d->current && QString::fromStdString(*d->current) == typed;
        if (typed.isEmpty() || d->mixed || unchanged || !QColor::isValidColorName(typed)) {
            setState(d->current, d->mixed); // nothing to write: show the value again
            return;
        }
        emit edited(typed.toStdString());
    });
    connect(d->none, &QCheckBox::clicked, this, [this](bool checked) {
        emit edited(checked ? std::string("none") : d->lastColor);
    });
}

ColorField::~ColorField() = default;

void ColorField::setPurpose(const QString& what)
{
    d->swatch->setAccessibleName(formatTr("Choose %1").arg(what));
    d->text->setAccessibleName(what);
    d->none->setAccessibleName(formatTr("%1: None").arg(what));
}

void ColorField::setState(const std::optional<std::string>& value, bool mixed)
{
    QSignalBlocker textBlocker(d->text);
    QSignalBlocker noneBlocker(d->none);
    d->mixed = mixed;
    d->current.reset();

    if (mixed) {
        d->none->setCheckState(Qt::PartiallyChecked);
        d->text->clear();
        d->text->setPlaceholderText(formatTr("Mixed"));
        d->swatch->setState(SwatchButton::State::Mixed, {});
        return;
    }

    const bool none = !value || value->empty() || *value == "none";
    if (none) {
        d->none->setCheckState(Qt::Checked);
        d->text->clear();
        d->text->setPlaceholderText(formatTr("None"));
        d->swatch->setState(SwatchButton::State::None, {});
        return;
    }

    const QColor color(QString::fromStdString(*value));
    d->current = value;
    d->lastColor = *value;
    d->none->setCheckState(Qt::Unchecked);
    d->text->setText(QString::fromStdString(*value));
    d->text->setPlaceholderText({});
    d->swatch->setState(color.isValid() ? SwatchButton::State::Color : SwatchButton::State::None, color);
}

// --- spin boxes --------------------------------------------------------------

namespace {

// Qt shows the number instead of the special text when that text is empty,
// so the mixed sentinel displays as one space.
const QString kMixedText = QStringLiteral(" ");

} // namespace

QSpinBox* createIntSpin(QWidget* parent, int minimum, int maximum, int step)
{
    auto* spin = new QSpinBox(parent);
    spin->setRange(minimum - 1, maximum);
    spin->setSpecialValueText(kMixedText);
    spin->setSingleStep(step);
    return spin;
}

QDoubleSpinBox* createDoubleSpin(QWidget* parent, double minimum, double maximum, int decimals, double step)
{
    auto* spin = new QDoubleSpinBox(parent);
    spin->setDecimals(decimals);
    spin->setRange(minimum - 1, maximum);
    spin->setSpecialValueText(kMixedText);
    spin->setSingleStep(step);
    return spin;
}

void setSpinState(QSpinBox* spin, std::optional<double> value)
{
    QSignalBlocker blocker(spin);
    spin->setValue(value ? static_cast<int>(std::lround(*value)) : spin->minimum());
}

void setSpinState(QDoubleSpinBox* spin, std::optional<double> value)
{
    QSignalBlocker blocker(spin);
    spin->setValue(value ? *value : spin->minimum());
}

void onSpinEdited(QSpinBox* spin, DocumentSession& session, std::function<void(double)> write)
{
    QObject::connect(spin, &QSpinBox::valueChanged, spin, [spin, write](int value) {
        if (value != spin->minimum())
            write(value);
    });
    QObject::connect(spin, &QAbstractSpinBox::editingFinished, spin, [&session] { session.endMerge(); });
}

void onSpinEdited(QDoubleSpinBox* spin, DocumentSession& session, std::function<void(double)> write)
{
    QObject::connect(spin, &QDoubleSpinBox::valueChanged, spin, [spin, write](double value) {
        if (value != spin->minimum())
            write(value);
    });
    QObject::connect(spin, &QAbstractSpinBox::editingFinished, spin, [&session] { session.endMerge(); });
}

// --- length fields ----------------------------------------------------------

LengthSpinBox::LengthSpinBox(double minimumPixels, double maximumPixels, QWidget* parent)
    : QDoubleSpinBox(parent),
      minimumPixels_(minimumPixels),
      maximumPixels_(maximumPixels),
      unit_(MeasureUnits::instance().unit())
{
    setSpecialValueText(kMixedText);
    applyUnit();
    connect(&MeasureUnits::instance(), &MeasureUnits::unitChanged, this, [this](MeasureUnit unit) {
        unit_ = unit;
        applyUnit();
    });
}

void LengthSpinBox::setExact(bool exact)
{
    exact_ = exact;
}

// Range, precision, step and suffix of the unit, and the shown value converted.
void LengthSpinBox::applyUnit()
{
    QSignalBlocker blocker(this);
    const double low = units::toUnit(minimumPixels_, unit_);
    setDecimals(units::decimals(unit_));
    setRange(low - 1, units::toUnit(maximumPixels_, unit_));
    setSingleStep(units::step(unit_));
    setSuffix(units::suffix(unit_));
    setValue(shown_ ? units::toUnit(*shown_, unit_) : minimum());
}

void LengthSpinBox::setPixels(std::optional<double> pixels)
{
    const std::optional<double> current = this->pixels();
    shown_ = pixels;
    if (hasFocus() && pixels && current && std::abs(*current - *pixels) < 1e-9)
        return;
    QSignalBlocker blocker(this);
    setValue(pixels ? units::toUnit(*pixels, unit_) : minimum());
}

std::optional<double> LengthSpinBox::pixels() const
{
    if (value() <= minimum())
        return std::nullopt;
    return units::fromUnit(value(), unit_, exact_);
}

void onLengthEdited(LengthSpinBox* spin, DocumentSession& session, std::function<void(double)> write)
{
    QObject::connect(spin, &QDoubleSpinBox::valueChanged, spin, [spin, write] {
        if (const std::optional<double> pixels = spin->pixels())
            write(*pixels);
    });
    QObject::connect(spin, &QAbstractSpinBox::editingFinished, spin, [&session] { session.endMerge(); });
}

// --- check boxes, buttons, combo boxes ---------------------------------------

void setCheckState(QCheckBox* box, std::optional<bool> on)
{
    QSignalBlocker blocker(box);
    if (!on)
        box->setCheckState(Qt::PartiallyChecked);
    else
        box->setCheckState(*on ? Qt::Checked : Qt::Unchecked);
}

void onClicked(QAbstractButton* button, std::function<void(bool)> write)
{
    QObject::connect(button, &QAbstractButton::clicked, button, [write](bool checked) { write(checked); });
}

void onActivated(QComboBox* combo, std::function<void(int)> write)
{
    QObject::connect(combo, &QComboBox::activated, combo, [write](int index) { write(index); });
}

QFormLayout* addFormGroup(QVBoxLayout* parent, const QString& title, QGroupBox** group)
{
    auto* box = new QGroupBox(title);
    auto* form = new QFormLayout(box);
    form->setFieldGrowthPolicy(QFormLayout::AllNonFixedFieldsGrow);
    parent->addWidget(box);
    if (group != nullptr)
        *group = box;
    return form;
}

QPushButton* createActionButton(QAction* action, QWidget* parent)
{
    auto* button = new QPushButton(parent);
    button->setObjectName(QStringLiteral("format.%1").arg(action->objectName()));
    const auto sync = [button, action] {
        button->setText(action->iconText());
        button->setEnabled(action->isEnabled());
        button->setToolTip(action->shortcut().isEmpty()
                               ? action->iconText()
                               : QStringLiteral("%1 (%2)").arg(action->iconText(),
                                                               action->shortcut().toString(QKeySequence::NativeText)));
    };
    sync();
    QObject::connect(action, &QAction::changed, button, sync);
    QObject::connect(button, &QPushButton::clicked, action, &QAction::trigger);
    return button;
}

QWidget* createActionButtonGrid(const QList<QAction*>& actions, const QString& objectName, QWidget* parent)
{
    auto* row = new QWidget(parent);
    row->setObjectName(objectName);
    auto* grid = new QGridLayout(row);
    grid->setContentsMargins(0, 0, 0, 0);
    int index = 0;
    for (QAction* action : actions) {
        const bool last = index == actions.size() - 1;
        grid->addWidget(createActionButton(action, row), index / 2, index % 2, 1, last && index % 2 == 0 ? 2 : 1);
        ++index;
    }
    return row;
}

} // namespace qraw::ui
