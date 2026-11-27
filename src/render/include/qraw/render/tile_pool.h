#pragma once

// Worker threads for tile rasterisation (ARCHITECTURE.md, "Threading rules").
//
// A dedicated QThreadPool (never the global one) whose threads never expire:
// an expired thread loses its font engines and the thread-local text layout
// cache, and the first drawText in a fresh thread costs about 100x a
// warm one. The pool runs jobs; it does not schedule: the owner (the GUI
// thread) decides which tile is next and hands over at most one or two jobs
// per worker, so that a re-prioritisation never waits behind stale work.
//
// A job holds the immutable snapshot, the key, the scale and a cancellation
// token. The worker checks the token between cells and gives up when it is
// set. Every job is answered, rasterised or not: the result (a null image when
// cancelled) goes back to the owner's thread by a queued call (never a
// blocking one) and the owner discards what is stale.

#include "qraw/render/tile_rasterizer.h"

#include <QImage>
#include <QThreadPool>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

class QObject;

namespace qraw::render {

// The rasteriser a worker calls; rasterizeTile by default. Tests inject a slow
// or counting one. Runs on worker threads: it must not touch GUI objects.
using TileRasterFn = std::function<QImage(const RenderSnapshot&, const TileKey&, double dpr,
                                          const std::atomic<bool>& cancelled)>;

struct TileJob {
    std::shared_ptr<const RenderSnapshot> snapshot;
    TileKey key;
    double dpr = 1;
    std::shared_ptr<std::atomic<bool>> cancel; // set by the owner when the result is no longer wanted
    std::uint64_t id = 0;                      // the owner's handle, echoed in the result
};

struct TileResult {
    std::uint64_t id = 0;
    TileKey key;
    std::uint64_t generation = 0; // of the snapshot the tile was rasterised from
    QImage image;                 // null: cancelled before it was finished
};

class TileRasterPool {
public:
    using Delivery = std::function<void(TileResult)>;

    // `deliver` is called on the thread of `context` (queued), once per job.
    // The pool must not outlive `context`'s thread affinity; the destructor
    // cancels nothing by itself, it waits for the running jobs: cancel the
    // tokens first when a fast shutdown matters.
    TileRasterPool(QObject* context, Delivery deliver, int threads, TileRasterFn rasterizer = {});
    ~TileRasterPool();
    TileRasterPool(const TileRasterPool&) = delete;
    TileRasterPool& operator=(const TileRasterPool&) = delete;

    // clamp(idealThreadCount / 2, 2, 8) (paths scaled about 7x and text 5 to 6x
    // at 8 threads and got worse beyond, SMT), or QRAW_RASTER_THREADS.
    [[nodiscard]] static int defaultThreadCount();

    void submit(TileJob job);
    [[nodiscard]] int threadCount() const { return pool_.maxThreadCount(); }
    void waitForDone() { pool_.waitForDone(); }

private:
    QObject* context_;
    Delivery deliver_;
    TileRasterFn rasterizer_;
    QThreadPool pool_;
};

} // namespace qraw::render
