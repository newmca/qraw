// TileCache (tile_cache.h).

#include "qraw/render/tile_cache.h"

#include "qraw/render/stats.h"

#include <QByteArray>

#include <algorithm>
#include <tuple>
#include <vector>

namespace qraw::render {

TileCache::TileCache(std::size_t budgetBytes) : budget_(budgetBytes) {}

std::size_t TileCache::defaultBudget()
{
    constexpr std::size_t kDefaultMiB = 192;
    bool ok = false;
    const qulonglong mib = qEnvironmentVariable("QRAW_TILE_CACHE_MB").toULongLong(&ok);
    return static_cast<std::size_t>(ok && mib > 0 ? mib : kDefaultMiB) * 1024 * 1024;
}

std::size_t TileCache::costOf(const QImage& image)
{
    return static_cast<std::size_t>(image.sizeInBytes());
}

const TileCache::Entry* TileCache::get(const TileKey& key)
{
    const auto it = entries_.find(key);
    if (it == entries_.end())
        return nullptr;
    it->second.usedFrame = frame_;
    return &it->second;
}

const TileCache::Entry* TileCache::peek(const TileKey& key) const
{
    const auto it = entries_.find(key);
    return it == entries_.end() ? nullptr : &it->second;
}

void TileCache::put(const TileKey& key, QImage image, std::uint64_t generation)
{
    const auto it = entries_.find(key);
    if (it != entries_.end()) {
        bytes_ -= costOf(it->second.image);
        entries_.erase(it);
    }
    bytes_ += costOf(image);
    Entry entry;
    entry.image = std::move(image);
    entry.generation = generation;
    entry.version = nextVersion_++;
    entry.usedFrame = frame_;
    entries_.emplace(key, std::move(entry));
    trim();
}

std::size_t TileCache::invalidate(const Damage& damage, double scale)
{
    if (damage.everything)
        return invalidateAll();
    std::size_t marked = 0;
    for (const QRectF& rect : damage.rects) {
        const TileRange range = tilesCovering(rect, scale);
        if (range.empty())
            continue;
        const auto mark = [&](Entry& entry) {
            if (!entry.dirty) {
                entry.dirty = true;
                ++marked;
            }
        };
        if (range.count() > static_cast<long long>(entries_.size())) {
            for (auto& [key, entry] : entries_) {
                if (key.scale == scale && range.contains(key.tx, key.ty))
                    mark(entry);
            }
        } else {
            for (int ty = range.ty0; ty < range.ty1; ++ty) {
                for (int tx = range.tx0; tx < range.tx1; ++tx) {
                    const auto it = entries_.find(TileKey{scale, tx, ty});
                    if (it != entries_.end())
                        mark(it->second);
                }
            }
        }
    }
    return marked;
}

std::size_t TileCache::invalidateAll()
{
    std::size_t marked = 0;
    for (auto& [key, entry] : entries_) {
        if (!entry.dirty) {
            entry.dirty = true;
            ++marked;
        }
    }
    return marked;
}

std::size_t TileCache::invalidateEverywhere(const Damage& damage)
{
    if (damage.everything)
        return invalidateAll();
    std::size_t marked = 0;
    for (const double scale : scales())
        marked += invalidate(damage, scale);
    return marked;
}

std::vector<double> TileCache::scales() const
{
    std::vector<double> result;
    for (const auto& [key, entry] : entries_) {
        if (std::find(result.begin(), result.end(), key.scale) == result.end())
            result.push_back(key.scale);
    }
    return result;
}

void TileCache::dropOtherScales(double scale)
{
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (it->first.scale != scale) {
            bytes_ -= costOf(it->second.image);
            it = entries_.erase(it);
            stats::add(stats::Counter::tilesDiscarded);
        } else {
            ++it;
        }
    }
}

std::size_t TileCache::eraseIf(const std::function<bool(const TileKey&, const Entry&)>& drop)
{
    std::size_t removed = 0;
    for (auto it = entries_.begin(); it != entries_.end();) {
        if (drop(it->first, it->second)) {
            bytes_ -= costOf(it->second.image);
            it = entries_.erase(it);
            stats::add(stats::Counter::tilesDiscarded);
            ++removed;
        } else {
            ++it;
        }
    }
    return removed;
}

void TileCache::clear()
{
    entries_.clear();
    bytes_ = 0;
}

void TileCache::setBudget(std::size_t bytes)
{
    budget_ = bytes;
    trim();
}

void TileCache::trim()
{
    // The tiles of the displayed bucket may use the whole budget; the other
    // buckets' (substitutes, the neighbour prefetch, a previous exact-scale
    // tiling) at most a third of it, so that prefetching cannot crowd out what
    // is on screen or double the memory.
    const bool capOthers = priorityScale_ > 0;
    const std::size_t otherBudget = budget_ / 3;
    std::size_t otherBytes = 0;
    if (capOthers) {
        for (const auto& [key, entry] : entries_)
            otherBytes += key.scale != priorityScale_ ? costOf(entry.image) : 0;
    }
    if (bytes_ <= budget_ && otherBytes <= otherBudget)
        return;
    // The other buckets' tiles go first, least recently displayed first within
    // a bucket; the tiles of the frame in progress stay.
    std::vector<std::tuple<bool, std::uint64_t, TileKey>> candidates;
    for (const auto& [key, entry] : entries_) {
        if (entry.usedFrame < frame_)
            candidates.emplace_back(key.scale == priorityScale_, entry.usedFrame, key);
    }
    std::sort(candidates.begin(), candidates.end(), [](const auto& a, const auto& b) {
        return std::tie(std::get<0>(a), std::get<1>(a)) < std::tie(std::get<0>(b), std::get<1>(b));
    });
    for (const auto& [priority, used, key] : candidates) {
        const bool other = capOthers && key.scale != priorityScale_;
        if (bytes_ <= budget_ && !(other && otherBytes > otherBudget))
            continue; // within budget (and, for another bucket's tile, within its share)
        const auto it = entries_.find(key);
        const std::size_t cost = costOf(it->second.image);
        bytes_ -= cost;
        if (other)
            otherBytes -= cost;
        entries_.erase(it);
        stats::add(stats::Counter::tilesDiscarded);
    }
}

} // namespace qraw::render
