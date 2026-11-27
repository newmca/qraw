#include "qraw/commands/ops_layout.h"

#include "layout_support.h"
#include "qraw/commands/ops_structure.h"
#include "qraw/commands/primitives.h"
#include "qraw/commands/queries.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/number.h"
#include "qraw/core/style.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <unordered_map>
#include <unordered_set>

namespace qraw::commands::ops {

namespace {

using core::Cell;
using core::Page;
using core::Point;
using core::Style;
using detail::Insets;
using detail::flag;
using detail::footerInsets;
using detail::isMovable;
using detail::number;
using detail::startInsets;

// --- mxStackLayout ----------------------------------------------------------------

struct Box {
    double x = 0, y = 0, width = 0, height = 0;
};

Box boxOf(const core::Geometry& geometry)
{
    return {geometry.x(), geometry.y(), geometry.width(), geometry.height()};
}

// The Graph.js:12656 configuration of one stack layout parent.
struct StackSettings {
    bool horizontal = true;
    double spacing = 0;
    double border = 0;
    double marginTop = 0, marginLeft = 0, marginRight = 0, marginBottom = 0;
    bool allowGaps = false;
    double gridSize = 0;
    bool fill = true;
    bool resizeParent = true;
    bool resizeParentMax = true;
    bool resizeLast = false;

    static StackSettings of(const Style& style)
    {
        // transparentBounds parents derive their size from their children
        // and keep a pinned geometry: positioned, but never read or resized.
        const bool transparent = style.value("transparentBounds", "") == "1";
        StackSettings s;
        s.resizeParentMax = !transparent && flag(style, "resizeParentMax", true);
        s.horizontal = flag(style, "horizontalStack", true);
        s.resizeParent = !transparent && flag(style, "resizeParent", true);
        s.resizeLast = !transparent && flag(style, "resizeLast", false);
        s.spacing = number(style, "stackSpacing", 0);
        s.border = number(style, "stackBorder", 0);
        s.marginLeft = number(style, "marginLeft", 0);
        s.marginRight = number(style, "marginRight", 0);
        s.marginTop = number(style, "marginTop", 0);
        s.marginBottom = number(style, "marginBottom", 0);
        s.allowGaps = number(style, "allowGaps", 0) != 0;
        s.fill = !transparent;
        if (s.allowGaps)
            s.gridSize = number(style, "stackUnitSize", 20);
        return s;
    }

    // mxStackLayout.snap
    [[nodiscard]] double snap(double value) const
    {
        if (gridSize > 0) {
            value = std::max(value, gridSize);
            if (value / gridSize > 1) {
                const double mod = std::fmod(value, gridSize);
                value += mod > gridSize / 2 ? (gridSize - mod) : -mod;
            }
        }
        return value;
    }
};

// mxLayoutManager getLayout skips collapsed cells.
bool isCollapsedCell(const Cell& cell) { return cell.isCollapsed(); }

// mxGraphLayout.isVertexIgnored: only visible vertices are laid out.
std::vector<std::string> layoutCells(const Page& page, const Cell& parent, const StackSettings& settings)
{
    std::vector<std::string> cells;
    for (const std::string& id : page.childIds(parent.id())) {
        const Cell* child = page.findCell(id);
        if (child != nullptr && child->isVertex() && child->isVisible() && child->geometry() != nullptr)
            cells.push_back(id);
    }
    if (settings.allowGaps) {
        // mxStackLayout.getLayoutCells: the stack is ordered by position.
        std::stable_sort(cells.begin(), cells.end(), [&](const std::string& a, const std::string& b) {
            const core::Geometry& ga = *page.findCell(a)->geometry();
            const core::Geometry& gb = *page.findCell(b)->geometry();
            return settings.horizontal ? ga.x() < gb.x() : ga.y() < gb.y();
        });
    }
    return cells;
}

// mxStackLayout.setChildGeometry: only differing values are written.
bool setChildBox(Transaction& tx, std::size_t pageIndex, const std::string& id, const Box& box)
{
    const Page* page = tx.page(pageIndex);
    const Cell* current = page->findCell(id);
    if (current == nullptr || current->geometry() == nullptr)
        return false;
    Cell changed = *current;
    core::Geometry& geometry = *changed.geometry();
    if (geometry.x() != box.x)
        geometry.setX(box.x);
    if (geometry.y() != box.y)
        geometry.setY(box.y);
    if (geometry.width() != box.width)
        geometry.setWidth(box.width);
    if (geometry.height() != box.height)
        geometry.setHeight(box.height);
    return changed == *current || tx.emplace<ReplaceCell>(pageIndex, std::move(changed));
}

// mxStackLayout.updateParentGeometry
bool updateParentGeometry(Transaction& tx, std::size_t pageIndex, const std::string& parentId, const Box& last,
                          const StackSettings& s)
{
    const Page* page = tx.page(pageIndex);
    const Cell* parent = page->findCell(parentId);
    const Insets start = startInsets(*page, *parent);
    const Insets footer = footerInsets(*page, *parent);
    const core::Geometry& geometry = *parent->geometry();

    double width = geometry.width();
    double height = geometry.height();
    if (s.horizontal) {
        const double tmp = last.x + last.width + s.marginRight + s.border + start.right + footer.right;
        width = s.resizeParentMax ? std::max(width, tmp) : tmp;
    } else {
        const double tmp = last.y + last.height + s.marginBottom + s.border + start.bottom + footer.bottom;
        height = s.resizeParentMax ? std::max(height, tmp) : tmp;
    }
    if (width == geometry.width() && height == geometry.height())
        return true;
    Cell changed = *parent;
    changed.geometry()->setWidth(width);
    changed.geometry()->setHeight(height);
    return tx.emplace<ReplaceCell>(pageIndex, std::move(changed));
}

// mxStackLayout.execute (mxgraph/src/layout/mxStackLayout.js). Not ported:
// `wrap` and `keepFirstLocation` (draw.io never sets them) and borderCollapse=false.
Result executeStack(Transaction& tx, std::size_t pageIndex, const std::string& parentId)
{
    const Page* page = tx.page(pageIndex);
    const Cell* parent = page->findCell(parentId);
    // A parent without geometry (a layer) takes its size from the window upstream.
    if (parent == nullptr || parent->geometry() == nullptr)
        return Result::success();
    const StackSettings s = StackSettings::of(parent->style());
    const bool horizontal = s.horizontal;
    const Box pgeo = boxOf(*parent->geometry());

    double fillValue = horizontal ? pgeo.height - s.marginTop - s.marginBottom : pgeo.width - s.marginLeft - s.marginRight;
    fillValue -= 2 * s.border;
    double x0 = s.border + s.marginLeft;
    double y0 = s.border + s.marginTop;

    // The title may be on any side; strips on the fill axis shrink the
    // children, strips at the near end shift the stack origin.
    Insets start = startInsets(*page, *parent);
    start.left = std::min(start.left, pgeo.width);
    start.top = std::min(start.top, pgeo.height);
    start.right = std::min(start.right, pgeo.width);
    start.bottom = std::min(start.bottom, pgeo.height);
    const Insets footer = footerInsets(*page, *parent);
    if (horizontal)
        fillValue -= start.top + start.bottom + footer.top + footer.bottom;
    else
        fillValue -= start.left + start.right + footer.left + footer.right;
    x0 += start.left + footer.left;
    y0 += start.top + footer.top;

    std::optional<Box> last;
    std::string lastChild;
    double lastValue = 0;
    bool moved = false;
    for (const std::string& id : layoutCells(*page, *parent, s)) {
        page = tx.page(pageIndex);
        const Cell& child = *page->findCell(id);
        Box geo = boxOf(*child.geometry());
        if (!isMovable(*page, child)) {
            // Locked cells stay; the stack goes on after them.
            last = geo;
            lastChild.clear();
            lastValue = horizontal ? geo.x + geo.width : geo.y + geo.height;
            continue;
        }
        moved = true;
        if (last) {
            const double temp = lastValue + s.spacing;
            if (horizontal)
                geo.x = s.snap((s.allowGaps ? std::max(temp, geo.x) : temp) - s.marginLeft) + s.marginLeft;
            else
                geo.y = s.snap((s.allowGaps ? std::max(temp, geo.y) : temp) - s.marginTop) + s.marginTop;
        } else if (horizontal) {
            geo.x = (s.allowGaps && geo.x > x0) ? std::max(s.snap(geo.x - s.marginLeft) + s.marginLeft, x0) : x0;
        } else {
            geo.y = (s.allowGaps && geo.y > y0) ? std::max(s.snap(geo.y - s.marginTop) + s.marginTop, y0) : y0;
        }
        if (horizontal)
            geo.y = y0;
        else
            geo.x = x0;
        if (s.fill) {
            if (horizontal)
                geo.height = fillValue;
            else
                geo.width = fillValue;
        }
        if (horizontal)
            geo.width = s.snap(geo.width);
        else
            geo.height = s.snap(geo.height);

        if (!setChildBox(tx, pageIndex, id, geo))
            return Result::failure("could not arrange cell '" + id + "'");
        lastChild = id;
        last = geo;
        lastValue = horizontal ? geo.x + geo.width : geo.y + geo.height;
    }

    if (!moved)
        last.reset(); // stacks of non-movable cells only are left alone
    if (s.resizeParent && last && !isCollapsedCell(*tx.page(pageIndex)->findCell(parentId))) {
        if (!updateParentGeometry(tx, pageIndex, parentId, *last, s))
            return Result::failure("could not resize '" + parentId + "'");
    } else if (s.resizeLast && last && !lastChild.empty()) {
        if (horizontal)
            last->width = pgeo.width - last->x - s.spacing - s.marginRight - start.right - footer.right;
        else
            last->height = pgeo.height - last->y - s.spacing - s.marginBottom - start.bottom - footer.bottom;
        if (!setChildBox(tx, pageIndex, lastChild, *last))
            return Result::failure("could not resize cell '" + lastChild + "'");
    }
    return Result::success();
}

// mxStackLayout.moveCell (mxStackLayout.js): the cell goes before the first
// sibling whose centre is beyond the point on the stack axis.
Result moveInStack(Transaction& tx, std::size_t pageIndex, const std::string& cellId, const Point& point)
{
    const Page* page = tx.page(pageIndex);
    const Cell* cell = page->findCell(cellId);
    const Cell* parent = cell != nullptr ? page->findCell(cell->parent()) : nullptr;
    if (parent == nullptr)
        return Result::success();
    const bool horizontal = StackSettings::of(parent->style()).horizontal;
    const Point parentOrigin = origin(*page, parent->id());
    const double value = horizontal ? point.x - parentOrigin.x : point.y - parentOrigin.y;

    const std::vector<std::string> siblings = page->childIds(parent->id());
    double last = 0;
    std::size_t i = 0;
    for (; i < siblings.size(); ++i) {
        if (siblings[i] == cellId)
            continue;
        const core::Geometry* bounds = page->findCell(siblings[i])->geometry();
        if (bounds == nullptr)
            continue;
        const double centre = horizontal ? bounds->x() + bounds->width() / 2 : bounds->y() + bounds->height() / 2;
        if (last <= value && centre > value)
            break;
        last = centre;
    }
    const std::size_t current = page->childPosition(cellId).value_or(0);
    const std::size_t index = i > current ? i - 1 : i; // counted without the cell, as mxCell.insert does
    if (index == current)
        return Result::success();
    return reparentCells(tx, pageIndex, {cellId}, parent->id(), index);
}

// --- registry ------------------------------------------------------------------------

struct LayoutHandler {
    // Arranges the children of the parent.
    Result (*execute)(Transaction&, std::size_t page, const std::string& parent);
    // Chooses the child position of a cell dropped at a point; may be null.
    Result (*moveCell)(Transaction&, std::size_t page, const std::string& cell, const Point& point);
};

constexpr LayoutHandler kStackLayout{executeStack, moveInStack};
constexpr LayoutHandler kTableLayout{detail::executeTableLayout, nullptr};
constexpr LayoutHandler kTreeLayout{detail::executeTreeChildLayout, nullptr};
constexpr LayoutHandler kCircleLayout{detail::executeCircleChildLayout, nullptr};
constexpr LayoutHandler kFlowLayout{detail::executeFlowChildLayout, nullptr};

// The handler for the cell's `childLayout` value, or nullptr.
const LayoutHandler* handlerFor(const Cell& parent)
{
    const std::string layout = parent.style().value("childLayout", "");
    if (layout == "stackLayout")
        return &kStackLayout;
    if (layout == "tableLayout")
        return &kTableLayout;
    if (layout == "treeLayout")
        return &kTreeLayout;
    if (layout == "circleLayout")
        return &kCircleLayout;
    if (layout == "flowLayout")
        return &kFlowLayout;
    return nullptr;
}

} // namespace

bool hasChildLayout(const core::Cell& cell)
{
    return cell.styleText().find("childLayout") != std::string::npos && handlerFor(cell) != nullptr;
}

namespace {

// Remembers which cells have a layout while one applyLayouts call runs.
class LayoutLookup {
public:
    explicit LayoutLookup(const Page& page) : page_(page) {}

    [[nodiscard]] bool has(const std::string& id)
    {
        const auto it = cache_.find(id);
        if (it != cache_.end())
            return it->second;
        const Cell* cell = page_.findCell(id);
        const bool result = cell != nullptr && hasChildLayout(*cell);
        cache_.emplace(id, result);
        return result;
    }

private:
    const Page& page_;
    std::unordered_map<std::string, bool> cache_;
};

// mxLayoutManager.addAncestorsWithLayout (bubbling) and addDescendantsWithLayout.
void collectCell(const Page& page, LayoutLookup& lookup, const std::string& id, std::unordered_set<std::string>& out)
{
    const Cell* cell = page.findCell(id);
    if (cell == nullptr)
        return;
    std::string current = id;
    for (std::size_t steps = 0; !current.empty() && steps <= page.cellCount(); ++steps) {
        const Cell* c = page.findCell(current);
        if (c == nullptr || c->parent().empty())
            break; // the root cell has no layout
        if (!out.contains(current) && lookup.has(current))
            out.insert(current);
        current = c->parent();
    }
    if (!lookup.has(id))
        return;
    std::vector<std::string> pending{id};
    while (!pending.empty()) {
        const std::string parent = pending.back();
        pending.pop_back();
        for (const std::string& child : page.childIds(parent)) {
            if (lookup.has(child)) {
                out.insert(child);
                pending.push_back(child);
            }
        }
    }
}

} // namespace

Result applyLayouts(Transaction& tx, std::size_t pageIndex, const ChangeSet& changes)
{
    const Page* page = tx.page(pageIndex);
    if (page == nullptr)
        return Result::success();
    const auto found = changes.pages().find(page->id());
    if (found == changes.pages().end() || found->second.reset)
        return Result::success();
    const ChangeSet::PageChanges& changed = found->second;

    LayoutLookup lookup(*page);
    std::unordered_set<std::string> affected;
    for (const auto* set : {&changed.added, &changed.changed, &changed.reordered, &changed.childrenChanged}) {
        for (const std::string& id : *set)
            collectCell(*page, lookup, id, affected);
    }
    if (affected.empty())
        return Result::success();

    // z-order is a depth-first walk, so ancestors come before their descendants.
    std::vector<std::string> sorted(affected.begin(), affected.end());
    std::erase_if(sorted, [&](const std::string& id) { return !page->zIndex(id); });
    std::sort(sorted.begin(), sorted.end(),
              [&](const std::string& a, const std::string& b) { return *page->zIndex(a) < *page->zIndex(b); });

    const auto run = [&](const std::string& id, bool nestedOnly) -> Result {
        const Page* now = tx.page(pageIndex);
        const Cell* cell = now->findCell(id);
        if (cell == nullptr || isCollapsedCell(*cell))
            return Result::success();
        if (nestedOnly) {
            const Cell* parent = now->findCell(cell->parent());
            if (parent == nullptr || !hasChildLayout(*parent))
                return Result::success();
        }
        const LayoutHandler* handler = handlerFor(*cell);
        return handler != nullptr ? handler->execute(tx, pageIndex, id) : Result::success();
    };
    // BEGIN_UPDATE pass: nested layouts first, deepest first.
    for (auto it = sorted.rbegin(); it != sorted.rend(); ++it) {
        if (Result r = run(*it, true); !r)
            return r;
    }
    // END_UPDATE pass: every layout, outermost first.
    for (const std::string& id : sorted) {
        if (Result r = run(id, false); !r)
            return r;
    }
    return Result::success();
}

Result moveInLayout(Transaction& tx, std::size_t pageIndex, const std::vector<std::string>& ids, const Point& point)
{
    for (const std::string& id : ids) {
        const Page* page = tx.page(pageIndex);
        const Cell* cell = page != nullptr ? page->findCell(id) : nullptr;
        const Cell* parent = cell != nullptr ? page->findCell(cell->parent()) : nullptr;
        if (parent == nullptr || isCollapsedCell(*parent))
            continue;
        const LayoutHandler* handler = handlerFor(*parent);
        if (handler == nullptr || handler->moveCell == nullptr)
            continue;
        if (Result r = handler->moveCell(tx, pageIndex, id, point); !r)
            return r;
    }
    return Result::success(ids);
}

} // namespace qraw::commands::ops
