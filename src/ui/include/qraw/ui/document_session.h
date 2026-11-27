#pragma once
// The open document as the UI sees it. Implemented in
// src/ui/document_session.cpp. See docs/design/ui.md §2.

#include "qraw/commands/change_set.h"
#include "qraw/commands/editor.h"
#include "qraw/commands/result.h"
#include "qraw/core/document.h"
#include "qraw/render/page_view.h"
#include "qraw/ui/selection_model.h"

#include <QObject>
#include <QString>

#include <cstddef>
#include <memory>
#include <string>

namespace qraw::ui {

// Owns the commands::Editor (model + undo history) of one open document and
// the UI state that belongs to it: the current page, the selection on that
// page, the current layer and the entered group. Every panel, the canvas and
// the controller observe this object; none of them touches core objects
// directly (ARCHITECTURE.md "Mutation rules"). GUI thread only. No file I/O here:
// that is DocumentController + FileService.
class DocumentSession : public QObject {
    Q_OBJECT

public:
    explicit DocumentSession(core::Document document = {}, QObject* parent = nullptr);
    ~DocumentSession() override;

    DocumentSession(const DocumentSession&) = delete;
    DocumentSession& operator=(const DocumentSession&) = delete;

    [[nodiscard]] commands::Editor& editor() { return editor_; }
    [[nodiscard]] const commands::Editor& editor() const { return editor_; }
    [[nodiscard]] const core::Document& document() const { return editor_.document(); }

    // --- pages ------------------------------------------------------------
    // The page shown in the canvas. Always < pageCount() unless the document
    // has no pages (then 0 and currentPage() is nullptr).
    [[nodiscard]] std::size_t currentPageIndex() const { return currentPage_; }
    [[nodiscard]] const core::Page* currentPage() const;
    // Clamps the index, clears the selection, the entered group and the
    // current layer, then emits currentPageChanged (only when it changed).
    void setCurrentPage(std::size_t index);

    // --- selection, layer, group -----------------------------------------
    [[nodiscard]] SelectionModel& selection() { return selection_; }
    [[nodiscard]] const SelectionModel& selection() const { return selection_; }

    // The layer new cells go into (draw.io's defaultParent when no group is
    // entered). Falls back to the first layer of the current page when unset
    // or no longer present ("" when the page has no layer).
    [[nodiscard]] std::string currentLayer() const;
    void setCurrentLayer(const std::string& layerId);

    // draw.io's currentRoot (Graph.enterGroup/exitGroup, Actions.js:576-577):
    // while a group is entered, selection and insertion are scoped to it.
    // "" when none. enterGroup ignores ids that are not vertices of the
    // current page. Both clear the selection and emit enteredGroupChanged.
    [[nodiscard]] const std::string& enteredGroup() const { return enteredGroup_; }
    void enterGroup(const std::string& id);
    void exitGroup();
    // The parent for inserted cells: the entered group, else currentLayer().
    [[nodiscard]] std::string insertionParent() const;

    // --- editing ----------------------------------------------------------
    // Runs `fn` as one undoable step (commands::Editor::edit). On success:
    // selects Result.cells when non-empty (ids on the current page) and
    // switches to Result.page when set. On failure emits statusMessage with
    // the error and changes nothing. Returns the Result.
    commands::Result edit(const QString& name, const commands::Editor::EditFunction& fn,
                          std::string mergeKey = {});
    // Ends a coalescing sequence (UndoStack::mergeBreak): format-panel
    // spin boxes and any other caller of edit() with a merge key call this
    // when the interaction is over (editingFinished, mouse release).
    void endMerge();

    // Undo/redo one step. Afterwards the cells that the step added or changed
    // and that still exist on the current page are selected (draw.io
    // EditorUi undo listener: graph.setSelectionCells(getSelectionCellsForChanges)).
    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const;
    [[nodiscard]] bool canRedo() const;
    [[nodiscard]] QString undoText() const; // the step name, "" when none
    [[nodiscard]] QString redoText() const;

    // Asks the open editors (the inline label editor) to commit what the user
    // typed. Call before anything saves, exports, replaces or closes the
    // document, so that it sees the typed text.
    void flushPendingEdits() { emit pendingEditsRequested(); }

    [[nodiscard]] bool isModified() const { return editor_.isModified(); }
    // Editor::markSaved; emits modifiedChanged when it changed.
    void markSaved();
    // Replaces the document (open, revert, new): history cleared, page 0,
    // selection/layer/group cleared. Emits documentChanged(Reset),
    // currentPageChanged and modifiedChanged as applicable.
    void resetDocument(core::Document document);

    // --- rendering --------------------------------------------------------
    // The render context for this document: built-in registries, light
    // theme, images resolved relative to baseDirectory().
    [[nodiscard]] const render::RenderContext& renderContext() const { return renderContext_; }
    // renderContext() with the page variables of the current page (%page%,
    // %pagenumber%, %pagecount%); the canvas builds its views with it.
    [[nodiscard]] render::RenderContext pageRenderContext() const;
    // The scheme the diagram is shown in (View > Dark Diagram): the dark scheme
    // follows the page's adaptiveColors. A viewing choice of the user, not part
    // of the document. Emits renderContextChanged.
    [[nodiscard]] bool diagramDark() const { return renderContext_.theme.dark; }
    void setDiagramDark(bool dark);
    [[nodiscard]] const QString& baseDirectory() const { return baseDirectory_; }
    void setBaseDirectory(const QString& directory);

signals:
    // The theme of renderContext() changed: every view of the document is built again.
    void renderContextChanged();
    void pendingEditsRequested(); // see flushPendingEdits(): an open editor connects and commits
    // Re-emitted commands::DocumentObserver::documentChanged. Emitted after
    // the session pruned the selection and clamped the current page, so
    // receivers see consistent state.
    void documentChanged(const qraw::commands::ChangeSet& changes, qraw::commands::ChangeOrigin origin);
    void undoStateChanged();
    void modifiedChanged(bool modified);
    void currentPageChanged(std::size_t index);
    void currentLayerChanged();
    void enteredGroupChanged();
    // A short user-visible message for the status bar (already translated
    // where it comes from the UI; operation errors are English, as
    // commands::Result::error).
    void statusMessage(const QString& message);

private:
    class Observer; // forwards commands::DocumentObserver calls to the slots below
    void onDocumentChanged(const commands::ChangeSet& changes, commands::ChangeOrigin origin);
    void onUndoStateChanged();
    void pruneSelection();
    void selectChangedCells(const commands::ChangeSet& changes);

    commands::Editor editor_;
    SelectionModel selection_;
    std::size_t currentPage_ = 0;
    std::string currentLayer_;
    std::string enteredGroup_;
    QString baseDirectory_;
    render::RenderContext renderContext_;
    bool lastModified_ = false;
    std::unique_ptr<Observer> observer_;
};

} // namespace qraw::ui
