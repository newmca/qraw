#pragma once
// Edit > Edit Tooltip... (draw.io 'editTooltip', Alt+Shift+T; MarkupDialog): the
// text shown when the pointer rests on the cell, stored in its `tooltip`
// property. A cell with an HTML label takes HTML here too; the canvas shows
// the tooltip sanitised (cell_tooltip.h). Leaving it empty removes the tooltip.
// Upstream edits it in a small WYSIWYG area; this is the source text.

#include "qraw/ui/data_actions.h"

#include <QDialog>

class QPlainTextEdit;

namespace qraw::ui {

class EditTooltipDialog : public QDialog {
    Q_OBJECT

public:
    explicit EditTooltipDialog(const actions::TooltipSheet& sheet, QWidget* parent = nullptr);

    [[nodiscard]] std::string text() const;

private:
    QPlainTextEdit* edit_ = nullptr;
};

} // namespace qraw::ui
