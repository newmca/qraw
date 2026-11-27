#pragma once
// The editor main window. Implemented in src/ui/main_window.cpp
// and src/ui/main_window_*.cpp. See docs/design/ui.md §1 and §6.

#include "qraw/diag/session_marker.h"
#include "qraw/ui/document_controller.h"

#include <QMainWindow>
#include <QStringList>

#include <memory>

class QDockWidget;

namespace qraw::ui {

class ActionRegistry;
class AppSettings;
class CanvasView;
class CellsPanel;
class DocumentSession;
class FindPanel;
class FormatPanel;
class LayersPanel;
class OutlineView;
class PageTabs;
class PaletteDock;

// Layout (draw.io desktop): menu bar File/Edit/View/Arrange/Extras/Help; a
// toolbar (undo, redo, delete, zoom, to front/back, fill/line color is
// out of scope); central widget = CanvasView above PageTabs; docks: palette
// (left), format (right), layers + outline + find (right, tabified/floating,
// hidden by default). Status bar: zoom %, page "n / m", selection
// ("3 shapes selected" / "Edge"), mouse position.
//
// Owns one DocumentSession + DocumentController (single document per
// window; File > New opens in the same window after maybeSave, like draw.io
// desktop's default). Implements DocumentPrompts with QMessageBox /
// QFileDialog (replaceable for tests: setPrompts).
class MainWindow : public QMainWindow {
    Q_OBJECT

public:
    enum class Panel { Palette, Format, Layers, Outline, Find, Cells };

    explicit MainWindow(QWidget* parent = nullptr);
    ~MainWindow() override;

    [[nodiscard]] DocumentSession& session() const;
    [[nodiscard]] DocumentController& controller() const;
    [[nodiscard]] CanvasView& canvas() const;
    [[nodiscard]] ActionRegistry& actions() const;
    [[nodiscard]] FormatPanel* formatPanel() const;
    [[nodiscard]] PaletteDock* paletteDock() const;
    [[nodiscard]] LayersPanel* layersPanel() const;
    [[nodiscard]] OutlineView* outlineView() const;
    [[nodiscard]] FindPanel* findPanel() const;
    [[nodiscard]] CellsPanel* cellsPanel() const;
    [[nodiscard]] PageTabs* pageTabs() const;
    [[nodiscard]] QDockWidget* dock(Panel panel) const;
    void showPanel(Panel panel, bool visible = true);

    // Replaces the dialog-based prompts (tests). Not owned; nullptr restores
    // the built-in QMessageBox/QFileDialog prompts.
    void setPrompts(DocumentPrompts* prompts);

    // What diag::collectPreviousRun found before this session began (main calls it before
    // startup()); the default is "the last session ended normally".
    void setPreviousRun(const diag::PreviousRun& previous);

    // Startup (called by main after show()): opens `files` (first in this
    // window; others are ignored with a status message -- single document
    // per window), then offers crash recovery (RecoveryDialog) when the last
    // session ended unexpectedly, or, if no file was given, when autosaved
    // documents are waiting; a recovered document takes this window while it
    // is empty, else a window of its own. Then shows the WelcomeDialog when
    // AppSettings::showWelcomeOnStart (only if no file was opened), else a
    // blank diagram.
    void startup(const QStringList& files);

    // Opens `files` in this window: the first one, the rest are ignored with
    // a status message. Used by startup() and for files the OS asks to open
    // (FileOpenHandler).
    void openFiles(const QStringList& files);

    // Applies theme, autosave interval and guides from AppSettings
    // (after Preferences).
    void applySettings();

protected:
    void closeEvent(QCloseEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
