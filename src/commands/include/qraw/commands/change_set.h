#pragma once

#include <map>
#include <set>
#include <string>

namespace qraw::commands {

// What a committed transaction (or an undo / redo) changed. Observers (the
// renderer, panels) use it to update their projection of the model.
//
// Cell changes are grouped by page id (core::Page::id(); "" for the page of a
// bare <mxGraphModel> file). Pages with duplicate ids share one entry, which
// only makes observers refresh more than needed.
//
// Recording rules (all idempotent; later records fold into earlier ones as
// if the changes happened in sequence):
//   cell added   then removed  -> no net change
//   cell removed then added    -> changed
//   cell added   then changed  -> added
//   cell added/changed then reordered -> both flags kept (reordered is
//                                        independent of the other three)
//   page added   then removed  -> no net change (its cell records go too)
//   page removed then added    -> page `reset` (rebuild it from scratch)
class ChangeSet {
public:
    struct PageChanges {
        std::set<std::string> added;
        std::set<std::string> removed;
        std::set<std::string> changed;   // attributes, style, value, geometry, wrapper, parent, terminals
        std::set<std::string> reordered; // z-order (position among <root> entries) changed
        // Cells whose set or order of children changed: the parent of every
        // added, removed or reordered cell and the old and new parent of a
        // reparented one (layers included). Not part of empty(): the cell
        // sets above already say that something changed; this one tells the
        // layout manager (ops_layout.h) which parents lost a child, which
        // the removed ids alone cannot. Folding is a plain union.
        std::set<std::string> childrenChanged;
        bool pagePropertiesChanged = false; // <diagram> or <mxGraphModel> attributes
        bool reset = false;                 // page content replaced wholesale: rebuild everything

        [[nodiscard]] bool empty() const
        {
            return added.empty() && removed.empty() && changed.empty() && reordered.empty()
                && !pagePropertiesChanged && !reset;
        }

        friend bool operator==(const PageChanges&, const PageChanges&) = default;
    };

    void cellAdded(const std::string& pageId, const std::string& cellId);
    void cellRemoved(const std::string& pageId, const std::string& cellId);
    void cellChanged(const std::string& pageId, const std::string& cellId);
    void cellReordered(const std::string& pageId, const std::string& cellId);
    // `parentId` gained, lost or reordered a child (PageChanges::childrenChanged).
    void childrenChanged(const std::string& pageId, const std::string& parentId);
    void pageChanged(const std::string& pageId);

    void pageAdded(const std::string& pageId);
    void pageRemoved(const std::string& pageId);
    // The order of pages (or of foreign nodes between them) changed.
    void pagesReordered() { pagesReordered_ = true; }
    // <mxfile> attributes or the document kind changed.
    void documentChanged() { documentChanged_ = true; }

    // Folds `later` into this change set as if both happened in sequence.
    void merge(const ChangeSet& later);

    [[nodiscard]] bool empty() const;
    // Per-page cell and property changes. Pages listed in pagesRemoved() have
    // no entry here.
    [[nodiscard]] const std::map<std::string, PageChanges>& pages() const { return pages_; }
    [[nodiscard]] const std::set<std::string>& pagesAdded() const { return pagesAdded_; }
    [[nodiscard]] const std::set<std::string>& pagesRemoved() const { return pagesRemoved_; }
    [[nodiscard]] bool pagesReorderedFlag() const { return pagesReordered_; }
    [[nodiscard]] bool documentChangedFlag() const { return documentChanged_; }

    friend bool operator==(const ChangeSet&, const ChangeSet&) = default;

private:
    std::map<std::string, PageChanges> pages_;
    std::set<std::string> pagesAdded_;
    std::set<std::string> pagesRemoved_;
    bool pagesReordered_ = false;
    bool documentChanged_ = false;
};

} // namespace qraw::commands
