#include "qraw/commands/editor.h"

#include <algorithm>
#include <utility>

namespace qraw::commands {

namespace {

// Sets a flag for the lifetime of the guard, also when an observer throws.
class FlagGuard {
public:
    explicit FlagGuard(bool& flag) : flag_(flag) { flag_ = true; }
    ~FlagGuard() { flag_ = false; }
    FlagGuard(const FlagGuard&) = delete;
    FlagGuard& operator=(const FlagGuard&) = delete;

private:
    bool& flag_;
};

} // namespace

Editor::Editor(core::Document document) : document_(std::move(document)) {}

Editor::~Editor() = default;

Result Editor::edit(std::string name, const EditFunction& fn, std::string mergeKey)
{
    if (notifying_)
        return Result::failure("edit started from an observer callback");

    if (transaction_) {
        // Nested: only this part is undone if it fails; the outer edit decides how to go on.
        const std::size_t savepoint = transaction_->savepoint();
        Result r;
        try {
            r = fn(*transaction_);
        } catch (...) {
            transaction_->rollbackTo(savepoint);
            throw;
        }
        if (!r.ok)
            transaction_->rollbackTo(savepoint);
        return r;
    }

    Transaction tx(document_);
    transaction_ = &tx;
    Result r;
    try {
        r = fn(tx);
    } catch (...) {
        transaction_ = nullptr;
        tx.rollback();
        throw;
    }
    transaction_ = nullptr;

    if (!r.ok) {
        tx.rollback();
        return r;
    }
    if (tx.empty())
        return r;

    Transaction::Committed committed = tx.commit();
    undoStack_.push(std::move(name), std::move(committed.group), std::move(mergeKey));
    notify(committed.changes, ChangeOrigin::Edit);
    notifyUndoState();
    return r;
}

bool Editor::undo()
{
    if (isEditing() || notifying_ || !undoStack_.canUndo())
        return false;
    ChangeSet changes;
    undoStack_.undo(document_, changes);
    notify(changes, ChangeOrigin::Undo);
    notifyUndoState();
    return true;
}

bool Editor::redo()
{
    if (isEditing() || notifying_ || !undoStack_.canRedo())
        return false;
    ChangeSet changes;
    if (!undoStack_.redo(document_, changes)) {
        notifyUndoState(); // the redo tail was dropped
        return false;
    }
    notify(changes, ChangeOrigin::Redo);
    notifyUndoState();
    return true;
}

void Editor::markSaved()
{
    undoStack_.setClean();
    notifyUndoState();
}

bool Editor::resetDocument(core::Document document)
{
    if (isEditing() || notifying_)
        return false;

    ChangeSet changes;
    for (const core::Page* page : std::as_const(document_).pages())
        changes.pageRemoved(page->id());
    document_ = std::move(document);
    for (const core::Page* page : std::as_const(document_).pages())
        changes.pageAdded(page->id());
    changes.documentChanged();

    undoStack_.clear();
    notify(changes, ChangeOrigin::Reset);
    notifyUndoState();
    return true;
}

void Editor::addObserver(DocumentObserver* observer)
{
    if (observer && std::find(observers_.begin(), observers_.end(), observer) == observers_.end())
        observers_.push_back(observer);
}

void Editor::removeObserver(DocumentObserver* observer)
{
    observers_.erase(std::remove(observers_.begin(), observers_.end(), observer), observers_.end());
}

void Editor::notify(const ChangeSet& changes, ChangeOrigin origin)
{
    FlagGuard guard(notifying_);
    const std::vector<DocumentObserver*> snapshot = observers_;
    for (DocumentObserver* observer : snapshot) {
        if (std::find(observers_.begin(), observers_.end(), observer) != observers_.end())
            observer->documentChanged(changes, origin);
    }
}

void Editor::notifyUndoState()
{
    FlagGuard guard(notifying_);
    const std::vector<DocumentObserver*> snapshot = observers_;
    for (DocumentObserver* observer : snapshot) {
        if (std::find(observers_.begin(), observers_.end(), observer) != observers_.end())
            observer->undoStateChanged();
    }
}

} // namespace qraw::commands
