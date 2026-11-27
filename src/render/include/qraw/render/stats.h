#pragma once

// Work counters of the canvas paint path (docs/BENCHMARKS.md, "Canvas paint benchmark").
//
// The counters are deterministic: they count operations, not time, so a CI
// test can assert exact ceilings ("hover repaints no cell") where wall-clock
// budgets would be flaky on a noisy runner. Every counter is a relaxed
// std::atomic incremented from any thread (an increment costs a few
// nanoseconds); the benchmark resets them before a scenario and reads them
// after. They are always compiled in and have no effect on rendering.

#include "qraw/core/shared_value.h"

#include <array>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace qraw::render::stats {

// The order is the column order of the benchmark table; append only at the
// end of a group so that the table layout stays stable.
enum class Counter : std::size_t {
    // --- filled today -------------------------------------------------------
    cellsPainted,        // SceneRenderer::paintShape calls (vertices and edges)
    labelsPainted,       // SceneRenderer::paintLabel calls that drew a label
    textLayouts,         // QTextDocument layouts created (label paint and text measuring)
    imagesResolved,      // resolveImage calls that decoded base64/percent data or read a file
    imagesDecoded,       // QImage / QSvgRenderer decodes of resolved image bytes
    bytesDecoded,        // encoded input bytes of both stages (resolve and decode)
    imageScales,         // raster images drawn at a size other than their own
    pageViewBuilds,      // PageView::build calls
    viewportFullUpdates, // CanvasView full-viewport update requests
    // --- filled by later packages (zero until then) ---------------------------
    largeValueCopies,    // shared buffers allocated by copying a large (>= 16 KiB) value; read from core::largeValueCopies()
    recordsBuilt,        // per-cell display lists recorded (the cells an edit changed)
    tilesRasterized,     // tiles rasterised (all threads)
    tilesBlitted,        // tiles blitted by the compositor
    tilesRasterizedSync, // tiles rasterised on the GUI thread, the frame waiting for them
    tilesSubstituted,    // tiles shown from another zoom bucket while the right one renders
    tilesDiscarded,      // tiles dropped by invalidation or eviction (the dirty and evicted ones are counted too)
    cellsReplayed,       // cell records replayed onto a raster target (tile rasterisation, scene render)
    activeReplayed,      // cell records replayed by the active layer (live, or into its bitmap)
    activeBitmaps,       // interaction snapshots (bitmaps of the lifted set) rasterised
    activeBitmapsDrawn,  // interaction snapshots blitted by the compositor
    cellsSkippedLod,     // cells a replay skipped as smaller than a device pixel (level of detail)
    labelsSkippedLod,    // labels a replay skipped as too small to read (level of detail)
    pageViewUpdates,     // PageView::update calls that patched the view in place (an edit that kept the tree)
    Count
};

inline constexpr std::size_t kCounterCount = static_cast<std::size_t>(Counter::Count);

using Snapshot = std::array<std::uint64_t, kCounterCount>;

namespace detail {
// One definition for the whole program (C++17 inline variable).
inline std::array<std::atomic<std::uint64_t>, kCounterCount> counters{};
} // namespace detail

inline void add(Counter counter, std::uint64_t amount = 1) noexcept
{
    detail::counters[static_cast<std::size_t>(counter)].fetch_add(amount, std::memory_order_relaxed);
}

[[nodiscard]] inline std::uint64_t get(Counter counter) noexcept
{
    if (counter == Counter::largeValueCopies)
        return core::largeValueCopies(); // counted in qraw_core, which has no render dependency
    return detail::counters[static_cast<std::size_t>(counter)].load(std::memory_order_relaxed);
}

// The name used in the benchmark output and its JSON ("cellsPainted").
[[nodiscard]] std::string_view name(Counter counter) noexcept;

[[nodiscard]] Snapshot snapshot() noexcept;
void reset() noexcept;

} // namespace qraw::render::stats
