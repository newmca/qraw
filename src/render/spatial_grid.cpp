// SpatialGrid (spatial_grid.h).

#include "qraw/render/spatial_grid.h"

#include <algorithm>
#include <cmath>

namespace qraw::render {

namespace {

constexpr double kMaxCoordinate = 1e7;
// A rectangle covering more cells than this moves to the next, coarser level.
constexpr long long kMaxCellsPerItem = 64;

std::uint64_t gridKey(long long gx, long long gy)
{
    return (static_cast<std::uint64_t>(gx + (1LL << 31)) << 32) | static_cast<std::uint64_t>(gy + (1LL << 31));
}

struct GridRange {
    long long x0, x1, y0, y1;
    [[nodiscard]] long long cells() const { return (x1 - x0 + 1) * (y1 - y0 + 1); }
};

double cellSizeOf(int level)
{
    double size = SpatialGrid::kCellSize;
    for (int i = 0; i < level; ++i)
        size *= SpatialGrid::kLevelScale;
    return size;
}

GridRange gridRange(const QRectF& r, double cellSize)
{
    return {static_cast<long long>(std::floor(r.left() / cellSize)),
            static_cast<long long>(std::floor(r.right() / cellSize)),
            static_cast<long long>(std::floor(r.top() / cellSize)),
            static_cast<long long>(std::floor(r.bottom() / cellSize))};
}

} // namespace

QRectF sanitizedBounds(const QRectF& r)
{
    if (!std::isfinite(r.left()) || !std::isfinite(r.top()) || !std::isfinite(r.width()) || !std::isfinite(r.height()))
        return {};
    const double l = std::clamp(r.left(), -kMaxCoordinate, kMaxCoordinate);
    const double t = std::clamp(r.top(), -kMaxCoordinate, kMaxCoordinate);
    const double rr = std::clamp(r.right(), -kMaxCoordinate, kMaxCoordinate);
    const double b = std::clamp(r.bottom(), -kMaxCoordinate, kMaxCoordinate);
    return QRectF(QPointF(l, t), QPointF(rr, b));
}

void SpatialGrid::build(std::vector<QRectF> bounds)
{
    bounds_ = std::move(bounds);
    for (Level& level : levels_)
        level.entries.clear();
    large_.clear();
    for (std::size_t i = 0; i < bounds_.size(); ++i) {
        const QRectF b = bounds_[i] = sanitizedBounds(bounds_[i]);
        if (b.isNull())
            continue;
        int chosen = -1;
        GridRange range{};
        for (int l = 0; l < kLevels && chosen < 0; ++l) {
            range = gridRange(b, cellSizeOf(l));
            if (range.cells() <= kMaxCellsPerItem)
                chosen = l;
        }
        if (chosen < 0) {
            large_.push_back(static_cast<std::uint32_t>(i));
            continue;
        }
        std::vector<Entry>& entries = levels_[chosen].entries;
        for (long long gy = range.y0; gy <= range.y1; ++gy) {
            for (long long gx = range.x0; gx <= range.x1; ++gx)
                entries.push_back({gridKey(gx, gy), static_cast<std::uint32_t>(i)});
        }
    }
    for (Level& level : levels_) {
        std::sort(level.entries.begin(), level.entries.end(), [](const Entry& a, const Entry& b) {
            return a.key != b.key ? a.key < b.key : a.item < b.item;
        });
    }
}

void SpatialGrid::query(const QRectF& rect, std::vector<std::uint32_t>& out) const
{
    out.clear();
    if (!std::isfinite(rect.left()) || !std::isfinite(rect.top()) || !std::isfinite(rect.width())
        || !std::isfinite(rect.height()))
        return;
    const QRectF r = sanitizedBounds(rect);
    const auto candidate = [&](std::uint32_t item) {
        const QRectF& b = bounds_[item];
        // Closed intersection: a hairline or an empty-sized item still counts.
        if (b.left() <= r.right() && b.right() >= r.left() && b.top() <= r.bottom() && b.bottom() >= r.top())
            out.push_back(item);
    };
    for (int l = 0; l < kLevels; ++l) {
        const std::vector<Entry>& entries = levels_[l].entries;
        if (entries.empty())
            continue;
        const GridRange range = gridRange(r, cellSizeOf(l));
        if (range.cells() > static_cast<long long>(entries.size())) {
            // A query bigger than the occupied grid: walk the entries instead.
            for (const Entry& entry : entries) {
                const auto gx = static_cast<long long>(entry.key >> 32) - (1LL << 31);
                const auto gy = static_cast<long long>(entry.key & 0xffffffffULL) - (1LL << 31);
                if (gx >= range.x0 && gx <= range.x1 && gy >= range.y0 && gy <= range.y1)
                    candidate(entry.item);
            }
            continue;
        }
        for (long long gy = range.y0; gy <= range.y1; ++gy) {
            for (long long gx = range.x0; gx <= range.x1; ++gx) {
                const std::uint64_t key = gridKey(gx, gy);
                auto it = std::lower_bound(entries.begin(), entries.end(), key,
                                           [](const Entry& e, std::uint64_t k) { return e.key < k; });
                for (; it != entries.end() && it->key == key; ++it)
                    candidate(it->item);
            }
        }
    }
    for (const std::uint32_t item : large_)
        candidate(item);
    std::sort(out.begin(), out.end());
    out.erase(std::unique(out.begin(), out.end()), out.end());
}

} // namespace qraw::render
