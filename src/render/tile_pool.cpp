// TileRasterPool (tile_pool.h).

#include "qraw/render/tile_pool.h"

#include "qraw/render/logging.h"

#include <QMetaObject>
#include <QObject>
#include <QRunnable>
#include <QThread>

#include <algorithm>
#include <utility>

namespace qraw::render {

TileRasterPool::TileRasterPool(QObject* context, Delivery deliver, int threads, TileRasterFn rasterizer)
    : context_(context), deliver_(std::move(deliver)), rasterizer_(std::move(rasterizer))
{
    if (!rasterizer_) {
        rasterizer_ = [](const RenderSnapshot& snapshot, const TileKey& key, double dpr, const std::atomic<bool>& cancelled) {
            return rasterizeTile(snapshot, key, dpr, &cancelled);
        };
    }
    pool_.setMaxThreadCount(std::max(1, threads));
    pool_.setExpiryTimeout(-1); // threads keep their font engines and thread-local caches
    pool_.setObjectName(QStringLiteral("qraw-raster"));
    qCDebug(lcRenderTiles) << "raster pool with" << pool_.maxThreadCount() << "worker thread(s)";
}

TileRasterPool::~TileRasterPool()
{
    pool_.clear();
    pool_.waitForDone();
}

int TileRasterPool::defaultThreadCount()
{
    bool ok = false;
    const int fromEnv = qEnvironmentVariableIntValue("QRAW_RASTER_THREADS", &ok);
    if (ok && fromEnv > 0)
        return std::min(fromEnv, 32);
    return std::clamp(QThread::idealThreadCount() / 2, 2, 8);
}

void TileRasterPool::submit(TileJob job)
{
    // The runnable owns what the worker needs by value and shares the
    // rasteriser (a std::function copy: no reference to this pool's members is
    // used after the job started, except the context pointer, which outlives
    // the pool by the contract above).
    QObject* const context = context_;
    const Delivery deliver = deliver_;
    const TileRasterFn rasterizer = rasterizer_;
    pool_.start(QRunnable::create([context, deliver, rasterizer, job = std::move(job)]() mutable {
        TileResult result;
        result.id = job.id;
        result.key = job.key;
        result.generation = job.snapshot->generation();
        const std::atomic<bool> never{false};
        const std::atomic<bool>& cancelled = job.cancel ? *job.cancel : never;
        if (!cancelled.load(std::memory_order_relaxed))
            result.image = rasterizer(*job.snapshot, job.key, job.dpr, cancelled);
        // Drop the snapshot on this thread, not on the GUI thread that receives the result.
        job.snapshot.reset();
        QMetaObject::invokeMethod(
            context, [deliver, result = std::move(result)]() mutable { deliver(std::move(result)); },
            Qt::QueuedConnection);
    }));
}

} // namespace qraw::render
