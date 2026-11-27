#include "qraw/ui/edit_diagram_dialog.h"

#include "qraw/format/codec.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QLabel>
#include <QPlainTextEdit>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::EditDiagramDialog)
};

} // namespace

EditDiagramDialog::EditDiagramDialog(const std::string& text, QWidget* parent) : QDialog(parent)
{
    setObjectName(QStringLiteral("EditDiagramDialog"));
    setWindowTitle(Text::tr("Edit Diagram"));
    resize(640, 460);

    auto* intro = new QLabel(Text::tr("The XML of this page. Changes replace its content as one undoable step."), this);
    intro->setWordWrap(true);

    edit_ = new QPlainTextEdit(this);
    edit_->setObjectName(QStringLiteral("diagram.text"));
    edit_->setAccessibleName(Text::tr("Diagram XML"));
    edit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit_->setTabChangesFocus(true);
    initial_ = QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
    edit_->setPlainText(initial_);
    edit_->setMinimumSize(420, 240);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("diagram.error"));
    error_->setWordWrap(true);
    error_->setTextInteractionFlags(Qt::TextSelectableByMouse);
    error_->setVisible(false);
    QFont bold = error_->font();
    bold.setBold(true);
    error_->setFont(bold);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addWidget(edit_, 1);
    if (static_cast<qsizetype>(text.size()) > kLargeText) {
        auto* note = new QLabel(Text::tr("This diagram is large (%1 MB). Editing may be slow.")
                                    .arg(static_cast<qsizetype>(text.size()) / (1024 * 1024)),
                                this);
        note->setObjectName(QStringLiteral("diagram.sizeNote"));
        note->setWordWrap(true);
        layout->addWidget(note);
    }
    layout->addWidget(error_);
    layout->addWidget(buttons);
    edit_->setFocus();
}

EditDiagramDialog::~EditDiagramDialog() = default;

std::string EditDiagramDialog::text() const
{
    return edit_->toPlainText().toStdString();
}

void EditDiagramDialog::accept()
{
    // Untouched text is not parsed: there is nothing to apply (and no 40 MB parse for a click on OK).
    if (edit_->toPlainText() == initial_) {
        model_.reset();
        QDialog::accept();
        return;
    }
    const std::string source = text();
    format::ModelTextResult parsed = format::parsePageModelText(QByteArrayView(source.data(), static_cast<qsizetype>(source.size())));
    if (!parsed.page) {
        showError(QString::fromStdString(parsed.error.message), parsed.error.line, parsed.error.column);
        return;
    }
    model_ = std::move(parsed.page);
    QDialog::accept();
}

// Shows the message and puts the text cursor where the parser stopped.
void EditDiagramDialog::showError(const QString& message, int line, int column)
{
    QString where;
    if (line > 0) {
        where = column > 0 ? Text::tr("Line %1, column %2: ").arg(line).arg(column) : Text::tr("Line %1: ").arg(line);
        const QTextBlock block = edit_->document()->findBlockByNumber(line - 1);
        if (block.isValid()) {
            QTextCursor cursor(block);
            cursor.setPosition(block.position() + qBound(0, column - 1, block.length() - 1));
            edit_->setTextCursor(cursor);
            edit_->centerCursor();
        }
    }
    error_->setText(Text::tr("The XML is not valid. %1%2").arg(where, message));
    error_->setVisible(true);
    edit_->setFocus();
}

} // namespace qraw::ui
