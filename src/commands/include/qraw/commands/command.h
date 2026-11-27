#pragma once

#include "qraw/commands/change_set.h"

namespace qraw::core {
class Document;
}

namespace qraw::commands {

// A reversible model mutation. Every change to a core::Document goes through
// a Command inside a Transaction (see ARCHITECTURE.md, "Mutation rules").
//
// Contract:
//   * apply() checks its preconditions first. When they do not hold it
//     returns false and leaves the document untouched.
//   * revert() is only called on the state apply() produced, and restores the
//     state before apply() exactly: equal model, therefore byte-identical
//     serialization (ARCHITECTURE.md, lossless model rule 6).
//   * apply() after revert() reproduces the same state again (redo).
//   * Both record what they touched in `changes`.
//
// Concrete commands are the primitives in primitives.h; higher-level
// operations (ops_*.h) are compositions of primitives, never Command
// subclasses of their own. This keeps undo exactness in one small place.
class Command {
public:
    Command() = default;
    Command(const Command&) = delete;
    Command& operator=(const Command&) = delete;
    virtual ~Command() = default;

    [[nodiscard]] virtual bool apply(core::Document& document, ChangeSet& changes) = 0;
    virtual void revert(core::Document& document, ChangeSet& changes) = 0;

    // Coalescing: absorbs `next`, which was applied directly after this
    // command, so that one revert() undoes both and one apply() redoes both.
    // Returns false (and changes nothing) when the two cannot be combined.
    // Used by the UndoStack when consecutive transactions share a merge key
    // (e.g. the steps of a drag). Must succeed whenever canMergeWith(next).
    virtual bool mergeWith(const Command& /*next*/) { return false; }
    // Side-effect-free check for mergeWith(next).
    [[nodiscard]] virtual bool canMergeWith(const Command& /*next*/) const { return false; }
};

} // namespace qraw::commands
