#pragma once
// Widgets and helpers shared by the Format panel tabs (draw.io's Format.js
// controls: color fields, mixed-aware spin boxes and check boxes). Mixed values are shown without writing: callers set the state
// under a QSignalBlocker (see FormatTab::refresh).

#include "qraw/ui/measure_units.h"

#include <QList>
#include <QWidget>

#include <QAbstractButton>
#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QSpinBox>

#include <functional>
#include <memory>
#include <optional>
#include <string>

class QAction;
class QFormLayout;
class QGroupBox;
class QPushButton;
class QVBoxLayout;

namespace qraw::ui {

class DocumentSession;

// A string of the Format panel (context "qraw::ui::FormatPanel").
[[nodiscard]] QString formatTr(const char* text);

// A color control: swatch button (opens QColorDialog), a text field and a
// "None" check box. Mixed values show a hatched swatch, an empty text field
// and an indeterminate None box. Emits `edited` only for user changes.
class ColorField : public QWidget {
    Q_OBJECT

public:
    explicit ColorField(QWidget* parent = nullptr);
    ~ColorField() override;

    // What the color is for ("Fill Color"): the accessible names of the swatch button, the text field and
    // the None box, which have no label of their own that a screen reader could read.
    void setPurpose(const QString& what);

    // nullopt, "" and "none" all mean no color. Does not emit `edited`.
    void setState(const std::optional<std::string>& value, bool mixed);

signals:
    // The chosen color, "none" or the text the user typed.
    void edited(std::string value);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

// --- spin boxes ---------------------------------------------------------------
// The minimum of these boxes is one below the real minimum: that value is the
// "mixed" sentinel, shown as blank text. onSpinEdited ignores it.
QSpinBox* createIntSpin(QWidget* parent, int minimum, int maximum, int step = 1);
QDoubleSpinBox* createDoubleSpin(QWidget* parent, double minimum, double maximum, int decimals = 2, double step = 1);
// nullopt shows the mixed sentinel. Does not emit valueChanged.
void setSpinState(QSpinBox* spin, std::optional<double> value);
void setSpinState(QDoubleSpinBox* spin, std::optional<double> value);

// Runs `write(value)` for every user change of the spin box (not the mixed
// sentinel). Changes share the merge key that `write` uses; the merge ends on
// editingFinished (Enter or focus loss).
void onSpinEdited(QSpinBox* spin, DocumentSession& session, std::function<void(double)> write);
void onSpinEdited(QDoubleSpinBox* spin, DocumentSession& session, std::function<void(double)> write);

// --- length fields -------------------------------------------------------------
// A spin box for a length in the model's pixels that shows and accepts the unit
// chosen in View > Units (measure_units.h): range, decimals, step and suffix
// follow the unit, and the box converts again when the unit changes. Mixed
// values work as in the other spin boxes (blank; the sentinel is one unit
// below the real minimum).
class LengthSpinBox : public QDoubleSpinBox {
    Q_OBJECT

public:
    LengthSpinBox(double minimumPixels, double maximumPixels, QWidget* parent = nullptr);

    // Typed values are not rounded to 0.1 px (the grid size, which has to be a
    // whole multiple of the unit).
    void setExact(bool exact);
    // Shows `pixels`; nullopt shows the mixed sentinel. Does not emit
    // valueChanged. A box being typed in keeps its text when the new value is
    // what the text means already, so that a refresh does not rewrite it.
    void setPixels(std::optional<double> pixels);
    // The value in pixels; nullopt while the box shows the mixed sentinel.
    [[nodiscard]] std::optional<double> pixels() const;

private:
    void applyUnit();

    double minimumPixels_;
    double maximumPixels_;
    bool exact_ = false;
    MeasureUnit unit_;
    std::optional<double> shown_; // what setPixels was given
};

// Runs `write(pixels)` for every user change of the box (not the mixed
// sentinel), merging like onSpinEdited.
void onLengthEdited(LengthSpinBox* spin, DocumentSession& session, std::function<void(double)> write);

// --- check boxes, buttons, combo boxes ----------------------------------------
// nullopt shows the indeterminate state (the selection disagrees).
void setCheckState(QCheckBox* box, std::optional<bool> on);
// Runs `write(checked)` on user clicks only.
void onClicked(QAbstractButton* button, std::function<void(bool)> write);
// Runs `write(index)` on user choices only.
void onActivated(QComboBox* combo, std::function<void(int)> write);

// A button that triggers `action` and follows its enabled state, text, tooltip and
// shortcut (the button rows of Format.js addActions: Copy Style, Edit Data, ...).
QPushButton* createActionButton(QAction* action, QWidget* parent);
// The buttons of `actions`, two to a row (a trailing odd one takes the whole row).
QWidget* createActionButtonGrid(const QList<QAction*>& actions, const QString& objectName, QWidget* parent = nullptr);

// --- layout ----------------------------------------------------------------
// Adds a titled group with a form layout to `parent`; `group` receives the box.
QFormLayout* addFormGroup(QVBoxLayout* parent, const QString& title, QGroupBox** group = nullptr);

} // namespace qraw::ui
