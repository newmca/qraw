#include "qraw/ui/main_window.h"

#include "main_window_impl.h"

#include "qraw/diag/data_locations.h"
#include "qraw/ui/accessibility.h"
#include "qraw/ui/app_setup.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cells_panel.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/find_panel.h"
#include "qraw/ui/format_panel.h"
#include "qraw/ui/layers_panel.h"
#include "qraw/ui/logging.h"
#include "qraw/ui/outline_view.h"
#include "qraw/ui/page_tabs.h"
#include "qraw/ui/palette_dock.h"
#include "qraw/ui/palette_library.h"
#include "qraw/ui/recovery_dialog.h"
#include "qraw/ui/welcome_dialog.h"

#include <QApplication>
#include <QCloseEvent>
#include <QDesktopServices>
#include <QDir>
#include <QDockWidget>
#include <QFileInfo>
#include <QLabel>
#include <QLocale>
#include <QPushButton>
#include <QMessageBox>
#include <QStatusBar>
#include <QUrl>
#include <QVBoxLayout>

#include <utility>

namespace qraw::ui {

MainWindow::Impl::Impl(MainWindow& owner)
    : window(owner), session(FileService::blankDocument()), prompts(&owner),
      controller(session, prompts, settings, &store)
{
    createStandardActions(actions);
}

MainWindow::Impl::~Impl() = default;

// --- construction -----------------------------------------------------------

void MainWindow::Impl::buildCentral()
{
    session.setDiagramDark(settings.diagramDark());
    canvas = new CanvasView(session);
    canvas->setGuidesEnabled(settings.guidesEnabled());
    pageTabs = new PageTabs(session);

    auto* central = new QWidget(&window);
    auto* layout = new QVBoxLayout(central);
    layout->setContentsMargins(0, 0, 0, 0);
    layout->setSpacing(0);
    layout->addWidget(canvas, 1);
    layout->addWidget(pageTabs);
    window.setCentralWidget(central);
}

void MainWindow::Impl::buildDocks()
{
    palette = new PaletteDock(PaletteLibrary::builtin(), session, *canvas, settings);
    canvas->addDropHandler(palette);
    format = new FormatPanel(session);
    layers = new LayersPanel(session);
    outline = new OutlineView(*canvas);
    find = new FindPanel(session);
    cells = new CellsPanel(*canvas);

    struct DockSpec {
        Panel panel;
        const char* objectName;
        QString title;
        QWidget* widget;
        Qt::DockWidgetArea area;
        bool visible;
    };
    const DockSpec specs[] = {
        {Panel::Palette, "dock.palette", tr("Shapes"), palette, Qt::LeftDockWidgetArea, true},
        {Panel::Format, "dock.format", tr("Format"), format, Qt::RightDockWidgetArea, true},
        {Panel::Layers, "dock.layers", tr("Layers"), layers, Qt::RightDockWidgetArea, false},
        {Panel::Outline, "dock.outline", tr("Outline"), outline, Qt::RightDockWidgetArea, false},
        {Panel::Find, "dock.find", tr("Find/Replace"), find, Qt::RightDockWidgetArea, false},
        {Panel::Cells, "dock.cells", tr("Cells"), cells, Qt::RightDockWidgetArea, false},
    };
    for (const DockSpec& spec : specs) {
        auto* dock = new QDockWidget(spec.title, &window);
        dock->setObjectName(QString::fromLatin1(spec.objectName));
        dock->setWidget(spec.widget);
        window.addDockWidget(spec.area, dock);
        docks[static_cast<std::size_t>(spec.panel)] = dock;
    }
    // Layers, outline, find and cells share one tab group.
    window.tabifyDockWidget(dock(Panel::Layers), dock(Panel::Outline));
    window.tabifyDockWidget(dock(Panel::Outline), dock(Panel::Find));
    window.tabifyDockWidget(dock(Panel::Find), dock(Panel::Cells));
    for (const DockSpec& spec : specs)
        dock(spec.panel)->setVisible(spec.visible);
}

void MainWindow::Impl::restoreWindowState()
{
    const QByteArray geometry = settings.windowGeometry();
    if (geometry.isEmpty() || !window.restoreGeometry(geometry))
        window.resize(1280, 800);
    const QByteArray state = settings.windowState();
    if (!state.isEmpty())
        window.restoreState(state);
    syncPanelActions();
}

void MainWindow::Impl::connectSignals()
{
    connect(&session, &DocumentSession::undoStateChanged, this, &Impl::updateActionState);
    connect(&session, &DocumentSession::currentPageChanged, this, [this] {
        updateActionState();
        updateStatusBar();
    });
    connect(&session, &DocumentSession::documentChanged, this, [this] {
        updateActionState();
        updateStatusBar();
    });
    connect(&session, &DocumentSession::enteredGroupChanged, this, &Impl::updateActionState);
    connect(&session, &DocumentSession::currentLayerChanged, this, &Impl::updateActionState); // Insert needs an unlocked layer
    connect(&session, &DocumentSession::modifiedChanged, this, &Impl::updateTitle);
    connect(&session, &DocumentSession::statusMessage, this,
            [this](const QString& message) { window.statusBar()->showMessage(message, 5000); });
    connect(&session.selection(), &SelectionModel::selectionChanged, this, [this] {
        updateActionState();
        updateStatusBar();
    });

    connect(&controller, &DocumentController::titleChanged, this, [this] {
        updateTitle();
        updateActionState(); // Revert depends on the file path
    });
    connect(&controller, &DocumentController::recentFilesChanged, this, &Impl::rebuildRecentMenu);
    connect(&controller, &DocumentController::libraryFileRequested, this, &Impl::openLibraryFile);
    connect(palette, &PaletteDock::statusMessage, this,
            [this](const QString& message) { window.statusBar()->showMessage(message, 5000); });

    connect(canvas, &CanvasView::zoomChanged, this, &Impl::updateZoomDisplay);
    connect(canvas, &CanvasView::freehandActiveChanged, this, [this](bool on) { actions[action_id::kFreehand]->setChecked(on); });
    connect(canvas, &CanvasView::mouseScenePosChanged, this, [this](QPointF position) {
        positionLabel->setText(tr("%1, %2").arg(qRound(position.x())).arg(qRound(position.y())));
    });
    connect(canvas, &CanvasView::contextMenuRequested, this,
            [this](QPoint globalPos, QPointF scenePos) { showContextMenu(globalPos, scenePos); });

    connect(find, &FindPanel::revealCell, this, [this](const QString& cellId) {
        const std::string id = cellId.toStdString();
        session.selection().set({id});
        const QRectF bounds = canvas->diagramScene().boundsOf({id});
        if (!bounds.isNull())
            canvas->centerOn(bounds);
    });

    for (QDockWidget* dockWidget : docks)
        connect(dockWidget, &QDockWidget::visibilityChanged, this, &Impl::syncPanelActions);

    window.installEventFilter(this);
    canvas->viewport()->installEventFilter(this);
}

// --- panels -------------------------------------------------------------------

void MainWindow::Impl::showPanel(Panel panel, bool visible)
{
    QDockWidget* target = dock(panel);
    target->setVisible(visible);
    if (visible)
        target->raise();
    syncPanelActions();
}

// Checked state of the View menu's panel toggles follows the docks.
void MainWindow::Impl::syncPanelActions()
{
    for (std::size_t i = 0; i < kPanelCount; ++i) {
        QAction* action = kPanelActionIds[i] != nullptr ? actions[kPanelActionIds[i]] : nullptr;
        if (action != nullptr && docks[i] != nullptr)
            action->setChecked(docks[i]->isVisibleTo(&window));
    }
}

// --- startup ----------------------------------------------------------------------

// Crash recovery (ui.md section 7, D-049): one dialog for everything the last session left, when
// it ended unexpectedly (stale session marker, new crash report) or autosaved copies are waiting.
void MainWindow::Impl::offerRecovery()
{
    const std::vector<RecoveryEntry> entries = controller.pendingRecoveries();
    const diag::PreviousRun previous = std::exchange(previousRun, {});
    if (!diag::shouldOfferRecovery(previous, static_cast<int>(entries.size())))
        return;
    qCInfo(lcUiSession) << "offering recovery:" << entries.size() << "autosaved document(s),"
                        << previous.staleSessions << "stale session(s)," << previous.crashReports.size()
                        << "new crash report(s)";
    const bool hasReports = !previous.crashReports.isEmpty();
    RecoveryDialog dialog(entries, hasReports ? diag::crashDirectory() : QString(), &window);
    dialog.exec();
    // The reports were put in front of the user, whatever he chose: they are not "new" any more. The
    // autosaved copies stay on disk until he recovers or discards them.
    if (hasReports)
        settings.setCrashReportsSeenUntil(diag::newestCrashReportTime(diag::crashDirectory()));

    switch (dialog.choice()) {
    case RecoveryDialog::Choice::Recover:
        recoverEntries(dialog.selectedEntries());
        break;
    case RecoveryDialog::Choice::Discard:
        for (const RecoveryEntry& entry : dialog.selectedEntries())
            controller.discardRecovery(entry);
        break;
    case RecoveryDialog::Choice::Later:
        break;
    }
}

// Each recovered document is unsaved and modified, with its original path known. This window takes
// the first while it is still empty; every other one opens in a window of its own (a window holds
// one document).
void MainWindow::Impl::recoverEntries(const std::vector<RecoveryEntry>& entries)
{
    int extra = 0;
    for (const RecoveryEntry& entry : entries) {
        const bool windowIsEmpty = controller.filePath().isEmpty() && !controller.isModified();
        if (windowIsEmpty) {
            controller.restoreRecovery(entry);
            continue;
        }
        auto* other = new MainWindow;
        other->setAttribute(Qt::WA_DeleteOnClose);
        ++extra;
        other->move(window.pos() + QPoint(32 * extra, 32 * extra));
        other->show();
        if (!other->controller().restoreRecovery(entry))
            other->close();
    }
}

void MainWindow::Impl::openLogFolder()
{
    const QString folder = diag::logDirectory();
    QDir().mkpath(folder);
    if (!QDesktopServices::openUrl(QUrl::fromLocalFile(folder)))
        window.statusBar()->showMessage(tr("The log folder %1 could not be opened.").arg(folder), 5000);
}

// Returns true when the dialog already put a document into the window.
bool MainWindow::Impl::offerWelcome()
{
    WelcomeDialog dialog(settings, &window);
    dialog.exec();
    switch (dialog.choice()) {
    case WelcomeDialog::Choice::OpenDialog:
        return controller.open();
    case WelcomeDialog::Choice::OpenRecent:
        return controller.openPath(dialog.path());
    case WelcomeDialog::Choice::Template:
        return controller.newFromTemplate(dialog.path());
    case WelcomeDialog::Choice::Blank:
    case WelcomeDialog::Choice::None:
        break;
    }
    return false;
}

void MainWindow::Impl::openFiles(const QStringList& paths)
{
    if (paths.isEmpty())
        return;
    controller.openPath(paths.first());
    if (paths.size() > 1)
        window.statusBar()->showMessage(
            tr("Qraw has one document per window: %n other file(s) ignored.", nullptr,
               static_cast<int>(paths.size()) - 1),
            5000);
}

// --- MainWindow ------------------------------------------------------------------------

MainWindow::MainWindow(QWidget* parent) : QMainWindow(parent), d(std::make_unique<Impl>(*this))
{
    setObjectName(QStringLiteral("MainWindow"));
    setAcceptDrops(true);
    d->buildCentral();
    d->buildDocks();
    d->buildMenus();
    installAccessibility();
    new FocusToolTips(*this); // icon-only buttons show their tooltip on keyboard focus
    d->format->setStyleActions(
        {d->actions[action_id::kCopyStyle], d->actions[action_id::kPasteStyle], d->actions[action_id::kSetDefaultStyle]});
    d->format->setDataActions({d->actions[action_id::kEditData], d->actions[action_id::kEditTooltip],
                               d->actions[action_id::kEditLink], d->actions[action_id::kEditImage]});
    d->buildToolBar();
    d->buildStatusBar();
    d->connectActions();
    d->connectSignals();
    d->restoreWindowState();
    d->controller.setAutosaveInterval(d->settings.autosaveIntervalSeconds());
    d->updateTitle();
    d->updateActionState();
    d->updateStatusBar();
    d->updateZoomDisplay(d->canvas->zoom());
    d->rebuildRecentMenu();
}

MainWindow::~MainWindow()
{
    // The panels and the canvas refer to the session owned by `d`: destroy
    // them before it goes.
    d->canvas->removeDropHandler(d->palette);
    for (QDockWidget* dock : d->docks)
        delete dock;
    delete takeCentralWidget();
}

DocumentSession& MainWindow::session() const
{
    return d->session;
}

DocumentController& MainWindow::controller() const
{
    return d->controller;
}

CanvasView& MainWindow::canvas() const
{
    return *d->canvas;
}

ActionRegistry& MainWindow::actions() const
{
    return d->actions;
}

FormatPanel* MainWindow::formatPanel() const
{
    return d->format;
}

PaletteDock* MainWindow::paletteDock() const
{
    return d->palette;
}

LayersPanel* MainWindow::layersPanel() const
{
    return d->layers;
}

OutlineView* MainWindow::outlineView() const
{
    return d->outline;
}

FindPanel* MainWindow::findPanel() const
{
    return d->find;
}

CellsPanel* MainWindow::cellsPanel() const
{
    return d->cells;
}

PageTabs* MainWindow::pageTabs() const
{
    return d->pageTabs;
}

QDockWidget* MainWindow::dock(Panel panel) const
{
    return d->dock(panel);
}

void MainWindow::showPanel(Panel panel, bool visible)
{
    d->showPanel(panel, visible);
}

void MainWindow::setPrompts(DocumentPrompts* prompts)
{
    d->prompts.setOverride(prompts);
}

void MainWindow::setPreviousRun(const diag::PreviousRun& previous)
{
    d->previousRun = previous;
}

void MainWindow::startup(const QStringList& files)
{
    qCDebug(lcUi) << "startup with" << files.size() << "file(s)";
    if (!files.isEmpty())
        d->openFiles(files);
    // Started with a file (a double click): the question about old autosaved copies waits for a plain
    // start, but a session that really crashed is reported now.
    if (files.isEmpty() || d->previousRun.endedUnexpectedly())
        d->offerRecovery();
    if (files.isEmpty() && d->controller.filePath().isEmpty() && !d->controller.isModified()
        && d->settings.showWelcomeOnStart())
        d->offerWelcome();
}

void MainWindow::openFiles(const QStringList& files)
{
    d->openFiles(files);
}

void MainWindow::applySettings()
{
    if (auto* application = qobject_cast<QApplication*>(QCoreApplication::instance()))
        applyTheme(*application, d->settings.theme());
    d->controller.setAutosaveInterval(d->settings.autosaveIntervalSeconds());
    d->canvas->setGuidesEnabled(d->settings.guidesEnabled());
    if (QAction* guides = d->actions[action_id::kGuides])
        guides->setChecked(d->canvas->guidesEnabled());
}

void MainWindow::closeEvent(QCloseEvent* event)
{
    if (!d->controller.maybeSave()) {
        event->ignore();
        return;
    }
    d->controller.closeSession();
    d->settings.setWindowGeometry(saveGeometry());
    d->settings.setWindowState(saveState());
    event->accept();
}

} // namespace qraw::ui
