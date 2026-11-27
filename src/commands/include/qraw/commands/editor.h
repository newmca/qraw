#pragma once

#include "qraw/commands/change_set.h"
#include "qraw/commands/result.h"
#include "qraw/commands/transaction.h"
#include "qraw/commands/undo_stack.h"
#include "qraw/core/document.h"

#include <functional>
#include <memory>
#include <string>
#include <vector>

namespace qraw::commands {

// Why observers are notified.
enum class ChangeOrigin {
    Edit,  // a committed Editor::edit()
    Undo,
    Redo,
    Reset, // Editor::resetDocument(): everything changed
};

// Receives one ChangeSet per committed transaction, undo or redo. Called
// after the model is consistent again, never in the middle of an edit.
// Observers may call const Editor accessors from the callback, but must not
// start an edit, undo or redo from it (the Editor refuses re-entrant
// undo/redo and returns failure for an edit started from a callback).
class DocumentObserver {
public:
    virtual ~DocumentObserver() = default;
    virtual void documentChanged(const ChangeSet& changes, ChangeOrigin origin) = 0;
    // Undo/redo availability, texts or the clean state may have changed.
    virtual void undoStateChanged() {}
};

// The facade the UI uses: owns the document and its undo history, runs
// operations as undoable steps and notifies observers. The UI never mutates
// core objects directly (ARCHITECTURE.md, "Mutation rules").
//
// Typical use:
//   editor.edit("Move", [&](Transaction& tx) {
//       return ops::moveCells(tx, page, ids, dx, dy);
//   }, "move:" + selectionKey);
//   ...on mouse release: editor.undoStack().mergeBreak();
class Editor {
public:
    using EditFunction = std::function<Result(Transaction&)>;

    explicit Editor(core::Document document = {});
    ~Editor();

    Editor(const Editor&) = delete;
    Editor& operator=(const Editor&) = delete;

    [[nodiscard]] const core::Document& document() const { return document_; }
    [[nodiscard]] UndoStack& undoStack() { return undoStack_; }
    [[nodiscard]] const UndoStack& undoStack() const { return undoStack_; }

    // Runs `fn` as one undoable step called `name`.
    //   * Outermost call: opens a Transaction, runs fn. If fn returns failure
    //     (or throws), everything is rolled back, nothing is recorded or
    //     notified, and the failure is returned (an exception is rethrown).
    //     On success with no applied commands, nothing is recorded or
    //     notified. Otherwise the transaction is committed, pushed onto the
    //     undo stack with `mergeKey` (see UndoStack::push), and observers get
    //     documentChanged(changes, Edit) followed by undoStateChanged().
    //   * Nested call (from inside another edit's fn): runs fn on the same
    //     transaction after taking a savepoint. On failure only the nested
    //     part is rolled back and the failure is returned to the caller;
    //     `name` and `mergeKey` are ignored. The outer step is still one
    //     undo entry.
    //   * Called from an observer callback: returns failure, changes nothing.
    Result edit(std::string name, const EditFunction& fn, std::string mergeKey = {});
    [[nodiscard]] bool isEditing() const { return transaction_ != nullptr; }

    // Undo / redo one step and notify (ChangeOrigin::Undo / Redo, then
    // undoStateChanged). Return false (no notification) when not possible,
    // while an edit is running, or from an observer callback.
    bool undo();
    bool redo();

    // Marks the current state as saved (UndoStack::setClean) and notifies
    // undoStateChanged.
    void markSaved();
    [[nodiscard]] bool isModified() const { return !undoStack_.isClean(); }

    // Replaces the document wholesale (open / revert). Clears the history
    // (the new state is clean) and notifies documentChanged
    // (ChangeOrigin::Reset) with a ChangeSet built by calling pageRemoved()
    // for every page id of the old document, then pageAdded() for every page
    // id of the new one, then documentChanged() -- so a page id present in
    // both folds into PageChanges::reset -- followed by undoStateChanged.
    // Refused (returns false) while an edit is running or from a callback.
    bool resetDocument(core::Document document);

    // Observers are not owned. Adding twice is a no-op; removing during a
    // notification is allowed (the removed observer gets no further calls).
    void addObserver(DocumentObserver* observer);
    void removeObserver(DocumentObserver* observer);

private:
    void notify(const ChangeSet& changes, ChangeOrigin origin);
    void notifyUndoState();

    core::Document document_;
    UndoStack undoStack_;
    std::vector<DocumentObserver*> observers_;
    Transaction* transaction_ = nullptr; // the open outermost transaction
    bool notifying_ = false;
};

} // namespace qraw::commands
