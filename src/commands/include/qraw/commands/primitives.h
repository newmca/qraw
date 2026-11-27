#pragma once

// The primitive commands. Every model mutation is one of these; operations
// (ops_*.h) only compose them. Each primitive stores whole before/after
// values (a core::Cell, a core::Page, an attribute list) and exact entry
// positions, so revert() restores the previous model exactly and saving
// after undo is byte-identical to saving before the edit.
//
// Addressing: pages by index into Document::pages() (robust against pages
// with missing or duplicate ids), cells by id within that page. Commands are
// applied and reverted strictly in sequence, so an index is always resolved
// against the same state it was computed on.
//
// Cells with an empty id cannot be addressed; with an id used more than once
// on the page, the first such cell is the one addressed (core::Page::findCell).
// See docs/design/commands.md, "Known limitations".

#include "qraw/commands/command.h"
#include "qraw/core/attributes.h"
#include "qraw/core/cell.h"
#include "qraw/core/document.h"
#include "qraw/core/page.h"

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace qraw::commands {

// --- cells ----------------------------------------------------------------

// Inserts a new cell before rootEntries()[entryIndex] (== size appends).
// Fails when the page does not exist, the index is out of range, the id is
// empty, or the id is already used on the page. Records cellAdded.
class InsertCell final : public Command {
public:
    InsertCell(std::size_t page, std::size_t entryIndex, core::Cell cell);

    [[nodiscard]] bool apply(core::Document& document, ChangeSet& changes) override;
    void revert(core::Document& document, ChangeSet& changes) override;

private:
    std::size_t page_;
    std::size_t entryIndex_;
    core::Cell cell_;
};

// Removes one cell (its descendants are not touched; operations remove them
// first). Revert re-inserts it at its original entry index. Fails when the
// cell does not exist. Records cellRemoved.
class RemoveCell final : public Command {
public:
    RemoveCell(std::size_t page, std::string cellId);

    [[nodiscard]] bool apply(core::Document& document, ChangeSet& changes) override;
    void revert(core::Document& document, ChangeSet& changes) override;

private:
    std::size_t page_;
    std::string cellId_;
    std::size_t entryIndex_ = 0; // captured by apply()
    core::Cell removed_;         // captured by apply()
};

// Replaces a cell's whole state (mxCell attributes, wrapper, geometry,
// foreign children) by `after`, keeping its position. `after.id()` names the
// cell and must not change: renaming ids is not an edit draw.io offers.
// Fails when the cell does not exist. Records cellChanged.
//
// Mergeable with a later ReplaceCell of the same page and cell: the merged
// command keeps this command's "before" and takes next's "after".
class ReplaceCell final : public Command {
public:
    ReplaceCell(std::size_t page, core::Cell after);

    [[nodiscard]] bool apply(core::Document& document, ChangeSet& changes) override;
    void revert(core::Document& document, ChangeSet& changes) override;
    [[nodiscard]] bool canMergeWith(const Command& next) const override;
    bool mergeWith(const Command& next) override;

private:
    std::size_t page_;
    core::Cell after_;
    core::Cell before_; // captured by apply()
};

// Replaces the whole state of several cells at once: ReplaceCell for each of
// `after`, in one pass over the page's indexes (core::Page::replaceCells), so
// that re-parenting thousands of cells costs O(cells), not O(cells) per cell
// (D-036). Fails when a cell does not exist or occurs twice; nothing changes
// then. Records cellChanged for each cell, and the parents that gained or
// lost a child. Not mergeable.
class ReplaceCells final : public Command {
public:
    ReplaceCells(std::size_t page, std::vector<core::Cell> after);

    [[nodiscard]] bool apply(core::Document& document, ChangeSet& changes) override;
    void revert(core::Document& document, ChangeSet& changes) override;

private:
    std::size_t page_;
    std::vector<core::Cell> after_;
    std::vector<core::Cell> before_; // captured by apply()
};

// Moves cells to one contiguous block of <root> entries (z-order change).
//
// On apply, `cellIds` are taken in their current entry order (the order in
// the vector does not matter; with `KeepGivenOrder` the block takes the order
// of the vector) and re-inserted contiguously at the position of the first
// entry at or after `beforeEntryIndex` that is not itself being moved (end
// when there is none; `beforeEntryIndex == rootEntries().size()` means end).
// Foreign XmlNode entries never move. Fails on an unknown or duplicate id, or
// an index greater than the entry count. Revert puts every cell back at its
// original entry index. One pass over the entries however many cells move
// (core::Page::moveEntries). Records cellReordered for each cell.
class ReorderCells final : public Command {
public:
    enum class Order { Entry, KeepGivenOrder };
    ReorderCells(std::size_t page, std::vector<std::string> cellIds, std::size_t beforeEntryIndex,
                 Order order = Order::Entry);

    [[nodiscard]] bool apply(core::Document& document, ChangeSet& changes) override;
    void revert(core::Document& document, ChangeSet& changes) override;

private:
    std::size_t page_;
    std::vector<std::string> cellIds_;
    std::size_t beforeEntryIndex_;
    Order order_;
    std::vector<std::size_t> originalEntryIndices_; // captured by apply(), matching orderedIds_
    std::vector<std::string> orderedIds_;           // captured by apply(): ids in block order
    std::size_t landing_ = 0;                       // captured by apply(): where the block starts
};

// --- pages ----------------------------------------------------------------

// Inserts a page before Document::entries()[entryIndex] (== size appends).
// Fails when the index is out of range. Records pageAdded.
class InsertPage final : public Command {
public:
    InsertPage(std::size_t entryIndex, core::Page page);

    [[nodiscard]] bool apply(core::Document& document, ChangeSet& changes) override;
    void revert(core::Document& document, ChangeSet& changes) override;

private:
    std::size_t entryIndex_;
    core::Page page_;
};

// Removes the `pageIndex`-th page. Revert re-inserts it at its original
// entry index. Fails when there is no such page. Records pageRemoved.
// (Refusing to remove the last page is policy, enforced by ops::removePage.)
class RemovePage final : public Command {
public:
    explicit RemovePage(std::size_t pageIndex);

    [[nodiscard]] bool apply(core::Document& document, ChangeSet& changes) override;
    void revert(core::Document& document, ChangeSet& changes) override;

private:
    std::size_t pageIndex_;
    std::size_t entryIndex_ = 0; // captured by apply()
    core::Page removed_;         // captured by apply()
};

// Moves the `pageIndex`-th page to the position of the first entry at or
// after `beforeEntryIndex` that is not the page itself (end when none).
// Indices refer to Document::entries() before the move. Fails on a bad page
// or entry index. Records pagesReordered.
class MovePage final : public Command {
public:
    MovePage(std::size_t pageIndex, std::size_t beforeEntryIndex);

    [[nodiscard]] bool apply(core::Document& document, ChangeSet& changes) override;
    void revert(core::Document& document, ChangeSet& changes) override;

private:
    std::size_t pageIndex_;
    std::size_t beforeEntryIndex_;
    std::size_t originalEntryIndex_ = 0; // captured by apply()
    std::size_t newEntryIndex_ = 0;      // captured by apply()
};

// Replaces the `pageIndex`-th page by `after`, at the same position. Fails
// when there is no such page. Records the page as removed and added again,
// which ChangeSet folds into "reset": observers rebuild the page.
class ReplacePage final : public Command {
public:
    ReplacePage(std::size_t pageIndex, core::Page after);

    [[nodiscard]] bool apply(core::Document& document, ChangeSet& changes) override;
    void revert(core::Document& document, ChangeSet& changes) override;

private:
    std::size_t pageIndex_;
    core::Page after_;
    core::Page before_; // captured by apply()
};

// The editable header of a page: <diagram> attributes (id, name, unknown
// ones) and <mxGraphModel> attributes (grid, page size, background, ...).
struct PageAttributes {
    core::Attributes diagram;
    core::Attributes model;

    static PageAttributes of(const core::Page& page) { return {page.diagramAttributes(), page.modelAttributes()}; }
    friend bool operator==(const PageAttributes&, const PageAttributes&) = default;
};

// Replaces a page's PageAttributes. Fails when there is no such page.
// Records pageChanged under the page id *after* the change (and also under
// the old id when the id changed). Mergeable with a later
// ReplacePageAttributes of the same page.
class ReplacePageAttributes final : public Command {
public:
    ReplacePageAttributes(std::size_t pageIndex, PageAttributes after);

    [[nodiscard]] bool apply(core::Document& document, ChangeSet& changes) override;
    void revert(core::Document& document, ChangeSet& changes) override;
    [[nodiscard]] bool canMergeWith(const Command& next) const override;
    bool mergeWith(const Command& next) override;

private:
    std::size_t pageIndex_;
    PageAttributes after_;
    PageAttributes before_; // captured by apply()
};

// --- document ---------------------------------------------------------------

// The editable header of the document: its top-level form and the <mxfile>
// attributes (e.g. `pages="3"`, which newer draw.io versions write).
struct DocumentHeader {
    core::DocumentKind kind = core::DocumentKind::MxFile;
    core::Attributes fileAttributes;

    static DocumentHeader of(const core::Document& document) { return {document.kind(), document.fileAttributes()}; }
    friend bool operator==(const DocumentHeader&, const DocumentHeader&) = default;
};

// Replaces the DocumentHeader. Never fails. Records documentChanged.
class ReplaceDocumentHeader final : public Command {
public:
    explicit ReplaceDocumentHeader(DocumentHeader after);

    [[nodiscard]] bool apply(core::Document& document, ChangeSet& changes) override;
    void revert(core::Document& document, ChangeSet& changes) override;

private:
    DocumentHeader after_;
    DocumentHeader before_; // captured by apply()
};

} // namespace qraw::commands
