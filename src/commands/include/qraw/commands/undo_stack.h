#pragma once

#include "qraw/commands/change_set.h"
#include "qraw/commands/command_group.h"

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::core {
class Document;
}

namespace qraw::commands {

// Linear undo history of committed transactions (one entry = one undo step,
// a named macro). Modeled on mxUndoManager
// (mxgraph/src/util/mxUndoManager.js) plus QUndoStack-style clean state and
// merging. The stack does not own the document; the Editor passes it in.
//
// Indices: entries [0, index()) are applied, [index(), count()) are undone
// and can be redone.
//
// Invariants:
//   * 0 <= index() <= count() <= limit() (when limit() > 0).
//   * cleanIndex() is in [0, count()] or nullopt (the clean state is no
//     longer reachable). isClean() == (cleanIndex() == index()).
//   * push() discards the redo tail [index(), count()) first. If the clean
//     index was in the discarded part, it becomes nullopt.
class UndoStack {
public:
    using Clock = std::chrono::steady_clock;

    // draw.io's history size (mxUndoManager default: 100). 0 = unlimited.
    static constexpr std::size_t kDefaultLimit = 100;

    explicit UndoStack(std::size_t limit = kDefaultLimit);

    // Records an already-applied, non-empty group as a new undo step, or
    // merges it into the top entry. Merging happens iff all of:
    //   * `mergeKey` is non-empty and equals the top entry's key,
    //   * there is no redo tail (index() == count()) and index() > 0,
    //   * the top entry is not the clean state (cleanIndex() != index()),
    //   * mergeBreak() was not called since the top entry was pushed,
    //   * mergeWindow() is unset, or now - (time of the top entry's last
    //     push or merge) <= mergeWindow(),
    //   * top.group->canMergeWith(*group).
    // A merge keeps the top entry's name and updates its timestamp.
    // Returns true when merged. An empty group is ignored (returns false).
    bool push(std::string name, std::unique_ptr<CommandGroup> group, std::string mergeKey = {});

    // Ends the current merge sequence: the next push() never merges into the
    // current top entry. The UI calls this when a gesture (drag, slider
    // scrub, typing burst) ends.
    void mergeBreak();

    [[nodiscard]] bool canUndo() const { return index_ > 0; }
    [[nodiscard]] bool canRedo() const { return index_ < entries_.size(); }
    // Reverts entry index()-1 and decrements index(). Returns false when
    // there is nothing to undo. Also ends the merge sequence.
    bool undo(core::Document& document, ChangeSet& changes);
    // Re-applies entry index() and increments index(). Returns false when
    // there is nothing to redo, or when re-applying failed (which means the
    // document was changed behind the stack's back; the entry is then
    // dropped together with the rest of the redo tail).
    bool redo(core::Document& document, ChangeSet& changes);

    // Names for "Undo <name>" / "Redo <name>"; "" when not available.
    [[nodiscard]] std::string_view undoText() const;
    [[nodiscard]] std::string_view redoText() const;
    [[nodiscard]] std::string_view text(std::size_t entryIndex) const;

    [[nodiscard]] std::size_t index() const { return index_; }
    [[nodiscard]] std::size_t count() const { return entries_.size(); }

    // Clean state = the document as last saved.
    void setClean() { clean_ = index_; }
    [[nodiscard]] bool isClean() const { return clean_ == index_; }
    [[nodiscard]] std::optional<std::size_t> cleanIndex() const { return clean_; }

    // Maximum number of entries; 0 = unlimited. When a push exceeds it, the
    // oldest entries are dropped and index()/cleanIndex() shift down (a
    // dropped clean entry makes cleanIndex() nullopt). Lowering the limit
    // trims immediately the same way.
    void setLimit(std::size_t limit);
    [[nodiscard]] std::size_t limit() const { return limit_; }

    // Optional time window for merging (see push()). Unset by default:
    // merging is then ended only by mergeBreak(), undo() or a key change.
    void setMergeWindow(std::optional<std::chrono::milliseconds> window) { mergeWindow_ = window; }
    [[nodiscard]] std::optional<std::chrono::milliseconds> mergeWindow() const { return mergeWindow_; }
    // Time source for the merge window; defaults to Clock::now. For tests.
    void setClock(std::function<Clock::time_point()> clock);

    // Drops all entries; index 0; the current state becomes clean.
    void clear();

private:
    struct Entry {
        std::string name;
        std::string mergeKey;
        std::unique_ptr<CommandGroup> group;
        Clock::time_point lastTouched;
    };

    void trimToLimit();

    std::vector<Entry> entries_;
    std::size_t index_ = 0;
    std::optional<std::size_t> clean_ = 0;
    std::size_t limit_;
    std::optional<std::chrono::milliseconds> mergeWindow_;
    std::function<Clock::time_point()> clock_;
    bool mergeBroken_ = false;
};

} // namespace qraw::commands
