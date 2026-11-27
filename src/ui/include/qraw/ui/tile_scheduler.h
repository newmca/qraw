#pragma once

// The scheduler of the canvas' content layer (ARCHITECTURE.md, "Canvas
// rendering": zoom buckets, fill policy, scheduling; "Threading rules").
//
// It owns the TileCache and the worker pool. The GUI thread composes a frame
// from finished tiles and never waits for a worker; workers rasterise tiles of
// the immutable snapshot and hand the images back by queued calls. All
// decisions live here, on the GUI thread:
//
//  * which zoom bucket is on screen (render::chooseDisplayBucket: the tiling
//    on screen stays, scaled, during a zoom gesture; the target bucket's
//    rasterisation starts when the zoom has been stable for the settle delay,
//    when the tiling would be scaled by more than 2x, or at once when the
//    target's visible tiles are all current); a settled zoom between two steps
//    gets one transient exact-scale tiling on top (Fit Window, continuous zoom:
//    drawn 1:1, not the bucket above scaled down), dropped at the next zoom;
//  * what a tile shows while its image is missing or stale, so that nothing
//    visible is ever blank: the exact tile, else the same tile of an older
//    version, else the same area of another bucket (scaled), else (first
//    paint, a jump to an unseen area) the tile is rasterised on the GUI thread
//    (stats tilesRasterizedSync);
//  * what the workers do next: bin 0 the visible tiles of the displayed bucket
//    from the viewport centre outwards, bin 1 a ring of one tile around the
//    viewport biased to the pan direction, bin 2 (only when nothing else is
//    pending and it is cheap) the visible tiles of the neighbouring buckets.
//    At most two jobs per worker are handed over; the plan is recomputed on
//    every scroll, zoom, edit and result, so a stale job is cancelled, never
//    waited for;
//  * edits: damaged tiles keep their old image until the new one arrives,
//    except that a few cheap visible ones are rasterised at once (an edit
//    under the user's hands is never one frame late), and the new tiles of one
//    edit appear together (or after one frame budget).
//
// Synchronous mode (setSynchronous) rasterises everything the frame needs on
// the GUI thread and never starts a worker: deterministic pixels for the
// screenshot harness and for tests that grab the viewport.

#include "qraw/render/compositor.h"
#include "qraw/render/render_snapshot.h"
#include "qraw/render/tile_cache.h"
#include "qraw/render/tile_pool.h"
#include "qraw/render/zoom_buckets.h"

#include <QObject>
#include <QPoint>
#include <QRect>
#include <QRegion>
#include <QSize>
#include <QTimer>

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace qraw::ui {

// What the scheduler needs to know about the view of one paint.
struct TileViewport {
    double zoom = 1;
    double dpr = 1;
    QPoint origin;    // the viewport device pixel that shows scene (0, 0)
    QSize deviceSize; // the viewport in device pixels
    friend bool operator==(const TileViewport&, const TileViewport&) = default;
};

class TileScheduler : public QObject {
    Q_OBJECT

public:
    // Policy constants (also documented in ARCHITECTURE.md).
    static constexpr int kSettleDelayMs = 150;     // zoom stable for this long: start the target bucket
    static constexpr int kBatchDeadlineMs = 16;    // one frame: the new tiles of an edit appear together by then
    static constexpr int kSyncMaxTiles = 4;        // an edit touching more visible tiles is rasterised by workers
    static constexpr double kSyncBudgetUs = 1500;  // ...and so is one whose estimated cost is higher
    static constexpr double kIdleBudgetUs = 40000; // neighbour buckets are prefetched only when this cheap
    static constexpr int kJobsPerWorker = 2;       // handed to the pool at once (running + queued)

    explicit TileScheduler(QObject* parent = nullptr);
    ~TileScheduler() override;

    [[nodiscard]] render::TileCache& cache() { return cache_; }
    [[nodiscard]] const render::TileCache& cache() const { return cache_; }

    void setSynchronous(bool on);
    [[nodiscard]] bool synchronous() const { return synchronous_; }
    // Tests and the benchmark. The settle delay applies to zoom changes after the call.
    void setSettleDelay(int ms) { settleDelayMs_ = ms; }
    // Replaces the rasteriser the workers call (tests: slow or counting jobs).
    // Cancels and waits for the jobs in progress.
    void setRasterizer(render::TileRasterFn rasterizer);
    [[nodiscard]] int workerCount() const;

    // --- state the view reports -----------------------------------------------
    // The view changed (zoom, device pixel ratio, scroll, size): the plan is
    // recomputed on the next turn of the event loop; a zoom or ratio change runs
    // the bucket policy and restarts the settle timer.
    void setView(const TileViewport& view);
    // A new snapshot; `damage` is what differs from the previous one.
    void setSnapshot(std::shared_ptr<const render::RenderSnapshot> snapshot, const render::Damage& damage);
    // The page was replaced: no tile of the old content may be shown.
    void reset();

    // The finished tiles (any zoom) that show part of `sceneRects`, as they
    // are now. Images are immutable and shared, so this holds no copy of the
    // pixels; it only keeps them alive when a new image replaces them. The
    // active layer takes them before it lifts cells out of the tiles, and gives
    // them back with reinstate() when the gesture is cancelled: the picture
    // before the lift returns at once, nothing is rasterised again.
    struct KeptTile {
        render::TileKey key;
        QImage image;
    };
    [[nodiscard]] std::vector<KeptTile> keepTiles(const std::vector<QRectF>& sceneRects) const;
    // Puts kept tiles back as current: they replace what the lifted content
    // made of them, in-flight and pending jobs for them are cancelled. Only
    // valid when the snapshot now is the content they were rasterised from.
    void reinstate(const std::vector<KeptTile>& kept);

    // The bucket on screen and its raster scale (bucket x device pixel ratio).
    [[nodiscard]] double displayBucket() const { return displayed_; }
    [[nodiscard]] double displayScale() const { return displayed_ * dpr_; }

    // The tiles of one paint: for every tile of the displayed bucket that meets
    // `exposed` (logical viewport pixels; empty: the whole viewport), the best
    // image available (see the fill policy above), at its place in device pixels.
    [[nodiscard]] std::vector<render::TileRef> compose(const TileViewport& view, const QRegion& exposed);

    // No work that changes the picture is pending: nothing queued or running
    // for the visible tiles, no settle timer, no held tiles, no substitute left.
    [[nodiscard]] bool isIdle() const;
    // Idle, and no job of any kind (ring or neighbour-bucket prefetch) is
    // running or planned: the benchmark waits for it before a scenario, so that
    // background work of the set-up is not charged to the frames.
    [[nodiscard]] bool isQuiescent() const { return isIdle() && unanswered_.empty() && !passPending_; }
    // Jobs handed to workers and not yet answered (tests).
    [[nodiscard]] int outstandingJobs() const { return int(unanswered_.size()); }

signals:
    // Pixels of the viewport (logical, with a margin) that show a tile that just arrived.
    void areaChanged(const QRect& viewportRect);
    // The displayed bucket changed outside a paint (the settle timer): repaint everything.
    void displayChanged();

private:
    struct Inflight {
        std::uint64_t id = 0;
        std::shared_ptr<std::atomic<bool>> cancel;
    };
    struct Held {
        QImage image;
        std::uint64_t generation = 0;
    };
    struct Want {
        render::TileKey key;
        int bin = 0;
        double order = 0;
    };

    void applyZoom(double zoom, double dpr);
    bool updateDisplayBucket(bool allowTargetReady);
    [[nodiscard]] bool visibleTilesClean(double bucket) const;
    // A zoom with its own tiling (not one of the buckets) that is not on screen yet.
    [[nodiscard]] bool refinementWanted() const;
    // Tiles of a zoom that is neither on screen nor the current one.
    void dropObsoleteExactTilings();
    [[nodiscard]] render::TileRange visibleRange(double bucket) const;
    [[nodiscard]] QRect tileDestRect(const render::TileKey& key) const; // device pixels
    [[nodiscard]] int toDevice(double sceneCoord, int origin) const;

    bool substitute(const render::TileKey& key, std::vector<render::TileRef>& out);
    void rasterizeNow(const render::TileKey& key);
    void schedulePass();
    void runPass();
    void submit(const render::TileKey& key);
    void cancelInflight(const render::TileKey& key);
    void cancelAllInflight();
    void onResult(render::TileResult result);
    void onSettled();
    void flushHeld();
    void emitTileArea(const render::TileKey& key);
    void ensurePool();

    render::TileCache cache_;
    render::ZoomBuckets buckets_;
    std::shared_ptr<const render::RenderSnapshot> snapshot_;
    std::unique_ptr<render::TileRasterPool> pool_;
    render::TileRasterFn rasterizer_;
    bool synchronous_ = false;

    // View.
    TileViewport view_;
    bool haveView_ = false;
    double zoom_ = 1;
    double dpr_ = 1;
    double displayed_ = 0; // 0: none yet
    bool settled_ = true;
    int settleDelayMs_ = kSettleDelayMs;
    QTimer settleTimer_;
    QPointF panDirection_;

    // Work.
    std::unordered_map<render::TileKey, Inflight, render::TileKeyHash> inflight_;
    // The ids of the jobs in the pool whose answer has not arrived: includes cancelled ones. A set and not a
    // counter, because a pool that is thrown away (setRasterizer) drops the jobs it has not started and they
    // are never answered: the set is emptied then, and a late answer of a job that did run finds no id.
    std::unordered_set<std::uint64_t> unanswered_;
    std::uint64_t nextJobId_ = 1;
    bool passPending_ = false;
    int queuedVisible_ = 0; // visible tiles planned by the last pass and not yet handed to a worker
    std::unordered_map<double, std::uint64_t> tooExpensive_; // neighbour bucket -> snapshot generation it was too costly to prefetch for

    // The new tiles of one edit appear together.
    bool batchActive_ = false;
    std::unordered_set<render::TileKey, render::TileKeyHash> batchPending_;
    std::unordered_map<render::TileKey, Held, render::TileKeyHash> held_;
    QTimer batchTimer_;
};

} // namespace qraw::ui
