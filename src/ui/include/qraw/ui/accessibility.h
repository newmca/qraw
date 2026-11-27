#pragma once
// Accessibility support that is not tied to one widget (D-050): the canvas's accessible interface, the
// tooltips that keyboard focus shows, and the names the Qt widgets leave empty.

#include <QObject>
#include <QString>

class QWidget;

namespace qraw::ui {

class CanvasView;

// The accessible description of the canvas: "<page name>, N shapes, M selected".
[[nodiscard]] QString canvasAccessibleDescription(const CanvasView& view);

// Registers the factory that gives a CanvasView its accessible interface (role Canvas, name "Diagram",
// the description above) and a filter that names every scroll bar of the application ("Horizontal Scroll
// Bar", "Vertical Scroll Bar": Qt leaves them nameless). Safe to call again; MainWindow and CanvasView
// call it when they are created.
void installAccessibility();

// Tells assistive technology that what the canvas shows changed (the selection, the shapes of the page).
void notifyCanvasChanged(CanvasView& view);

// While a button without a text of its own (icon only) has the keyboard focus its tooltip is shown, so
// that a keyboard user sees what the button does (KDE HIG, "Text and labels"). Installed on the
// application for the lifetime of the object, and only for widgets of `window` and its children.
class FocusToolTips final : public QObject {
    Q_OBJECT

public:
    explicit FocusToolTips(QWidget& window);

    bool eventFilter(QObject* watched, QEvent* event) override;

private:
    QWidget& window_;
};

} // namespace qraw::ui
