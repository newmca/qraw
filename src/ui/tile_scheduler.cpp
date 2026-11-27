// TileScheduler (qraw/ui/tile_scheduler.h).

#include "qraw/ui/tile_scheduler.h"

#include "canvas_layout.h"
#include "qraw/render/stats.h"
#include "qraw/ui/logging.h"

#include <QMetaObject>

#include <algorithm>
#include <cmath>

namespace qraw::ui {

using render::TileKey;
using render::TileRange;
using render::TileRef;
using render::kTileSize;
namespace stats = render::stats;

namespace {

constexpr double kMinZoomBucket = 0.01; // CanvasView::kMinZoom
constexpr double kSeamGuard = 1e-6;     // device pixels: a tile that ends on a border does not reach the next one

QRect aligned(const QRect& deviceRect, double dpr)
{
    return QRectF(QPointF(deviceRect.topLeft()) / dpr, QSizeF(deviceRect.size()) / dpr).toAlignedRect();
}

} // namespace

TileScheduler::TileScheduler(QObject* parent)
    : QObject(parent), buckets_(canvas::zoomSteps(), kMinZoomBucket)
{
    settleTimer_.setSingleShot(true);
    connect(&settleTimer_, &QTimer::timeout, this, &TileScheduler::onSettled);
    batchTimer_.setSingleShot(true);
    connect(&batchTimer_, &QTimer::timeout, this, [this] {
        batchActive_ = false;
        batchPending_.clear();
        flushHeld();
    });
}

TileScheduler::~TileScheduler()
{
    cancelAllInflight();
    pool_.reset(); // waits for the running jobs; their queued results die with this object
}

void TileScheduler::setSynchronous(bool on)
{
    if (on == synchronous_)
        return;
    synchronous_ = on;
    if (on) {
        cancelAllInflight();
        flushHeld();
        batchActive_ = false;
        batchPending_.clear();
        settleTimer_.stop();
        settled_ = true;
        queuedVisible_ = 0;
    }
    if (updateDisplayBucket(false))
        emit displayChanged();
    schedulePass();
}

void TileScheduler::setRasterizer(render::TileRasterFn rasterizer)
{
    cancelAllInflight();
    pool_.reset(); // waits for the running jobs and drops the queued ones, which are never answered
    unanswered_.clear();
    rasterizer_ = std::move(rasterizer);
}

int TileScheduler::workerCount() const
{
    return pool_ ? pool_->threadCount() : render::TileRasterPool::defaultThreadCount();
}

void TileScheduler::ensurePool()
{
    if (!pool_) {
        qCDebug(lcUiCanvas) << "starting the tile raster pool";
        pool_ = std::make_unique<render::TileRasterPool>(
            this, [this](render::TileResult result) { onResult(std::move(result)); },
            render::TileRasterPool::defaultThreadCount(), rasterizer_);
    }
}

// --- view and bucket policy ----------------------------------------------------

void TileScheduler::setView(const TileViewport& view)
{
    if (haveView_ && view.zoom == view_.zoom && view.dpr == view_.dpr) {
        const QPoint moved = view.origin - view_.origin;
        if (!moved.isNull()) {
            // The content moves against the scroll: what appears is on the side the origin left.
            const double length = std::hypot(double(moved.x()), double(moved.y()));
            panDirection_ = QPointF(-moved.x() / length, -moved.y() / length);
        }
    }
    const bool same = haveView_ && view == view_;
    view_ = view;
    haveView_ = true;
    if (view.zoom != zoom_ || view.dpr != dpr_)
        applyZoom(view.zoom, view.dpr);
    else if (!same)
        schedulePass();
}

void TileScheduler::applyZoom(double zoom, double dpr)
{
    zoom_ = zoom;
    dpr_ = dpr;
    if (synchronous_ || displayed_ <= 0) {
        settled_ = true; // nothing on screen to keep: the first tiling is the target
    } else {
        settled_ = false;
        settleTimer_.start(settleDelayMs_);
    }
    updateDisplayBucket(false);
    dropObsoleteExactTilings();
    schedulePass();
}

bool TileScheduler::updateDisplayBucket(bool allowTargetReady)
{
    if (zoom_ <= 0)
        return false;
    const std::optional<double> shown = displayed_ > 0 ? std::optional<double>(displayed_) : std::nullopt;
    bool targetReady = false;
    bool exactReady = false;
    if (!synchronous_ && allowTargetReady && haveView_) {
        if (!settled_) {
            const double target = buckets_.ceiling(zoom_);
            targetReady = target != displayed_ && visibleTilesClean(target);
        } else if (refinementWanted()) {
            exactReady = visibleTilesClean(zoom_);
        }
    }
    // Synchronous mode has no buckets: every zoom is rasterised at its own scale.
    const double bucket = synchronous_
        ? zoom_
        : render::chooseDisplayBucket(buckets_, zoom_, shown, settled_, targetReady, exactReady);
    cache_.setPriorityScale(bucket * dpr_);
    if (bucket == displayed_)
        return false;
    displayed_ = bucket;
    dropObsoleteExactTilings();
    return true;
}

bool TileScheduler::refinementWanted() const
{
    return !synchronous_ && settled_ && displayed_ > 0 && !render::isBucket(buckets_, zoom_)
        && std::abs(displayed_ / zoom_ - 1) > 1e-9;
}

void TileScheduler::dropObsoleteExactTilings()
{
    const double shown = displayScale();
    const double current = zoom_ * dpr_;
    cache_.eraseIf([&](const TileKey& key, const render::TileCache::Entry&) {
        return !render::isBucket(buckets_, key.scale / dpr_) && key.scale != shown && key.scale != current;
    });
}

bool TileScheduler::visibleTilesClean(double bucket) const
{
    const TileRange range = visibleRange(bucket);
    if (range.empty())
        return false;
    for (int ty = range.ty0; ty < range.ty1; ++ty) {
        for (int tx = range.tx0; tx < range.tx1; ++tx) {
            const render::TileCache::Entry* entry = cache_.peek({bucket * dpr_, tx, ty});
            if (entry == nullptr || entry->dirty)
                return false;
        }
    }
    return true;
}

void TileScheduler::onSettled()
{
    settled_ = true;
    const bool changed = updateDisplayBucket(false);
    schedulePass();
    if (changed)
        emit displayChanged();
}

TileRange TileScheduler::visibleRange(double bucket) const
{
    if (!haveView_ || bucket <= 0)
        return {};
    const double ratio = view_.zoom / bucket; // device pixels of the screen per pixel of the bucket
    return render::tilesCoveringDevice(QRectF(-view_.origin.x() / ratio, -view_.origin.y() / ratio,
                                              view_.deviceSize.width() / ratio, view_.deviceSize.height() / ratio));
}

int TileScheduler::toDevice(double sceneCoord, int origin) const
{
    return origin + static_cast<int>(std::lround(sceneCoord * view_.zoom * view_.dpr));
}

QRect TileScheduler::tileDestRect(const TileKey& key) const
{
    // Edges are rounded, not the size: neighbouring tiles meet on the same device pixel.
    const QRectF scene = render::tileSceneRect(key);
    const int x0 = toDevice(scene.left(), view_.origin.x());
    const int y0 = toDevice(scene.top(), view_.origin.y());
    const int x1 = toDevice(scene.right(), view_.origin.x());
    const int y1 = toDevice(scene.bottom(), view_.origin.y());
    return QRect(x0, y0, x1 - x0, y1 - y0);
}

// --- snapshot and damage -----------------------------------------------------------

void TileScheduler::setSnapshot(std::shared_ptr<const render::RenderSnapshot> snapshot, const render::Damage& damage)
{
    // What already arrived was rasterised from the previous snapshot: it enters the
    // cache first, so that this damage invalidates it where it applies.
    flushHeld();
    batchActive_ = false;
    batchPending_.clear();
    batchTimer_.stop();
    snapshot_ = std::move(snapshot);
    if (damage.empty())
        return;

    cache_.invalidateEverywhere(damage);
    // A job in flight that this damage touches shows content that is gone.
    std::vector<TileKey> doomed;
    for (const auto& [key, job] : inflight_) {
        bool touched = damage.everything;
        for (std::size_t i = 0; !touched && i < damage.rects.size(); ++i)
            touched = render::tilesCovering(damage.rects[i], key.scale).contains(key.tx, key.ty);
        if (touched)
            doomed.push_back(key);
    }
    for (const TileKey& key : doomed)
        cancelInflight(key);

    // Stale images are kept where somebody will see them soon: the displayed
    // bucket's visible tiles and the ring around them. Elsewhere a stale tile
    // would be shown after a scroll or a zoom, before its replacement exists.
    TileRange ring = visibleRange(displayed_);
    ring = {ring.tx0 - 1, ring.ty0 - 1, ring.tx1 + 1, ring.ty1 + 1};
    const double scale = displayScale();
    cache_.eraseIf([&](const TileKey& key, const render::TileCache::Entry& entry) {
        return entry.dirty && !(key.scale == scale && ring.contains(key.tx, key.ty));
    });

    if (!synchronous_ && haveView_ && displayed_ > 0) {
        std::vector<TileKey> dirty;
        const TileRange visible = visibleRange(displayed_);
        for (int ty = visible.ty0; ty < visible.ty1; ++ty) {
            for (int tx = visible.tx0; tx < visible.tx1; ++tx) {
                const TileKey key{scale, tx, ty};
                const render::TileCache::Entry* entry = cache_.peek(key);
                if (entry != nullptr && entry->dirty)
                    dirty.push_back(key);
            }
        }
        // A few cheap tiles under the user's hands: rasterised now, so that typing
        // and small edits never show a stale frame.
        double cost = 0;
        if (dirty.size() <= std::size_t(kSyncMaxTiles)) {
            for (const TileKey& key : dirty) {
                cost += render::estimateTileCostUs(*snapshot_, key, kSyncBudgetUs);
                if (cost > kSyncBudgetUs)
                    break;
            }
        }
        if (!dirty.empty() && dirty.size() <= std::size_t(kSyncMaxTiles) && cost <= kSyncBudgetUs) {
            for (const TileKey& key : dirty) {
                rasterizeNow(key);
                emitTileArea(key);
            }
        } else if (!dirty.empty()) {
            batchActive_ = true;
            batchPending_.insert(dirty.begin(), dirty.end());
            batchTimer_.start(kBatchDeadlineMs);
        }
    }
    schedulePass();
}

void TileScheduler::reset()
{
    cancelAllInflight();
    held_.clear();
    batchActive_ = false;
    batchPending_.clear();
    batchTimer_.stop();
    cache_.clear();
    queuedVisible_ = 0;
    schedulePass();
}

std::vector<TileScheduler::KeptTile> TileScheduler::keepTiles(const std::vector<QRectF>& sceneRects) const
{
    std::vector<KeptTile> kept;
    std::unordered_set<TileKey, render::TileKeyHash> seen;
    for (const double scale : cache_.scales()) {
        for (const QRectF& rect : sceneRects) {
            const TileRange range = render::tilesCovering(rect, scale);
            for (int ty = range.ty0; ty < range.ty1; ++ty) {
                for (int tx = range.tx0; tx < range.tx1; ++tx) {
                    const TileKey key{scale, tx, ty};
                    const render::TileCache::Entry* entry = cache_.peek(key);
                    if (entry == nullptr || entry->dirty)
                        continue;
                    if (seen.insert(key).second)
                        kept.push_back({key, entry->image});
                }
            }
        }
    }
    return kept;
}

void TileScheduler::reinstate(const std::vector<KeptTile>& kept)
{
    if (!snapshot_)
        return;
    for (const KeptTile& tile : kept) {
        cancelInflight(tile.key);
        held_.erase(tile.key);
        batchPending_.erase(tile.key);
        cache_.put(tile.key, tile.image, snapshot_->generation());
        emitTileArea(tile.key);
    }
    if (batchActive_ && batchPending_.empty()) {
        batchActive_ = false;
        batchTimer_.stop();
        flushHeld();
    }
    schedulePass();
}

void TileScheduler::flushHeld()
{
    for (auto& [key, held] : held_) {
        cache_.put(key, std::move(held.image), held.generation);
        emitTileArea(key);
    }
    held_.clear();
}

void TileScheduler::emitTileArea(const TileKey& key)
{
    if (!haveView_ || key.scale != displayScale())
        return;
    emit areaChanged(aligned(tileDestRect(key), view_.dpr).adjusted(-1, -1, 1, 1));
}

// --- frame composition ------------------------------------------------------------------

std::vector<TileRef> TileScheduler::compose(const TileViewport& view, const QRegion& exposed)
{
    std::vector<TileRef> tiles;
    if (!snapshot_ || view.deviceSize.isEmpty())
        return tiles;
    setView(view);
    updateDisplayBucket(true);
    cache_.beginFrame();

    const double dpr = view_.dpr;
    const double scale = displayScale();
    const double ratio = view_.zoom / displayed_;
    const QRect wanted = exposed.boundingRect();
    const QRectF deviceWanted = exposed.isEmpty()
        ? QRectF(QPointF(0, 0), QSizeF(view_.deviceSize))
        : QRectF(wanted.x() * dpr, wanted.y() * dpr, wanted.width() * dpr, wanted.height() * dpr);
    const TileRange range = render::tilesCoveringDevice(QRectF(
        (deviceWanted.left() - view_.origin.x()) / ratio, (deviceWanted.top() - view_.origin.y()) / ratio,
        deviceWanted.width() / ratio, deviceWanted.height() / ratio));

    for (int ty = range.ty0; ty < range.ty1; ++ty) {
        for (int tx = range.tx0; tx < range.tx1; ++tx) {
            const TileKey key{scale, tx, ty};
            const QRect dest = tileDestRect(key);
            if (!exposed.isEmpty() && !exposed.intersects(aligned(dest, dpr)))
                continue;
            const render::TileCache::Entry* entry = cache_.get(key);
            if (entry == nullptr) {
                if (!synchronous_ && substitute(key, tiles))
                    continue;
                rasterizeNow(key);
                entry = cache_.get(key);
            } else if (entry->dirty && synchronous_) {
                rasterizeNow(key);
                entry = cache_.get(key);
            }
            tiles.push_back({key, entry->version, entry->image, dest.topLeft(), QRectF(QPointF(0, 0), entry->image.size()),
                             dest.size()});
        }
    }
    return tiles;
}

bool TileScheduler::substitute(const TileKey& key, std::vector<TileRef>& out)
{
    // Only the part of the tile that the viewport shows has to be covered: after a
    // large zoom change the new bucket's tile reaches far beyond what the old
    // tiling has cached, and nobody sees that part.
    const double k = view_.zoom * view_.dpr;
    const double guard = 2 / k; // device pixels: the compositor clips, rounding must not open a gap
    const QRectF viewportScene(-view_.origin.x() / k - guard, -view_.origin.y() / k - guard,
                               view_.deviceSize.width() / k + 2 * guard, view_.deviceSize.height() / k + 2 * guard);
    const QRectF scene = render::tileSceneRect(key).intersected(viewportScene);
    if (scene.isEmpty())
        return true; // nothing of it is on screen
    std::vector<double> scales = cache_.scales();
    std::erase(scales, key.scale);
    const auto distance = [&](double s) { return std::abs(std::log(s / key.scale)); };
    std::sort(scales.begin(), scales.end(), [&](double a, double b) { return distance(a) < distance(b); });

    for (const double s : scales) {
        const TileRange range = render::tilesCoveringDevice(QRectF(
            scene.left() * s + kSeamGuard, scene.top() * s + kSeamGuard, scene.width() * s - 2 * kSeamGuard,
            scene.height() * s - 2 * kSeamGuard));
        constexpr long long kMaxPieces = 16; // a bucket far away covers the area with many tiny pieces: not worth it
        if (range.empty() || range.count() > kMaxPieces)
            continue;
        std::vector<TileRef> pieces;
        bool complete = true;
        for (int ty = range.ty0; ty < range.ty1 && complete; ++ty) {
            for (int tx = range.tx0; tx < range.tx1 && complete; ++tx) {
                const TileKey other{s, tx, ty};
                const render::TileCache::Entry* entry = cache_.get(other);
                if (entry == nullptr || entry->dirty) {
                    complete = false;
                    break;
                }
                const QRectF part = render::tileSceneRect(other).intersected(scene);
                if (part.isEmpty())
                    continue;
                const QRect device = render::tileDeviceRect(other);
                const int x0 = toDevice(part.left(), view_.origin.x());
                const int y0 = toDevice(part.top(), view_.origin.y());
                const int x1 = toDevice(part.right(), view_.origin.x());
                const int y1 = toDevice(part.bottom(), view_.origin.y());
                if (x1 <= x0 || y1 <= y0)
                    continue;
                const QRectF source(part.left() * s - device.left(), part.top() * s - device.top(), part.width() * s,
                                    part.height() * s);
                pieces.push_back({other, entry->version, entry->image, QPoint(x0, y0), source, QSize(x1 - x0, y1 - y0)});
            }
        }
        if (!complete)
            continue;
        stats::add(stats::Counter::tilesSubstituted);
        out.insert(out.end(), pieces.begin(), pieces.end());
        return true;
    }
    return false;
}

void TileScheduler::rasterizeNow(const TileKey& key)
{
    cancelInflight(key);
    stats::add(stats::Counter::tilesRasterizedSync);
    cache_.put(key, render::rasterizeTile(*snapshot_, key, view_.dpr), snapshot_->generation());
    batchPending_.erase(key);
}

// --- worker plan ------------------------------------------------------------------------

void TileScheduler::schedulePass()
{
    if (passPending_ || synchronous_)
        return;
    passPending_ = true;
    QMetaObject::invokeMethod(this, [this] { runPass(); }, Qt::QueuedConnection);
}

void TileScheduler::runPass()
{
    passPending_ = false;
    queuedVisible_ = 0;
    if (synchronous_ || !snapshot_ || !haveView_ || displayed_ <= 0)
        return;

    const double scale = displayScale();
    const TileRange visible = visibleRange(displayed_);
    const TileRange ring{visible.tx0 - 1, visible.ty0 - 1, visible.tx1 + 1, visible.ty1 + 1};
    const double ratio = view_.zoom / displayed_;
    const QPointF centre((view_.deviceSize.width() / 2.0 - view_.origin.x()) / ratio / kTileSize,
                         (view_.deviceSize.height() / 2.0 - view_.origin.y()) / ratio / kTileSize);

    const auto needs = [&](const TileKey& key) {
        const render::TileCache::Entry* entry = cache_.peek(key);
        return (entry == nullptr || entry->dirty) && !inflight_.contains(key) && !held_.contains(key);
    };
    std::vector<Want> wants;
    std::unordered_set<TileKey, render::TileKeyHash> wantedKeys;
    for (int ty = ring.ty0; ty < ring.ty1; ++ty) {
        for (int tx = ring.tx0; tx < ring.tx1; ++tx) {
            const TileKey key{scale, tx, ty};
            wantedKeys.insert(key);
            if (!needs(key))
                continue;
            const QPointF offset(tx + 0.5 - centre.x(), ty + 0.5 - centre.y());
            const double distance = std::hypot(offset.x(), offset.y());
            const bool isVisible = visible.contains(tx, ty);
            // Visible: nearest the centre first. Ring: the pan direction first.
            const double order = isVisible ? distance
                                           : 0.25 * distance - (panDirection_.x() * offset.x() + panDirection_.y() * offset.y());
            wants.push_back({key, isVisible ? 0 : 1, order});
        }
    }

    // A settled zoom between two steps: the visible tiles at exactly that zoom
    // (nearest the centre first, after the displayed tiling's own).
    if (refinementWanted()) {
        const QPointF exactCentre((view_.deviceSize.width() / 2.0 - view_.origin.x()) / kTileSize,
                                  (view_.deviceSize.height() / 2.0 - view_.origin.y()) / kTileSize);
        const TileRange range = visibleRange(zoom_);
        for (int ty = range.ty0; ty < range.ty1; ++ty) {
            for (int tx = range.tx0; tx < range.tx1; ++tx) {
                const TileKey key{zoom_ * dpr_, tx, ty};
                wantedKeys.insert(key);
                if (needs(key)) {
                    const QPointF offset(tx + 0.5 - exactCentre.x(), ty + 0.5 - exactCentre.y());
                    wants.push_back({key, 0, 1000 + std::hypot(offset.x(), offset.y())});
                }
            }
        }
    }

    // Idle: the neighbouring buckets' visible tiles, when that is cheap, so that
    // the next zoom step is sharp on its first frame.
    const bool displayedBusy = std::any_of(inflight_.begin(), inflight_.end(),
                                           [&](const auto& item) { return item.first.scale == scale; });
    if (wants.empty() && !displayedBusy) {
        for (const std::optional<double> neighbour : {buckets_.above(displayed_), buckets_.below(displayed_)}) {
            if (!neighbour)
                continue;
            const TileRange range = visibleRange(*neighbour);
            if (range.count() > 64)
                continue;
            // Estimating walks the records of a tile: once a bucket turned out too
            // expensive for this snapshot it is not asked again until the next edit.
            const auto skipped = tooExpensive_.find(*neighbour);
            if (skipped != tooExpensive_.end() && skipped->second == snapshot_->generation())
                continue;
            std::vector<Want> found;
            double cost = 0;
            for (int ty = range.ty0; ty < range.ty1; ++ty) {
                for (int tx = range.tx0; tx < range.tx1; ++tx) {
                    const TileKey key{*neighbour * dpr_, tx, ty};
                    wantedKeys.insert(key);
                    if (!needs(key))
                        continue;
                    cost += render::estimateTileCostUs(*snapshot_, key, kIdleBudgetUs);
                    found.push_back({key, 2, std::hypot(tx + 0.5 - centre.x(), ty + 0.5 - centre.y())});
                    if (cost > kIdleBudgetUs)
                        break;
                }
                if (cost > kIdleBudgetUs)
                    break;
            }
            if (cost <= kIdleBudgetUs)
                wants.insert(wants.end(), found.begin(), found.end());
            else
                tooExpensive_[*neighbour] = snapshot_->generation();
        }
    }

    // Jobs nobody wants any more are cancelled, never waited for.
    std::vector<TileKey> stale;
    for (const auto& [key, job] : inflight_) {
        if (!wantedKeys.contains(key))
            stale.push_back(key);
    }
    for (const TileKey& key : stale)
        cancelInflight(key);

    std::sort(wants.begin(), wants.end(), [](const Want& a, const Want& b) {
        return a.bin != b.bin ? a.bin < b.bin : a.order < b.order;
    });
    queuedVisible_ = int(std::count_if(wants.begin(), wants.end(), [](const Want& w) { return w.bin == 0; }));
    if (wants.empty())
        return;
    ensurePool();
    const std::size_t cap = std::size_t(pool_->threadCount()) * kJobsPerWorker;
    for (const Want& want : wants) {
        if (unanswered_.size() >= cap)
            break;
        submit(want.key);
        if (want.bin == 0)
            --queuedVisible_;
    }
}

void TileScheduler::submit(const TileKey& key)
{
    Inflight job;
    job.id = nextJobId_++;
    job.cancel = std::make_shared<std::atomic<bool>>(false);
    inflight_[key] = job;
    unanswered_.insert(job.id);
    pool_->submit({snapshot_, key, dpr_, job.cancel, job.id});
}

void TileScheduler::cancelInflight(const TileKey& key)
{
    const auto it = inflight_.find(key);
    if (it == inflight_.end())
        return;
    it->second.cancel->store(true, std::memory_order_relaxed);
    inflight_.erase(it); // its answer will find no job and be discarded
}

void TileScheduler::cancelAllInflight()
{
    for (auto& [key, job] : inflight_)
        job.cancel->store(true, std::memory_order_relaxed);
    inflight_.clear();
}

void TileScheduler::onResult(render::TileResult result)
{
    unanswered_.erase(result.id);
    const auto it = inflight_.find(result.key);
    const bool current = it != inflight_.end() && it->second.id == result.id;
    if (current)
        inflight_.erase(it);
    const double scale = displayScale();
    const bool bucketWanted = result.key.scale == scale || (refinementWanted() && result.key.scale == zoom_ * dpr_)
        || (buckets_.above(displayed_) && result.key.scale == *buckets_.above(displayed_) * dpr_)
        || (buckets_.below(displayed_) && result.key.scale == *buckets_.below(displayed_) * dpr_);
    if (!current || result.image.isNull() || !bucketWanted) {
        stats::add(stats::Counter::tilesDiscarded); // cancelled, damaged since, or a bucket nobody shows
        schedulePass();
        return;
    }
    if (batchActive_ && batchPending_.erase(result.key) > 0) {
        held_[result.key] = {std::move(result.image), result.generation};
        if (batchPending_.empty()) {
            batchActive_ = false;
            batchTimer_.stop();
            flushHeld();
        }
    } else {
        cache_.put(result.key, std::move(result.image), result.generation);
        emitTileArea(result.key);
    }
    // The last tile of the exact-scale tiling: it takes over from the bucket above.
    if (refinementWanted() && result.key.scale == zoom_ * dpr_ && updateDisplayBucket(true))
        emit displayChanged();
    schedulePass();
}

bool TileScheduler::isIdle() const
{
    if (synchronous_ || !haveView_ || !snapshot_)
        return true;
    // A running settle timer only matters when it will change what is shown: the
    // bucket, or an exact-scale tiling for a zoom between two steps.
    const bool bucketPending = settleTimer_.isActive()
        && (displayed_ != buckets_.ceiling(zoom_) || !render::isBucket(buckets_, zoom_));
    if (passPending_ || bucketPending || batchActive_ || !held_.empty() || queuedVisible_ > 0)
        return false;
    const TileRange visible = visibleRange(displayed_);
    const double scale = displayScale();
    if (refinementWanted())
        return false; // the exact-scale tiling is on its way
    return std::none_of(inflight_.begin(), inflight_.end(), [&](const auto& item) {
        return item.first.scale == scale && visible.contains(item.first.tx, item.first.ty);
    });
}

} // namespace qraw::ui
