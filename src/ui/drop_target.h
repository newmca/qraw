#pragma once
// Where dragged, dropped or pasted cells land: the container under the
// pointer (Graph.getDropTarget) and the "dragged out of its parent" rule of
// mxGraphHandler. Private to the editor UI.
//
// Upstream (relative to $DRAWIO_SRC/src/main/webapp/):
//   js/grapheditor/Graph.js          getDropTarget (~39090), isValidDropTarget (~35843),
//                                    isContainer (~19548), isSwimlane (~34420)
//   mxgraph/src/view/mxGraph.js      getDropTarget (:11977), isValidDropTarget (:11923)
//   mxgraph/src/handler/mxGraphHandler.js  moveCells (:1979), shouldRemoveCellsFromParent (:1947)
//
// Deliberate differences: the pointer picks the topmost visible vertex (not
// mxGraph.getSwimlaneAt's depth-first swimlane, which can pick a lane under
// a sibling that covers it); a collapsed container is never a target, and
// tables and table rows are not (tables are a later package).

#include "qraw/render/page_view.h"

#include <QPointF>

#include <cstdint>
#include <string>
#include <vector>

namespace qraw::ui {

class DiagramScene;
class DocumentSession;

// The container `cells` would be dropped into when released at `scenePos`:
// the topmost vertex under the pointer that is not one of `cells` (or inside
// one), or the nearest ancestor of it, that is a valid drop target. A target
// is valid when it is not locked, not on a hidden or locked layer, not
// `dropTarget=0`, not a `part` unless it is a container, not a table, not a
// lane while a lane is dragged, and is a container (swimlane, `container=1`)
// or an expanded cell that already has children. "" when there is none (the
// cells keep their parent) or when an edge label would leave its edge.
// `cells` may be empty (palette drop, paste).
[[nodiscard]] std::string dropTarget(const DocumentSession& session, const render::PageView& view,
                                     const std::vector<std::string>& cells, QPointF scenePos);
[[nodiscard]] std::string dropTarget(const DocumentSession& session, const DiagramScene& scene,
                                     const std::vector<std::string>& cells, QPointF scenePos);

// The parent the cells get on release, plus the container to highlight.
struct MoveDestination {
    std::string target; // container under the pointer (highlighted); "" if none
    std::string parent; // new parent of cells that are not in it already; "" keeps parents
};

// `target` when there is one; else, as mxGraphHandler.moveCells, the
// insertion parent (DocumentSession::insertionParent) when the first cell's
// parent is a vertex and the pointer left that parent's bounds; else "".
// `allowDrop` false (Alt held) suppresses the target only.
[[nodiscard]] MoveDestination moveDestination(const DocumentSession& session, const render::PageView& view,
                                              const std::vector<std::string>& cells, QPointF scenePos,
                                              bool allowDrop = true);
// The same, looking only at the states the scene's hit grid finds under the
// pointer (what the canvas uses on every mouse move).
[[nodiscard]] MoveDestination moveDestination(const DocumentSession& session, const DiagramScene& scene,
                                              const std::vector<std::string>& cells, QPointF scenePos,
                                              bool allowDrop = true);

} // namespace qraw::ui
