#pragma once
// Help > Keyboard Shortcuts (draw.io's shortcuts.svg page, as a searchable
// table).

#include <QDialog>

#include <memory>

namespace qraw::ui {

class ActionRegistry;

// A filter field and a two-column table (action text without '&',
// QKeySequence::NativeText of every shortcut joined with ", "), grouped by
// ActionRegistry category in registry order, plus the canvas-only keys that
// are not actions (ActionRegistry::canvasKeys(): arrow nudge, Shift+arrow
// grid nudge, Alt+drag no snapping, Space+drag pan, Ctrl+wheel zoom,
// Enter/F2 edit, Escape). Read-only.
class ShortcutsDialog : public QDialog {
    Q_OBJECT

public:
    explicit ShortcutsDialog(const ActionRegistry& registry, QWidget* parent = nullptr);
    ~ShortcutsDialog() override;

    void setFilter(const QString& text);
    [[nodiscard]] int visibleRowCount() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
