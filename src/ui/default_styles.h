#pragma once
// Set as Default Style on the UI side: which kind of default a cell is for, and
// the application of the defaults to the cells of an insertion. The merge
// itself is commands/default_style.h (pure); the defaults are stored in
// AppSettings (style/defaultVertex, style/defaultEdge).

#include "qraw/commands/default_style.h"
#include "qraw/ui/cell_transfer.h"

namespace qraw::core {
class Cell;
class Page;
} // namespace qraw::core

namespace qraw::ui {

// The kind of default a cell of `page` is for: Edge for edges, EdgeLabel for a
// text or edgeLabel vertex of an edge (EditorUi.setDefaultStyle), else Vertex.
[[nodiscard]] commands::StyleTarget defaultStyleTarget(const core::Page& page, const core::Cell& cell);

// Applies the stored defaults to every cell of `bundle` (Graph.pasteCellStyles
// on the inserted cells and their descendants). Does nothing without defaults.
void applyStoredDefaults(CellBundle& bundle);

} // namespace qraw::ui
