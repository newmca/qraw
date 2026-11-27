#pragma once
// The text widget of the inline label editor (mxCellEditor's contenteditable
// div). Private to the editor UI.

#include <QTextEdit>

namespace qraw::ui {

// Reports the keys that end editing instead of handling them as text input:
// Escape cancels, Ctrl+Enter / Cmd+Enter and Tab commit. Plain Enter inserts
// a newline (the QTextEdit default). Ctrl+B/I/U toggle bold/italic/underline
// of the selection for rich text. The widget accepts ShortcutOverride for all
// of these so that window shortcuts do not steal them (ui.md §6).
class LabelTextEdit : public QTextEdit {
    Q_OBJECT

public:
    LabelTextEdit(bool richText, QWidget* parent);

    [[nodiscard]] bool isRichText() const { return richText_; }

signals:
    void commitRequested();
    void cancelRequested();
    // Focus moved elsewhere (not to a popup, not a window deactivation).
    void focusLost();

protected:
    void keyPressEvent(QKeyEvent* event) override;
    bool event(QEvent* event) override;
    void focusOutEvent(QFocusEvent* event) override;

private:
    enum class Command { None, Cancel, Commit, Bold, Italic, Underline };
    [[nodiscard]] static Command commandFor(const QKeyEvent& event);
    void toggleCharFormat(Command command);

    bool richText_;
};

} // namespace qraw::ui
