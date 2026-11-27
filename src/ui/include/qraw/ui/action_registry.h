#pragma once
// Every menu/toolbar command as a QAction with a stable id, a category and
// draw.io's shortcuts (docs/design/ui.md §6). The registry
// itself is implemented in src/ui/action_registry.cpp;
// createStandardActions (texts, shortcuts, icons) is in
// src/ui/standard_actions.cpp and MainWindow connects the behaviour.

#include <QKeySequence>
#include <QList>
#include <QObject>
#include <QString>
#include <QStringList>

#include <vector>

class QAction;

namespace qraw::ui {

// Action ids: draw.io's action names (js/grapheditor/Actions.js) where one
// exists, so the shortcut table can cite them.
namespace action_id {
// File
inline constexpr char kNew[] = "new";
inline constexpr char kOpen[] = "open";
inline constexpr char kOpenRecent[] = "openRecent"; // submenu holder (no shortcut)
inline constexpr char kNewLibrary[] = "newLibrary";
inline constexpr char kOpenLibrary[] = "openLibrary";
inline constexpr char kSave[] = "save";
inline constexpr char kSaveAs[] = "saveAs";
inline constexpr char kRevert[] = "revert";
inline constexpr char kExport[] = "exportAs";
inline constexpr char kPageSetup[] = "pageSetup";
inline constexpr char kPrint[] = "print";
inline constexpr char kPrintPreview[] = "printPreview";
inline constexpr char kClose[] = "close";
inline constexpr char kQuit[] = "quit";
// Edit
inline constexpr char kUndo[] = "undo";
inline constexpr char kRedo[] = "redo";
inline constexpr char kCut[] = "cut";
inline constexpr char kCopy[] = "copy";
inline constexpr char kCopyAsImage[] = "copyAsImage";
inline constexpr char kPaste[] = "paste";
inline constexpr char kDelete[] = "delete";
inline constexpr char kDeleteAll[] = "deleteAll";
inline constexpr char kDuplicate[] = "duplicate";
inline constexpr char kEditLabel[] = "edit";
inline constexpr char kEditStyle[] = "editStyle";
inline constexpr char kEditShape[] = "editShape";
inline constexpr char kEditData[] = "editData";
inline constexpr char kEditTooltip[] = "editTooltip";
inline constexpr char kEditLink[] = "editLink";
inline constexpr char kEditImage[] = "editImage";
inline constexpr char kOpenLink[] = "openLink";
inline constexpr char kCopyStyle[] = "copyStyle";
inline constexpr char kPasteStyle[] = "pasteStyle";
inline constexpr char kSetDefaultStyle[] = "setAsDefaultStyle";
inline constexpr char kClearDefaultStyle[] = "clearDefaultStyle";
inline constexpr char kSelectVertices[] = "selectVertices";
inline constexpr char kSelectEdges[] = "selectEdges";
inline constexpr char kSelectAll[] = "selectAll";
inline constexpr char kSelectNone[] = "selectNone";
inline constexpr char kLockUnlock[] = "lockUnlock";
inline constexpr char kFind[] = "findReplace";
// View
inline constexpr char kFormatPanel[] = "format";
inline constexpr char kOutline[] = "outline";
inline constexpr char kLayers[] = "layers";
inline constexpr char kCells[] = "cells";
inline constexpr char kShapes[] = "toggleShapes";
inline constexpr char kPageView[] = "pageView";
inline constexpr char kGrid[] = "grid";
inline constexpr char kGuides[] = "guides";
inline constexpr char kDarkDiagram[] = "darkDiagram";
inline constexpr char kResetView[] = "resetView";
inline constexpr char kZoomIn[] = "zoomIn";
inline constexpr char kZoomOut[] = "zoomOut";
inline constexpr char kFitWindow[] = "fitWindow";
inline constexpr char kFitPage[] = "fitPage";
inline constexpr char kFitSelection[] = "fitSelection";
inline constexpr char kActualSize[] = "actualSize";
inline constexpr char kFullscreen[] = "fullscreen";
// View > Units (exclusive, checkable)
inline constexpr char kUnitPoints[] = "unitPoints";
inline constexpr char kUnitInches[] = "unitInches";
inline constexpr char kUnitMillimeters[] = "unitMillimeters";
inline constexpr char kUnitCentimeters[] = "unitCentimeters";
inline constexpr char kUnitMeters[] = "unitMeters";
// Arrange
inline constexpr char kToFront[] = "toFront";
inline constexpr char kToBack[] = "toBack";
inline constexpr char kBringForward[] = "bringForward";
inline constexpr char kSendBackward[] = "sendBackward";
inline constexpr char kGroup[] = "group";
inline constexpr char kUngroup[] = "ungroup";
inline constexpr char kRemoveFromGroup[] = "removeFromGroup";
inline constexpr char kEnterGroup[] = "enterGroup";
inline constexpr char kExitGroup[] = "exitGroup";
inline constexpr char kHome[] = "home";
inline constexpr char kAlignLeft[] = "alignLeft";
inline constexpr char kAlignCenter[] = "alignCenter";
inline constexpr char kAlignRight[] = "alignRight";
inline constexpr char kAlignTop[] = "alignTop";
inline constexpr char kAlignMiddle[] = "alignMiddle";
inline constexpr char kAlignBottom[] = "alignBottom";
inline constexpr char kDistributeHorizontal[] = "distributeHorizontal";
inline constexpr char kDistributeVertical[] = "distributeVertical";
inline constexpr char kClearWaypoints[] = "clearWaypoints";
inline constexpr char kFlipHorizontal[] = "flipHorizontal";
inline constexpr char kFlipVertical[] = "flipVertical";
inline constexpr char kTurn[] = "turn";
inline constexpr char kAutosize[] = "autosize";
inline constexpr char kEditGeometry[] = "editGeometry";
inline constexpr char kMergeCells[] = "mergeCells";
inline constexpr char kUnmergeCells[] = "unmergeCells";
inline constexpr char kLayoutVerticalTree[] = "layoutVerticalTree";
inline constexpr char kLayoutHorizontalTree[] = "layoutHorizontalTree";
inline constexpr char kLayoutRadialTree[] = "layoutRadialTree";
inline constexpr char kLayoutOrgChart[] = "layoutOrgChart";
inline constexpr char kLayoutCircle[] = "layoutCircle";
inline constexpr char kLayoutHorizontalFlow[] = "layoutHorizontalFlow";
inline constexpr char kLayoutVerticalFlow[] = "layoutVerticalFlow";
inline constexpr char kLayoutOrganic[] = "layoutOrganic";
// Insert (the Table item is a picker, not an action of the registry)
inline constexpr char kInsertText[] = "insertText";
inline constexpr char kInsertImage[] = "insertImage";
inline constexpr char kInsertLink[] = "insertLink";
inline constexpr char kInsertHorizontalLine[] = "insertHorizontalLine";
inline constexpr char kFreehand[] = "freehand";
inline constexpr char kInsertVerticalLine[] = "insertVerticalLine";
// Insert > Advanced
inline constexpr char kInsertFromText[] = "insertFromText";
inline constexpr char kInsertCsv[] = "insertCsv";
inline constexpr char kInsertMermaid[] = "insertMermaid";
// Pages
inline constexpr char kInsertPage[] = "insertPage";
inline constexpr char kDuplicatePage[] = "duplicatePage";
inline constexpr char kRenamePage[] = "renamePage";
inline constexpr char kDeletePage[] = "deletePage";
inline constexpr char kPreviousPage[] = "previousPage";
inline constexpr char kNextPage[] = "nextPage";
// Extras / Help
inline constexpr char kEditDiagram[] = "editDiagram";
inline constexpr char kPreferences[] = "preferences";
inline constexpr char kKeyboardShortcuts[] = "keyboardShortcuts";
inline constexpr char kOpenLogFolder[] = "openLogFolder";
inline constexpr char kAbout[] = "about";
} // namespace action_id

struct ActionInfo {
    QString id;
    QString category; // translated menu title ("File", "Edit", ...)
    QAction* action;
    // The action always opens a dialog that asks for more input: its text ends with the ellipsis (KDE HIG, D-050).
    bool opensDialog = false;
};

// The character that ends the text of a command that opens a dialog: the typographic ellipsis U+2026,
// which is what the KDE HIG asks for ("real typographic characters"), never three dots (D-050).
inline constexpr char16_t kEllipsis = u'\u2026';
// `text` with the ellipsis at the end, once ("&Open" -> "&Open…").
[[nodiscard]] QString withEllipsis(const QString& text);

// A key or gesture handled by the canvas itself (not a QAction), listed in
// the shortcuts dialog.
struct CanvasKey {
    QString keys;        // already in native text, e.g. "Shift+Arrow"
    QString description; // translated
};

class ActionRegistry : public QObject {
    Q_OBJECT

public:
    explicit ActionRegistry(QObject* parent = nullptr);
    ~ActionRegistry() override;

    // Creates a QAction (parent: the registry, objectName: id, text: `text`,
    // shortcuts: `shortcuts`, shortcut context Qt::WindowShortcut) and
    // records it under `category`. An id registered twice returns the
    // existing action unchanged.
    QAction* add(const QString& id, const QString& category, const QString& text,
                 const QList<QKeySequence>& shortcuts = {});
    // Marks the action as one that always opens a dialog asking for more input, and ends its text with the
    // ellipsis (the texts to translate carry none: the rule lives here, in one place). False for unknown ids.
    bool markOpensDialog(const QString& id);
    // nullptr for unknown ids.
    [[nodiscard]] QAction* action(const QString& id) const;
    [[nodiscard]] QAction* operator[](const char* id) const { return action(QString::fromLatin1(id)); }
    // In registration order.
    [[nodiscard]] const std::vector<ActionInfo>& actions() const { return actions_; }
    [[nodiscard]] QStringList categories() const; // distinct, in first-registration order

    // The canvas keys of docs/design/ui.md §6 (translated).
    [[nodiscard]] static std::vector<CanvasKey> canvasKeys();

private:
    std::vector<ActionInfo> actions_;
};

// Registers every action_id above with its translated text, category,
// draw.io shortcut(s) (ui.md §6) and checkable state where it is a toggle
// (format, outline, layers, toggleShapes, pageView, grid, guides,
// fullscreen). Behaviour is not connected here.
void createStandardActions(ActionRegistry& registry);

} // namespace qraw::ui
