// createStandardActions: every id registered, shortcuts of ui.md section 6
// present and unique.
#include "support/shell_env.h"
#include "support/test_support.h"

#include <QAction>
#include <QHash>

using namespace qraw::ui;

namespace {

namespace id = action_id;

const std::vector<const char*>& allIds()
{
    static const std::vector<const char*> ids{
        id::kNew,        id::kOpen,        id::kOpenRecent,        id::kSave,         id::kSaveAs,
        id::kNewLibrary, id::kOpenLibrary, id::kEditShape, id::kEditData, id::kEditTooltip, id::kEditLink, id::kEditImage, id::kOpenLink,
        id::kRevert,     id::kExport,      id::kPageSetup,         id::kPrint,        id::kPrintPreview,
        id::kClose,      id::kQuit,        id::kUndo,              id::kRedo,         id::kCut,
        id::kCopy,       id::kCopyAsImage, id::kPaste,       id::kDelete,            id::kDeleteAll,    id::kDuplicate,
        id::kEditLabel,  id::kEditStyle,   id::kCopyStyle,         id::kPasteStyle,   id::kSetDefaultStyle, id::kClearDefaultStyle, id::kSelectVertices,
        id::kSelectEdges, id::kSelectAll,  id::kSelectNone,        id::kLockUnlock,   id::kFind,
        id::kFormatPanel, id::kOutline,    id::kLayers, id::kCells,            id::kShapes,       id::kPageView,
        id::kGrid,       id::kGuides,      id::kDarkDiagram, id::kFreehand, id::kResetView,         id::kZoomIn,       id::kZoomOut,
        id::kFitWindow,  id::kFitPage,     id::kFitSelection,      id::kActualSize,   id::kFullscreen,
        id::kUnitPoints, id::kUnitInches, id::kUnitMillimeters, id::kUnitCentimeters, id::kUnitMeters,
        id::kToFront,    id::kToBack,      id::kBringForward,      id::kSendBackward, id::kGroup,
        id::kUngroup,    id::kRemoveFromGroup, id::kEnterGroup,    id::kExitGroup,    id::kHome,
        id::kAlignLeft,  id::kAlignCenter, id::kAlignRight,        id::kAlignTop,     id::kAlignMiddle,
        id::kAlignBottom, id::kDistributeHorizontal, id::kDistributeVertical, id::kClearWaypoints,
        id::kFlipHorizontal, id::kFlipVertical, id::kTurn, id::kAutosize, id::kEditGeometry,
        id::kMergeCells, id::kUnmergeCells,
        id::kLayoutVerticalTree, id::kLayoutHorizontalTree, id::kLayoutRadialTree, id::kLayoutOrgChart, id::kLayoutCircle,
        id::kLayoutHorizontalFlow, id::kLayoutVerticalFlow, id::kLayoutOrganic,
        id::kInsertText, id::kInsertImage, id::kInsertLink, id::kInsertHorizontalLine, id::kInsertVerticalLine,
        id::kInsertFromText, id::kInsertCsv, id::kInsertMermaid,
        id::kInsertPage, id::kDuplicatePage, id::kRenamePage,      id::kDeletePage,   id::kPreviousPage,
        id::kNextPage,   id::kEditDiagram, id::kPreferences, id::kKeyboardShortcuts, id::kOpenLogFolder, id::kAbout};
    return ids;
}

} // namespace

class TestShellActions : public QObject {
    Q_OBJECT

private slots:
    void registersEveryActionId()
    {
        ActionRegistry registry;
        createStandardActions(registry);
        QCOMPARE(registry.actions().size(), allIds().size());
        for (const char* actionId : allIds()) {
            QAction* action = registry[actionId];
            QVERIFY2(action != nullptr, actionId);
            QVERIFY2(!action->text().isEmpty(), actionId);
            QCOMPARE(action->objectName(), QString::fromLatin1(actionId));
        }
        QCOMPARE(registry.categories().size(), 7);
    }

    void followsTheDrawioMenuWording()
    {
        ActionRegistry registry;
        createStandardActions(registry);
        QCOMPARE(registry[id::kSaveAs]->text(), QStringLiteral("Save &As\u2026"));
        QCOMPARE(registry[id::kPrintPreview]->text(), QStringLiteral("Print Pre&view"));
        QCOMPARE(registry[id::kEditStyle]->text(), QStringLiteral("Edit Style\u2026"));
        QCOMPARE(registry[id::kAbout]->text(), QStringLiteral("About Qraw"));
    }

    void shortcutsArePresentAndUnique()
    {
        ActionRegistry registry;
        createStandardActions(registry);

        QHash<QKeySequence, QString> owner;
        for (const ActionInfo& info : registry.actions()) {
            for (const QKeySequence& sequence : info.action->shortcuts()) {
                QVERIFY2(!owner.contains(sequence),
                         qPrintable(info.id + QStringLiteral(" and ") + owner.value(sequence)
                                    + QStringLiteral(" share ") + sequence.toString()));
                owner.insert(sequence, info.id);
            }
        }

        const auto has = [&](const char* actionId, const QKeySequence& sequence) {
            return registry[actionId]->shortcuts().contains(sequence);
        };
        QVERIFY(has(id::kSave, QKeySequence(Qt::CTRL | Qt::Key_S)));
        QVERIFY(has(id::kSaveAs, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_S)));
        QVERIFY(has(id::kDuplicate, QKeySequence(Qt::CTRL | Qt::Key_D)));
        QVERIFY(has(id::kSetDefaultStyle, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_D)));
        QVERIFY(has(id::kClearDefaultStyle, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_R)));
        QVERIFY(has(id::kGroup, QKeySequence(Qt::CTRL | Qt::Key_G)));
        QVERIFY(has(id::kUngroup, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_U)));
        QVERIFY(has(id::kToFront, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_F)));
        QVERIFY(has(id::kToBack, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_B)));
        QVERIFY(has(id::kLockUnlock, QKeySequence(Qt::CTRL | Qt::Key_L)));
        QVERIFY(has(id::kFitPage, QKeySequence(Qt::CTRL | Qt::Key_J)));
        QVERIFY(has(id::kFitWindow, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_H)));
        QVERIFY(has(id::kDelete, QKeySequence(Qt::Key_Delete)));
        QVERIFY(has(id::kDelete, QKeySequence(Qt::Key_Backspace)));
        QVERIFY(has(id::kRedo, QKeySequence(Qt::CTRL | Qt::Key_Y)));
        QVERIFY(has(id::kZoomIn, QKeySequence(Qt::CTRL | Qt::Key_Equal)));
        // Arrange: draw.io's Ctrl+R (turn), Ctrl+Shift+Y (autosize), Ctrl+Shift+M (Edit Geometry).
        QVERIFY(has(id::kTurn, QKeySequence(Qt::CTRL | Qt::Key_R)));
        QVERIFY(has(id::kAutosize, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_Y)));
        QVERIFY(has(id::kEditGeometry, QKeySequence(Qt::CTRL | Qt::SHIFT | Qt::Key_M)));
        QCOMPARE(registry[id::kEditGeometry]->text(), QStringLiteral("Edit Geometry\u2026"));
        QVERIFY(registry[id::kOpenRecent]->shortcuts().isEmpty());
    }

    void togglesAreCheckable()
    {
        ActionRegistry registry;
        createStandardActions(registry);
        const QStringList toggles{QString::fromLatin1(id::kFormatPanel), QString::fromLatin1(id::kOutline),
                                  QString::fromLatin1(id::kLayers), QString::fromLatin1(id::kCells),      QString::fromLatin1(id::kShapes),
                                  QString::fromLatin1(id::kPageView),    QString::fromLatin1(id::kGrid),
                                  QString::fromLatin1(id::kGuides),      QString::fromLatin1(id::kFullscreen),
                                  QString::fromLatin1(id::kDarkDiagram), QString::fromLatin1(id::kFreehand),
                                  // View > Units: exclusive, one is checked
                                  QString::fromLatin1(id::kUnitPoints),  QString::fromLatin1(id::kUnitInches),
                                  QString::fromLatin1(id::kUnitMillimeters), QString::fromLatin1(id::kUnitCentimeters),
                                  QString::fromLatin1(id::kUnitMeters)};
        for (const ActionInfo& info : registry.actions())
            QCOMPARE(info.action->isCheckable(), toggles.contains(info.id));
    }
};

SHELL_TEST_MAIN(TestShellActions)
#include "tst_shell_actions.moc"
