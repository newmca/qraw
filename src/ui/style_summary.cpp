// StyleSummary: aggregates the resolved styles of the selection (see
// style_summary.h). Ported from draw.io's selection state
// (js/grapheditor/EditorUi.js:1534) and Format.getCurrentCellStyle.
#include "qraw/ui/style_summary.h"

#include "qraw/core/number.h"
#include "qraw/render/resolved_style.h"
#include "qraw/ui/cell_queries.h"

namespace qraw::ui {
namespace {

// One numeric value over several cells: the common value, or nothing when
// the cells disagree or none has one.
class CommonNumber {
public:
    void add(double value)
    {
        if (!any_) {
            any_ = true;
            value_ = value;
        } else if (value != value_) {
            mixed_ = true;
        }
    }

    [[nodiscard]] std::optional<double> value() const
    {
        if (!any_ || mixed_)
            return std::nullopt;
        return value_;
    }

private:
    bool any_ = false;
    bool mixed_ = false;
    double value_ = 0;
};

bool isOnState(const std::optional<std::string>& value)
{
    return value && (*value == "1" || *value == "true");
}

} // namespace

StyleSummary StyleSummary::of(const core::Page& page, const std::vector<std::string>& ids,
                              const render::Stylesheet& stylesheet)
{
    StyleSummary summary;
    CommonNumber x, y, width, height;
    for (const std::string& id : ids) {
        const core::Cell* cell = page.findCell(id);
        if (cell == nullptr)
            continue;

        const bool edge = cell->isEdge();
        ++summary.cellCount_;
        if (edge)
            ++summary.edgeCount_;
        else
            ++summary.vertexCount_;

        const auto kind = edge ? render::CellKind::Edge : render::CellKind::Vertex;
        summary.styles_.push_back(stylesheet.resolve(cell->style(), kind).map());
        summary.anyHtml_ = summary.anyHtml_ || isHtmlLabel(page, id);
        summary.anyLocked_ = summary.anyLocked_ || isCellLocked(page, id);

        if (const core::Geometry* geometry = cell->geometry(); geometry != nullptr && !edge) {
            x.add(geometry->x());
            y.add(geometry->y());
            width.add(geometry->width());
            height.add(geometry->height());
        }
    }
    summary.geometry_ = GeometryValue{x.value(), y.value(), width.value(), height.value()};
    return summary;
}

StyleValue StyleSummary::value(std::string_view key) const
{
    StyleValue result;
    if (styles_.empty())
        return result;

    const std::string* first = styles_.front().find(key);
    bool anyHas = false;
    bool allHave = true;
    bool same = true;
    for (const render::StyleMap& style : styles_) {
        const std::string* current = style.find(key);
        if (current != nullptr)
            anyHas = true;
        else
            allHave = false;
        if ((current == nullptr) != (first == nullptr) || (current != nullptr && *current != *first))
            same = false;
    }

    if (!anyHas)
        return result;
    if (allHave && same)
        result.value = *first;
    else
        result.mixed = true;
    return result;
}

std::optional<double> StyleSummary::number(std::string_view key, double fallback) const
{
    const StyleValue current = value(key);
    if (current.mixed)
        return std::nullopt;
    if (!current.value)
        return fallback;
    return core::parseNumber(*current.value, fallback);
}

std::optional<bool> StyleSummary::flag(std::string_view key, bool fallback) const
{
    const StyleValue current = value(key);
    if (current.mixed)
        return std::nullopt;
    if (!current.value)
        return fallback;
    return isOnState(current.value);
}

std::optional<bool> StyleSummary::fontStyleBit(int bit) const
{
    if (styles_.empty())
        return false;

    std::size_t set = 0;
    for (const render::StyleMap& style : styles_) {
        const auto* text = style.find("fontStyle");
        const int fontStyle = text ? static_cast<int>(core::parseNumber(*text, 0)) : 0;
        if ((fontStyle & bit) != 0)
            ++set;
    }
    if (set == styles_.size())
        return true;
    if (set == 0)
        return false;
    return std::nullopt;
}

} // namespace qraw::ui
