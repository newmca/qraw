#include "main_window_prompts.h"

#include <QFileDialog>
#include <QMessageBox>
#include <QPushButton>

namespace qraw::ui {

DocumentPrompts::SaveChoice DialogPrompts::askSaveChanges(const QString& documentName)
{
    QMessageBox box(QMessageBox::Warning, tr("Unsaved Changes"),
                    tr("Do you want to save the changes to \"%1\"?").arg(documentName), QMessageBox::NoButton,
                    parent_);
    box.setInformativeText(tr("Your changes will be lost if you don't save them."));
    QPushButton* save = box.addButton(tr("&Save"), QMessageBox::AcceptRole);
    QPushButton* discard = box.addButton(tr("&Don't Save"), QMessageBox::DestructiveRole);
    box.addButton(QMessageBox::Cancel);
    box.setDefaultButton(save);
    box.exec();
    if (box.clickedButton() == static_cast<QAbstractButton*>(save))
        return SaveChoice::Save;
    return box.clickedButton() == static_cast<QAbstractButton*>(discard) ? SaveChoice::Discard : SaveChoice::Cancel;
}

std::optional<QString> DialogPrompts::askOpenPath(const QString& filter)
{
    const QString path = QFileDialog::getOpenFileName(parent_, tr("Open Diagram"), QString(), filter);
    if (path.isEmpty())
        return std::nullopt;
    return path;
}

std::optional<QString> DialogPrompts::askSavePath(const QString& suggested, const QString& filter)
{
    const QString path = QFileDialog::getSaveFileName(parent_, tr("Save Diagram"), suggested, filter);
    if (path.isEmpty())
        return std::nullopt;
    return path;
}

void DialogPrompts::showError(const QString& title, const QString& message)
{
    QMessageBox::critical(parent_, title, message);
}

void DialogPrompts::showWarnings(const QString& title, const QStringList& messages)
{
    if (messages.isEmpty())
        return;
    QMessageBox box(QMessageBox::Warning, title, tr("The file was opened with warnings."), QMessageBox::Ok, parent_);
    box.setDetailedText(messages.join(QLatin1Char('\n')));
    box.exec();
}

} // namespace qraw::ui
