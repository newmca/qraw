// Menus (docs/design/ui.md section 6), toolbar, status bar and the canvas
// context menu.
#include "main_window_impl.h"

#include "qraw/ui/canvas_view.h"
#include "qraw/ui/edit_actions.h"
#include "qraw/ui/insert_actions.h"
#include "qraw/ui/table_size_picker.h"

#include <QAbstractSpinBox>
#include <QAction>
#include <QComboBox>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QLabel>
#include <QLineEdit>
#include <QKeyEvent>
#include <QKeySequenceEdit>
#include <QMenu>
#include <QMenuBar>
#include <QMimeData>
#include <QPlainTextEdit>
#include <QStatusBar>
#include <QTextEdit>
#include <QToolBar>
#include <QUrl>

#include <initializer_list>

namespace qraw::ui {

namespace {

// Kinds (a)-(c) of isTextInput without the focus proxy rule.
bool isOwnTextInput(const QObject* object)
{
    if (qobject_cast<const QLineEdit*>(object) != nullptr || qobject_cast<const QTextEdit*>(object) != nullptr
        || qobject_cast<const QPlainTextEdit*>(object) != nullptr
        || qobject_cast<const QAbstractSpinBox*>(object) != nullptr
        || qobject_cast<const QKeySequenceEdit*>(object) != nullptr)
        return true;
    if (const auto* combo = qobject_cast<const QComboBox*>(object); combo != nullptr && combo->isEditable())
        return true;
    return object->property("qrawTextInput").toBool();
}

} // namespace

// A text input is one of:
//   (a) QLineEdit, QTextEdit, QPlainTextEdit, QAbstractSpinBox or QKeySequenceEdit;
//   (b) a QComboBox that is editable;
//   (c) any QWidget with the dynamic property "qrawTextInput" set to true.
//       This is the opt-in for custom editors that edit text;
//   (d) a QWidget whose focusProxy() is an input of kind (a) to (c).
// Do not test Qt::WA_InputMethodEnabled: QGraphicsView toggles it, which would
// swallow the canvas shortcuts.
bool isTextInput(const QObject* object)
{
    if (isOwnTextInput(object))
        return true;
    const auto* widget = qobject_cast<const QWidget*>(object);
    return widget != nullptr && widget->focusProxy() != nullptr && isOwnTextInput(widget->focusProxy());
}

namespace {

// A null entry is a separator.
void fillMenu(QMenu& menu, const ActionRegistry& actions, std::initializer_list<const char*> ids)
{
    for (const char* id : ids) {
        if (id == nullptr)
            menu.addSeparator();
        else if (QAction* action = actions.action(QString::fromLatin1(id)))
            menu.addAction(action);
    }
}

// The shortcuts that keep working while a text field has the focus: file
// commands, Find, view and window commands. Every other action changes the
// selection or the document (Ctrl+D duplicate, Ctrl+Backspace delete all,
// Ctrl+G group, ...) and must not fire while the user edits text.
bool survivesTextEditing(const QString& id)
{
    static const QStringList global{
        QString::fromLatin1(action_id::kNew),          QString::fromLatin1(action_id::kOpen),
        QString::fromLatin1(action_id::kSave),         QString::fromLatin1(action_id::kSaveAs),
        QString::fromLatin1(action_id::kExport),       QString::fromLatin1(action_id::kPrint),
        QString::fromLatin1(action_id::kClose),        QString::fromLatin1(action_id::kQuit),
        QString::fromLatin1(action_id::kFind),         QString::fromLatin1(action_id::kFormatPanel),
        QString::fromLatin1(action_id::kOutline),      QString::fromLatin1(action_id::kLayers),
        QString::fromLatin1(action_id::kShapes),     QString::fromLatin1(action_id::kCells),
        QString::fromLatin1(action_id::kZoomIn),       QString::fromLatin1(action_id::kZoomOut),
        QString::fromLatin1(action_id::kFitWindow),    QString::fromLatin1(action_id::kFitPage),
        QString::fromLatin1(action_id::kActualSize),   QString::fromLatin1(action_id::kFullscreen),
        QString::fromLatin1(action_id::kKeyboardShortcuts)};
    return global.contains(id);
}

// Installed on the application: while a text field of the main window has the
// focus it owns the keys of the document actions, so that for example
// Ctrl+Backspace deletes a word and not the selected cells.
class TextShortcutGuard final : public QObject {
public:
    TextShortcutGuard(const QWidget& window, const ActionRegistry& actions)
        : QObject(const_cast<QWidget*>(&window)), window_(window), actions_(actions)
    {
        qApp->installEventFilter(this);
    }

    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() != QEvent::ShortcutOverride)
            return false;
        const auto* widget = qobject_cast<const QWidget*>(watched);
        if (widget == nullptr || widget->window() != &window_ || !isTextInput(widget))
            return false;
        const QKeySequence pressed(static_cast<const QKeyEvent*>(event)->keyCombination());
        for (const ActionInfo& info : actions_.actions()) {
            if (!survivesTextEditing(info.id) && info.action->shortcuts().contains(pressed)) {
                event->accept();
                return true;
            }
        }
        return false;
    }

private:
    const QWidget& window_;
    const ActionRegistry& actions_;
};

QMenu* addSubmenu(QMenu& parent, const QString& title, const ActionRegistry& actions,
                  std::initializer_list<const char*> ids)
{
    QMenu* submenu = parent.addMenu(title);
    fillMenu(*submenu, actions, ids);
    return submenu;
}

QMenu* addTopMenu(QMenuBar& bar, const QString& title, const QString& objectName)
{
    QMenu* menu = bar.addMenu(title);
    menu->setObjectName(objectName);
    return menu;
}

// File drops that the window opens (ui.md section 7).
bool isOpenableFile(const QUrl& url)
{
    static const QStringList suffixes{QStringLiteral("drawio"), QStringLiteral("xml"), QStringLiteral("png"),
                                      QStringLiteral("svg"), QStringLiteral("pdf")};
    return url.isLocalFile() && suffixes.contains(url.toLocalFile().section(QLatin1Char('.'), -1).toLower());
}

QStringList openableFiles(const QMimeData& mime)
{
    QStringList paths;
    for (const QUrl& url : mime.urls()) {
        if (isOpenableFile(url))
            paths.append(url.toLocalFile());
    }
    return paths;
}

} // namespace

using namespace action_id;

void MainWindow::Impl::buildMenus()
{
    new TextShortcutGuard(window, actions);
    QMenuBar* bar = window.menuBar();

    QMenu* file = addTopMenu(*bar, tr("&File"), QStringLiteral("menu.file"));
    fillMenu(*file, actions, {kNew, kOpen, kOpenRecent, nullptr, kNewLibrary, kOpenLibrary, nullptr, kSave, kSaveAs, kRevert, nullptr, kExport, nullptr, kPageSetup,
                              kPrint, kPrintPreview, nullptr, kClose, kQuit});
    // The Open Recent holder action carries the submenu (rebuilt on demand).
    recentMenu = new QMenu(&window);
    recentMenu->setObjectName(QStringLiteral("menu.recent"));
    actions[kOpenRecent]->setMenu(recentMenu);

    QMenu* edit = addTopMenu(*bar, tr("&Edit"), QStringLiteral("menu.edit"));
    fillMenu(*edit, actions, {kUndo,        kRedo,        nullptr,        kCut,          kCopy,
                              kCopyAsImage, kPaste,       kDelete,      kDeleteAll,     nullptr,       kDuplicate,
                              nullptr,      kEditData,    kEditTooltip,   nullptr,       kEditLabel,    kEditStyle,
                              kEditShape,   nullptr,      kEditLink,      kEditImage,    kOpenLink,     nullptr,
                              kCopyStyle,   kPasteStyle,  kSetDefaultStyle, kClearDefaultStyle,
                              nullptr,      kSelectVertices, kSelectEdges, kSelectAll,   kSelectNone,
                              nullptr,      kLockUnlock,  nullptr,        kFind});

    QMenu* view = addTopMenu(*bar, tr("&View"), QStringLiteral("menu.view"));
    fillMenu(*view, actions, {kFormatPanel, kOutline, kLayers, kCells, kShapes, nullptr, kPageView, kGrid, kGuides});
    // View > Diagram Appearance: the canvas follows the theme, or is pinned light or dark.
    QMenu* appearanceMenu = addSubmenu(*view, tr("Diagram Appearance"), actions,
                                       {kDiagramFollowTheme, kDiagramLight, kDiagramDark});
    appearanceMenu->setObjectName(QStringLiteral("menu.diagramAppearance"));
    fillMenu(*view, actions, {nullptr, kResetView, kZoomIn, kZoomOut, kFitWindow, kFitPage, kFitSelection, kActualSize, nullptr});
    // View > Units: the unit of the length fields (a preference, the diagram stays in pixels).
    QMenu* unitMenu = addSubmenu(*view, tr("Units"), actions,
                                 {kUnitPoints, kUnitInches, kUnitMillimeters, kUnitCentimeters, kUnitMeters});
    unitMenu->setObjectName(QStringLiteral("menu.units"));
    view->addSeparator();
    fillMenu(*view, actions, {kFullscreen});

    QMenu* arrange = addTopMenu(*bar, tr("&Arrange"), QStringLiteral("menu.arrange"));
    fillMenu(*arrange, actions, {kToFront, kToBack, kBringForward, kSendBackward, nullptr});
    addSubmenu(*arrange, tr("Direction"), actions, {kFlipHorizontal, kFlipVertical, nullptr, kTurn});
    addSubmenu(*arrange, tr("Align"), actions,
               {kAlignLeft, kAlignCenter, kAlignRight, nullptr, kAlignTop, kAlignMiddle, kAlignBottom});
    addSubmenu(*arrange, tr("Distribute"), actions, {kDistributeHorizontal, kDistributeVertical});
    arrange->addSeparator();
    fillMenu(*arrange, actions, {kAutosize, kEditGeometry, nullptr});
    addSubmenu(*arrange, tr("Layout"), actions,
               {kLayoutHorizontalFlow, kLayoutVerticalFlow, nullptr, kLayoutHorizontalTree, kLayoutVerticalTree,
                kLayoutRadialTree, kLayoutOrgChart, nullptr, kLayoutOrganic, kLayoutCircle});
    // Merge Cells and Unmerge Cells of a range of table cells (draw.io has them in the Format panel and the
    // popup menu only; a menu entry lets the keyboard reach them too).
    addSubmenu(*arrange, tr("Table"), actions, {kMergeCells, kUnmergeCells});
    addSubmenu(*arrange, tr("Navigation"), actions, {kHome, kExitGroup, kEnterGroup});
    arrange->addSeparator();
    fillMenu(*arrange, actions, {kGroup, kUngroup, kRemoveFromGroup, nullptr, kClearWaypoints, nullptr});
    addSubmenu(*arrange, tr("Pages"), actions,
               {kInsertPage, kDuplicatePage, kRenamePage, kDeletePage, nullptr, kPreviousPage, kNextPage});

    tableAction = new TableSizeAction(this);
    connect(tableAction, &TableSizeAction::tableChosen, this, &Impl::insertTable);
    fillInsertMenu(*addTopMenu(*bar, tr("&Insert"), QStringLiteral("menu.insert")));

    QMenu* extras = addTopMenu(*bar, tr("E&xtras"), QStringLiteral("menu.extras"));
    fillMenu(*extras, actions, {kEditDiagram, nullptr, kPreferences});

    QMenu* help = addTopMenu(*bar, tr("&Help"), QStringLiteral("menu.help"));
    fillMenu(*help, actions, {kKeyboardShortcuts, kOpenLogFolder, nullptr, kAbout});
}

// draw.io's Insert menu: text, image, link, lines, Table and Advanced (the diagrams made from text).
void MainWindow::Impl::fillInsertMenu(QMenu& menu)
{
    fillMenu(menu, actions, {kInsertText, kInsertImage, kInsertLink, nullptr, kInsertHorizontalLine, kInsertVerticalLine,
                       nullptr, kFreehand});
    QMenu* table = menu.addMenu(tr("Table"));
    table->setObjectName(QStringLiteral("menu.insertTable"));
    table->menuAction()->setObjectName(QStringLiteral("action.insertTableMenu"));
    table->addAction(tableAction);
    menu.addSeparator();
    QMenu* advanced = menu.addMenu(tr("Advanced"));
    advanced->setObjectName(QStringLiteral("menu.insertAdvanced"));
    advanced->menuAction()->setObjectName(QStringLiteral("action.insertAdvancedMenu"));
    fillMenu(*advanced, actions, {kInsertFromText, kInsertCsv, kInsertMermaid});
    // A submenu with only a disabled picker would still open: the entry follows the picker.
    connect(&menu, &QMenu::aboutToShow, table, [this, table, advanced] {
        const bool canInsert = actions::canInsert(session);
        table->menuAction()->setEnabled(canInsert);
        advanced->menuAction()->setEnabled(canInsert);
    });
}

void MainWindow::Impl::buildToolBar()
{
    toolBar = window.addToolBar(tr("Main"));
    toolBar->setObjectName(QStringLiteral("toolbar.main"));
    toolBar->setMovable(false);

    zoomCombo = new QComboBox(toolBar);
    zoomCombo->setObjectName(QStringLiteral("zoomCombo"));
    zoomCombo->setEditable(true);
    zoomCombo->setInsertPolicy(QComboBox::NoInsert);
    zoomCombo->setToolTip(tr("Zoom"));
    zoomCombo->setAccessibleName(tr("Zoom"));
    zoomCombo->lineEdit()->setMaxLength(12);
    for (const int percent : {25, 50, 75, 100, 125, 150, 200, 300, 400})
        zoomCombo->addItem(tr("%1 %").arg(percent));
    zoomCombo->addItem(tr("Fit"));
    connect(zoomCombo, &QComboBox::textActivated, this, &Impl::applyZoomText);

    toolBar->addAction(actions[kUndo]);
    toolBar->addAction(actions[kRedo]);
    toolBar->addSeparator();
    toolBar->addAction(actions[kDelete]);
    toolBar->addSeparator();
    toolBar->addAction(actions[kZoomOut]);
    toolBar->addWidget(zoomCombo);
    toolBar->addAction(actions[kZoomIn]);
    toolBar->addSeparator();
    toolBar->addAction(actions[kToFront]);
    toolBar->addAction(actions[kToBack]);
    toolBar->addSeparator();
    actions[kFreehand]->setToolTip(tr("Freehand: drag on the canvas to draw a stroke (X)"));
    toolBar->addAction(actions[kFreehand]);
}

void MainWindow::Impl::buildStatusBar()
{
    auto addLabel = [this](const QString& objectName, int minimumWidth) {
        auto* label = new QLabel(window.statusBar());
        label->setObjectName(objectName);
        label->setMinimumWidth(minimumWidth);
        window.statusBar()->addPermanentWidget(label);
        return label;
    };
    selectionLabel = addLabel(QStringLiteral("status.selection"), 130);
    positionLabel = addLabel(QStringLiteral("status.position"), 90);
    pageLabel = addLabel(QStringLiteral("status.page"), 90);
    zoomLabel = addLabel(QStringLiteral("status.zoom"), 60);
}

// The popup menu of the canvas: cut/copy/paste... on a selection, paste and
// selection commands on blank canvas (Menus.js 'createPopupMenu').
std::unique_ptr<QMenu> MainWindow::Impl::buildContextMenu(bool onSelection)
{
    auto menu = std::make_unique<QMenu>(&window);
    if (onSelection) {
        fillMenu(*menu, actions,
                 {kCut, kCopy, kCopyAsImage, kPaste, kDelete, nullptr, kDuplicate, nullptr, kEditLabel, kEditStyle, kEditData, kEditLink,
                  kLockUnlock, nullptr, kCopyStyle, kPasteStyle, nullptr, kToFront, kToBack, nullptr, kTurn, kAutosize,
                  kEditGeometry, nullptr, kGroup, kUngroup});
        // Menus.js:792: Merge Cells for a range of table cells, else Unmerge Cells for a merged cell.
        if (actions[kMergeCells]->isEnabled())
            menu->addAction(actions[kMergeCells]);
        else if (actions[kUnmergeCells]->isEnabled())
            menu->addAction(actions[kUnmergeCells]);
        // Menus.addPopupMenuCellEditItems: Edit Image where the cell has an image, and the link to open.
        if (actions[kEditImage]->isEnabled())
            menu->addAction(actions[kEditImage]);
        if (actions[kOpenLink]->isEnabled())
            menu->addAction(actions[kOpenLink]);
        // Menus.addPopupMenuStyleItems: with exactly one cell selected.
        if (actions::canSetDefaultStyle(session))
            menu->addAction(actions[kSetDefaultStyle]);
    } else {
        fillMenu(*menu, actions, {kPaste, nullptr, kSelectAll, kSelectVertices, kSelectEdges, nullptr});
        // Menus.js addPopupMenuItems: Insert, at the point of the menu.
        QMenu* insert = menu->addMenu(tr("Insert"));
        insert->setObjectName(QStringLiteral("menu.contextInsert"));
        fillInsertMenu(*insert);
        if (actions[kClearDefaultStyle]->isEnabled()) {
            menu->addSeparator();
            menu->addAction(actions[kClearDefaultStyle]);
        }
    }
    return menu;
}

void MainWindow::Impl::showContextMenu(QPoint globalPos, QPointF scenePos)
{
    updateActionState(); // the defaults may have changed since the last selection change
    const std::unique_ptr<QMenu> menu = buildContextMenu(!session.selection().isEmpty());
    menuInsertPoint = scenePos; // the commands run inside exec()
    menu->exec(globalPos);
    menuInsertPoint.reset();
}

// Opens files dropped on the window or on the canvas. Cell drops from the
// palette carry no file URLs and stay with the canvas.
bool MainWindow::Impl::eventFilter(QObject* watched, QEvent* event)
{
    switch (event->type()) {
    case QEvent::DragEnter:
    case QEvent::DragMove: {
        auto* drag = static_cast<QDropEvent*>(event);
        if (openableFiles(*drag->mimeData()).isEmpty())
            return false;
        drag->acceptProposedAction();
        return true;
    }
    case QEvent::Drop: {
        auto* drop = static_cast<QDropEvent*>(event);
        const QStringList paths = openableFiles(*drop->mimeData());
        if (paths.isEmpty())
            return false;
        drop->acceptProposedAction();
        // After the drag ends: opening may ask to save the current document.
        QMetaObject::invokeMethod(this, [this, paths] { openFiles(paths); }, Qt::QueuedConnection);
        return true;
    }
    default:
        return QObject::eventFilter(watched, event);
    }
}

} // namespace qraw::ui
