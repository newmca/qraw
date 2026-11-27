// The Insert menu (draw.io's Insert menu): Text, Image..., Link..., lines and the
// Table size picker. The model edits are in insert_actions.cpp; this file opens
// the dialogs and chooses where the new cell goes.
#include "main_window_impl.h"

#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cell_transfer.h"
#include "qraw/ui/edit_image_dialog.h"
#include "qraw/ui/edit_link_dialog.h"
#include "qraw/ui/import_dialog.h"
#include "qraw/ui/insert_actions.h"
#include "qraw/ui/label_editor.h"

#include <QMessageBox>
#include <QMimeData>
#include <QStatusBar>

namespace qraw::ui {

// The context menu's point while that menu runs, else where the pointer is over
// the canvas, else the viewport centre (Editor.js getInsertPoint,
// EditorUi.js getCenterInsertPoint override for the background popup menu).
QPointF MainWindow::Impl::insertPoint() const
{
    return menuInsertPoint.value_or(canvas->insertPoint());
}

void MainWindow::Impl::insertText()
{
    if (!actions::canInsert(session))
        return;
    // After the menu has closed and given the focus back: the editor takes it from the canvas.
    QMetaObject::invokeMethod(
        this,
        [this, at = insertPoint()] {
            if (LabelEditor* editor = canvas->interaction().labelEditor())
                editor->insertTextAt(at);
        },
        Qt::QueuedConnection);
}

void MainWindow::Impl::insertImage()
{
    if (!actions::canInsert(session))
        return;
    const QPointF at = insertPoint(); // the pointer is over the dialog's menu or the canvas now, not later
    EditImageDialog dialog(session.baseDirectory(), &window);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (const std::optional<EditImageDialog::Insertion> insertion = dialog.insertion())
        actions::insertImage(session, insertion->image, insertion->size, at);
}

void MainWindow::Impl::insertLink()
{
    if (!actions::canInsert(session))
        return;
    const QPointF at = insertPoint();
    std::vector<EditLinkDialog::PageChoice> pages;
    const core::Document& document = session.document();
    for (std::size_t i = 0; i < document.pageCount(); ++i)
        pages.push_back({document.page(i)->id(), QString::fromStdString(document.page(i)->name())});
    EditLinkDialog dialog(actions::LinkSheet{}, std::move(pages), &window);
    dialog.setWindowTitle(tr("Insert Link"));
    if (dialog.exec() != QDialog::Accepted)
        return;
    const actions::LinkEdit edit = dialog.edit();
    if (edit.setLink)
        actions::insertLinkCell(session, edit.link, edit.newWindow, at);
}

void MainWindow::Impl::insertLine(bool vertical)
{
    actions::insertLine(session, vertical, insertPoint());
}

void MainWindow::Impl::insertTable(int rows, int columns)
{
    actions::insertTable(session, rows, columns, insertPoint());
}

// Insert > Advanced: the dialog collects the text and imports it; the cells go in as one edit.
void MainWindow::Impl::insertAdvanced(ImportDialog::Kind kind)
{
    if (!actions::canInsert(session))
        return;
    const QPointF at = insertPoint(); // the pointer is over the dialog's menu or the canvas now, not later
    ImportDialog dialog(kind, actions::importLabelMeasure(session), &window);
    if (dialog.exec() != QDialog::Accepted)
        return;
    const commands::imports::Result& result = dialog.result();
    const QString name = kind == ImportDialog::Kind::FromText ? tr("Insert From Text")
                         : kind == ImportDialog::Kind::Csv    ? tr("Insert CSV")
                                                              : tr("Insert Mermaid Diagram");
    if (!actions::insertImported(session, result, at, name)) {
        window.statusBar()->showMessage(tr("Nothing was inserted."), 8000);
        return;
    }
    window.statusBar()->showMessage(tr("Inserted %1 cells.").arg(result.cells.size()), 8000);
    if (result.warnings.empty())
        return;
    // Input that was understood but not used: said once, without stopping the work.
    QStringList lines;
    for (const commands::imports::Diagnostic& warning : result.warnings)
        lines << QString::fromStdString(commands::imports::describe(warning));
    auto* box = new QMessageBox(QMessageBox::Information, name, tr("The cells were inserted, but part of the input was not used:"),
                                QMessageBox::Ok, &window);
    box->setObjectName(QStringLiteral("import.warnings"));
    box->setInformativeText(lines.join(QLatin1Char('\n')));
    box->setAttribute(Qt::WA_DeleteOnClose);
    box->open();
}

// Ctrl+V of an image: true when the clipboard held one (inserted, or refused with a message).
bool MainWindow::Impl::pasteImage(const QMimeData& mime)
{
    if (!mime.hasImage() || cellsFromMimeData(mime))
        return false;
    QString error;
    if (!actions::insertClipboardImage(session, mime, insertPoint(), &error) && !error.isEmpty())
        window.statusBar()->showMessage(error, 8000);
    return true;
}

} // namespace qraw::ui
