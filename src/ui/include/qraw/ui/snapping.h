#pragma once
// Grid snapping and alignment guides (mxGraphHandler.snap, mxGuide).


#include <QLineF>
#include <QPointF>
#include <QRectF>
#include <Qt>

#include <string>
#include <vector>

namespace qraw::ui {

class DiagramScene;

struct SnapResult {
    QPointF delta;              // the snapped translation
    std::vector<QLineF> guides; // guide lines to draw (scene coordinates)
};

// Reads the grid settings from the scene's current page (gridEnabled,
// gridSize) on every call. Guides are on when CanvasView::guidesEnabled().
class Snapper {
public:
    explicit Snapper(const DiagramScene& scene, bool guidesEnabled = true);

    // mxGraph.snap: round(v / gridSize) * gridSize when the grid is on,
    // else v unchanged.
    [[nodiscard]] double snap(double value) const;
    [[nodiscard]] QPointF snap(QPointF point) const;

    // The translation for moving `bounds` by `delta` (mxGraphHandler.getDelta
    // + snap, mxgraph/src/handler/mxGraphHandler.js:1092/1108, and
    // mxGuide.move, mxgraph/src/util/mxGuide.js:204):
    //  1. With guides on and Alt not held: compare the moved bounds' left,
    //     centre and right x (top, middle, bottom y) with the same lines of
    //     every vertex state not in `moving` (and not a descendant of one)
    //     that intersects the visible area; the closest within
    //     tolerance = gridSize / 2 (mxGuide.getGuideTolerance, grid on) or
    //     kHitTolerance px (grid off) snaps that axis and yields a guide
    //     line spanning both shapes.
    //  2. An axis without a guide snaps to the grid when the grid is on:
    //     the moved bounds' top-left lands on the grid (mxGraphHandler
    //     snaps the delta so that bounds.x + dx is a grid multiple).
    //  3. Alt held disables both (mxGraphHandler: isGridEnabledEvent).
    // `pixel` is page units per screen pixel (1 / zoom).
    [[nodiscard]] SnapResult snapMove(const QRectF& bounds, QPointF delta, const std::vector<std::string>& moving,
                                      Qt::KeyboardModifiers modifiers, double pixel) const;

    // Resize snapping (mxVertexHandler.union with gridEnabled): the moved
    // edges of `bounds` are snapped to the grid; guides are not used.
    [[nodiscard]] QRectF snapResize(const QRectF& bounds, Qt::KeyboardModifiers modifiers) const;

private:
    const DiagramScene& scene_;
    bool guidesEnabled_;
};

} // namespace qraw::ui
