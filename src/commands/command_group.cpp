#include "qraw/commands/command_group.h"

#include <iterator>

namespace qraw::commands {

bool CommandGroup::apply(core::Document& document, ChangeSet& changes)
{
    for (std::size_t i = 0; i < commands_.size(); ++i) {
        if (!commands_[i]->apply(document, changes)) {
            for (std::size_t j = i; j-- > 0;)
                commands_[j]->revert(document, changes);
            return false;
        }
    }
    return true;
}

void CommandGroup::revert(core::Document& document, ChangeSet& changes)
{
    for (auto it = commands_.rbegin(); it != commands_.rend(); ++it)
        (*it)->revert(document, changes);
}

bool CommandGroup::canMergeWith(const Command& next) const
{
    const auto* other = dynamic_cast<const CommandGroup*>(&next);
    if (!other || other->commands_.size() != commands_.size() || commands_.empty())
        return false;
    for (std::size_t i = 0; i < commands_.size(); ++i) {
        if (!commands_[i]->canMergeWith(*other->commands_[i]))
            return false;
    }
    return true;
}

bool CommandGroup::mergeWith(const Command& next)
{
    if (!canMergeWith(next))
        return false;
    const auto& other = static_cast<const CommandGroup&>(next);
    for (std::size_t i = 0; i < commands_.size(); ++i)
        commands_[i]->mergeWith(*other.commands_[i]);
    return true;
}

std::vector<std::unique_ptr<Command>> CommandGroup::takeFrom(std::size_t from)
{
    std::vector<std::unique_ptr<Command>> out;
    if (from >= commands_.size())
        return out;
    const auto first = commands_.begin() + static_cast<std::ptrdiff_t>(from);
    out.assign(std::make_move_iterator(first), std::make_move_iterator(commands_.end()));
    commands_.erase(first, commands_.end());
    return out;
}

} // namespace qraw::commands
