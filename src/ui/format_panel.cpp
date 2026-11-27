// The Format panel (draw.io's Format, js/grapheditor/Format.js, refresh): the
// tabs for the current selection.
#include "qraw/ui/format_panel.h"

#include "format_controls.h"
#include "format_edit.h"
#include "format_tab.h"

#include "qraw/ui/document_session.h"

#include <QAbstractSpinBox>
#include <QApplication>
#include <QLineEdit>
#include <QScrollArea>
#include <QTabWidget>
#include <QVBoxLayout>

#include <vector>

namespace qraw::ui {

namespace {

// What the tab set is built for: the page (nothing selected) or cells.
enum class Mode { None, Diagram, Cells };

// Commits the field that has the focus inside the panel, as Enter or Tab would
// (called on DocumentSession::pendingEditsRequested, so Ctrl+S saves what was
// typed). The field emits editingFinished; the field's own handlers decide
// whether that writes: an unchanged color is ignored, and a spin box only ends
// its merge. The later focus-out editingFinished therefore adds no undo entry.
void commitFocusedField(const QWidget& panel)
{
    QWidget* focus = QApplication::focusWidget();
    if (focus == nullptr || (focus != &panel && !panel.isAncestorOf(focus)))
        return;
    // A spin box's text is its child QLineEdit (lineEdit() is protected): the
    // spin box holds the value to commit.
    QAbstractSpinBox* spin = qobject_cast<QAbstractSpinBox*>(focus);
    if (spin == nullptr && qobject_cast<QLineEdit*>(focus) != nullptr)
        spin = qobject_cast<QAbstractSpinBox*>(focus->parentWidget());
    if (spin != nullptr) {
        spin->interpretText();
        emit spin->editingFinished();
    } else if (auto* edit = qobject_cast<QLineEdit*>(focus)) {
        emit edit->editingFinished();
    }
}

} // namespace

struct FormatPanel::Impl {
    explicit Impl(DocumentSession& owner) : session(owner) {}

    // Replaces the tabs with the set for `next`. The replaced tabs and their
    // pages are kept until the panel dies: the lambdas attached to their
    // controls capture the tab, and the pages are only deleted later.
    void rebuild(Mode next);

    DocumentSession& session;
    QTabWidget* tabs = nullptr;
    Mode mode = Mode::None;
    QList<QAction*> styleActions;
    QList<QAction*> dataActions;
    std::vector<std::unique_ptr<FormatTab>> active;
    std::vector<std::unique_ptr<FormatTab>> replaced;
};

void FormatPanel::Impl::rebuild(Mode next)
{
    while (tabs->count() > 0) {
        QWidget* page = tabs->widget(0);
        tabs->removeTab(0);
        page->deleteLater();
    }
    for (auto& tab : active)
        replaced.push_back(std::move(tab));
    active.clear();

    auto add = [this](std::unique_ptr<FormatTab> tab, const QString& title) {
        tabs->addTab(tab->page(), title);
        active.push_back(std::move(tab));
    };
    switch (next) {
    case Mode::Diagram:
        add(makeDiagramTab(session, dataActions), formatTr("Diagram"));
        break;
    case Mode::Cells:
        add(makeStyleTab(session, styleActions), formatTr("Style"));
        add(makeTextTab(session), formatTr("Text"));
        add(makeArrangeTab(session, dataActions), formatTr("Arrange"));
        break;
    case Mode::None:
        break;
    }
    mode = next;
}

FormatPanel::FormatPanel(DocumentSession& session, QWidget* parent)
    : QWidget(parent), d(std::make_unique<Impl>(session))
{
    auto* layout = new QVBoxLayout(this);
    layout->setContentsMargins(0, 0, 0, 0);
    d->tabs = new QTabWidget(this);
    layout->addWidget(d->tabs);

    connect(&session.selection(), &SelectionModel::selectionChanged, this, [this] { refresh(); });
    connect(&session, &DocumentSession::documentChanged, this, [this] { refresh(); });
    connect(&session, &DocumentSession::currentPageChanged, this, [this] { refresh(); });
    connect(&session, &DocumentSession::pendingEditsRequested, this, [this] { commitFocusedField(*this); });

    refresh();
}

FormatPanel::~FormatPanel() = default;

void FormatPanel::setStyleActions(const QList<QAction*>& actions)
{
    d->styleActions = actions;
    d->mode = Mode::None; // the Style tab is rebuilt with the buttons
    refresh();
}

void FormatPanel::setDataActions(const QList<QAction*>& actions)
{
    d->dataActions = actions;
    d->mode = Mode::None; // the Diagram and Arrange tabs are rebuilt with the buttons
    refresh();
}

void FormatPanel::refresh()
{
    const StyleSummary summary = summarizeSelection(d->session);
    const Mode next = summary.empty() ? Mode::Diagram : Mode::Cells;
    if (next != d->mode)
        d->rebuild(next);
    for (auto& tab : d->active)
        tab->refresh(summary);
}

QTabWidget* FormatPanel::tabs() const
{
    return d->tabs;
}

} // namespace qraw::ui
