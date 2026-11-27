#pragma once
// The QGraphicsScene projection of the current page (docs/design/ui.md §3).
// It owns the page background, page tiles and grid, and patches the
// projection incrementally when an edit keeps the tree.

#include "qraw/commands/change_set.h"
#include "qraw/render/page_view.h"
#include "qraw/render/render_snapshot.h"
#include "qraw/render/scene_renderer.h"
#include "qraw/render/spatial_grid.h"

#include <QGraphicsScene>
#include <QPointF>
#include <QRectF>

#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace qraw::ui {

class DocumentSession;

// Scene coordinates are page units: a CellState at (x, y) is drawn at scene
// (x, y). Zoom is the view transform only.
//
// The scene listens to DocumentSession::documentChanged and
// currentPageChanged and keeps the render::PageView of the current page. The
// cells are not QGraphicsItems: they are painted from an immutable
// render::RenderSnapshot (published with every projection change, with the
// scene areas it changed in contentDamaged; CanvasView rasterises tiles from
// it) and hit tested through a render::SpatialGrid over the states' boxes,
// whose indices are paint indices (D-035). The QGraphicsScene itself stays for
// the coordinates and for the rotated label editor's proxy widget (no index,
// scene rectangle = the page area). Selection is the SelectionModel's; the
// overlay paints it.
class DiagramScene : public QGraphicsScene {
    Q_OBJECT

public:
    explicit DiagramScene(DocumentSession& session, QObject* parent = nullptr);
    ~DiagramScene() override;

    [[nodiscard]] DocumentSession& session() const { return session_; }
    [[nodiscard]] const render::PageView& pageView() const { return view_; }
    [[nodiscard]] const render::SceneRenderer& renderer() const { return renderer_; }
    [[nodiscard]] const render::CellState* state(std::string_view id) const { return view_.find(id); }

    // --- hit testing (scene coordinates; tolerance in page units) ---------
    // Every selectable cell (cell_queries.h isCellSelectable) whose shape
    // contains `p`, topmost (last painted) first. A vertex is hit inside its
    // rotated bounds grown by `tolerance`; an edge when the squared distance
    // to its polyline (geometry::distanceSquaredToPolyline) is <= tolerance^2
    // or its label box contains p. Labels of vertices count as the vertex.
    [[nodiscard]] std::vector<std::string> cellsAt(QPointF p, double tolerance) const;
    // The first of cellsAt() accepted by `filter` (all when empty), or "".
    [[nodiscard]] std::string cellAt(QPointF p, double tolerance,
                                     const std::function<bool(const render::CellState&)>& filter = {}) const;
    // Selectable cells whose bounds lie completely inside `rect`
    // (mxGraph.getCells used by mxRubberband), in paint order. Only cells
    // whose parent is `scope` ("" = any layer child) are returned.
    [[nodiscard]] std::vector<std::string> cellsInRect(const QRectF& rect, std::string_view scope = {}) const;
    // The paint indices (ascending, indices into pageView().states()) of the
    // states whose box meets `rect` (closed): the box holds the paint bounds
    // grown by a pixel, the rotated bounds and the bounds, so a state that
    // paints or is hit inside `rect` is always among them, and most of the
    // others are not. The candidates of every point and rectangle query
    // (cellsAt, the drop target, the fold controls, the arrows of a hovered
    // vertex); O(the answer), not O(cells).
    [[nodiscard]] std::vector<std::uint32_t> statesIn(const QRectF& rect) const;

    // --- page layout --------------------------------------------------------
    // The page rectangle(s) covering the content (Graph.getPageLayout,
    // js/grapheditor/Graph.js:15483): whole pages of pageWidth x pageHeight
    // (times pageScale) starting at (0, 0), extended in each direction to
    // cover PageView::bounds(). One page when the page is empty.
    [[nodiscard]] QRectF pageArea() const;
    // Union of the given cells' paint bounds (PageView::boundsOf); null when
    // none is known.
    [[nodiscard]] QRectF boundsOf(const std::vector<std::string>& ids) const;

    // Rebuilds everything from the current page (also used on Reset).
    void rebuild();

    // --- rendering ------------------------------------------------------------
    // The current immutable snapshot (never null). Records are shared between
    // consecutive snapshots: an edit records only the cells that changed.
    [[nodiscard]] std::shared_ptr<const render::RenderSnapshot> snapshot() const { return snapshot_; }
    // What the canvas tiles show: the snapshot without the cells the active
    // layer holds (setExcluded); the snapshot itself when none.
    [[nodiscard]] std::shared_ptr<const render::RenderSnapshot> tileSnapshot() const { return tileSnapshot_; }
    // Takes the cells `ids` out of the tiles (the active layer draws them live
    // while the user moves them); an empty set puts them back. The area they
    // occupy is announced as damage, so the tiles under them are rasterised
    // again without them (or with them again).
    void setExcluded(std::unordered_set<std::string> ids);
    [[nodiscard]] const std::unordered_set<std::string>& excluded() const { return excluded_; }
    // Counts the projection changes that alter the content (an edit, a page
    // switch); not the exclusion or the hidden label. A holder of tiles of an
    // earlier revision knows they may be stale.
    [[nodiscard]] std::uint64_t contentRevision() const { return revision_; }
    // Paints `source` of the scene onto `target` like QGraphicsScene::render
    // (which this hides: the scene's items paint nothing), from the snapshot:
    // backdrop, page, grid and every cell, at screen image quality. For the
    // outline view and for tests; the canvas itself composites tiles.
    void render(QPainter* painter, const QRectF& target = QRectF(), const QRectF& source = QRectF(),
                Qt::AspectRatioMode aspectRatioMode = Qt::KeepAspectRatio);

    // The cell whose label is not painted while the label editor shows it
    // (mxCellEditor hides the cell's own text); "" = none. Screen only: no
    // model change, exports are unaffected.
    void setHiddenLabel(std::string_view id);
    [[nodiscard]] const std::string& hiddenLabel() const { return hiddenLabel_; }

signals:
    // A new snapshot was published and these scene areas differ from the
    // previous one (everything on a page switch or a page property change):
    // the tiles they touch are stale. Emitted before projectionChanged().
    void contentDamaged(const qraw::render::Damage& damage);
    // Emitted after the projection was updated for a change, a page switch or a
    // rebuild. Handlers and the overlay refresh on it.
    void projectionChanged();

private:
    void applyChanges(const commands::ChangeSet& changes);
    // Rebuilds the hit grid and the scene rectangle from view_.
    void indexStates();
    // What is painted under the cells: page background (mxGraphModel@background,
    // white default), page tiles when pageVisible and the grid when gridEnabled
    // (draw.io's Graph.createSvgGrid look: minor lines every gridSize, major
    // every 4 * gridSize, skipped when < 4 px apart), drawn by the rasteriser.
    [[nodiscard]] render::PageDecor decor() const;
    // Builds the snapshot of the current view (recording `changed` and the
    // cells whose label visibility changed; everything when `everything`) and
    // announces the damage.
    void publishSnapshot(const std::unordered_set<std::string>* changed, bool everything);

    DocumentSession& session_;
    render::SceneRenderer renderer_;
    render::PageView view_;
    render::SpatialGrid hitGrid_; // the boxes of view_'s states, item = paint index
    std::string hiddenLabel_;
    std::shared_ptr<const render::RenderSnapshot> snapshot_;
    std::shared_ptr<const render::RenderSnapshot> tileSnapshot_;
    std::unordered_set<std::string> excluded_;
    std::uint64_t revision_ = 0;
};

} // namespace qraw::ui
