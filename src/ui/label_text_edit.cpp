#include "label_text_edit.h"

#include <QFocusEvent>
#include <QKeyEvent>
#include <QTextCharFormat>

namespace qraw::ui {

LabelTextEdit::LabelTextEdit(bool richText, QWidget* parent) : QTextEdit(parent), richText_(richText)
{
    setAcceptRichText(richText);
    setFrameShape(QFrame::NoFrame);
    // Transparent: the shape below shows through (mxCellEditor draws no background).
    setAutoFillBackground(false);
    viewport()->setAutoFillBackground(false);
    setVerticalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    setTabChangesFocus(false);
}

LabelTextEdit::Command LabelTextEdit::commandFor(const QKeyEvent& event)
{
    const Qt::KeyboardModifiers modifiers = event.modifiers() & ~Qt::KeypadModifier;
    const bool ctrl = modifiers & (Qt::ControlModifier | Qt::MetaModifier);
    switch (event.key()) {
    case Qt::Key_Escape:
        return Command::Cancel;
    case Qt::Key_Return:
    case Qt::Key_Enter:
        return ctrl ? Command::Commit : Command::None;
    case Qt::Key_Tab:
        return modifiers == Qt::NoModifier ? Command::Commit : Command::None;
    case Qt::Key_B:
        return modifiers == Qt::ControlModifier ? Command::Bold : Command::None;
    case Qt::Key_I:
        return modifiers == Qt::ControlModifier ? Command::Italic : Command::None;
    case Qt::Key_U:
        return modifiers == Qt::ControlModifier ? Command::Underline : Command::None;
    default:
        return Command::None;
    }
}

void LabelTextEdit::toggleCharFormat(Command command)
{
    QTextCharFormat format;
    const QTextCharFormat current = currentCharFormat();
    switch (command) {
    case Command::Bold:
        format.setFontWeight(current.fontWeight() >= QFont::Bold ? QFont::Normal : QFont::Bold);
        break;
    case Command::Italic:
        format.setFontItalic(!current.fontItalic());
        break;
    case Command::Underline:
        format.setFontUnderline(!current.fontUnderline());
        break;
    default:
        return;
    }
    mergeCurrentCharFormat(format);
}

void LabelTextEdit::keyPressEvent(QKeyEvent* event)
{
    switch (commandFor(*event)) {
    case Command::Cancel:
        event->accept();
        emit cancelRequested();
        return;
    case Command::Commit:
        event->accept();
        emit commitRequested();
        return;
    case Command::Bold:
    case Command::Italic:
    case Command::Underline:
        event->accept();
        if (richText_)
            toggleCharFormat(commandFor(*event));
        return;
    case Command::None:
        break;
    }
    QTextEdit::keyPressEvent(event);
}

bool LabelTextEdit::event(QEvent* event)
{
    if (event->type() == QEvent::ShortcutOverride) {
        auto* key = static_cast<QKeyEvent*>(event);
        if (commandFor(*key) != Command::None) {
            event->accept();
            return true;
        }
    }
    return QTextEdit::event(event);
}

void LabelTextEdit::focusOutEvent(QFocusEvent* event)
{
    QTextEdit::focusOutEvent(event);
    if (event->reason() != Qt::PopupFocusReason && event->reason() != Qt::ActiveWindowFocusReason)
        emit focusLost();
}

} // namespace qraw::ui
