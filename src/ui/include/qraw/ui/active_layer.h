#pragma once

// The active layer of the canvas: the cells under the user's hands
// (ARCHITECTURE.md, "Canvas rendering": interaction snapshots; Core Animation
// style, docs/research/fast-canvas-architectures.md 3.8).
//
// While a move, resize or rotate gesture runs, the cells it changes are
// lifted out of the content tiles and drawn live on top of them, from the
// display lists they already have, under a translation (or, for the one cell
// of a resize, a record made for the previewed geometry). The tiles under the
// lifted cells are rasterised once without them; after that a mouse move
// costs one replay of a few cells: no tile, no PageView build, no cell
// recorded except the edges that follow a moved end.
//
//   gesture start    liftMove / liftVertex: the tiles that show the cells are
//                    kept (for cancel), the scene stops showing the cells in
//                    its tile snapshot (DiagramScene::setExcluded), and the
//                    layer draws them.
//   every move       setMoveDelta / setVertexGeometry: the layer's records
//                    change, only the area they occupy is repainted.
//   release          commit(): the edit has been applied; the scene shows the
//                    cells in their new place again, and the layer keeps
//                    drawing them until the tiles of that place are ready (no
//                    flash), then drops them.
//   cancel           cancel(): the kept tiles come back at once.
//
// When replaying the lifted cells every frame would cost more than the frame
// budget (hundreds of cells, many glyphs: the estimate of
// render::estimateRecordCostUs), they are rasterised once into one bitmap at
// the current zoom and the layer blits it at a whole device offset
// (the "interaction snapshot"). Edges that are connected to one lifted cell
// and not lifted themselves are re-routed each frame (PageView::previewEdgeState)
// and recorded again, as one cell is cheap.
//
// GUI thread only. Owned by the CanvasView.

#include "qraw/render/compositor.h"
#include "qraw/render/render_snapshot.h"
#include "qraw/ui/tile_scheduler.h"

#include <QImage>
#include <QObject>
#include <QPoint>
#include <QPointF>
#include <QRectF>
#include <QTimer>
#include <QTransform>

#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <unordered_set>
#include <vector>

namespace qraw::ui {

class CanvasView;
class DiagramScene;

class ActiveLayer : public QObject {
    Q_OBJECT

public:
    // Replaying the lifted cells costs more than this per frame (estimate, us):
    // they become a bitmap. A frame at 60 Hz is 16.6 ms; the overlay, the
    // compositor and the event loop need the rest.
    static constexpr double kReplayBudgetUs = 4000;
    // A bitmap bigger than this (device pixels) is not made: the cells are replayed.
    static constexpr long long kMaxBitmapPixels = 16'000'000;
    // The layer is dropped after this long even when the tiles are not ready.
    static constexpr int kRetireTimeoutMs = 1000;
    // How often a retiring layer asks the scheduler whether the tiles are ready.
    static constexpr int kRetirePollMs = 4;

    enum class State {
        Idle,
        Lifted,   // a gesture holds cells
        Retiring, // the edit is done; waiting for the tiles of the new place
    };

    ActiveLayer(CanvasView& view, DiagramScene& scene, TileScheduler& tiles);

    [[nodiscard]] State state() const { return state_; }
    [[nodiscard]] bool isActive() const { return state_ != State::Idle; }
    // The lifted cells are drawn from a bitmap, not replayed (tests, benchmark).
    [[nodiscard]] bool usesBitmap() const { return bitmapInUse_; }
    // Tests: the estimate above which a bitmap is used.
    void setReplayBudgetUs(double us) { budgetUs_ = us; }
    // Ids of the cells the tiles do not show now.
    [[nodiscard]] const std::unordered_set<std::string>& liftedIds() const { return lifted_; }

    // --- a move (or Ctrl+drag duplicate) of `moving` (the topmost cells; their
    // descendants come along). False when there is nothing to lift: the
    // gesture then runs with its overlay alone.
    bool liftMove(const std::vector<std::string>& moving, bool duplicate);
    // The translation (scene units) and whether the copies are shown while the
    // originals stay (Ctrl).
    void setMoveDelta(QPointF delta, bool duplicate);

    // --- the resize or rotation of one vertex: the vertex is drawn from a
    // record made for the previewed geometry, its descendants stay as they
    // are, and the edges attached to it follow.
    bool liftVertex(const std::string& id);
    // Absolute, unrotated bounds and the rotation in degrees (nullopt: unchanged).
    void setVertexGeometry(const QRectF& bounds, std::optional<double> rotation);

    // The gesture's edit was applied.
    void commit();
    // The gesture ended without an edit (Escape, focus loss, no change).
    void cancel();
    // Forgets everything without touching the scene (the view is going away).
    void discard();

    // What the compositor draws on top of the tiles. `sceneToDevice` maps
    // scene units to viewport device pixels (scale and an integer offset).
    [[nodiscard]] std::vector<render::ActiveItem> items(const QTransform& sceneToDevice);
    // The scene area the layer paints now.
    [[nodiscard]] QRectF bounds() const;

private:
    struct Reroute {
        std::string id;
        bool sourceMoves = false;
        bool targetMoves = false;
        std::shared_ptr<const render::CellRecord> record; // the edge as it is drawn now
    };
    enum class Mode { Move, Reshape };

    void collect(const std::vector<std::size_t>& rigid, const std::string& reshaped);
    void setExcluded(bool excluded);
    void rerouteEdges();
    void applyChange(const QRectF& before);
    [[nodiscard]] bool wantsBitmap(double scale);
    [[nodiscard]] bool ensureBitmap(double scale);
    void scheduleRetireCheck();
    void retireIfReady();
    void drop();

    CanvasView& view_;
    DiagramScene& scene_;
    TileScheduler& tiles_;

    State state_ = State::Idle;
    Mode mode_ = Mode::Move;
    bool duplicate_ = false;
    QPointF delta_;
    double budgetUs_ = kReplayBudgetUs;

    // What the layer holds.
    std::vector<std::shared_ptr<const render::CellRecord>> rigid_; // moved as a whole, in paint order
    QRectF rigidBounds_;                                           // union of their visual bounds, untranslated
    std::string reshaped_;                                         // Reshape: the vertex whose record is previewed
    std::shared_ptr<const render::CellRecord> reshapedRecord_;
    render::Rect reshapedBounds_;                                  // Reshape: the previewed geometry
    std::optional<double> reshapedRotation_;
    std::vector<Reroute> reroutes_;
    std::unordered_set<std::string> lifted_; // everything out of the tiles (rigid, reshaped, re-routed)
    std::vector<QRectF> liftedRects_;        // where the tiles show them (visual bounds)

    // For cancel.
    std::vector<TileScheduler::KeptTile> kept_;
    bool keptTaken_ = false;
    std::uint64_t revision_ = 0; // DiagramScene::contentRevision at the lift

    // The interaction snapshot.
    QImage bitmap_;
    double bitmapScale_ = 0;
    QPoint bitmapOrigin_; // device pixel (at bitmapScale_, scene origin) of the bitmap's top-left
    double costScale_ = -1;
    double costUs_ = 0;
    bool bitmapInUse_ = false;

    // Retiring.
    QTimer retireTimer_; // gives up waiting for the tiles
    QTimer retirePoll_;  // asks again whether they are ready
    bool retireCheckPending_ = false;
    QRectF lastBounds_;
};

} // namespace qraw::ui
