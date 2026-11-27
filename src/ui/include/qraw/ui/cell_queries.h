#pragma once
// Read-only model queries the UI needs in several places (canvas, tools,
// panels). Implemented in src/ui/cell_queries.cpp. Pure functions over
// core::Page; no Qt widgets.

#include "qraw/core/page.h"

#include <string>
#include <string_view>
#include <vector>

namespace qraw::ui {

// A direct child of the root cell (draw.io model.isLayer).
[[nodiscard]] bool isLayer(const core::Page& page, std::string_view id);

// The layer has visible="0" or its style has locked=1 (Layers dialog,
// LayersWindow: lock sets style locked=1 on the layer cell).
[[nodiscard]] bool isLayerHidden(const core::Page& page, std::string_view layerId);
[[nodiscard]] bool isLayerLocked(const core::Page& page, std::string_view layerId);

// Graph.isCellLocked (js/grapheditor/Graph.js:1655): the cell or any
// ancestor has style locked=1. Locked cells can be selected (except when the
// layer is locked) but not moved, resized, rotated, deleted or connected.
[[nodiscard]] bool isCellLocked(const core::Page& page, std::string_view id);

// Cells of a locked or hidden layer are never selectable or hit
// (LayersWindow lock = "not selectable"). The root and layers themselves are
// not selectable either.
[[nodiscard]] bool isCellSelectable(const core::Page& page, std::string_view id);

// A vertex with at least one child cell (the target of enter-group and of
// "click selects the group first").
[[nodiscard]] bool hasChildren(const core::Page& page, std::string_view id);

// The ancestor of `id` (or `id` itself) whose parent is `scope`; `scope` ""
// means "any layer" (the cell's ancestor that is a child of a layer). ""
// when `id` is not inside `scope`. This is the cell a click on `id` selects
// first (mxGraph.getInitialCellForEvent: topmost selectable ancestor below
// the current root).
[[nodiscard]] std::string ancestorInScope(const core::Page& page, std::string_view id, std::string_view scope);

// The cell's label as the user edits it: the wrapper `label` for wrapped
// cells, else `value` (core::Cell::value already does this).
[[nodiscard]] std::string labelOf(const core::Page& page, std::string_view id);

// Style key html=1 (labels are HTML, edited as rich text).
[[nodiscard]] bool isHtmlLabel(const core::Page& page, std::string_view id);

} // namespace qraw::ui
