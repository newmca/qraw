#pragma once
// Moving the selection from cell to cell with the keyboard (Tab, Shift+Tab,
// Alt+Tab, Alt+Shift+Tab). Pure functions over the model and the page view; no
// widgets. Port of Graph.selectCell and Graph.selectParentCell
// (js/grapheditor/Graph.js:34710, EditorUi.js onKeyDown) with the differences
// listed in COMPATIBILITY.md ("Keyboard navigation").

#include "qraw/core/page.h"
#include "qraw/render/page_view.h"

#include <string>
#include <string_view>
#include <vector>

namespace qraw::ui::navigation {

// The cells Tab visits, in the order it visits them: the vertices and edges that are shown (the page
// view's states, which are in depth-first document order, so a container comes before its children and
// a layer's cells before the next layer's) and that the user can select (not hidden, not in a locked
// layer). `scope` is the entered group, "" for none: only its descendants are listed. `keep` stays in
// the list even when it is no longer selectable (the cell the user is on, draw.io's `temp == cell`).
[[nodiscard]] std::vector<std::string> tabOrder(const core::Page& page, const render::PageView& view,
                                                std::string_view scope, std::string_view keep = {});

// The cell after (`forward`) or before `current` in `order`, wrapping around at the ends. With no
// current cell, the first one (forward) or the last one. "" for an empty order. A `current` that is not
// in the order counts as none.
[[nodiscard]] std::string neighbour(const std::vector<std::string>& order, std::string_view current,
                                    bool forward);

// Alt+Tab: the first child of `id` that is in `order`'s set of navigable cells; "" when it has none.
[[nodiscard]] std::string firstChild(const core::Page& page, const std::vector<std::string>& order,
                                     std::string_view id);

// Alt+Shift+Tab: the container of `id` (a vertex or an edge, never a layer, and not the entered
// group `scope` itself, which is the top of the view); "" when `id` sits directly on a layer or at the
// top of the entered group.
[[nodiscard]] std::string container(const core::Page& page, std::string_view id, std::string_view scope);

} // namespace qraw::ui::navigation
