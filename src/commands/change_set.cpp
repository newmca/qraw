#include "qraw/commands/change_set.h"

namespace qraw::commands {

void ChangeSet::cellAdded(const std::string& pageId, const std::string& cellId)
{
    auto& p = pages_[pageId];
    if (p.removed.erase(cellId) > 0)
        p.changed.insert(cellId); // removed then re-added
    else
        p.added.insert(cellId);
}

void ChangeSet::cellRemoved(const std::string& pageId, const std::string& cellId)
{
    auto& p = pages_[pageId];
    p.changed.erase(cellId);
    p.reordered.erase(cellId);
    if (p.added.erase(cellId) == 0) // added then removed: no net change
        p.removed.insert(cellId);
}

void ChangeSet::cellChanged(const std::string& pageId, const std::string& cellId)
{
    auto& p = pages_[pageId];
    if (!p.added.contains(cellId))
        p.changed.insert(cellId);
}

void ChangeSet::cellReordered(const std::string& pageId, const std::string& cellId)
{
    auto& p = pages_[pageId];
    if (!p.added.contains(cellId))
        p.reordered.insert(cellId);
}

void ChangeSet::childrenChanged(const std::string& pageId, const std::string& parentId)
{
    if (!parentId.empty())
        pages_[pageId].childrenChanged.insert(parentId);
}

void ChangeSet::pageChanged(const std::string& pageId) { pages_[pageId].pagePropertiesChanged = true; }

void ChangeSet::pageAdded(const std::string& pageId)
{
    if (pagesRemoved_.erase(pageId) > 0) {
        pages_[pageId].reset = true; // removed then added: rebuild it
        return;
    }
    pagesAdded_.insert(pageId);
}

void ChangeSet::pageRemoved(const std::string& pageId)
{
    pages_.erase(pageId);
    if (pagesAdded_.erase(pageId) == 0) // added then removed: no net change
        pagesRemoved_.insert(pageId);
}

void ChangeSet::merge(const ChangeSet& later)
{
    // Page removals come first so that a page removed and re-added in
    // `later` ends up reset rather than with stale cell records.
    for (const auto& id : later.pagesRemoved_)
        pageRemoved(id);
    for (const auto& id : later.pagesAdded_)
        pageAdded(id);
    for (const auto& [pageId, changes] : later.pages_) {
        for (const auto& id : changes.removed)
            cellRemoved(pageId, id);
        for (const auto& id : changes.added)
            cellAdded(pageId, id);
        for (const auto& id : changes.changed)
            cellChanged(pageId, id);
        for (const auto& id : changes.reordered)
            cellReordered(pageId, id);
        for (const auto& id : changes.childrenChanged)
            childrenChanged(pageId, id);
        if (changes.pagePropertiesChanged)
            pageChanged(pageId);
        if (changes.reset)
            pages_[pageId].reset = true;
    }
    pagesReordered_ = pagesReordered_ || later.pagesReordered_;
    documentChanged_ = documentChanged_ || later.documentChanged_;
}

bool ChangeSet::empty() const
{
    if (!pagesAdded_.empty() || !pagesRemoved_.empty() || pagesReordered_ || documentChanged_)
        return false;
    for (const auto& [id, changes] : pages_) {
        if (!changes.empty())
            return false;
    }
    return true;
}

} // namespace qraw::commands
