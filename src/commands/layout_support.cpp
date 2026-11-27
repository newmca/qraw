#include "layout_support.h"

#include "qraw/commands/queries.h"
#include "qraw/core/number.h"

#include <algorithm>
#include <cmath>
#include <string>
#include <vector>

namespace qraw::commands::ops::detail {

namespace {

using core::Cell;
using core::Page;
using core::Style;

enum class Side { Top, Right, Bottom, Left };

// mxGraph.getSwimlaneDirection (mxGraph.js:9970): the side the title is on.
Side titleSide(const Style& style)
{
    const std::string dir = style.value("direction", "east");
    const bool flipH = style.value("flipH", "0") == "1";
    const bool flipV = style.value("flipV", "0") == "1";
    const std::string horizontal = style.value("horizontal", "1");
    int n = (horizontal == "0" || horizontal == "false") ? 3 : 0;
    if (dir == "north")
        n--;
    else if (dir == "west")
        n += 2;
    else if (dir == "south")
        n += 1;
    const int mod = ((n % 2) + 2) % 2;
    if (flipH && mod == 1)
        n += 2;
    if (flipV && mod == 0)
        n += 2;
    constexpr Side order[] = {Side::Top, Side::Right, Side::Bottom, Side::Left};
    return order[((n % 4) + 4) % 4];
}

bool isTableShape(const Page& page, const Cell& cell)
{
    const Cell* root = page.rootCell();
    if (cell.isEdge() || cell.parent().empty() || (root && cell.parent() == root->id()))
        return false;
    const std::string shape = cell.style().value("shape", "");
    return shape == "table" || shape == "tableRow";
}

} // namespace

double number(const Style& style, std::string_view key, double fallback)
{
    const auto value = style.value(key);
    return value ? core::parseNumber(*value, fallback) : fallback;
}

bool flag(const Style& style, std::string_view key, bool fallback)
{
    return style.value(key, fallback ? "1" : "0") == "1";
}

bool isLocked(const Page& page, const Cell& cell)
{
    const Cell* current = &cell;
    for (std::size_t steps = 0; current != nullptr && steps <= page.cellCount(); ++steps) {
        if (current->style().value("locked", "") == "1")
            return true;
        current = current->parent().empty() ? nullptr : page.findCell(current->parent());
    }
    return false;
}

bool isMovable(const Page& page, const Cell& cell)
{
    return !isLocked(page, cell) && cell.style().value("movable", "1") != "0";
}

bool isSwimlaneLike(const Page& page, const Cell& cell) { return isSwimlane(page, cell) || isTableShape(page, cell); }

Insets startInsets(const Page& page, const Cell& cell)
{
    Insets result;
    double size = 0;
    if (isSwimlane(page, cell))
        size = swimlaneStartSize(page, cell);
    else if (isTableShape(page, cell))
        size = number(cell.style(), "startSize", 40);
    else
        return result;
    switch (titleSide(cell.style())) {
    case Side::Top: result.top = size; break;
    case Side::Left: result.left = size; break;
    case Side::Bottom: result.bottom = size; break;
    case Side::Right: result.right = size; break;
    }
    return result;
}

Insets footerInsets(const Page& page, const Cell& cell)
{
    Insets result;
    const double size = std::max(0.0, std::trunc(number(cell.style(), "footerSize", 0)));
    if (size <= 0)
        return result;
    if (!isSwimlaneLike(page, cell)) {
        result.bottom = size;
        return result;
    }
    switch (titleSide(cell.style())) {
    case Side::Top: result.bottom = size; break;
    case Side::Left: result.right = size; break;
    case Side::Bottom: result.top = size; break;
    case Side::Right: result.left = size; break;
    }
    return result;
}

void applyRouting(core::Style& style, EdgeRouting routing)
{
    if (routing == EdgeRouting::Straight) {
        style.set("noEdgeStyle", "1");
    } else if (routing == EdgeRouting::Elbow) {
        style.remove("noEdgeStyle");
        style.set("edgeStyle", "elbowEdgeStyle");
        style.set("elbow", "vertical");
    } else if (routing == EdgeRouting::Orthogonal) {
        style.remove("noEdgeStyle");
        if (style.value("edgeStyle", "") != "orthogonalEdgeStyle") {
            style.remove("elbow");
            style.set("edgeStyle", "orthogonalEdgeStyle");
        }
    }
}

} // namespace qraw::commands::ops::detail
