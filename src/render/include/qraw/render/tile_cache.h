#pragma once

// The tiles rasterised so far (ARCHITECTURE.md, "Canvas rendering").
//
// Key -> {image, snapshot generation}. A tile image is immutable once it is
// put: a change is a new image (a GPU compositor uploads once per version).
// invalidate() marks the tiles a content change touches dirty and keeps their
// old image as a stale entry (the scheduler shows it while the new one renders).
// A byte budget (192 MiB; QRAW_TILE_CACHE_MB) is enforced by priority, then
// least recently displayed: tiles of the displayed zoom bucket outrank the
// other buckets', and tiles requested by the frame in progress are never
// evicted. GUI thread only: workers hand their images over by queued calls
// and the scheduler puts them here.

#include "qraw/render/tile_rasterizer.h"

#include <QImage>

#include <cstddef>
#include <cstdint>
#include <functional>
#include <unordered_map>
#include <vector>

namespace qraw::render {

class TileCache {
public:
    struct Entry {
        QImage image;
        std::uint64_t generation = 0; // the snapshot generation the image shows
        std::uint64_t version = 0;    // changes with every put(): the compositor's upload key
        bool dirty = false;           // stale: the content changed since the image was rasterised
        std::uint64_t usedFrame = 0;  // the last frame that displayed it
    };

    explicit TileCache(std::size_t budgetBytes = defaultBudget());

    // 192 MiB, or QRAW_TILE_CACHE_MB.
    [[nodiscard]] static std::size_t defaultBudget();

    // A new frame begins: tiles get()-ed from now on are the visible ones.
    void beginFrame() { ++frame_; }

    // The entry for `key` (stale or current), marked as displayed by this
    // frame; nullptr when there is none. The pointer is valid until the next
    // put(), invalidate*() or drop*() call.
    [[nodiscard]] const Entry* get(const TileKey& key);
    // The entry without marking it used (tests, scheduling).
    [[nodiscard]] const Entry* peek(const TileKey& key) const;
    // Stores a freshly rasterised tile (replacing any entry), marks it used by
    // this frame and evicts what the budget requires.
    void put(const TileKey& key, QImage image, std::uint64_t generation);

    // Marks every tile of `scale` that shows part of the damage dirty; returns
    // how many entries changed from current to dirty. Tiles of other scales
    // are not touched.
    std::size_t invalidate(const Damage& damage, double scale);
    std::size_t invalidateAll();
    // The same for every scale in the cache (an edit makes every bucket's
    // tiles stale, whichever one is displayed).
    std::size_t invalidateEverywhere(const Damage& damage);
    // The distinct scales that have tiles.
    [[nodiscard]] std::vector<double> scales() const;
    // The scale of the displayed bucket: the budget evicts the other scales'
    // tiles first.
    void setPriorityScale(double scale) { priorityScale_ = scale; }
    // A scale change: removes every tile whose scale is not `scale` (counted in
    // stats tilesDiscarded).
    void dropOtherScales(double scale);
    // Removes the entries `drop` selects (counted in stats tilesDiscarded);
    // returns how many.
    std::size_t eraseIf(const std::function<bool(const TileKey&, const Entry&)>& drop);
    void clear();

    [[nodiscard]] std::size_t size() const { return entries_.size(); }
    [[nodiscard]] std::size_t bytes() const { return bytes_; }
    [[nodiscard]] std::size_t budget() const { return budget_; }
    void setBudget(std::size_t bytes);
    [[nodiscard]] std::uint64_t frame() const { return frame_; }

private:
    void trim();
    [[nodiscard]] static std::size_t costOf(const QImage& image);

    std::unordered_map<TileKey, Entry, TileKeyHash> entries_;
    std::size_t bytes_ = 0;
    std::size_t budget_;
    std::uint64_t frame_ = 1;
    std::uint64_t nextVersion_ = 1;
    double priorityScale_ = 0;
};

} // namespace qraw::render
