#include "qraw/ui/edit_tooltip_dialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QShortcut>
#include <QVBoxLayout>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::EditTooltipDialog)
};

} // namespace

EditTooltipDialog::EditTooltipDialog(const actions::TooltipSheet& sheet, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("EditTooltipDialog"));
    setWindowTitle(Text::tr("Edit Tooltip"));

    auto* label = new QLabel(Text::tr("Tooltip:"), this);
    edit_ = new QPlainTextEdit(this);
    edit_->setObjectName(QStringLiteral("tooltip.text"));
    edit_->setAccessibleName(Text::tr("Tooltip"));
    edit_->setPlainText(QString::fromStdString(sheet.text));
    label->setBuddy(edit_);
    if (sheet.html)
        edit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont)); // the text is HTML source
    edit_->setMinimumSize(380, 160);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* apply = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(apply, &QShortcut::activated, this, &QDialog::accept);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(label);
    layout->addWidget(edit_, 1);
    if (sheet.html) {
        auto* hint = new QLabel(Text::tr("The label of this cell is HTML, so the tooltip can use HTML too."), this);
        hint->setObjectName(QStringLiteral("tooltip.hint"));
        hint->setWordWrap(true);
        hint->setEnabled(false);
        layout->addWidget(hint);
    }
    layout->addWidget(buttons);
}

std::string EditTooltipDialog::text() const
{
    return edit_->toPlainText().toStdString();
}

} // namespace qraw::ui
