#include "qraw/ui/accessibility.h"

#include "qraw/core/page.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/document_session.h"

#include <QAbstractButton>
#include <QAbstractScrollArea>
#include <QAccessible>
#include <QAccessibleWidget>
#include <QApplication>
#include <QCoreApplication>
#include <QEvent>
#include <QFocusEvent>
#include <QPointer>
#include <QScrollBar>
#include <QToolButton>
#include <QToolTip>

#include <algorithm>
#include <mutex>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::Accessibility)
};

// The interface of the canvas: a QAccessibleWidget (focus, geometry, state) whose description says what
// the diagram holds. The cells themselves are not children of it (KNOWN_ISSUES.md); the Cells panel
// lists them.
class CanvasAccessible final : public QAccessibleWidget {
public:
    explicit CanvasAccessible(CanvasView* view) : QAccessibleWidget(view, QAccessible::Canvas) {}

    QString text(QAccessible::Text text) const override
    {
        if (text == QAccessible::Description)
            return canvasAccessibleDescription(*static_cast<const CanvasView*>(object()));
        return QAccessibleWidget::text(text);
    }
};

QAccessibleInterface* accessibleFactory(const QString&, QObject* object)
{
    if (auto* view = qobject_cast<CanvasView*>(object))
        return new CanvasAccessible(view);
    return nullptr;
}

// Names a scroll bar when it is polished (before it is first shown), whichever widget made it.
class ScrollBarNames final : public QObject {
public:
    explicit ScrollBarNames(QObject* parent) : QObject(parent) { parent->installEventFilter(this); }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Polish || event->type() == QEvent::Show) {
            if (auto* bar = qobject_cast<QScrollBar*>(watched); bar != nullptr && bar->accessibleName().isEmpty()) {
                bar->setAccessibleName(bar->orientation() == Qt::Horizontal ? Text::tr("Horizontal Scroll Bar")
                                                                            : Text::tr("Vertical Scroll Bar"));
            }
        }
        return false;
    }
};

} // namespace

QString canvasAccessibleDescription(const CanvasView& view)
{
    const core::Page* page = view.session().currentPage();
    if (page == nullptr)
        return {};
    const core::PageStats stats = page->stats();
    const int shapes = static_cast<int>(stats.vertices + stats.edges);
    const int selected = static_cast<int>(view.session().selection().size());
    const QString count = shapes == 1 ? Text::tr("%n shape", nullptr, shapes) : Text::tr("%n shapes", nullptr, shapes);
    return Text::tr("%1, %2, %3 selected")
        .arg(QString::fromStdString(page->name()), count)
        .arg(selected);
}

void installAccessibility()
{
    static std::once_flag once;
    std::call_once(once, [] {
        QAccessible::installFactory(&accessibleFactory);
        new ScrollBarNames(QCoreApplication::instance());
    });
}

void notifyCanvasChanged(CanvasView& view)
{
    // No QAccessible::isActive() check here: updateAccessibility drops the event itself when nothing
    // listens, and that check would also hide the event from an installed update handler (the tests') on
    // a platform plugin that has no accessibility (Qt 6.8's offscreen: isActive() stays false there).
    QAccessibleEvent event(&view, QAccessible::DescriptionChanged);
    QAccessible::updateAccessibility(&event);
}

// --- tooltips on keyboard focus ------------------------------------------------

namespace {

bool isIconOnly(const QAbstractButton& button)
{
    if (const auto* tool = qobject_cast<const QToolButton*>(&button);
        tool != nullptr && tool->toolButtonStyle() == Qt::ToolButtonIconOnly)
        return true;
    // No word on it ("+"): it needs its tooltip as much as a bare icon does.
    return std::none_of(button.text().begin(), button.text().end(), [](QChar c) { return c.isLetter(); });
}

bool isKeyboardFocus(Qt::FocusReason reason)
{
    return reason == Qt::TabFocusReason || reason == Qt::BacktabFocusReason || reason == Qt::ShortcutFocusReason;
}

} // namespace

FocusToolTips::FocusToolTips(QWidget& window) : QObject(&window), window_(window)
{
    qApp->installEventFilter(this);
}

bool FocusToolTips::eventFilter(QObject* watched, QEvent* event)
{
    if (event->type() != QEvent::FocusIn && event->type() != QEvent::FocusOut)
        return false;
    auto* button = qobject_cast<QAbstractButton*>(watched);
    if (button == nullptr || button->window() != &window_ || !isIconOnly(*button))
        return false;
    if (event->type() == QEvent::FocusOut) {
        if (QToolTip::isVisible() && QToolTip::text() == button->toolTip())
            QToolTip::hideText();
        return false;
    }
    if (isKeyboardFocus(static_cast<const QFocusEvent*>(event)->reason()) && !button->toolTip().isEmpty()) {
        // After the event: the tooltip hides itself on every focus change, including this one.
        QMetaObject::invokeMethod(
            button,
            [button = QPointer<QAbstractButton>(button)] {
                if (button != nullptr && button->hasFocus() && !button->toolTip().isEmpty())
                    QToolTip::showText(button->mapToGlobal(QPoint(0, button->height())), button->toolTip(), button);
            },
            Qt::QueuedConnection);
    }
    return false;
}

} // namespace qraw::ui
