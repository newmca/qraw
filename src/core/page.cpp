#include "qraw/core/page.h"

#include "qraw/core/number.h"

#include <algorithm>
#include <unordered_map>
#include <unordered_set>
#include <utility>
#include <vector>

namespace qraw::core {

namespace {
constexpr std::initializer_list<std::string_view> kModelOrder = {
    "dx",   "dy",        "grid",      "gridSize",   "guides",     "tooltips", "connect", "arrows",
    "fold", "page",      "pageScale", "pageWidth",  "pageHeight", "background", "math",  "shadow",    "adaptiveColors"};

const Cell* asCell(const Page::RootEntry& e) { return std::get_if<Cell>(&e); }

Diagnostic pageDiagnostic(const Page& page, const std::string& cellId, std::string message)
{
    Diagnostic d;
    d.severity = Diagnostic::Severity::Warning;
    d.message = std::move(message);
    d.pageId = page.id();
    d.cellId = cellId;
    return d;
}

// Cells whose parent chain loops (or leads into a loop). Each cell is
// visited once, so this is linear in the cell count however long the chains
// are: a walk stops at the first cell whose outcome is already known.
std::unordered_set<const Cell*> cellsWithCyclicParentChain(const Page& page)
{
    enum class State : unsigned char { OnPath, Acyclic, Cyclic };
    std::unordered_map<const Cell*, State> state;
    std::unordered_set<const Cell*> cyclic;
    std::vector<const Cell*> path;
    for (const Cell* start : page.cells()) {
        path.clear();
        const Cell* walk = start;
        State outcome = State::Acyclic;
        while (walk) {
            const auto [it, inserted] = state.try_emplace(walk, State::OnPath);
            if (!inserted) {
                outcome = it->second == State::Acyclic ? State::Acyclic : State::Cyclic;
                break;
            }
            path.push_back(walk);
            walk = walk->parent().empty() ? nullptr : page.findCell(walk->parent());
        }
        for (const Cell* visited : path) {
            state[visited] = outcome;
            if (outcome == State::Cyclic)
                cyclic.insert(visited);
        }
    }
    return cyclic;
}
} // namespace

// --- model attributes --------------------------------------------------------

double Page::dx() const { return parseNumber(modelAttributes_.value("dx"), 0); }
double Page::dy() const { return parseNumber(modelAttributes_.value("dy"), 0); }
bool Page::gridEnabled() const { return modelAttributes_.value("grid") != "0"; }
double Page::gridSize() const { return parseNumber(modelAttributes_.value("gridSize"), 10); }
bool Page::pageVisible() const { return modelAttributes_.value("page") != "0"; }
double Page::pageScale() const { return parseNumber(modelAttributes_.value("pageScale"), 1); }
double Page::pageWidth() const { return parseNumber(modelAttributes_.value("pageWidth"), 827); }
double Page::pageHeight() const { return parseNumber(modelAttributes_.value("pageHeight"), 1169); }
bool Page::math() const { return modelAttributes_.value("math") == "1"; }
bool Page::shadow() const { return modelAttributes_.value("shadow") == "1"; }

std::string Page::adaptiveColors() const { return modelAttributes_.value("adaptiveColors"); }

std::string Page::background() const
{
    const std::string& v = modelAttributes_.value("background");
    return v == "none" ? std::string() : v;
}

void Page::setGridEnabled(bool on) { modelAttributes_.setOrdered("grid", on ? "1" : "0", kModelOrder); }
void Page::setGridSize(double size) { modelAttributes_.setOrdered("gridSize", formatNumber(size), kModelOrder); }
void Page::setPageVisible(bool on) { modelAttributes_.setOrdered("page", on ? "1" : "0", kModelOrder); }
void Page::setPageScale(double scale) { modelAttributes_.setOrdered("pageScale", formatNumber(scale), kModelOrder); }
void Page::setMath(bool on) { modelAttributes_.setOrdered("math", on ? "1" : "0", kModelOrder); }
void Page::setShadow(bool on) { modelAttributes_.setOrdered("shadow", on ? "1" : "0", kModelOrder); }

void Page::setPageSize(double width, double height)
{
    modelAttributes_.setOrdered("pageWidth", formatNumber(width), kModelOrder);
    modelAttributes_.setOrdered("pageHeight", formatNumber(height), kModelOrder);
}

void Page::setAdaptiveColors(std::string_view value)
{
    if (value.empty() || value == "default")
        modelAttributes_.remove("adaptiveColors");
    else
        modelAttributes_.setOrdered("adaptiveColors", value, kModelOrder);
}

void Page::setBackground(std::string_view color)
{
    if (color.empty())
        modelAttributes_.remove("background");
    else
        modelAttributes_.setOrdered("background", color, kModelOrder);
}

// --- entries -----------------------------------------------------------------

void Page::appendEntry(RootEntry entry)
{
    const Cell* c = asCell(entry);
    const bool firstWithId = c && index_.try_emplace(c->id(), rootEntries_.size()).second;
    if (c)
        ++cellCount_;
    rootEntries_.push_back(std::move(entry));
    if (firstWithId)
        linkCell(std::get<Cell>(rootEntries_.back()));
}

const Cell* Page::findCell(std::string_view id) const
{
    const auto it = index_.find(id);
    return it == index_.end() ? nullptr : asCell(rootEntries_[it->second]);
}

Cell* Page::findCell(std::string_view id) { return const_cast<Cell*>(std::as_const(*this).findCell(id)); }

std::vector<const Cell*> Page::cells() const
{
    std::vector<const Cell*> out;
    out.reserve(cellCount_);
    for (const auto& e : rootEntries_) {
        if (const Cell* c = asCell(e))
            out.push_back(c);
    }
    return out;
}

std::vector<Cell*> Page::cells()
{
    std::vector<Cell*> out;
    out.reserve(cellCount_);
    for (auto& e : rootEntries_) {
        if (Cell* c = std::get_if<Cell>(&e))
            out.push_back(c);
    }
    return out;
}

std::optional<std::size_t> Page::zIndex(std::string_view id) const
{
    std::size_t z = 0;
    for (const auto& e : rootEntries_) {
        if (const Cell* c = asCell(e)) {
            if (c->id() == id)
                return z;
            ++z;
        }
    }
    return std::nullopt;
}

Cell* Page::addCell(Cell cell, std::optional<std::size_t> zIndex)
{
    if (cell.id().empty())
        cell.setId(generateId());
    if (index_.contains(cell.id()))
        return nullptr;

    auto pos = rootEntries_.end();
    if (zIndex) {
        std::size_t z = 0;
        for (auto it = rootEntries_.begin(); it != rootEntries_.end(); ++it) {
            if (asCell(*it) && z++ == *zIndex) {
                pos = it;
                break;
            }
        }
    }
    return insertCellEntry(static_cast<std::size_t>(pos - rootEntries_.begin()), std::move(cell));
}

std::optional<Cell> Page::takeCell(std::string_view id)
{
    const auto it = index_.find(id);
    if (it == index_.end())
        return std::nullopt;
    const auto offset = static_cast<std::ptrdiff_t>(it->second);
    unlinkCell(std::get<Cell>(rootEntries_[it->second]));
    Cell taken = std::get<Cell>(std::move(rootEntries_[it->second]));
    rootEntries_.erase(rootEntries_.begin() + offset);
    index_.erase(it);
    if (static_cast<std::size_t>(offset) < rootEntries_.size())
        shiftIndex(static_cast<std::size_t>(offset) + 1, -1);
    --cellCount_;
    return taken;
}

std::optional<std::size_t> Page::entryIndex(std::string_view id) const
{
    const auto it = index_.find(id);
    return it == index_.end() ? std::nullopt : std::optional<std::size_t>(it->second);
}

Cell* Page::insertCellAt(std::size_t entryIndex, Cell cell)
{
    if (entryIndex > rootEntries_.size())
        return nullptr;
    return insertCellEntry(entryIndex, std::move(cell));
}

// Inserting or removing an entry moves the later ones by one place: adds
// `delta` to the position of every entry at `from` or behind. Shifting
// the stored positions is much cheaper than rebuilding the index, which
// matters for operations that add or remove thousands of cells.
void Page::shiftIndex(std::size_t from, std::ptrdiff_t delta)
{
    for (auto& entry : index_) {
        if (entry.second >= from)
            entry.second = static_cast<std::size_t>(static_cast<std::ptrdiff_t>(entry.second) + delta);
    }
}

Cell* Page::insertCellEntry(std::size_t offset, Cell cell)
{
    if (offset < rootEntries_.size()) // appending at the end moves nothing
        shiftIndex(offset, +1);
    const bool firstWithId = index_.try_emplace(cell.id(), offset).second;
    ++cellCount_;
    rootEntries_.insert(rootEntries_.begin() + static_cast<std::ptrdiff_t>(offset), std::move(cell));
    Cell* inserted = std::get_if<Cell>(&rootEntries_[offset]);
    if (firstWithId)
        linkCell(*inserted);
    return inserted;
}

bool Page::replaceCell(Cell cell)
{
    Cell* current = findCell(cell.id());
    if (!current)
        return false;
    const bool relink = current->parent() != cell.parent() || current->source() != cell.source()
                        || current->target() != cell.target();
    if (relink)
        unlinkCell(*current);
    *current = std::move(cell);
    if (relink)
        linkCell(*current);
    return true;
}

// --- bulk changes ----------------------------------------------------------------

namespace {
// Below this many cells the indexes are updated per cell, above it rebuilt.
constexpr std::size_t kRelinkLimit = 64;
} // namespace

bool Page::replaceCells(std::vector<Cell> cells)
{
    std::unordered_set<std::string_view> seen;
    std::vector<Cell*> targets;
    targets.reserve(cells.size());
    for (const Cell& cell : cells) {
        Cell* current = findCell(cell.id());
        if (!current || !seen.insert(cell.id()).second)
            return false;
        targets.push_back(current);
    }
    std::vector<std::size_t> relink; // positions in `cells` whose parent, source or target changes
    for (std::size_t i = 0; i < cells.size(); ++i) {
        const Cell& a = *targets[i];
        const Cell& b = cells[i];
        if (a.parent() != b.parent() || a.source() != b.source() || a.target() != b.target())
            relink.push_back(i);
    }
    const bool rebuild = relink.size() > kRelinkLimit;
    if (!rebuild) {
        for (const std::size_t i : relink)
            unlinkCell(*targets[i]);
    }
    for (std::size_t i = 0; i < cells.size(); ++i)
        *targets[i] = std::move(cells[i]);
    if (rebuild) {
        rebuildGroups();
    } else {
        for (const std::size_t i : relink)
            linkCell(*targets[i]);
    }
    return true;
}

bool Page::moveEntries(const std::vector<std::size_t>& sources, std::size_t landing)
{
    const std::size_t k = sources.size();
    if (k == 0 || landing + k > rootEntries_.size())
        return false;
    std::vector<std::size_t> sorted = sources;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end() || sorted.back() >= rootEntries_.size())
        return false;
    std::vector<std::size_t> moved;
    moved.reserve(k);
    for (const std::size_t source : sources) {
        const Cell* cell = asCell(rootEntries_[source]);
        if (!cell)
            return false;
        moved.push_back(source);
    }

    // The window: from the first position that changes to the last.
    const std::size_t lo = std::min(sorted.front(), landing);
    const std::size_t hi = std::max(sorted.back(), landing + k - 1);
    std::vector<std::size_t> takeFrom;
    takeFrom.reserve(hi - lo + 1);
    std::size_t next = lo; // the next entry that stays, in its old order
    std::size_t skip = 0;  // how many of `sorted` were passed
    for (std::size_t q = lo; q <= hi; ++q) {
        if (q >= landing && q < landing + k) {
            takeFrom.push_back(sources[q - landing]);
            continue;
        }
        while (skip < k && sorted[skip] == next) {
            ++skip;
            ++next;
        }
        takeFrom.push_back(next++);
    }
    permuteEntries(lo, takeFrom, moved);
    return true;
}

bool Page::restoreEntries(const std::vector<std::size_t>& sources, std::size_t landing)
{
    const std::size_t k = sources.size();
    if (k == 0 || landing + k > rootEntries_.size())
        return false;
    std::vector<std::size_t> sorted = sources;
    std::sort(sorted.begin(), sorted.end());
    if (std::adjacent_find(sorted.begin(), sorted.end()) != sorted.end() || sorted.back() >= rootEntries_.size())
        return false;

    const std::size_t lo = std::min(sorted.front(), landing);
    const std::size_t hi = std::max(sorted.back(), landing + k - 1);
    // Where each block element goes: position -> its place in the block.
    std::unordered_map<std::size_t, std::size_t> blockPlace;
    blockPlace.reserve(k);
    for (std::size_t j = 0; j < k; ++j)
        blockPlace.emplace(sources[j], j);
    std::vector<std::size_t> takeFrom;
    takeFrom.reserve(hi - lo + 1);
    std::size_t next = lo; // the next entry outside the block, in its current order
    for (std::size_t q = lo; q <= hi; ++q) {
        if (const auto it = blockPlace.find(q); it != blockPlace.end()) {
            takeFrom.push_back(landing + it->second);
            continue;
        }
        if (next >= landing && next < landing + k)
            next = landing + k;
        takeFrom.push_back(next++);
    }
    std::vector<std::size_t> moved;
    moved.reserve(k);
    for (std::size_t j = 0; j < k; ++j)
        moved.push_back(landing + j);
    permuteEntries(lo, takeFrom, moved);
    return true;
}

void Page::permuteEntries(std::size_t lo, const std::vector<std::size_t>& takeFrom, const std::vector<std::size_t>& moved)
{
    // The cells whose relative order changes leave their parent / source /
    // target lists before the move and enter them again after it, in the
    // right place; a big move rebuilds the lists instead.
    const bool rebuild = moved.size() > kRelinkLimit;
    std::vector<std::string> movedIds;
    if (!rebuild) {
        for (const std::size_t position : moved) {
            const Cell& cell = std::get<Cell>(rootEntries_[position]);
            movedIds.push_back(cell.id());
            unlinkCell(cell);
        }
    }
    std::vector<RootEntry> window;
    window.reserve(takeFrom.size());
    for (const std::size_t from : takeFrom)
        window.push_back(std::move(rootEntries_[from]));
    for (std::size_t i = 0; i < window.size(); ++i)
        rootEntries_[lo + i] = std::move(window[i]);
    // Positions: the first of duplicate ids keeps the entry of the index, so
    // walk back to front and leave an earlier duplicate outside the window alone.
    for (std::size_t q = lo + window.size(); q-- > lo;) {
        if (const Cell* cell = asCell(rootEntries_[q])) {
            const auto it = index_.find(cell->id());
            if (it != index_.end() && it->second >= lo)
                it->second = q;
        }
    }
    if (rebuild) {
        rebuildGroups();
    } else {
        for (const std::string& id : movedIds)
            linkCell(*findCell(id));
    }
}

void Page::rebuildGroups()
{
    byParent_.clear();
    bySource_.clear();
    byTarget_.clear();
    for (std::size_t i = 0; i < rootEntries_.size(); ++i) {
        const Cell* cell = asCell(rootEntries_[i]);
        if (!cell)
            continue;
        const auto it = index_.find(cell->id());
        if (it == index_.end() || it->second != i)
            continue; // not the first cell with this id: not listed
        if (!cell->parent().empty() && cell->parent() != cell->id())
            byParent_.append(cell->parent(), cell->id());
        if (!cell->source().empty())
            bySource_.append(cell->source(), cell->id());
        if (!cell->target().empty())
            byTarget_.append(cell->target(), cell->id());
    }
}

// --- parent / source / target indexes ------------------------------------------

namespace {
const std::vector<std::string> kNoIds;
}

const Page::GroupedIds::Ids* Page::GroupedIds::find(std::string_view key) const
{
    const auto it = groups_.find(key);
    return it == groups_.end() ? nullptr : &it->second;
}

// Every listed id has an entry in `entries`, so both operations binary-search
// the group on the entry index of the id.
void Page::GroupedIds::link(std::string_view key, const std::string& id, const IdIndex& entries)
{
    Ids& group = groups_.try_emplace(std::string(key)).first->second;
    const std::size_t entry = entries.find(id)->second;
    const auto position = std::lower_bound(group.begin(), group.end(), entry, [&](const std::string& other, std::size_t e) {
        return entries.find(other)->second < e;
    });
    group.insert(position, id);
}

void Page::GroupedIds::append(std::string_view key, const std::string& id)
{
    groups_.try_emplace(std::string(key)).first->second.push_back(id);
}

void Page::GroupedIds::unlink(std::string_view key, const std::string& id, const IdIndex& entries)
{
    const auto it = groups_.find(key);
    if (it == groups_.end())
        return;
    Ids& group = it->second;
    const std::size_t entry = entries.find(id)->second;
    const auto position = std::lower_bound(group.begin(), group.end(), entry, [&](const std::string& other, std::size_t e) {
        return entries.find(other)->second < e;
    });
    if (position != group.end() && *position == id)
        group.erase(position);
    if (group.empty())
        groups_.erase(it);
}

void Page::linkCell(const Cell& cell)
{
    if (!cell.parent().empty() && cell.parent() != cell.id())
        byParent_.link(cell.parent(), cell.id(), index_);
    if (!cell.source().empty())
        bySource_.link(cell.source(), cell.id(), index_);
    if (!cell.target().empty())
        byTarget_.link(cell.target(), cell.id(), index_);
}

void Page::unlinkCell(const Cell& cell)
{
    if (!cell.parent().empty() && cell.parent() != cell.id())
        byParent_.unlink(cell.parent(), cell.id(), index_);
    if (!cell.source().empty())
        bySource_.unlink(cell.source(), cell.id(), index_);
    if (!cell.target().empty())
        byTarget_.unlink(cell.target(), cell.id(), index_);
}

const Cell* Page::rootCell() const
{
    for (const auto& e : rootEntries_) {
        if (const Cell* c = asCell(e); c && c->parent().empty())
            return c;
    }
    return nullptr;
}

const std::vector<std::string>& Page::childIds(std::string_view parentId) const
{
    const GroupedIds::Ids* ids = byParent_.find(parentId);
    return ids ? *ids : kNoIds;
}

std::vector<const Cell*> Page::children(std::string_view parentId) const
{
    const auto& ids = childIds(parentId);
    std::vector<const Cell*> out;
    out.reserve(ids.size());
    for (const std::string& id : ids)
        out.push_back(findCell(id));
    return out;
}

std::optional<std::size_t> Page::childPosition(std::string_view id) const
{
    const Cell* cell = findCell(id);
    if (!cell || cell->parent().empty() || cell->parent() == id)
        return std::nullopt;
    const auto& siblings = childIds(cell->parent());
    const std::size_t entry = index_.find(id)->second;
    const auto position = std::lower_bound(siblings.begin(), siblings.end(), entry, [&](const std::string& other, std::size_t e) {
        return index_.find(other)->second < e;
    });
    return static_cast<std::size_t>(position - siblings.begin());
}

std::vector<const Cell*> Page::connectedEdges(std::string_view id) const
{
    std::vector<const Cell*> out;
    for (const GroupedIds* grouped : {&bySource_, &byTarget_}) {
        if (const GroupedIds::Ids* ids = grouped->find(id)) {
            for (const std::string& edgeId : *ids) {
                if (const Cell* edge = findCell(edgeId); edge && edge->isEdge())
                    out.push_back(edge);
            }
        }
    }
    // Source edges and target edges are each in z-order; merge, dropping an
    // edge that has `id` at both ends.
    std::sort(out.begin(), out.end(), [&](const Cell* a, const Cell* b) { return index_.find(a->id())->second < index_.find(b->id())->second; });
    out.erase(std::unique(out.begin(), out.end()), out.end());
    return out;
}

std::vector<const Cell*> Page::layers() const
{
    const Cell* root = rootCell();
    return root ? children(root->id()) : std::vector<const Cell*>{};
}

std::vector<const Cell*> Page::descendants(std::string_view id) const
{
    std::vector<const Cell*> out;
    std::unordered_set<std::string_view> visited{id}; // guards against parent cycles
    // Depth-first, preserving z-order among siblings: a stack of "next child
    // to visit" cursors instead of recursion, as chains can be deep.
    struct Cursor {
        const std::vector<std::string>* ids;
        std::size_t next;
    };
    std::vector<Cursor> path;
    const auto descend = [&](std::string_view parent) {
        if (const auto& ids = childIds(parent); !ids.empty())
            path.push_back({&ids, 0});
    };
    descend(id);
    while (!path.empty()) {
        Cursor& top = path.back();
        if (top.next == top.ids->size()) {
            path.pop_back();
            continue;
        }
        const std::string& childId = (*top.ids)[top.next++];
        if (!visited.insert(childId).second)
            continue;
        out.push_back(findCell(childId));
        descend(childId); // may invalidate `top`
    }
    return out;
}

std::string Page::generateId()
{
    while (true) {
        std::string candidate = std::to_string(idCounter_++);
        if (!index_.contains(candidate))
            return candidate;
    }
}

PageStats Page::stats() const
{
    PageStats s;
    s.cells = cellCount_;
    for (const Cell* c : cells()) {
        s.vertices += c->isVertex() ? 1 : 0;
        s.edges += c->isEdge() ? 1 : 0;
    }
    s.layers = layers().size();
    return s;
}

std::vector<Diagnostic> Page::validate() const
{
    std::vector<Diagnostic> out;
    std::unordered_set<std::string_view> seen;
    std::size_t roots = 0;
    const std::unordered_set<const Cell*> cyclic = cellsWithCyclicParentChain(*this);
    for (const Cell* c : cells()) {
        if (c->id().empty())
            out.push_back(pageDiagnostic(*this, {}, "cell without id"));
        else if (!seen.insert(c->id()).second)
            out.push_back(pageDiagnostic(*this, c->id(), "duplicate cell id '" + c->id() + "'"));

        if (c->parent().empty())
            ++roots;
        else if (!findCell(c->parent()))
            out.push_back(pageDiagnostic(*this, c->id(), "dangling parent '" + c->parent() + "'"));
        if (!c->source().empty() && !findCell(c->source()))
            out.push_back(pageDiagnostic(*this, c->id(), "dangling source '" + c->source() + "'"));
        if (!c->target().empty() && !findCell(c->target()))
            out.push_back(pageDiagnostic(*this, c->id(), "dangling target '" + c->target() + "'"));
        if (c->isVertex() && c->isEdge())
            out.push_back(pageDiagnostic(*this, c->id(), "cell is both vertex and edge"));

        if (cyclic.contains(c))
            out.push_back(pageDiagnostic(*this, c->id(), "parent chain of '" + c->id() + "' contains a cycle"));
    }
    if (cellCount_ > 0 && roots == 0)
        out.push_back(pageDiagnostic(*this, {}, "page has no root cell (a cell without parent)"));
    return out;
}

} // namespace qraw::core
