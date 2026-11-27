// Edit Data, Edit Link, Edit Tooltip, Edit Image and Open Link: the dialogs are
// opened here, the model edits are in data_actions.cpp (M9, draw.io's cell data
// actions).
#include "main_window_impl.h"

#include "qraw/ui/canvas_view.h"
#include "qraw/ui/data_actions.h"
#include "qraw/ui/edit_data_dialog.h"
#include "qraw/ui/edit_image_dialog.h"
#include "qraw/ui/edit_link_dialog.h"
#include "qraw/ui/edit_tooltip_dialog.h"

namespace qraw::ui {

void MainWindow::Impl::editData()
{
    if (!actions::canEditData(session))
        return;
    const actions::DataSheet sheet = actions::readDataSheet(session);
    EditDataDialog dialog(sheet, &window);
    if (dialog.exec() != QDialog::Accepted)
        return;
    actions::editData(session, sheet, dialog.change());
}

void MainWindow::Impl::editLink()
{
    if (!actions::canEditLink(session))
        return;
    const actions::LinkSheet sheet = actions::readLinkSheet(session);
    std::vector<EditLinkDialog::PageChoice> pages;
    const core::Document& document = session.document();
    for (std::size_t i = 0; i < document.pageCount(); ++i)
        pages.push_back({document.page(i)->id(), QString::fromStdString(document.page(i)->name())});
    EditLinkDialog dialog(sheet, std::move(pages), &window);
    if (dialog.exec() != QDialog::Accepted)
        return;
    actions::editLink(session, sheet, dialog.edit());
}

void MainWindow::Impl::editTooltip()
{
    const std::optional<actions::TooltipSheet> sheet = actions::readTooltipSheet(session);
    if (!sheet)
        return;
    EditTooltipDialog dialog(*sheet, &window);
    if (dialog.exec() != QDialog::Accepted)
        return;
    actions::editTooltip(session, sheet->id, dialog.text());
}

void MainWindow::Impl::editImage()
{
    if (!actions::canEditImage(session))
        return;
    const actions::ImageSheet sheet = actions::readImageSheet(session);
    EditImageDialog dialog(sheet, session.baseDirectory(), &window);
    if (dialog.exec() != QDialog::Accepted)
        return;
    actions::editImage(session, dialog.edit());
}

void MainWindow::Impl::openLink()
{
    if (const std::optional<std::string> link = actions::selectedLink(session))
        actions::followLink(session, *link);
}

} // namespace qraw::ui
