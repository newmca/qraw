// createStandardActions: every menu/toolbar command with its text, category
// and shortcuts (docs/design/ui.md section 6). Behaviour is connected by
// MainWindow (main_window_actions.cpp).
#include "qraw/ui/action_registry.h"

#include <QAction>
#include <QCoreApplication>
#include <QIcon>

namespace qraw::ui {

namespace {

constexpr const char* kContext = "qraw::ui::StandardActions";

using Keys = QList<QKeySequence>;

// Texts are translated at runtime through QCoreApplication::translate;
// QT_TRANSLATE_NOOP lets lupdate find them.
QString translated(const char* source)
{
    return QCoreApplication::translate(kContext, source);
}

// Platform key bindings of `standard` followed by `extra`, without duplicates.
Keys standard(QKeySequence::StandardKey key, const Keys& extra = {}, const Keys& taken = {})
{
    Keys result;
    for (const QKeySequence& sequence : QKeySequence::keyBindings(key)) {
        if (!taken.contains(sequence))
            result.append(sequence);
    }
    for (const QKeySequence& sequence : extra) {
        if (!result.contains(sequence))
            result.append(sequence);
    }
    return result;
}

constexpr Qt::KeyboardModifier kCtrl = Qt::ControlModifier;
constexpr Qt::KeyboardModifier kShift = Qt::ShiftModifier;
constexpr Qt::KeyboardModifier kAlt = Qt::AltModifier;

Keys keys(std::initializer_list<QKeyCombination> combinations)
{
    Keys result;
    for (const QKeyCombination& combination : combinations)
        result.append(QKeySequence(combination));
    return result;
}

QKeyCombination ctrl(Qt::Key key)
{
    return QKeyCombination(kCtrl, key);
}

QKeyCombination ctrlShift(Qt::Key key)
{
    return QKeyCombination(kCtrl | kShift, key);
}

struct Entry {
    const char* id;
    const char* text;
    Keys shortcuts;
};

void registerGroup(ActionRegistry& registry, const QString& category, const std::vector<Entry>& entries)
{
    for (const Entry& entry : entries)
        registry.add(QString::fromLatin1(entry.id), category, translated(entry.text), entry.shortcuts);
}

void registerFile(ActionRegistry& registry)
{
    registerGroup(registry, QCoreApplication::translate(kContext, "File"),
                  {
                      {action_id::kNew, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "&New"),
                       standard(QKeySequence::New)},
                      {action_id::kOpen, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "&Open"),
                       standard(QKeySequence::Open)},
                      {action_id::kOpenRecent, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Open &Recent"), {}},
                      {action_id::kNewLibrary, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "New Library"), {}},
                      {action_id::kOpenLibrary, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Open Library"), {}},
                      {action_id::kSave, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "&Save"),
                       standard(QKeySequence::Save)},
                      {action_id::kSaveAs, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Save &As"),
                       standard(QKeySequence::SaveAs, keys({ctrlShift(Qt::Key_S)}))},
                      {action_id::kRevert, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Revert"), {}},
                      {action_id::kExport, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "&Export As"), {}},
                      {action_id::kPageSetup, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Page Setup"), {}},
                      {action_id::kPrint, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "&Print"),
                       standard(QKeySequence::Print)},
                      {action_id::kPrintPreview, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Print Pre&view"),
                       {}},
                      {action_id::kClose, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "&Close"),
                       standard(QKeySequence::Close)},
                      {action_id::kQuit, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "&Quit"),
                       standard(QKeySequence::Quit)},
                  });
}

void registerEdit(ActionRegistry& registry)
{
    registerGroup(
        registry, QCoreApplication::translate(kContext, "Edit"),
        {
            {action_id::kUndo, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "&Undo"),
             standard(QKeySequence::Undo)},
            {action_id::kRedo, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "&Redo"),
             standard(QKeySequence::Redo, keys({ctrl(Qt::Key_Y)}))},
            {action_id::kCut, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Cu&t"), standard(QKeySequence::Cut)},
            {action_id::kCopy, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "&Copy"),
             standard(QKeySequence::Copy)},
            // draw.io's Ctrl+Alt+X (EditorUi.js:10659).
            {action_id::kCopyAsImage, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Copy as Image"),
             keys({QKeyCombination(kCtrl | kAlt, Qt::Key_X)})},
            {action_id::kPaste, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "&Paste"),
             standard(QKeySequence::Paste)},
            // QKeySequence::Delete would add Ctrl+D on X11, which is Duplicate here.
            {action_id::kDelete, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "&Delete"),
             keys({QKeyCombination(Qt::Key_Delete), QKeyCombination(Qt::Key_Backspace)})},
            {action_id::kDeleteAll, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Delete All"),
             keys({ctrl(Qt::Key_Delete), ctrl(Qt::Key_Backspace)})},
            {action_id::kDuplicate, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Duplicate"),
             keys({ctrl(Qt::Key_D)})},
            {action_id::kEditLabel, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Edit"),
             keys({QKeyCombination(Qt::Key_F2)})},
            {action_id::kEditStyle, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Edit Style"),
             keys({ctrl(Qt::Key_E)})},
            {action_id::kEditShape, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Edit Shape"), {}},
            // draw.io's Edit menu: Edit Data, Edit Tooltip, Edit Style, Edit, Edit Link, Open Link.
            {action_id::kEditData, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Edit Data"),
             keys({ctrl(Qt::Key_M)})},
            {action_id::kEditTooltip, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Edit Tooltip"),
             keys({QKeyCombination(kAlt | kShift, Qt::Key_T)})},
            {action_id::kEditLink, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Edit Link"),
             keys({QKeyCombination(kAlt | kShift, Qt::Key_L)})},
            {action_id::kEditImage, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Edit Image"), {}},
            {action_id::kOpenLink, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Open Link"), {}},
            {action_id::kCopyStyle, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Copy Style"),
             keys({QKeyCombination(kAlt | kShift, Qt::Key_Q)})},
            {action_id::kPasteStyle, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Paste Style"),
             keys({QKeyCombination(kAlt | kShift, Qt::Key_W)})},
            {action_id::kSetDefaultStyle, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Set as Default Style"),
             keys({ctrlShift(Qt::Key_D)})},
            {action_id::kClearDefaultStyle, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Clear Default Style"),
             keys({ctrlShift(Qt::Key_R)})},
            {action_id::kSelectVertices, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Select Vertices"),
             keys({ctrlShift(Qt::Key_I)})},
            {action_id::kSelectEdges, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Select Edges"),
             keys({ctrlShift(Qt::Key_E)})},
            {action_id::kSelectAll, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Select &All"),
             standard(QKeySequence::SelectAll)},
            {action_id::kSelectNone, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Select None"),
             keys({ctrlShift(Qt::Key_A)})},
            {action_id::kLockUnlock, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Lock/Unlock"),
             keys({ctrl(Qt::Key_L)})},
            {action_id::kFind, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "&Find/Replace"),
             standard(QKeySequence::Find)},
        });
}

void registerView(ActionRegistry& registry)
{
    registerGroup(
        registry, QCoreApplication::translate(kContext, "View"),
        {
            {action_id::kFormatPanel, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Format Panel"),
             keys({ctrlShift(Qt::Key_P)})},
            {action_id::kOutline, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Outline"),
             keys({ctrlShift(Qt::Key_O)})},
            {action_id::kLayers, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Layers"),
             keys({ctrlShift(Qt::Key_L)})},
            {action_id::kCells, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Cells"), {}},
            {action_id::kShapes, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Shapes"),
             keys({ctrlShift(Qt::Key_K)})},
            {action_id::kPageView, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Page View"), {}},
            {action_id::kGrid, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Grid"), keys({ctrlShift(Qt::Key_G)})},
            {action_id::kGuides, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Guides"), {}},
            {action_id::kResetView, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Reset View"),
             keys({QKeyCombination(Qt::Key_Home)})},
            {action_id::kZoomIn, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Zoom In"),
             standard(QKeySequence::ZoomIn, keys({ctrl(Qt::Key_Equal)}))},
            {action_id::kZoomOut, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Zoom Out"),
             standard(QKeySequence::ZoomOut)},
            {action_id::kFitWindow, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Fit Window"),
             keys({ctrlShift(Qt::Key_H)})},
            {action_id::kFitPage, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Fit Page"),
             keys({ctrl(Qt::Key_J)})},
            {action_id::kFitSelection, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Fit Selection"), {}},
            // draw.io's Ctrl+0 is "custom zoom"; Qraw resets to 100 %.
            {action_id::kActualSize, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Actual Size"),
             keys({ctrl(Qt::Key_0)})},
            {action_id::kFullscreen, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Full Screen"),
             // Plasma's theme binds Full Screen to Ctrl+Shift+F, which draw.io uses for To Front; F11 is kept
             // (KDE's own alternative) and the clash is not taken.
             standard(QKeySequence::FullScreen, keys({QKeyCombination(Qt::Key_F11)}), keys({ctrlShift(Qt::Key_F)}))},
            {action_id::kDiagramFollowTheme, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Follow Theme"), {}},
            {action_id::kDiagramLight, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Light"), {}},
            {action_id::kDiagramDark, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Dark"), {}},
            {action_id::kUnitPoints, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Points"), {}},
            {action_id::kUnitInches, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Inches"), {}},
            {action_id::kUnitMillimeters, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Millimeters"), {}},
            {action_id::kUnitCentimeters, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Centimeters"), {}},
            {action_id::kUnitMeters, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Meters"), {}},
        });
}

void registerArrange(ActionRegistry& registry)
{
    registerGroup(
        registry, QCoreApplication::translate(kContext, "Arrange"),
        {
            {action_id::kToFront, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "To Front"),
             keys({ctrlShift(Qt::Key_F)})},
            {action_id::kToBack, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "To Back"),
             keys({ctrlShift(Qt::Key_B)})},
            {action_id::kBringForward, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Bring Forward"),
             keys({QKeyCombination(kCtrl | kAlt | kShift, Qt::Key_F)})},
            {action_id::kSendBackward, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Send Backward"),
             keys({QKeyCombination(kCtrl | kAlt | kShift, Qt::Key_B)})},
            {action_id::kGroup, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Group"), keys({ctrl(Qt::Key_G)})},
            {action_id::kUngroup, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Ungroup"),
             keys({ctrlShift(Qt::Key_U)})},
            {action_id::kRemoveFromGroup, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Remove from Group"), {}},
            {action_id::kEnterGroup, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Enter Group"),
             keys({ctrlShift(Qt::Key_End)})},
            {action_id::kExitGroup, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Exit Group"),
             keys({ctrlShift(Qt::Key_Home)})},
            {action_id::kHome, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Home"),
             keys({QKeyCombination(kShift, Qt::Key_Home)})},
            {action_id::kAlignLeft, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Left"), {}},
            {action_id::kAlignCenter, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Center"), {}},
            {action_id::kAlignRight, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Right"), {}},
            {action_id::kAlignTop, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Top"), {}},
            {action_id::kAlignMiddle, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Middle"), {}},
            {action_id::kAlignBottom, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Bottom"), {}},
            {action_id::kDistributeHorizontal, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Horizontal"), {}},
            {action_id::kDistributeVertical, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Vertical"), {}},
            {action_id::kClearWaypoints, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Clear Waypoints"),
             keys({QKeyCombination(kAlt | kShift, Qt::Key_R)})},
            {action_id::kFlipHorizontal, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Flip Horizontal"), {}},
            {action_id::kFlipVertical, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Flip Vertical"), {}},
            {action_id::kTurn, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Rotate 90 Degrees"),
             keys({ctrl(Qt::Key_R)})},
            {action_id::kAutosize, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Autosize"),
             keys({ctrlShift(Qt::Key_Y)})},
            {action_id::kEditGeometry, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Edit Geometry"),
             keys({ctrlShift(Qt::Key_M)})},
            {action_id::kMergeCells, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Merge Cells"), {}},
            {action_id::kUnmergeCells, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Unmerge Cells"), {}},
            {action_id::kLayoutHorizontalTree, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Horizontal Tree"), {}},
            {action_id::kLayoutVerticalTree, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Vertical Tree"), {}},
            {action_id::kLayoutRadialTree, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Radial Tree"), {}},
            {action_id::kLayoutOrgChart, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Org Chart"), {}},
            {action_id::kLayoutCircle, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Circle"), {}},
            {action_id::kLayoutHorizontalFlow, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Horizontal Flow"), {}},
            {action_id::kLayoutVerticalFlow, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Vertical Flow"), {}},
            {action_id::kLayoutOrganic, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Organic"), {}},
            {action_id::kInsertPage, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Insert Page"), {}},
            {action_id::kDuplicatePage, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Duplicate Page"), {}},
            {action_id::kRenamePage, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Rename Page"), {}},
            {action_id::kDeletePage, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Delete Page"), {}},
            {action_id::kPreviousPage, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Previous Page"),
             keys({ctrl(Qt::Key_PageUp)})},
            {action_id::kNextPage, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Next Page"),
             keys({ctrl(Qt::Key_PageDown)})},
        });
}

// draw.io's Insert menu. Its single-key shortcuts (A, L) would be letters typed on the canvas.
void registerInsert(ActionRegistry& registry)
{
    registerGroup(registry, QCoreApplication::translate(kContext, "Insert"),
                  {
                      {action_id::kInsertText, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Text"), {}},
                      {action_id::kInsertImage, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Image"), {}},
                      {action_id::kInsertLink, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Link"), {}},
                      {action_id::kInsertHorizontalLine,
                       QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Horizontal Line"), {}},
                      {action_id::kInsertVerticalLine, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Vertical Line"),
                       {}},
                      // draw.io binds the bare X key to insertFreehand (EditorUi.js).
                      {action_id::kFreehand, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Freehand"),
                       keys({QKeyCombination(Qt::Key_X)})},
                      {action_id::kInsertFromText, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "From Text"), {}},
                      {action_id::kInsertCsv, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "CSV"), {}},
                      {action_id::kInsertMermaid, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Mermaid"), {}},
                  });
}

void registerExtrasAndHelp(ActionRegistry& registry)
{
    registerGroup(registry, QCoreApplication::translate(kContext, "Extras"),
                  {{action_id::kEditDiagram, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Edit Diagram"), {}},
                   {action_id::kPreferences, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Preferences"), {}}});
    registerGroup(registry, QCoreApplication::translate(kContext, "Help"),
                  {
                      {action_id::kKeyboardShortcuts,
                       QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Keyboard Shortcuts"),
                       keys({QKeyCombination(Qt::Key_F1)})},
                      {action_id::kOpenLogFolder, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "Open Log Folder"),
                       {}},
                      {action_id::kAbout, QT_TRANSLATE_NOOP("qraw::ui::StandardActions", "About Qraw"), {}},
                  });
}

// The commands that always open a dialog asking for more input; their text ends with the ellipsis (HIG).
// Print Preview, Find/Replace (a panel), Keyboard Shortcuts and About show something and ask nothing.
void markDialogActions(ActionRegistry& registry)
{
    for (const char* id : {action_id::kOpen, action_id::kNewLibrary, action_id::kOpenLibrary, action_id::kSaveAs, action_id::kExport, action_id::kPageSetup, action_id::kPrint, action_id::kEditStyle, action_id::kEditShape, action_id::kEditData, action_id::kEditTooltip, action_id::kEditLink, action_id::kEditImage, action_id::kEditGeometry, action_id::kEditDiagram, action_id::kPreferences, action_id::kRenamePage, action_id::kInsertImage, action_id::kInsertLink, action_id::kInsertFromText, action_id::kInsertCsv, action_id::kInsertMermaid}) {
        registry.markOpensDialog(QString::fromLatin1(id));
    }
}

void makeCheckable(ActionRegistry& registry)
{
    for (const char* id : {action_id::kFormatPanel, action_id::kOutline, action_id::kLayers, action_id::kCells, action_id::kShapes,
                           action_id::kPageView, action_id::kGrid, action_id::kGuides, action_id::kFreehand,
                           action_id::kFullscreen,
                           action_id::kDiagramFollowTheme, action_id::kDiagramLight, action_id::kDiagramDark,
                           action_id::kUnitPoints, action_id::kUnitInches, action_id::kUnitMillimeters,
                           action_id::kUnitCentimeters, action_id::kUnitMeters}) {
        if (QAction* action = registry[id])
            action->setCheckable(true);
    }
}

// Theme icons for the toolbar; absent themes leave the action text-only.
void assignIcons(ActionRegistry& registry)
{
    struct IconName {
        const char* id;
        const char* themeName;
    };
    for (const IconName& icon : {IconName{action_id::kUndo, "edit-undo"}, IconName{action_id::kRedo, "edit-redo"},
                                 IconName{action_id::kDelete, "edit-delete"}, IconName{action_id::kZoomIn, "zoom-in"},
                                 IconName{action_id::kZoomOut, "zoom-out"}, IconName{action_id::kToFront, "go-top"},
                                 IconName{action_id::kToBack, "go-bottom"}, IconName{action_id::kFreehand, "draw-freehand"}}) {
        if (QAction* action = registry[icon.id])
            action->setIcon(QIcon::fromTheme(QString::fromLatin1(icon.themeName)));
    }
}

} // namespace

void createStandardActions(ActionRegistry& registry)
{
    registerFile(registry);
    registerEdit(registry);
    registerView(registry);
    registerArrange(registry);
    registerInsert(registry);
    registerExtrasAndHelp(registry);
    markDialogActions(registry);
    makeCheckable(registry);
    assignIcons(registry);
}

} // namespace qraw::ui
