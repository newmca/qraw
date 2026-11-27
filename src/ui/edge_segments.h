#pragma once
// Dragging one segment of an orthogonal edge: where the waypoints end up.
// A port of mxEdgeSegmentHandler (getPreviewPoints, updatePreviewState,
// isDefaultRoute; mxgraph/src/handler/mxEdgeSegmentHandler.js). Plain data in
// and out, so it is unit-testable without a canvas. Private to the editor UI.

#include "edge_handle_model.h"

#include "qraw/render/page_view.h"

#include <QPointF>

#include <cstddef>
#include <vector>

namespace qraw::ui::edgeh {

struct SegmentDrag {
    // The waypoints to store, in the edge parent's coordinates and rounded to
    // 0.01 (mxEdgeHandler.roundLength). Empty: the edge routes the same way
    // without waypoints.
    std::vector<core::Point> points;
    std::vector<QPointF> route; // the preview route (absolute)
};

// The result of dragging the segment `segment` (an index into
// segmentPoints(handles.route)) so that it passes through `pointer`
// (absolute, already snapped). A horizontal segment moves vertically and a
// vertical one horizontally; the neighbouring segments follow, and the edge
// style re-routes the result.
[[nodiscard]] SegmentDrag dragSegment(const render::PageView& view, const render::CellState& edge,
                                      const EdgeHandles& handles, std::size_t segment, QPointF pointer);

} // namespace qraw::ui::edgeh
