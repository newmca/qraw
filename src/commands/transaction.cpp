#include "qraw/commands/transaction.h"

#include "qraw/core/document.h"

namespace qraw::commands {

Transaction::Transaction(core::Document& document) : document_(document) {}

Transaction::~Transaction()
{
    if (!committed_)
        rollback();
}

const core::Page* Transaction::page(std::size_t index) const { return document_.page(index); }

bool Transaction::add(std::unique_ptr<Command> command)
{
    if (!command || committed_)
        return false;
    ChangeSet changes;
    if (!command->apply(document_, changes))
        return false;
    group_.append(std::move(command));
    perCommand_.push_back(std::move(changes));
    return true;
}

std::string Transaction::newCellId(std::size_t page)
{
    core::Page* p = document_.page(page);
    return p ? p->generateId() : std::string();
}

void Transaction::rollbackTo(std::size_t savepoint)
{
    if (savepoint >= group_.size())
        return;
    auto undone = group_.takeFrom(savepoint);
    ChangeSet ignored; // the net effect is recomputed from perCommand_
    for (auto it = undone.rbegin(); it != undone.rend(); ++it)
        (*it)->revert(document_, ignored);
    perCommand_.resize(savepoint);
}

ChangeSet Transaction::changes() const
{
    ChangeSet total;
    for (const auto& c : perCommand_)
        total.merge(c);
    return total;
}

Transaction::Committed Transaction::commit()
{
    Committed out;
    out.changes = changes();
    out.group = std::make_unique<CommandGroup>(group_.takeFrom(0));
    perCommand_.clear();
    committed_ = true;
    return out;
}

} // namespace qraw::commands
