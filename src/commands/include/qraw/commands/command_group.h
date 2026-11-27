#pragma once

#include "qraw/commands/command.h"

#include <cstddef>
#include <memory>
#include <vector>

namespace qraw::commands {

// An ordered list of commands that is applied and reverted as one unit: the
// payload of one undo step (and of a Transaction while it is open).
//
// apply() runs the children in order. If a child fails, the children applied
// so far are reverted and apply() returns false. revert() runs them in
// reverse order.
class CommandGroup final : public Command {
public:
    CommandGroup() = default;
    explicit CommandGroup(std::vector<std::unique_ptr<Command>> commands) : commands_(std::move(commands)) {}

    [[nodiscard]] bool apply(core::Document& document, ChangeSet& changes) override;
    void revert(core::Document& document, ChangeSet& changes) override;

    // True when `next` is a CommandGroup of the same size whose child i can
    // merge into this group's child i, for every i.
    [[nodiscard]] bool canMergeWith(const Command& next) const override;
    // All-or-nothing: checks canMergeWith() first, then merges child-wise.
    bool mergeWith(const Command& next) override;

    [[nodiscard]] bool empty() const { return commands_.empty(); }
    [[nodiscard]] std::size_t size() const { return commands_.size(); }
    [[nodiscard]] const std::vector<std::unique_ptr<Command>>& commands() const { return commands_; }

    // Appends a command that has already been applied.
    void append(std::unique_ptr<Command> command) { commands_.push_back(std::move(command)); }
    // Removes and returns the commands from position `from` on, without
    // reverting them.
    std::vector<std::unique_ptr<Command>> takeFrom(std::size_t from);

private:
    std::vector<std::unique_ptr<Command>> commands_;
};

} // namespace qraw::commands
