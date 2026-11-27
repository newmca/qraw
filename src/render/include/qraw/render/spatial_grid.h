#pragma once

// Uniform grids over rectangles, for "which of these meet that rectangle"
// (docs/research/fast-canvas-architectures.md 3.11; ARCHITECTURE.md, "Canvas
// rendering"). The render snapshot indexes the visual bounds of its records
// with it (tile membership), and the canvas indexes the hit bounds of the
// cell states (hit testing, rubber band, drop targets, D-035).
//
// Three grids of growing cell size: a rectangle is registered in the finest
// one where it covers a handful of cells, so a long edge or a big container
// does not fill thousands of small cells. Built once from a vector of
// rectangles and read-only afterwards, so any thread may query it. An item is its position in that vector, which callers
// make the paint index: a query returns indices in ascending order, so the
// z-order comes for free.

#include <QRectF>

#include <cstdint>
#include <vector>

namespace qraw::render {

class SpatialGrid {
public:
    static constexpr int kLevels = 3;
    static constexpr double kCellSize = 256;  // model units, the finest level
    static constexpr double kLevelScale = 16; // the cell size of each level over the one before

    // Indexes `bounds`; item i is bounds[i]. A null rectangle (no width and no
    // height, or not finite) is not indexed and never found. Replaces the
    // previous content.
    void build(std::vector<QRectF> bounds);

    // Sets `out` (cleared first) to the items whose bounds meet `rect`, sorted
    // ascending without duplicates. The intersection is closed: touching
    // edges count, and so does a rectangle without width or height.
    void query(const QRectF& rect, std::vector<std::uint32_t>& out) const;

    [[nodiscard]] std::size_t size() const { return bounds_.size(); }
    [[nodiscard]] const QRectF& bounds(std::size_t item) const { return bounds_[item]; }

private:
    struct Entry {
        std::uint64_t key; // the grid cell
        std::uint32_t item;
    };
    struct Level {
        std::vector<Entry> entries; // one per (item, grid cell), sorted by key then item
    };

    std::vector<QRectF> bounds_;
    Level levels_[kLevels];
    std::vector<std::uint32_t> large_; // items too big for the coarsest grid: always candidates
};

// A rectangle with every coordinate finite and clamped to +-1e7: bounds beyond
// that are corrupt data (or a deliberate attack on the grid), and the clamp
// keeps every grid coordinate representable. Not finite: a null rectangle.
[[nodiscard]] QRectF sanitizedBounds(const QRectF& r);

} // namespace qraw::render
