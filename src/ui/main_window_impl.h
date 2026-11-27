#pragma once
// Private implementation of MainWindow. Split by concern:
//   main_window.cpp          construction, panels, startup, close
//   main_window_menus.cpp    menus, toolbar, status bar, context menu
//   main_window_actions.cpp  behaviour of every action
//   main_window_state.cpp    enabled/checked state, title, status labels
//   main_window_insert.cpp   the Insert menu's behaviour
//   main_window_data.cpp     Edit Data, Edit Link, Edit Tooltip, Edit Image

#include "main_window_prompts.h"

#include "qraw/diag/session_marker.h"

#include "qraw/ui/action_registry.h"
#include "qraw/ui/app_settings.h"
#include "qraw/ui/document_controller.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/file_service.h"
#include "qraw/ui/import_dialog.h"
#include "qraw/ui/main_window.h"

#include <QObject>
#include <QPointF>

#include <array>
#include <memory>
#include <optional>
#include <string>
#include <vector>

class QComboBox;
class QDockWidget;
class QLabel;
class QMenu;
class QMimeData;
class QToolBar;

namespace qraw::ui {

// Widgets that edit text: the label editor, line edits (Format panel, Find,
// palette search, spin boxes, combo boxes) and plain text edits.
[[nodiscard]] bool isTextInput(const QObject* object);

class CanvasView;
class CellsPanel;
class FindPanel;
class FormatPanel;
class LayersPanel;
class OutlineView;
class PageTabs;
class PaletteDock;
class TableSizeAction;

// A QObject so that lupdate/moc give its strings the context
// "qraw::ui::MainWindow::Impl" and so that it can receive signals and filter
// the window's drag events.
struct MainWindow::Impl : QObject {
    Q_OBJECT

public:
    explicit Impl(MainWindow& owner);
    ~Impl() override;

    // The View menu toggle of each panel, in MainWindow::Panel order (Find/Replace
    // is a command, not a toggle: it has none).
    static constexpr std::size_t kPanelCount = 6;
    static constexpr std::array<const char*, kPanelCount> kPanelActionIds{
        action_id::kShapes, action_id::kFormatPanel, action_id::kLayers, action_id::kOutline, nullptr, action_id::kCells};

    // --- construction (main_window.cpp, main_window_menus.cpp) ---------------
    void buildCentral();
    void buildDocks();
    void buildMenus();
    void buildToolBar();
    void buildStatusBar();
    void connectActions();
    void connectSignals();
    void restoreWindowState();

    // --- panels ------------------------------------------------------------------
    [[nodiscard]] QDockWidget* dock(Panel panel) const { return docks[static_cast<std::size_t>(panel)]; }
    void showPanel(Panel panel, bool visible);
    void syncPanelActions();

    // --- startup / settings ------------------------------------------------------
    void offerRecovery();
    void recoverEntries(const std::vector<RecoveryEntry>& entries);
    void openLogFolder();
    bool offerWelcome();
    void openFiles(const QStringList& paths);

    // --- behaviour (main_window_actions.cpp) -------------------------------------
    void exportAs();
    void pageSetup();
    void printDocument();
    void printPreview();
    void closeDocument();
    void editLabel();
    void editStyle();
    void editShape();
    void editGeometry();
    // main_window_data.cpp
    void editData();
    void editLink();
    void editTooltip();
    void editImage();
    void openLink();
    // main_window_insert.cpp
    void insertText();
    void insertImage();
    void insertLink();
    void insertLine(bool vertical);
    void insertTable(int rows, int columns);
    void insertAdvanced(ImportDialog::Kind kind);
    bool pasteImage(const QMimeData& mime);
    // Where the Insert commands put the new cell.
    [[nodiscard]] QPointF insertPoint() const;
    void newLibrary();
    void openLibrary();
    void openLibraryFile(const QString& path);
    void copy();
    void copyAsImage();
    void cut();
    void paste();
    // While a text field of the window has the focus the clipboard, undo and
    // select-all actions belong to it; true when `slot` was forwarded.
    bool forwardToTextInput(const char* slot);
    // Menu and toolbar commands end a running label edit first, so that the
    // command sees (saves, exports, undoes, ...) the typed text.
    void commitLabelEdit(const QString& actionId);
    void findReplace();
    void togglePageProperty(bool grid, bool on);
    void toggleGuides(bool on);
    void toggleDarkDiagram(bool on);
    void toggleFullscreen(bool on);
    void enterGroup();
    void exitGroup();
    void goHome();
    void renamePage();
    void gotoPage(int delta);
    void showPreferences();
    void editDiagram();
    void connectUnitActions();
    void showShortcuts();
    void showAbout();
    void applyZoomText(const QString& text);

    // --- state (main_window_state.cpp) -------------------------------------------
    void updateActionState();
    void updateStatusBar();
    void updateTitle();
    void updateZoomDisplay(double zoom);
    void rebuildRecentMenu();
    [[nodiscard]] QString suggestedExportPath() const;

    // --- context menu (main_window_menus.cpp) ------------------------------------
    [[nodiscard]] std::unique_ptr<QMenu> buildContextMenu(bool onSelection);
    void showContextMenu(QPoint globalPos, QPointF scenePos);
    // Fills the Insert menu of the menu bar and of the background context menu.
    void fillInsertMenu(QMenu& menu);

    bool eventFilter(QObject* watched, QEvent* event) override;

    MainWindow& window;
    // Declaration order is destruction order reversed: the controller needs
    // the session and prompts, the session needs nothing.
    AppSettings settings;
    RecoveryStore store;
    DocumentSession session;
    ForwardingPrompts prompts;
    DocumentController controller;
    ActionRegistry actions;

    CanvasView* canvas = nullptr;
    PageTabs* pageTabs = nullptr;
    PaletteDock* palette = nullptr;
    FormatPanel* format = nullptr;
    LayersPanel* layers = nullptr;
    OutlineView* outline = nullptr;
    FindPanel* find = nullptr;
    CellsPanel* cells = nullptr;
    std::array<QDockWidget*, kPanelCount> docks{};

    TableSizeAction* tableAction = nullptr; // Insert > Table
    // The point of the canvas context menu while it runs (Insert in the background menu).
    std::optional<QPointF> menuInsertPoint;
    QMenu* recentMenu = nullptr;
    QToolBar* toolBar = nullptr;
    QComboBox* zoomCombo = nullptr;
    QLabel* zoomLabel = nullptr;
    QLabel* pageLabel = nullptr;
    QLabel* selectionLabel = nullptr;
    QLabel* positionLabel = nullptr;

    diag::PreviousRun previousRun; // asked once, at startup

    std::string styleBuffer; // Copy Style / Paste Style
};

} // namespace qraw::ui
