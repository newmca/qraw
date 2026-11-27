#include "qraw/commands/undo_stack.h"

#include "qraw/core/document.h"

#include <utility>

namespace qraw::commands {

namespace {
UndoStack::Clock::time_point defaultNow() { return UndoStack::Clock::now(); }
} // namespace

UndoStack::UndoStack(std::size_t limit) : limit_(limit), clock_(defaultNow) {}

void UndoStack::setClock(std::function<Clock::time_point()> clock)
{
    clock_ = clock ? std::move(clock) : std::function<Clock::time_point()>(defaultNow);
}

bool UndoStack::push(std::string name, std::unique_ptr<CommandGroup> group, std::string mergeKey)
{
    if (!group || group->empty())
        return false;

    if (!mergeKey.empty() && index_ == entries_.size() && index_ > 0) {
        Entry& top = entries_.back();
        const bool withinWindow = !mergeWindow_ || clock_() - top.lastTouched <= *mergeWindow_;
        if (top.mergeKey == mergeKey && clean_ != index_ && !mergeBroken_ && withinWindow
            && top.group->canMergeWith(*group)) {
            top.group->mergeWith(*group);
            top.lastTouched = clock_();
            return true;
        }
    }

    // Branching off: the redo tail is gone, and so is a clean state inside it.
    entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index_), entries_.end());
    if (clean_ && *clean_ > index_)
        clean_.reset();
    entries_.push_back(Entry{std::move(name), std::move(mergeKey), std::move(group), clock_()});
    index_ = entries_.size();
    mergeBroken_ = false;
    trimToLimit();
    return false;
}

void UndoStack::mergeBreak() { mergeBroken_ = true; }

bool UndoStack::undo(core::Document& document, ChangeSet& changes)
{
    if (index_ == 0)
        return false;
    entries_[index_ - 1].group->revert(document, changes);
    --index_;
    mergeBroken_ = true;
    return true;
}

bool UndoStack::redo(core::Document& document, ChangeSet& changes)
{
    if (index_ >= entries_.size())
        return false;
    if (!entries_[index_].group->apply(document, changes)) {
        // The document diverged from the history: the tail cannot be trusted.
        entries_.erase(entries_.begin() + static_cast<std::ptrdiff_t>(index_), entries_.end());
        if (clean_ && *clean_ > index_)
            clean_.reset();
        return false;
    }
    ++index_;
    mergeBroken_ = true;
    return true;
}

std::string_view UndoStack::text(std::size_t entryIndex) const
{
    return entryIndex < entries_.size() ? std::string_view(entries_[entryIndex].name) : std::string_view();
}

std::string_view UndoStack::undoText() const { return index_ > 0 ? text(index_ - 1) : std::string_view(); }

std::string_view UndoStack::redoText() const { return text(index_); }

void UndoStack::setLimit(std::size_t limit)
{
    limit_ = limit;
    trimToLimit();
}

void UndoStack::trimToLimit()
{
    while (limit_ > 0 && entries_.size() > limit_) {
        if (index_ > 0) {
            // Forget the oldest applied step; everything shifts down by one.
            entries_.erase(entries_.begin());
            --index_;
            if (clean_)
                clean_ = *clean_ == 0 ? std::nullopt : std::optional<std::size_t>(*clean_ - 1);
        } else {
            // Everything is undone: drop from the far end of the redo tail.
            entries_.pop_back();
            if (clean_ && *clean_ > entries_.size())
                clean_.reset();
        }
    }
}

void UndoStack::clear()
{
    entries_.clear();
    index_ = 0;
    clean_ = 0;
    mergeBroken_ = false;
}

} // namespace qraw::commands
