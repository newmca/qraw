// Behaviour of every action: MainWindow connects the registry's actions to the
// controller, the session, the canvas and the dialogs (ui.md section 6).
#include "main_window_impl.h"

#include "qraw/commands/ops_pages.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/format/codec.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/edit_actions.h"
#include "qraw/ui/edit_diagram_dialog.h"
#include "qraw/ui/edit_geometry_dialog.h"
#include "qraw/ui/edit_shape_dialog.h"
#include "qraw/ui/export_dialog.h"
#include "qraw/ui/find_panel.h"
#include "qraw/ui/page_tabs.h"
#include "qraw/ui/palette_dock.h"
#include "qraw/ui/preferences_dialog.h"
#include "qraw/ui/print_support.h"
#include "qraw/ui/shortcuts_dialog.h"

#include <QAction>
#include <QApplication>
#include <QActionGroup>
#include <QClipboard>
#include <QComboBox>
#include <QDir>
#include <QFileDialog>
#include <QFileInfo>
#include <QDialog>
#include <QGuiApplication>
#include <QInputDialog>
#include <QLineEdit>
#include <QMessageBox>
#include <QMimeData>
#include <QRegularExpression>
#include <QStatusBar>

#include <functional>

namespace qraw::ui {

using namespace action_id;
namespace ops = commands::ops;

namespace {

constexpr int kMaxZoomPercent = static_cast<int>(CanvasView::kMaxZoom * 100);

// "150", "150 %" or "150%" -> 1.5; nullopt otherwise.
std::optional<double> parsePercent(const QString& text)
{
    static const QRegularExpression pattern(QStringLiteral("^\\s*(\\d+(?:[.,]\\d+)?)\\s*%?\\s*$"));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch())
        return std::nullopt;
    bool ok = false;
    const double percent = QString(match.captured(1)).replace(QLatin1Char(','), QLatin1Char('.')).toDouble(&ok);
    if (!ok || percent <= 0 || percent > kMaxZoomPercent)
        return std::nullopt;
    return percent / 100;
}

} // namespace

bool MainWindow::Impl::forwardToTextInput(const char* slot)
{
    QWidget* focus = QApplication::focusWidget();
    return focus != nullptr && focus->window() == &window && isTextInput(focus)
        && QMetaObject::invokeMethod(focus, slot);
}

void MainWindow::Impl::commitLabelEdit(const QString& actionId)
{
    // View commands (zoom, panels, ...) leave the editor open; it follows the zoom.
    static const QStringList keepsEditing{
        QString::fromLatin1(kZoomIn),       QString::fromLatin1(kZoomOut),    QString::fromLatin1(kFitWindow),
        QString::fromLatin1(kFitPage),      QString::fromLatin1(kFitSelection), QString::fromLatin1(kActualSize),
        QString::fromLatin1(kResetView),    QString::fromLatin1(kFullscreen), QString::fromLatin1(kFormatPanel),
        QString::fromLatin1(kOutline),      QString::fromLatin1(kLayers),     QString::fromLatin1(kShapes),
        QString::fromLatin1(kCells),
        QString::fromLatin1(kAbout),        QString::fromLatin1(kKeyboardShortcuts),
        QString::fromLatin1(kOpenLogFolder)};
    static const QStringList textCommands{QString::fromLatin1(kUndo),  QString::fromLatin1(kRedo),
                                          QString::fromLatin1(kCut),   QString::fromLatin1(kCopy),
                                          QString::fromLatin1(kPaste), QString::fromLatin1(kSelectAll)};
    if (keepsEditing.contains(actionId))
        return;
    QWidget* focus = QApplication::focusWidget();
    if (textCommands.contains(actionId) && focus != nullptr && isTextInput(focus))
        return; // acts on the text itself
    session.flushPendingEdits();
}

void MainWindow::Impl::connectActions()
{
    // First slot of every action: a running label edit is committed before the command runs.
    for (const ActionInfo& info : actions.actions()) {
        connect(info.action, &QAction::triggered, this, [this, id = info.id] { commitLabelEdit(id); });
    }
    auto on = [this](const char* id, std::function<void()> fn) {
        connect(actions[id], &QAction::triggered, this, [fn = std::move(fn)] { fn(); });
    };
    auto onToggle = [this](const char* id, std::function<void(bool)> fn) {
        connect(actions[id], &QAction::triggered, this, std::move(fn));
    };
    // Edit actions that the session rejects report through its status line.
    auto edit = [&](const char* id, std::function<bool()> fn) { on(id, [fn = std::move(fn)] { fn(); }); };
    auto order = [this](ops::ZOrder how) { return [this, how] { return actions::order(session, how); }; };
    auto align = [this](actions::Align how) { return [this, how] { return actions::align(session, how); }; };

    // File
    on(kNew, [this] { controller.newDocument(); });
    on(kOpen, [this] { controller.open(); });
    on(kNewLibrary, [this] { newLibrary(); });
    on(kOpenLibrary, [this] { openLibrary(); });
    on(kSave, [this] { controller.save(); });
    on(kSaveAs, [this] { controller.saveAs(); });
    on(kRevert, [this] { controller.revert(); });
    on(kExport, [this] { exportAs(); });
    on(kPageSetup, [this] { pageSetup(); });
    on(kPrint, [this] { printDocument(); });
    on(kPrintPreview, [this] { printPreview(); });
    on(kClose, [this] { closeDocument(); });
    on(kQuit, [this] { window.close(); });

    // Edit
    // The clipboard, undo and select-all commands go to a focused text field first.
    auto textOr = [this](const char* slot, std::function<void()> fn) {
        return [this, slot, fn = std::move(fn)] {
            if (!forwardToTextInput(slot))
                fn();
        };
    };
    on(kUndo, textOr("undo", [this] { session.undo(); }));
    on(kRedo, textOr("redo", [this] { session.redo(); }));
    on(kCut, textOr("cut", [this] { cut(); }));
    on(kCopy, textOr("copy", [this] { copy(); }));
    on(kCopyAsImage, [this] { copyAsImage(); });
    on(kPaste, textOr("paste", [this] { paste(); }));
    edit(kDelete, [this] { return actions::deleteSelection(session, false); });
    edit(kDeleteAll, [this] { return actions::deleteSelection(session, true); });
    edit(kDuplicate, [this] { return actions::duplicateSelection(session); });
    on(kEditLabel, [this] { editLabel(); });
    on(kEditStyle, [this] { editStyle(); });
    on(kEditShape, [this] { editShape(); });
    on(kEditData, [this] { editData(); });
    on(kEditTooltip, [this] { editTooltip(); });
    on(kEditLink, [this] { editLink(); });
    on(kEditImage, [this] { editImage(); });
    on(kOpenLink, [this] { openLink(); });
    on(kCopyStyle, [this] {
        actions::copyStyle(session, styleBuffer);
        updateActionState(); // Paste Style becomes available
    });
    edit(kPasteStyle, [this] { return actions::pasteStyle(session, styleBuffer); });
    on(kSetDefaultStyle, [this] {
        actions::setAsDefaultStyle(session);
        updateActionState(); // Clear Default Style becomes available
    });
    on(kClearDefaultStyle, [this] {
        actions::clearDefaultStyle();
        updateActionState();
    });
    on(kInsertText, [this] { insertText(); });
    on(kInsertImage, [this] { insertImage(); });
    on(kInsertLink, [this] { insertLink(); });
    on(kInsertHorizontalLine, [this] { insertLine(false); });
    on(kInsertVerticalLine, [this] { insertLine(true); });
    on(kInsertFromText, [this] { insertAdvanced(ImportDialog::Kind::FromText); });
    on(kInsertCsv, [this] { insertAdvanced(ImportDialog::Kind::Csv); });
    on(kInsertMermaid, [this] { insertAdvanced(ImportDialog::Kind::Mermaid); });
    on(kSelectVertices, [this] { actions::selectVertices(session); });
    on(kSelectEdges, [this] { actions::selectEdges(session); });
    on(kSelectAll, textOr("selectAll", [this] { actions::selectAll(session); }));
    on(kSelectNone, [this] { actions::selectNone(session); });
    edit(kLockUnlock, [this] { return actions::toggleLock(session); });
    on(kFind, [this] { findReplace(); });

    // View
    for (std::size_t i = 0; i < kPanelCount; ++i) {
        const auto panel = static_cast<Panel>(i);
        if (kPanelActionIds[i] != nullptr)
            onToggle(kPanelActionIds[i], [this, panel](bool visible) { showPanel(panel, visible); });
    }
    onToggle(kPageView, [this](bool checked) { togglePageProperty(false, checked); });
    onToggle(kGrid, [this](bool checked) { togglePageProperty(true, checked); });
    onToggle(kGuides, [this](bool checked) { toggleGuides(checked); });
    onToggle(kDarkDiagram, [this](bool checked) { toggleDarkDiagram(checked); });
    onToggle(kFreehand, [this](bool checked) { canvas->setFreehandActive(checked); });
    on(kResetView, [this] { canvas->resetZoom(); });
    on(kZoomIn, [this] { canvas->zoomIn(); });
    on(kZoomOut, [this] { canvas->zoomOut(); });
    on(kFitWindow, [this] { canvas->fitWindow(); });
    on(kFitPage, [this] { canvas->fitPage(); });
    on(kFitSelection, [this] { canvas->fitSelection(); });
    on(kActualSize, [this] { canvas->setZoom(1); });
    onToggle(kFullscreen, [this](bool checked) { toggleFullscreen(checked); });

    // Arrange
    edit(kToFront, order(ops::ZOrder::ToFront));
    edit(kToBack, order(ops::ZOrder::ToBack));
    edit(kBringForward, order(ops::ZOrder::BringForward));
    edit(kSendBackward, order(ops::ZOrder::SendBackward));
    edit(kGroup, [this] { return actions::group(session); });
    edit(kUngroup, [this] { return actions::ungroup(session); });
    edit(kRemoveFromGroup, [this] { return actions::removeFromGroup(session); });
    on(kEnterGroup, [this] { enterGroup(); });
    on(kExitGroup, [this] { exitGroup(); });
    on(kHome, [this] { goHome(); });
    edit(kAlignLeft, align(actions::Align::Left));
    edit(kAlignCenter, align(actions::Align::Center));
    edit(kAlignRight, align(actions::Align::Right));
    edit(kAlignTop, align(actions::Align::Top));
    edit(kAlignMiddle, align(actions::Align::Middle));
    edit(kAlignBottom, align(actions::Align::Bottom));
    edit(kDistributeHorizontal, [this] { return actions::distribute(session, true); });
    edit(kDistributeVertical, [this] { return actions::distribute(session, false); });
    edit(kClearWaypoints, [this] { return actions::clearWaypoints(session); });
    edit(kFlipHorizontal, [this] { return actions::flip(session, actions::Flip::Horizontal); });
    edit(kFlipVertical, [this] { return actions::flip(session, actions::Flip::Vertical); });
    edit(kTurn, [this] { return actions::turn(session); });
    edit(kAutosize, [this] { return actions::autosize(session); });
    on(kEditGeometry, [this] { editGeometry(); });
    edit(kMergeCells, [this] { return actions::mergeCells(session); });
    edit(kUnmergeCells, [this] { return actions::unmergeCells(session); });
    using commands::ops::LayoutKind;
    edit(kLayoutVerticalTree, [this] { return actions::arrangeLayout(session, LayoutKind::VerticalTree); });
    edit(kLayoutHorizontalTree, [this] { return actions::arrangeLayout(session, LayoutKind::HorizontalTree); });
    edit(kLayoutRadialTree, [this] { return actions::arrangeLayout(session, LayoutKind::RadialTree); });
    edit(kLayoutOrgChart, [this] { return actions::arrangeLayout(session, LayoutKind::OrgChart); });
    edit(kLayoutCircle, [this] { return actions::arrangeLayout(session, LayoutKind::Circle); });
    edit(kLayoutHorizontalFlow, [this] { return actions::arrangeLayout(session, LayoutKind::HorizontalFlow); });
    edit(kLayoutVerticalFlow, [this] { return actions::arrangeLayout(session, LayoutKind::VerticalFlow); });
    edit(kLayoutOrganic, [this] { return actions::arrangeLayout(session, LayoutKind::Organic); });

    // Pages
    edit(kInsertPage, [this] { return pageTabs->insertPage(); });
    edit(kDuplicatePage, [this] { return pageTabs->duplicateCurrentPage(); });
    on(kRenamePage, [this] { renamePage(); });
    edit(kDeletePage, [this] { return pageTabs->removeCurrentPage(); });
    on(kPreviousPage, [this] { gotoPage(-1); });
    on(kNextPage, [this] { gotoPage(1); });

    // Extras / Help
    on(kEditDiagram, [this] { editDiagram(); });
    on(kPreferences, [this] { showPreferences(); });
    connectUnitActions();
    on(kKeyboardShortcuts, [this] { showShortcuts(); });
    on(kOpenLogFolder, [this] { openLogFolder(); });
    on(kAbout, [this] { showAbout(); });
}

// --- file ----------------------------------------------------------------------------

QString MainWindow::Impl::suggestedExportPath() const
{
    const QString directory = controller.filePath().isEmpty() ? QDir::homePath()
                                                              : QFileInfo(controller.filePath()).absolutePath();
    QString base = controller.filePath().isEmpty() ? controller.displayName()
                                                   : QFileInfo(controller.filePath()).completeBaseName();
    // "x.drawio.png" -> "x": strip the embedded-diagram double suffix.
    if (base.endsWith(QStringLiteral(".drawio"), Qt::CaseInsensitive))
        base.chop(7);
    return directory + QLatin1Char('/') + base + QStringLiteral(".png");
}

void MainWindow::Impl::exportAs()
{
    ExportDialog dialog(!session.selection().isEmpty(), suggestedExportPath(), &window);
    dialog.setDark(session.diagramDark());
    if (dialog.exec() != QDialog::Accepted)
        return;
    const ExportRequest request = dialog.request();
    QString error;
    if (!runExport(session.document(), session.currentPageIndex(), session.selection().cells(), request, &error)) {
        QMessageBox::critical(&window, tr("Export"), error.isEmpty() ? tr("The export failed.") : error);
        return;
    }
    window.statusBar()->showMessage(tr("Exported to %1").arg(QDir::toNativeSeparators(request.path)), 5000);
}

// Page Setup lives in the Format panel's Diagram tab, which shows when
// nothing is selected (draw.io's Format panel "Diagram" tab).
void MainWindow::Impl::pageSetup()
{
    session.selection().clear();
    showPanel(Panel::Format, true);
}

void MainWindow::Impl::printDocument()
{
    showPrintDialog(session.document(), &window);
}

void MainWindow::Impl::printPreview()
{
    showPrintPreview(session.document(), &window);
}

// draw.io Desktop closes the window here; Qraw is single-window, so the
// window stays and shows a blank diagram (after the unsaved-changes prompt).
void MainWindow::Impl::closeDocument()
{
    controller.newDocument();
}

// --- edit ----------------------------------------------------------------------------

void MainWindow::Impl::copy()
{
    if (QClipboard* clipboard = QGuiApplication::clipboard())
        actions::copySelection(session, *clipboard);
}

// Edit > Copy as Image: the selection, or the page, as an image on the clipboard.
void MainWindow::Impl::copyAsImage()
{
    QClipboard* clipboard = QGuiApplication::clipboard();
    if (clipboard != nullptr && actions::copyAsImage(session, *clipboard, window.devicePixelRatioF()))
        window.statusBar()->showMessage(tr("Copied as an image"), 3000);
}

void MainWindow::Impl::cut()
{
    if (QClipboard* clipboard = QGuiApplication::clipboard())
        actions::cutSelection(session, *clipboard);
}

void MainWindow::Impl::paste()
{
    const QClipboard* clipboard = QGuiApplication::clipboard();
    const QMimeData* mime = clipboard != nullptr ? clipboard->mimeData() : nullptr;
    if (mime != nullptr && !pasteImage(*mime))
        actions::paste(session, *mime);
}

void MainWindow::Impl::editLabel()
{
    if (!session.selection().isEmpty())
        canvas->editLabel(session.selection().first());
}

void MainWindow::Impl::editStyle()
{
    const core::Page* page = session.currentPage();
    const core::Cell* cell = page != nullptr ? page->findCell(session.selection().first()) : nullptr;
    if (cell == nullptr)
        return;
    bool accepted = false;
    const QString text = QInputDialog::getMultiLineText(&window, tr("Edit Style"), tr("Style:"),
                                                        QString::fromStdString(cell->styleText()), &accepted);
    if (!accepted)
        return;
    const std::vector<std::string> ids = session.selection().cells();
    const std::size_t pageIndex = session.currentPageIndex();
    session.edit(tr("Edit Style"), [&](commands::Transaction& tx) {
        return ops::setStyleText(tx, pageIndex, ids, text.trimmed().toStdString());
    });
}

void MainWindow::Impl::editShape()
{
    const core::Page* page = session.currentPage();
    const core::Cell* cell = page != nullptr ? page->findCell(session.selection().first()) : nullptr;
    if (cell == nullptr || !stencilXmlOf(*cell))
        return;
    EditShapeDialog dialog(*cell, &window);
    if (dialog.exec() != QDialog::Accepted)
        return;
    if (const std::optional<std::string> value = dialog.shapeValue())
        actions::setShape(session, *value);
}

// Arrange > Edit Geometry...: the dialog only opens with vertices selected (canEditGeometry).
void MainWindow::Impl::editGeometry()
{
    if (!actions::canEditGeometry(session))
        return;
    EditGeometryDialog dialog(actions::selectedVertexBounds(session), &window);
    if (dialog.exec() != QDialog::Accepted)
        return;
    actions::editGeometry(session, dialog.edit());
}

// File > New Library / Open Library: shape libraries live in the palette and
// are files of their own, not part of the document (D-008).
void MainWindow::Impl::newLibrary()
{
    const QString path = QFileDialog::getSaveFileName(&window, tr("New Library"), QString(), tr("Libraries (*.xml)"));
    if (path.isEmpty())
        return;
    QString error;
    if (!palette->newLibrary(path, &error))
        prompts.showError(tr("New Library"), error);
}

void MainWindow::Impl::openLibrary()
{
    const std::optional<QString> path = prompts.askOpenPath(tr("Libraries (*.xml);;All files (*)"));
    if (path)
        openLibraryFile(*path);
}

void MainWindow::Impl::openLibraryFile(const QString& path)
{
    QString error;
    if (palette->openLibrary(path, &error))
        showPanel(Panel::Palette, true);
    else
        prompts.showError(tr("Open Library"), error);
}

void MainWindow::Impl::findReplace()
{
    showPanel(Panel::Find, true);
    find->activate();
}

// --- view ------------------------------------------------------------------------------

// pageView and grid are page attributes, so toggling them is an undoable edit.
void MainWindow::Impl::togglePageProperty(bool grid, bool on)
{
    ops::PageProperties properties;
    if (grid)
        properties.grid = on;
    else
        properties.pageVisible = on;
    const QString name = grid ? tr("Grid") : tr("Page View");
    session.edit(name, [&](commands::Transaction& tx) {
        return ops::setPageProperties(tx, session.currentPageIndex(), properties);
    });
    updateActionState(); // a rejected edit restores the checkmark
}

void MainWindow::Impl::toggleGuides(bool on)
{
    canvas->setGuidesEnabled(on);
    settings.setGuidesEnabled(on);
}

void MainWindow::Impl::toggleDarkDiagram(bool on)
{
    session.setDiagramDark(on);
    settings.setDiagramDark(on);
}

void MainWindow::Impl::toggleFullscreen(bool on)
{
    if (on)
        window.showFullScreen();
    else
        window.showNormal();
}

void MainWindow::Impl::applyZoomText(const QString& text)
{
    if (text == tr("Fit")) {
        canvas->fitWindow();
        return;
    }
    if (const std::optional<double> zoom = parsePercent(text))
        canvas->setZoom(*zoom);
    else
        updateZoomDisplay(canvas->zoom()); // not a number: show the current zoom again
}

// --- arrange ---------------------------------------------------------------------------

void MainWindow::Impl::enterGroup()
{
    const core::Page* page = session.currentPage();
    if (page == nullptr || session.selection().size() != 1)
        return;
    const std::string id = session.selection().first();
    if (!hasChildren(*page, id))
        return;
    session.selection().clear();
    session.enterGroup(id);
}

void MainWindow::Impl::exitGroup()
{
    session.exitGroup();
    session.selection().clear();
}

// Actions.js 'home': back to the top of the diagram.
void MainWindow::Impl::goHome()
{
    for (std::string previous; session.enteredGroup() != previous && !session.enteredGroup().empty();) {
        previous = session.enteredGroup();
        session.exitGroup();
    }
    session.selection().clear();
}

// --- pages -----------------------------------------------------------------------------

void MainWindow::Impl::renamePage()
{
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return;
    bool accepted = false;
    const QString name = QInputDialog::getText(&window, tr("Rename Page"), tr("Page name:"), QLineEdit::Normal,
                                               QString::fromStdString(page->name()), &accepted);
    if (accepted && !name.trimmed().isEmpty())
        pageTabs->renamePage(session.currentPageIndex(), name.trimmed());
}

void MainWindow::Impl::gotoPage(int delta)
{
    const std::size_t count = session.document().pageCount();
    const long target = static_cast<long>(session.currentPageIndex()) + delta;
    if (target >= 0 && static_cast<std::size_t>(target) < count)
        session.setCurrentPage(static_cast<std::size_t>(target));
}

// --- extras / help ---------------------------------------------------------------------

// Extras > Edit Diagram...: the page's XML in a dialog; OK replaces the page content as one edit.
void MainWindow::Impl::editDiagram()
{
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return;
    session.flushPendingEdits();
    const std::string before = format::pageModelText(*page);
    EditDiagramDialog dialog(before, &window);
    if (dialog.exec() != QDialog::Accepted)
        return;
    std::optional<core::Page> model = dialog.takeModel();
    // Text that means the same model is not an edit (no undo step, no rebuild).
    if (!model || format::pageModelText(*model) == before)
        return;
    actions::replacePageModel(session, std::move(*model));
}

// View > Units: the five actions are exclusive and follow the preference, which the
// Format panel's Units box changes too.
void MainWindow::Impl::connectUnitActions()
{
    struct Entry {
        const char* id;
        MeasureUnit unit;
    };
    static constexpr Entry entries[] = {{kUnitPoints, MeasureUnit::Points},
                                        {kUnitInches, MeasureUnit::Inches},
                                        {kUnitMillimeters, MeasureUnit::Millimeters},
                                        {kUnitCentimeters, MeasureUnit::Centimeters},
                                        {kUnitMeters, MeasureUnit::Meters}};
    auto* group = new QActionGroup(&window);
    group->setExclusive(true);
    for (const Entry& entry : entries) {
        QAction* action = actions[entry.id];
        group->addAction(action);
        connect(action, &QAction::triggered, &window, [unit = entry.unit] { MeasureUnits::instance().setUnit(unit); });
    }
    const auto sync = [this] {
        const MeasureUnit current = MeasureUnits::instance().unit();
        for (const Entry& entry : entries)
            actions[entry.id]->setChecked(entry.unit == current);
    };
    connect(&MeasureUnits::instance(), &MeasureUnits::unitChanged, &window, sync);
    sync();
}

void MainWindow::Impl::showPreferences()
{
    PreferencesDialog dialog(settings, &window);
    connect(&dialog, &PreferencesDialog::settingsApplied, &window, [this] { window.applySettings(); });
    dialog.exec();
}

void MainWindow::Impl::showShortcuts()
{
    ShortcutsDialog dialog(actions, &window);
    dialog.exec();
}

void MainWindow::Impl::showAbout()
{
    const QString text =
        QStringLiteral("<h3>Qraw %1</h3>").arg(QCoreApplication::applicationVersion())
        + QStringLiteral("<p>%1</p>").arg(tr("A native Qt diagram editor that opens and saves draw.io files."))
        + QStringLiteral("<p>%1</p>").arg(tr("Released under the Apache License 2.0."))
        + QStringLiteral("<p>%1</p>").arg(tr("Shape and template data from draw.io (Apache-2.0 / CC-BY-4.0)."));
    QMessageBox::about(&window, tr("About Qraw"), text);
}

} // namespace qraw::ui
