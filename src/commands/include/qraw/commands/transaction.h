#pragma once

#include "qraw/commands/change_set.h"
#include "qraw/commands/command.h"
#include "qraw/commands/command_group.h"

#include <cstddef>
#include <memory>
#include <string>
#include <utility>
#include <vector>

namespace qraw::core {
class Document;
class Page;
}

namespace qraw::commands {

// An open, atomic batch of commands on one document.
//
// Commands are applied as soon as they are added, so an operation that adds
// several commands sees the effect of the earlier ones (as in draw.io's
// model.beginUpdate()/endUpdate()). The transaction is the only holder of
// write access to the document while it is open; operations read the model
// through document() / page().
//
// Atomicity: rollback() reverts everything; destroying an open transaction
// rolls it back. Nesting: savepoint()/rollbackTo() undo only the commands
// added after the savepoint (the Editor uses this for nested edits: a failing
// inner operation is undone, and the outer one decides whether to go on).
//
// Not thread-safe; the document must not be changed by anything else while
// the transaction is open.
class Transaction {
public:
    explicit Transaction(core::Document& document);
    ~Transaction(); // rolls back unless commit() was called

    Transaction(const Transaction&) = delete;
    Transaction& operator=(const Transaction&) = delete;

    [[nodiscard]] const core::Document& document() const { return document_; }
    // The `index`-th page, or nullptr.
    [[nodiscard]] const core::Page* page(std::size_t index) const;

    // Applies `command` and records it. When apply() fails the command is
    // discarded, the document is unchanged and false is returned; the
    // transaction stays usable.
    [[nodiscard]] bool add(std::unique_ptr<Command> command);
    template <class C, class... Args>
    [[nodiscard]] bool emplace(Args&&... args)
    {
        return add(std::make_unique<C>(std::forward<Args>(args)...));
    }

    // A cell id that is unused on the page (core::Page::generateId(), D-009).
    // Ids handed out are not reserved: insert the cell before asking again
    // or generate all ids up front from one call sequence (each call returns
    // a different id). Returns "" when there is no such page.
    [[nodiscard]] std::string newCellId(std::size_t page);

    // Number of applied commands; a savepoint for rollbackTo().
    [[nodiscard]] std::size_t savepoint() const { return group_.size(); }
    // Reverts and discards the commands added after `savepoint`, newest
    // first. changes() is restored to what it was at the savepoint.
    void rollbackTo(std::size_t savepoint);
    void rollback() { rollbackTo(0); }

    [[nodiscard]] bool empty() const { return group_.empty(); }
    // Net changes of the applied commands, in order.
    [[nodiscard]] ChangeSet changes() const;

    struct Committed {
        std::unique_ptr<CommandGroup> group; // already applied
        ChangeSet changes;
    };
    // Closes the transaction and hands over its applied commands. The
    // document keeps their effect; afterwards the transaction is empty and
    // its destructor does nothing. Committing an empty transaction yields an
    // empty group.
    [[nodiscard]] Committed commit();

private:
    core::Document& document_;
    CommandGroup group_;
    std::vector<ChangeSet> perCommand_; // parallel to group_.commands()
    bool committed_ = false;
};

} // namespace qraw::commands
