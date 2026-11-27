#pragma once
// What the Format panel shows for a selection: the effective value of each
// style key across all selected cells, with "mixed" when they differ
// (draw.io EditorUi.createSelectionState, js/grapheditor/EditorUi.js:1534,
// and Format's getCurrentCellStyle).
// (src/ui/style_summary.cpp). No widgets: unit-testable.

#include "qraw/core/page.h"
#include "qraw/render/stylesheet.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::ui {

struct StyleValue {
    std::optional<std::string> value; // the common effective value; nullopt when absent everywhere or mixed
    bool mixed = false;               // cells disagree (absent vs present also counts as different)
};

// Geometry of the selection for the Arrange tab (absolute coordinates for
// cells whose parent is a layer, parent-relative otherwise -- as draw.io's
// Arrange tab shows mxGeometry values).
struct GeometryValue {
    std::optional<double> x, y, width, height; // nullopt when mixed or no vertex
};

class StyleSummary {
public:
    // Resolves each selected cell's style with `stylesheet` (draw.io's
    // getCurrentCellStyle = stylesheet defaults + named styles + own keys)
    // and aggregates. Unknown ids are ignored.
    [[nodiscard]] static StyleSummary of(const core::Page& page, const std::vector<std::string>& ids,
                                         const render::Stylesheet& stylesheet);

    [[nodiscard]] bool empty() const { return cellCount_ == 0; }
    [[nodiscard]] std::size_t cellCount() const { return cellCount_; }
    [[nodiscard]] std::size_t vertexCount() const { return vertexCount_; }
    [[nodiscard]] std::size_t edgeCount() const { return edgeCount_; }
    [[nodiscard]] bool hasVertices() const { return vertexCount_ > 0; }
    [[nodiscard]] bool hasEdges() const { return edgeCount_ > 0; }
    // Any selected cell has html=1 / any is locked (isCellLocked).
    [[nodiscard]] bool anyHtml() const { return anyHtml_; }
    [[nodiscard]] bool anyLocked() const { return anyLocked_; }

    // The effective value of `key` (resolved style) across the selection.
    [[nodiscard]] StyleValue value(std::string_view key) const;
    // Convenience: value(key) parsed as a number / a "1"/"0" flag, with the
    // draw.io default when absent everywhere. nullopt when mixed.
    [[nodiscard]] std::optional<double> number(std::string_view key, double fallback) const;
    [[nodiscard]] std::optional<bool> flag(std::string_view key, bool fallback) const;
    // fontStyle bit (render::FontStyleBit) set on all / none / mixed.
    [[nodiscard]] std::optional<bool> fontStyleBit(int bit) const;

    [[nodiscard]] const GeometryValue& geometry() const { return geometry_; }

private:
    std::size_t cellCount_ = 0;
    std::size_t vertexCount_ = 0;
    std::size_t edgeCount_ = 0;
    bool anyHtml_ = false;
    bool anyLocked_ = false;
    std::vector<render::StyleMap> styles_;
    GeometryValue geometry_;
};

} // namespace qraw::ui
