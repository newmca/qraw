// Enabled/checked state of the actions, window title and status bar.
#include "main_window_impl.h"

#include "qraw/ui/app_settings.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/data_actions.h"
#include "qraw/ui/edit_actions.h"
#include "qraw/ui/edit_shape_dialog.h"
#include "qraw/ui/insert_actions.h"

#include <QAction>
#include <QComboBox>
#include <QFileInfo>
#include <QLabel>
#include <QMenu>
#include <QSignalBlocker>

namespace qraw::ui {

using namespace action_id;

namespace {

struct SelectionSummary {
    std::size_t count = 0;
    std::size_t vertices = 0;
    std::size_t edges = 0;
    bool singleParent = false; // exactly one vertex that has children
};

SelectionSummary summarize(const DocumentSession& session)
{
    SelectionSummary summary;
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return summary;
    const std::vector<std::string>& ids = session.selection().cells();
    for (const std::string& id : ids) {
        const core::Cell* cell = page->findCell(id);
        if (cell == nullptr)
            continue;
        ++summary.count;
        if (cell->isVertex())
            ++summary.vertices;
        else if (cell->isEdge())
            ++summary.edges;
    }
    summary.singleParent = ids.size() == 1 && summary.vertices == 1 && hasChildren(*page, ids.front());
    return summary;
}

} // namespace

void MainWindow::Impl::updateActionState()
{
    auto enable = [this](const char* id, bool enabled) { actions[id]->setEnabled(enabled); };
    auto setUndoText = [this](const char* id, const QString& plain, const QString& withName, const QString& name) {
        actions[id]->setText(name.isEmpty() ? plain : withName.arg(name));
    };

    const SelectionSummary selection = summarize(session);
    const bool hasSelection = selection.count > 0;
    const core::Page* page = session.currentPage();
    const std::size_t pageCount = session.document().pageCount();

    enable(kUndo, session.canUndo());
    enable(kRedo, session.canRedo());
    setUndoText(kUndo, tr("&Undo"), tr("&Undo %1"), session.undoText());
    setUndoText(kRedo, tr("&Redo"), tr("&Redo %1"), session.redoText());

    enable(kCopyAsImage, actions::canCopyAsImage(session));
    // A page that could not be decoded has no model to show (its payload is kept as it is).
    enable(kEditDiagram, page != nullptr && page->encoding() != core::PageEncoding::Opaque);
    for (const char* id : {kCut, kCopy, kDelete, kDeleteAll, kDuplicate, kEditLabel, kEditStyle, kCopyStyle,
                           kLockUnlock, kToFront, kToBack, kBringForward, kSendBackward, kRemoveFromGroup,
                           kAlignLeft, kAlignCenter, kAlignRight, kAlignTop, kAlignMiddle, kAlignBottom,
                           kDistributeHorizontal, kDistributeVertical, kClearWaypoints, kFlipHorizontal,
                           kFlipVertical, kTurn})
        enable(id, hasSelection);
    enable(kAutosize, selection.vertices > 0);
    enable(kEditGeometry, actions::canEditGeometry(session));
    enable(kMergeCells, actions::canMergeCells(session));
    enable(kUnmergeCells, actions::canUnmergeCells(session));
    for (const char* id : {kLayoutVerticalTree, kLayoutHorizontalTree, kLayoutRadialTree, kLayoutOrgChart, kLayoutCircle,
                           kLayoutHorizontalFlow, kLayoutVerticalFlow, kLayoutOrganic})
        enable(id, actions::canArrangeLayout(session));
    enable(kPaste, true);
    const bool canInsert = actions::canInsert(session);
    // A tool that is on can always be turned off.
    enable(kFreehand, canInsert || canvas->freehandActive());
    for (const char* id : {kInsertText, kInsertImage, kInsertLink, kInsertHorizontalLine, kInsertVerticalLine, kInsertFromText,
                           kInsertCsv, kInsertMermaid})
        enable(id, canInsert);
    {
        const core::Cell* only = (selection.count == 1 && page != nullptr) ? page->findCell(session.selection().first()) : nullptr;
        enable(kEditShape, only != nullptr && stencilXmlOf(*only).has_value());
    }
    // draw.io (EditorUi.js:8890, :39279, :39312): Edit Data is always on (no selection edits the diagram's own data),
    // Edit Link follows the unlocked selection, Edit Tooltip needs exactly one cell, Open Link one linked cell.
    enable(kEditData, actions::canEditData(session));
    enable(kEditLink, actions::canEditLink(session));
    enable(kEditTooltip, actions::canEditTooltip(session));
    enable(kEditImage, actions::canEditImage(session));
    enable(kOpenLink, actions::selectedLink(session).has_value());
    enable(kPasteStyle, hasSelection && !styleBuffer.empty());
    enable(kSetDefaultStyle, actions::canSetDefaultStyle(session));
    enable(kClearDefaultStyle, !AppSettings().defaultStyles().empty());
    // One vertex becomes a container (draw.io groups a single cell too).
    enable(kGroup, selection.count >= 2 || (selection.count == 1 && selection.vertices == 1));
    enable(kUngroup, selection.singleParent);
    enable(kEnterGroup, selection.singleParent);
    enable(kExitGroup, !session.enteredGroup().empty());
    enable(kHome, !session.enteredGroup().empty());

    enable(kRevert, !controller.filePath().isEmpty());
    enable(kSave, true);
    enable(kDeletePage, pageCount > 1);
    enable(kPreviousPage, session.currentPageIndex() > 0);
    enable(kNextPage, session.currentPageIndex() + 1 < pageCount);

    actions[kPageView]->setChecked(page != nullptr && page->pageVisible());
    actions[kGrid]->setChecked(page != nullptr && page->gridEnabled());
    actions[kGuides]->setChecked(canvas->guidesEnabled());
    actions[kFullscreen]->setChecked(window.isFullScreen());
}

void MainWindow::Impl::updateStatusBar()
{
    const SelectionSummary selection = summarize(session);
    const int count = static_cast<int>(selection.count);
    QString text;
    if (count == 0)
        text = tr("No selection");
    else if (selection.edges == selection.count)
        text = count == 1 ? tr("%n edge selected", nullptr, count) : tr("%n edges selected", nullptr, count);
    else
        text = count == 1 ? tr("%n shape selected", nullptr, count) : tr("%n shapes selected", nullptr, count);
    selectionLabel->setText(text);

    pageLabel->setText(tr("Page %1 of %2").arg(session.currentPageIndex() + 1).arg(session.document().pageCount()));
}

void MainWindow::Impl::updateTitle()
{
    window.setWindowTitle(QStringLiteral("%1[*] - Qraw").arg(controller.displayName()));
    window.setWindowModified(controller.isModified());
}

void MainWindow::Impl::updateZoomDisplay(double zoom)
{
    const QString percent = tr("%1 %").arg(qRound(zoom * 100));
    zoomLabel->setText(percent);
    const QSignalBlocker blocker(zoomCombo);
    zoomCombo->setCurrentIndex(-1);
    zoomCombo->setEditText(percent);
}

// Open Recent: one entry per remembered file, then "Clear Recent Files".
void MainWindow::Impl::rebuildRecentMenu()
{
    recentMenu->clear();
    const QStringList files = settings.recentFiles();
    actions[kOpenRecent]->setEnabled(!files.isEmpty());
    int number = 1;
    for (const QString& path : files) {
        const QString text = QStringLiteral("&%1 %2").arg(number++).arg(QFileInfo(path).fileName());
        QAction* entry = recentMenu->addAction(text, this, [this, path] {
            if (!controller.openPath(path) && !QFileInfo::exists(path)) {
                settings.removeRecentFile(path);
                rebuildRecentMenu();
            }
        });
        entry->setToolTip(path);
        entry->setStatusTip(path);
    }
    if (files.isEmpty())
        return;
    recentMenu->addSeparator();
    recentMenu->addAction(tr("&Clear Recent Files"), this, [this] {
        settings.clearRecentFiles();
        rebuildRecentMenu();
    });
}

} // namespace qraw::ui
