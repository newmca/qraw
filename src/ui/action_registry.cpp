#include "qraw/ui/action_registry.h"

#include <QAction>

namespace qraw::ui {

ActionRegistry::ActionRegistry(QObject* parent) : QObject(parent) {}

ActionRegistry::~ActionRegistry() = default;

QAction* ActionRegistry::add(const QString& id, const QString& category, const QString& text,
                             const QList<QKeySequence>& shortcuts)
{
    if (QAction* existing = action(id))
        return existing;
    auto* action = new QAction(text, this);
    action->setObjectName(id);
    action->setShortcuts(shortcuts);
    action->setShortcutContext(Qt::WindowShortcut);
    actions_.push_back(ActionInfo{id, category, action});
    return action;
}

QString withEllipsis(const QString& text)
{
    return text.endsWith(QChar(kEllipsis)) ? text : text + QChar(kEllipsis);
}

bool ActionRegistry::markOpensDialog(const QString& id)
{
    for (ActionInfo& info : actions_) {
        if (info.id == id) {
            info.opensDialog = true;
            info.action->setText(withEllipsis(info.action->text()));
            return true;
        }
    }
    return false;
}

QAction* ActionRegistry::action(const QString& id) const
{
    for (const ActionInfo& info : actions_) {
        if (info.id == id)
            return info.action;
    }
    return nullptr;
}

QStringList ActionRegistry::categories() const
{
    QStringList result;
    for (const ActionInfo& info : actions_) {
        if (!result.contains(info.category))
            result << info.category;
    }
    return result;
}

std::vector<CanvasKey> ActionRegistry::canvasKeys()
{
    // Canvas-only keys and gestures (docs/design/ui.md §6); draw.io's
    // EditorUi.createKeyHandler and mxGraphHandler / mxPanningHandler.
    return {
        {tr("Arrow keys"), tr("Move the selection by 1 px")},
        {tr("Shift+Arrow keys"), tr("Move the selection by the grid size")},
        {tr("Tab / Shift+Tab"), tr("Select the next or previous cell (the focus stays on the canvas)")},
        {tr("Alt+Tab / Alt+Shift+Tab"), tr("Select the first cell inside the selected container, or the container")},
        {tr("Ctrl+Tab / Ctrl+Shift+Tab"), tr("Leave the canvas for the next or previous control")},
        {tr("Enter / F2"), tr("Edit the label of the selected cell")},
        {tr("Escape"), tr("Cancel the current operation, stop editing, or clear the selection")},
        {tr("Shift+Click"), tr("Add to or remove from the selection")},
        {tr("Alt+Drag"), tr("Move or resize without snapping")},
        {tr("Ctrl+Drag"), tr("Copy the dragged cells")},
        {tr("Shift+Drag"), tr("Move horizontally or vertically only")},
        {tr("Space+Drag / Middle drag / Right drag"), tr("Pan the canvas")},
        {tr("Ctrl+Mouse wheel"), tr("Zoom in or out")},
        {tr("Shift+Mouse wheel"), tr("Scroll horizontally")},
        {tr("Double click"), tr("Edit a label, enter a group, or insert text on blank canvas")},
    };
}

} // namespace qraw::ui
