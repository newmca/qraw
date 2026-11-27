#include "qraw/commands/primitives.h"

#include <algorithm>
#include <cassert>
#include <set>
#include <utility>

namespace qraw::commands {

namespace {

core::Page* pageAt(core::Document& document, std::size_t index) { return document.page(index); }

// A cell that moved to another parent changes the children of both.
void recordParentChange(ChangeSet& changes, const std::string& pageId, const core::Cell& before, const core::Cell& after)
{
    if (before.parent() == after.parent())
        return;
    changes.childrenChanged(pageId, before.parent());
    changes.childrenChanged(pageId, after.parent());
}

// Entry position at which a block lands: the first entry at or after
// `before` that is not being moved, counted in the list without the moved
// entries. `moved` holds ascending entry indices.
std::size_t landingIndex(const std::vector<std::size_t>& moved, std::size_t before, std::size_t entryCount)
{
    std::size_t anchor = before;
    std::size_t m = 0;
    while (m < moved.size() && moved[m] < anchor)
        ++m;
    // Skip moved entries sitting exactly at the anchor.
    while (m < moved.size() && moved[m] == anchor) {
        ++anchor;
        ++m;
    }
    anchor = std::min(anchor, entryCount);
    // Moved entries before the anchor disappear from in front of it.
    const auto removedBefore = static_cast<std::size_t>(
        std::count_if(moved.begin(), moved.end(), [&](std::size_t i) { return i < anchor; }));
    return anchor - removedBefore;
}

} // namespace

// --- InsertCell --------------------------------------------------------------

InsertCell::InsertCell(std::size_t page, std::size_t entryIndex, core::Cell cell)
    : page_(page), entryIndex_(entryIndex), cell_(std::move(cell))
{
}

bool InsertCell::apply(core::Document& document, ChangeSet& changes)
{
    core::Page* page = pageAt(document, page_);
    if (!page || entryIndex_ > page->rootEntries().size() || cell_.id().empty() || page->findCell(cell_.id()))
        return false;
    page->insertCellAt(entryIndex_, cell_);
    changes.cellAdded(page->id(), cell_.id());
    changes.childrenChanged(page->id(), cell_.parent());
    return true;
}

void InsertCell::revert(core::Document& document, ChangeSet& changes)
{
    core::Page* page = pageAt(document, page_);
    assert(page && page->entryIndex(cell_.id()) == entryIndex_);
    page->takeCell(cell_.id());
    changes.cellRemoved(page->id(), cell_.id());
    changes.childrenChanged(page->id(), cell_.parent());
}

// --- RemoveCell --------------------------------------------------------------

RemoveCell::RemoveCell(std::size_t page, std::string cellId) : page_(page), cellId_(std::move(cellId)) {}

bool RemoveCell::apply(core::Document& document, ChangeSet& changes)
{
    core::Page* page = pageAt(document, page_);
    if (!page || cellId_.empty())
        return false;
    const auto index = page->entryIndex(cellId_);
    if (!index)
        return false;
    entryIndex_ = *index;
    removed_ = *page->takeCell(cellId_);
    changes.cellRemoved(page->id(), cellId_);
    changes.childrenChanged(page->id(), removed_.parent());
    return true;
}

void RemoveCell::revert(core::Document& document, ChangeSet& changes)
{
    core::Page* page = pageAt(document, page_);
    assert(page);
    [[maybe_unused]] const core::Cell* restored = page->insertCellAt(entryIndex_, removed_);
    assert(restored);
    changes.cellAdded(page->id(), cellId_);
    changes.childrenChanged(page->id(), removed_.parent());
}

// --- ReplaceCell -------------------------------------------------------------

ReplaceCell::ReplaceCell(std::size_t page, core::Cell after) : page_(page), after_(std::move(after)) {}

bool ReplaceCell::apply(core::Document& document, ChangeSet& changes)
{
    core::Page* page = pageAt(document, page_);
    if (!page || after_.id().empty())
        return false;
    const core::Cell* cell = page->findCell(after_.id());
    if (!cell)
        return false;
    before_ = *cell;
    page->replaceCell(after_);
    changes.cellChanged(page->id(), after_.id());
    recordParentChange(changes, page->id(), before_, after_);
    return true;
}

void ReplaceCell::revert(core::Document& document, ChangeSet& changes)
{
    core::Page* page = pageAt(document, page_);
    assert(page);
    [[maybe_unused]] const bool restored = page->replaceCell(before_);
    assert(restored);
    changes.cellChanged(page->id(), after_.id());
    recordParentChange(changes, page->id(), before_, after_);
}

bool ReplaceCell::canMergeWith(const Command& next) const
{
    const auto* other = dynamic_cast<const ReplaceCell*>(&next);
    return other && other->page_ == page_ && other->after_.id() == after_.id();
}

bool ReplaceCell::mergeWith(const Command& next)
{
    if (!canMergeWith(next))
        return false;
    after_ = static_cast<const ReplaceCell&>(next).after_;
    return true;
}

// --- ReplaceCells ------------------------------------------------------------

ReplaceCells::ReplaceCells(std::size_t page, std::vector<core::Cell> after) : page_(page), after_(std::move(after)) {}

bool ReplaceCells::apply(core::Document& document, ChangeSet& changes)
{
    core::Page* page = pageAt(document, page_);
    if (!page)
        return false;
    before_.clear();
    before_.reserve(after_.size());
    for (const core::Cell& after : after_) {
        const core::Cell* cell = after.id().empty() ? nullptr : page->findCell(after.id());
        if (!cell)
            return false;
        before_.push_back(*cell);
    }
    if (!page->replaceCells(after_))
        return false; // a cell twice
    for (std::size_t i = 0; i < after_.size(); ++i) {
        changes.cellChanged(page->id(), after_[i].id());
        recordParentChange(changes, page->id(), before_[i], after_[i]);
    }
    return true;
}

void ReplaceCells::revert(core::Document& document, ChangeSet& changes)
{
    core::Page* page = pageAt(document, page_);
    assert(page);
    [[maybe_unused]] const bool restored = page->replaceCells(before_);
    assert(restored);
    for (std::size_t i = 0; i < after_.size(); ++i) {
        changes.cellChanged(page->id(), after_[i].id());
        recordParentChange(changes, page->id(), before_[i], after_[i]);
    }
}

// --- ReorderCells ------------------------------------------------------------

ReorderCells::ReorderCells(std::size_t page, std::vector<std::string> cellIds, std::size_t beforeEntryIndex, Order order)
    : page_(page), cellIds_(std::move(cellIds)), beforeEntryIndex_(beforeEntryIndex), order_(order)
{
}

bool ReorderCells::apply(core::Document& document, ChangeSet& changes)
{
    core::Page* page = pageAt(document, page_);
    if (!page || beforeEntryIndex_ > page->rootEntries().size())
        return false;

    std::vector<std::pair<std::size_t, std::string>> located;
    located.reserve(cellIds_.size());
    std::set<std::size_t> seen;
    for (const auto& id : cellIds_) {
        const auto index = id.empty() ? std::nullopt : page->entryIndex(id);
        if (!index || !seen.insert(*index).second)
            return false;
        located.emplace_back(*index, id);
    }
    if (located.empty())
        return true; // nothing to move
    if (order_ == Order::Entry)
        std::sort(located.begin(), located.end());

    originalEntryIndices_.clear();
    orderedIds_.clear();
    for (const auto& [index, id] : located) {
        originalEntryIndices_.push_back(index);
        orderedIds_.push_back(id);
    }
    landing_ = landingIndex(std::vector<std::size_t>(seen.begin(), seen.end()), beforeEntryIndex_,
                            page->rootEntries().size());
    if (!page->moveEntries(originalEntryIndices_, landing_))
        return false;

    for (const auto& id : orderedIds_) {
        changes.cellReordered(page->id(), id);
        changes.childrenChanged(page->id(), page->findCell(id)->parent());
    }
    return true;
}

void ReorderCells::revert(core::Document& document, ChangeSet& changes)
{
    core::Page* page = pageAt(document, page_);
    assert(page);
    if (orderedIds_.empty())
        return;
    [[maybe_unused]] const bool restored = page->restoreEntries(originalEntryIndices_, landing_);
    assert(restored);
    for (const auto& id : orderedIds_) {
        changes.cellReordered(page->id(), id);
        changes.childrenChanged(page->id(), page->findCell(id)->parent());
    }
}

// --- InsertPage --------------------------------------------------------------

InsertPage::InsertPage(std::size_t entryIndex, core::Page page) : entryIndex_(entryIndex), page_(std::move(page)) {}

bool InsertPage::apply(core::Document& document, ChangeSet& changes)
{
    if (entryIndex_ > document.entries().size())
        return false;
    document.insertPageAt(entryIndex_, page_);
    changes.pageAdded(page_.id());
    return true;
}

void InsertPage::revert(core::Document& document, ChangeSet& changes)
{
    // The page index of the inserted entry: pages before it.
    std::size_t pageIndex = 0;
    for (std::size_t i = 0; i < entryIndex_; ++i)
        pageIndex += std::holds_alternative<core::Page>(document.entries()[i]) ? 1 : 0;
    [[maybe_unused]] const auto taken = document.takePage(pageIndex);
    assert(taken);
    changes.pageRemoved(page_.id());
}

// --- RemovePage --------------------------------------------------------------

RemovePage::RemovePage(std::size_t pageIndex) : pageIndex_(pageIndex) {}

bool RemovePage::apply(core::Document& document, ChangeSet& changes)
{
    const auto entry = document.pageEntryIndex(pageIndex_);
    if (!entry)
        return false;
    entryIndex_ = *entry;
    removed_ = *document.takePage(pageIndex_);
    changes.pageRemoved(removed_.id());
    return true;
}

void RemovePage::revert(core::Document& document, ChangeSet& changes)
{
    [[maybe_unused]] const core::Page* restored = document.insertPageAt(entryIndex_, removed_);
    assert(restored);
    changes.pageAdded(removed_.id());
}

// --- MovePage ----------------------------------------------------------------

MovePage::MovePage(std::size_t pageIndex, std::size_t beforeEntryIndex)
    : pageIndex_(pageIndex), beforeEntryIndex_(beforeEntryIndex)
{
}

bool MovePage::apply(core::Document& document, ChangeSet& changes)
{
    const auto entry = document.pageEntryIndex(pageIndex_);
    if (!entry || beforeEntryIndex_ > document.entries().size())
        return false;
    originalEntryIndex_ = *entry;
    newEntryIndex_ = landingIndex({originalEntryIndex_}, beforeEntryIndex_, document.entries().size());
    core::Page page = *document.takePage(pageIndex_);
    document.insertPageAt(newEntryIndex_, std::move(page));
    changes.pagesReordered();
    return true;
}

void MovePage::revert(core::Document& document, ChangeSet& changes)
{
    std::size_t pageIndex = 0;
    for (std::size_t i = 0; i < newEntryIndex_; ++i)
        pageIndex += std::holds_alternative<core::Page>(document.entries()[i]) ? 1 : 0;
    core::Page page = *document.takePage(pageIndex);
    document.insertPageAt(originalEntryIndex_, std::move(page));
    changes.pagesReordered();
}

// --- ReplacePage -------------------------------------------------------------

ReplacePage::ReplacePage(std::size_t pageIndex, core::Page after) : pageIndex_(pageIndex), after_(std::move(after)) {}

bool ReplacePage::apply(core::Document& document, ChangeSet& changes)
{
    const auto entry = document.pageEntryIndex(pageIndex_);
    if (!entry)
        return false;
    before_ = *document.takePage(pageIndex_);
    document.insertPageAt(*entry, after_);
    changes.pageRemoved(before_.id());
    changes.pageAdded(after_.id());
    return true;
}

void ReplacePage::revert(core::Document& document, ChangeSet& changes)
{
    const auto entry = document.pageEntryIndex(pageIndex_);
    [[maybe_unused]] const bool found = entry.has_value();
    assert(found);
    document.takePage(pageIndex_);
    document.insertPageAt(*entry, before_);
    changes.pageRemoved(after_.id());
    changes.pageAdded(before_.id());
}

// --- ReplacePageAttributes ---------------------------------------------------

ReplacePageAttributes::ReplacePageAttributes(std::size_t pageIndex, PageAttributes after)
    : pageIndex_(pageIndex), after_(std::move(after))
{
}

bool ReplacePageAttributes::apply(core::Document& document, ChangeSet& changes)
{
    core::Page* page = pageAt(document, pageIndex_);
    if (!page)
        return false;
    before_ = PageAttributes::of(*page);
    page->diagramAttributes() = after_.diagram;
    page->modelAttributes() = after_.model;
    if (before_.diagram.value("id") != page->id())
        changes.pageChanged(before_.diagram.value("id"));
    changes.pageChanged(page->id());
    return true;
}

void ReplacePageAttributes::revert(core::Document& document, ChangeSet& changes)
{
    core::Page* page = pageAt(document, pageIndex_);
    assert(page);
    const std::string afterId = page->id();
    page->diagramAttributes() = before_.diagram;
    page->modelAttributes() = before_.model;
    if (afterId != page->id())
        changes.pageChanged(afterId);
    changes.pageChanged(page->id());
}

bool ReplacePageAttributes::canMergeWith(const Command& next) const
{
    const auto* other = dynamic_cast<const ReplacePageAttributes*>(&next);
    return other && other->pageIndex_ == pageIndex_;
}

bool ReplacePageAttributes::mergeWith(const Command& next)
{
    if (!canMergeWith(next))
        return false;
    after_ = static_cast<const ReplacePageAttributes&>(next).after_;
    return true;
}

// --- ReplaceDocumentHeader ---------------------------------------------------

ReplaceDocumentHeader::ReplaceDocumentHeader(DocumentHeader after) : after_(std::move(after)) {}

bool ReplaceDocumentHeader::apply(core::Document& document, ChangeSet& changes)
{
    before_ = DocumentHeader::of(document);
    document.setKind(after_.kind);
    document.fileAttributes() = after_.fileAttributes;
    changes.documentChanged();
    return true;
}

void ReplaceDocumentHeader::revert(core::Document& document, ChangeSet& changes)
{
    document.setKind(before_.kind);
    document.fileAttributes() = before_.fileAttributes;
    changes.documentChanged();
}

} // namespace qraw::commands
