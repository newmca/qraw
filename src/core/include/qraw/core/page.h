#pragma once

#include "qraw/core/attributes.h"
#include "qraw/core/cell.h"
#include "qraw/core/diagnostic.h"
#include "qraw/core/foreign_children.h"
#include "qraw/core/xml_node.h"

#include <cstddef>
#include <functional>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <variant>
#include <vector>

namespace qraw::core {

// How a page was stored in the file. Reproduced on save by default.
enum class PageEncoding {
    Inline,     // <diagram><mxGraphModel>...</mxGraphModel></diagram>
    Compressed, // <diagram>base64(deflateRaw(encodeURIComponent(xml)))</diagram>
    Opaque,     // payload that could not be decoded; kept verbatim in diagramForeign()
};

// What surrounds <mxGraphModel> inside a compressed payload: the XML
// declaration, and comments / PIs / DOCTYPE before and after the model.
// Kept so that compressing, reading and re-saving a page loses nothing.
struct PayloadFrame {
    XmlDeclaration declaration;
    std::vector<XmlNode> prolog;
    std::vector<XmlNode> epilog;

    [[nodiscard]] bool empty() const { return !declaration.present && prolog.empty() && epilog.empty(); }
    friend bool operator==(const PayloadFrame&, const PayloadFrame&) = default;
};

struct PageStats {
    std::size_t cells = 0;
    std::size_t vertices = 0;
    std::size_t edges = 0;
    std::size_t layers = 0;
};

// One <diagram> (a page) and its <mxGraphModel>.
//
// <root> entries are kept in document order, which is the z-order. Entries
// the model does not understand stay interleaved with the cells. Pointers
// and references to cells are invalidated by structural changes
// (addCell/removeCell/appendEntry).
class Page {
public:
    using RootEntry = std::variant<Cell, XmlNode>;

    // <diagram> attributes (id, name, unknown ones).
    [[nodiscard]] Attributes& diagramAttributes() { return diagramAttributes_; }
    [[nodiscard]] const Attributes& diagramAttributes() const { return diagramAttributes_; }
    [[nodiscard]] const std::string& id() const { return diagramAttributes_.value("id"); }
    void setId(std::string_view id) { diagramAttributes_.setOrdered("id", id, {"id", "name"}); }
    [[nodiscard]] const std::string& name() const { return diagramAttributes_.value("name"); }
    void setName(std::string_view name) { diagramAttributes_.setOrdered("name", name, {"id", "name"}); }

    [[nodiscard]] PageEncoding encoding() const { return encoding_; }
    void setEncoding(PageEncoding encoding) { encoding_ = encoding; }

    // Children of <diagram> other than <mxGraphModel>. For Opaque pages this
    // holds the undecodable payload.
    [[nodiscard]] ForeignChildren& diagramForeign() { return diagramForeign_; }
    [[nodiscard]] const ForeignChildren& diagramForeign() const { return diagramForeign_; }

    // Declaration, prolog and epilog of a compressed payload. Only written
    // while the page is stored compressed; when saving inline, its comments
    // and PIs are placed around the <mxGraphModel> instead.
    [[nodiscard]] PayloadFrame& payloadFrame() { return payloadFrame_; }
    [[nodiscard]] const PayloadFrame& payloadFrame() const { return payloadFrame_; }

    // False for a <diagram> that had no model at all; a model is written once
    // it has cells or attributes.
    [[nodiscard]] bool hasModel() const { return hasModel_ || !rootEntries_.empty() || !modelAttributes_.empty(); }
    void setHasModel(bool hasModel) { hasModel_ = hasModel; }

    // <mxGraphModel> attributes with typed accessors (draw.io defaults when absent).
    [[nodiscard]] Attributes& modelAttributes() { return modelAttributes_; }
    [[nodiscard]] const Attributes& modelAttributes() const { return modelAttributes_; }
    [[nodiscard]] double dx() const;
    [[nodiscard]] double dy() const;
    [[nodiscard]] bool gridEnabled() const;
    [[nodiscard]] double gridSize() const;
    [[nodiscard]] bool pageVisible() const;
    [[nodiscard]] double pageScale() const;
    [[nodiscard]] double pageWidth() const;
    [[nodiscard]] double pageHeight() const;
    [[nodiscard]] std::string background() const; // empty: none
    [[nodiscard]] bool math() const;
    [[nodiscard]] bool shadow() const;
    // mxGraphModel@adaptiveColors as written ("auto", "simple", "none"); empty when absent (draw.io: auto).
    [[nodiscard]] std::string adaptiveColors() const;
    void setGridEnabled(bool on);
    void setGridSize(double size);
    void setPageVisible(bool on);
    void setPageScale(double scale);
    void setPageSize(double width, double height);
    void setBackground(std::string_view color);
    void setMath(bool on);
    void setShadow(bool on);
    // "" or "default" removes the attribute, as draw.io does (Editor.js setGraphXml).
    void setAdaptiveColors(std::string_view value);

    // Children of <mxGraphModel> other than <root>, and <root>'s attributes.
    [[nodiscard]] ForeignChildren& modelForeign() { return modelForeign_; }
    [[nodiscard]] const ForeignChildren& modelForeign() const { return modelForeign_; }
    [[nodiscard]] Attributes& rootAttributes() { return rootAttributes_; }
    // False only for a model read without a <root> element; <root> is
    // written whenever this is true or the page has entries.
    [[nodiscard]] bool rootElementPresent() const { return rootElementPresent_; }
    void setRootElementPresent(bool present) { rootElementPresent_ = present; }
    [[nodiscard]] const Attributes& rootAttributes() const { return rootAttributes_; }

    // --- <root> entries -------------------------------------------------
    [[nodiscard]] const std::vector<RootEntry>& rootEntries() const { return rootEntries_; }
    // Appends an entry as read from a file. Never rejects anything (duplicate
    // ids are kept; the index points to the first one).
    void appendEntry(RootEntry entry);

    [[nodiscard]] std::size_t cellCount() const { return cellCount_; }
    // The non-const overload is for editing a cell in place. It must not
    // change the cell's id or parent (the id index and the child index
    // would go stale): replace the cell with replaceCell() instead.
    [[nodiscard]] Cell* findCell(std::string_view id);
    [[nodiscard]] const Cell* findCell(std::string_view id) const;
    // All cells in z-order.
    [[nodiscard]] std::vector<const Cell*> cells() const;
    [[nodiscard]] std::vector<Cell*> cells();
    // Position of the cell among the cells (z-order), if present.
    [[nodiscard]] std::optional<std::size_t> zIndex(std::string_view id) const;

    // Inserts a cell before the `zIndex`-th cell (end when absent or out of
    // range). An empty id is replaced by generateId(). Returns nullptr and
    // changes nothing if the id is already used.
    Cell* addCell(Cell cell, std::optional<std::size_t> zIndex = std::nullopt);
    // Removes and returns the cell; its children are not touched.
    std::optional<Cell> takeCell(std::string_view id);
    // Replaces the stored cell that has `cell.id()` by `cell` (same position),
    // keeping the child index right when the parent changes. Returns false
    // when there is no such cell.
    bool replaceCell(Cell cell);

    // Entry-level access, used by commands to restore exact positions even
    // when foreign entries are interleaved with the cells.
    // Position in rootEntries() of the (first) cell with `id`.
    [[nodiscard]] std::optional<std::size_t> entryIndex(std::string_view id) const;
    // Inserts `cell` before rootEntries()[entryIndex] (entryIndex ==
    // rootEntries().size() appends). The id is used as is: it is neither
    // generated nor checked for uniqueness, like appendEntry(). Returns
    // nullptr and changes nothing when entryIndex is out of range.
    Cell* insertCellAt(std::size_t entryIndex, Cell cell);

    // --- bulk changes ---------------------------------------------------------
    // For operations that touch thousands of cells (group, ungroup, bring to
    // front): one pass over the entries, not one per cell (D-036). The result
    // is what the same sequence of single changes gives.
    //
    // Replaces the stored cells that have the ids of `cells` by them, in
    // place, keeping the indexes right. Returns false and changes nothing when
    // an id is unknown or occurs twice.
    bool replaceCells(std::vector<Cell> cells);
    // Moves the entries at `sources` (distinct positions of cells, in the order
    // they take in the block) to one contiguous block that starts at position
    // `landing` of the sequence without them (landing + sources.size() must not
    // exceed the entry count). Every other entry, foreign nodes included, keeps
    // its relative order. Returns false and changes nothing for bad arguments.
    bool moveEntries(const std::vector<std::size_t>& sources, std::size_t landing);
    // The inverse of moveEntries(sources, landing): the block goes back to
    // `sources`.
    bool restoreEntries(const std::vector<std::size_t>& sources, std::size_t landing);

    // --- structure helpers --------------------------------------------
    // The first cell without a parent (draw.io's id "0").
    [[nodiscard]] const Cell* rootCell() const;
    // Direct children of `parentId`, in z-order. O(children): served from an
    // index kept up to date by every mutation (D-025). Cells with a
    // duplicate id are not listed (they are not addressable either).
    [[nodiscard]] std::vector<const Cell*> children(std::string_view parentId) const;
    // Ids of the direct children in z-order, without building cell lists
    // (empty when there are none). Valid until the next mutation.
    [[nodiscard]] const std::vector<std::string>& childIds(std::string_view parentId) const;
    [[nodiscard]] std::size_t childCount(std::string_view parentId) const { return childIds(parentId).size(); }
    // Position of the cell among its parent's children (the position
    // children() would give it); nullopt for cells without a listed parent.
    [[nodiscard]] std::optional<std::size_t> childPosition(std::string_view id) const;
    // Edges whose source or target is `id`, once each, in z-order. O(their
    // number): served from the same kind of index as children().
    [[nodiscard]] std::vector<const Cell*> connectedEdges(std::string_view id) const;
    // Layers are the children of the root cell.
    [[nodiscard]] std::vector<const Cell*> layers() const;
    // All transitive children of `id` (depth-first, z-order), excluding `id`.
    // O(size of the subtree).
    [[nodiscard]] std::vector<const Cell*> descendants(std::string_view id) const;
    // A fresh id that is unused on this page.
    [[nodiscard]] std::string generateId();

    [[nodiscard]] PageStats stats() const;
    // Reference integrity: empty/duplicate ids, dangling parent/source/target,
    // parent cycles, missing root cell. Problems are reported, never fixed.
    [[nodiscard]] std::vector<Diagnostic> validate() const;

private:
    struct StringHash {
        using is_transparent = void;
        std::size_t operator()(std::string_view s) const noexcept { return std::hash<std::string_view>{}(s); }
    };

    using IdIndex = std::unordered_map<std::string, std::size_t, StringHash, std::equal_to<>>;
    // Cell ids grouped by one attribute value (parent, source or target),
    // each group in z-order, i.e. by ascending entry index.
    class GroupedIds {
    public:
        using Ids = std::vector<std::string>;
        [[nodiscard]] const Ids* find(std::string_view key) const;
        void link(std::string_view key, const std::string& id, const IdIndex& entries);
        void unlink(std::string_view key, const std::string& id, const IdIndex& entries);
        // Adds an id behind the ones listed (the caller walks the entries in order).
        void append(std::string_view key, const std::string& id);
        void clear() { groups_.clear(); }

    private:
        std::unordered_map<std::string, Ids, StringHash, std::equal_to<>> groups_;
    };

    void shiftIndex(std::size_t from, std::ptrdiff_t delta);
    Cell* insertCellEntry(std::size_t offset, Cell cell);
    // Upkeep of the parent / source / target indexes. Both need the cell's
    // entry in index_ to be current.
    void linkCell(const Cell& cell);
    void unlinkCell(const Cell& cell);
    // Rebuilds the three indexes from the entries in one pass.
    void rebuildGroups();
    // Puts entries `lo` .. `lo + takeFrom.size() - 1` in the order of the
    // positions in `takeFrom` (entry q of the window becomes the old entry
    // takeFrom[q - lo]) and brings the indexes up to date. `moved` are the ids
    // of the cells whose relative order changes.
    void permuteEntries(std::size_t lo, const std::vector<std::size_t>& takeFrom, const std::vector<std::size_t>& moved);

    Attributes diagramAttributes_;
    PageEncoding encoding_ = PageEncoding::Inline;
    ForeignChildren diagramForeign_;
    PayloadFrame payloadFrame_;
    bool hasModel_ = true;
    Attributes modelAttributes_;
    ForeignChildren modelForeign_;
    Attributes rootAttributes_;
    bool rootElementPresent_ = true;
    std::vector<RootEntry> rootEntries_;
    IdIndex index_; // id -> entry index
    // Kept exact by every mutation (D-025). Cells without the attribute, and
    // cells that are their own parent, are not listed.
    GroupedIds byParent_;
    GroupedIds bySource_;
    GroupedIds byTarget_;
    std::size_t cellCount_ = 0;
    std::size_t idCounter_ = 2; // "0" and "1" are draw.io's root and default layer
};

} // namespace qraw::core
