#pragma once
// The gates of the CTest entry bench_paint (qraw-bench --paint-synthetic
// --check-budgets). EVERY PERFORMANCE CHANGE THAT IMPROVES THE NUMBERS TIGHTENS
// THESE TABLES in the same commit, and records the
// before/after in docs/BENCHMARKS.md.
//
// Two kinds of gate, both applied to the synthetic image-heavy document:
//  1. Work-counter ceilings (exact, deterministic: they count operations, so
//     they cannot flake on a noisy CI runner). This is the real gate.
//  2. Wall-clock p95 budgets (a loose smoke test; --no-time-budgets or
//     QRAW_BENCH_TIME_BUDGETS=off skips them, as ci.yml does: hosted runners are too
//     noisy for them): 3 x the p95 measured in a
//     Release build on the reference machine, times kDebugFactor in a non
//     Release build, times QRAW_TEST_TIMEOUT_SCALE (sanitizer builds). The
//     figures were measured with the frame counts of the CTest entry
//     (--frame-scale 0.25, tests/bench/CMakeLists.txt): zoomContinuous zooms
//     out less far with fewer frames, so its p95 depends on that setting.

#include "qraw/render/stats.h"

#include <cstddef>
#include <string_view>

namespace qraw::bench::budgets {

// What a counter ceiling is multiplied by.
enum class Per {
    Frame,        // an absolute ceiling per frame
    VisibleImage, // per image cell in the viewport at the start of the scenario
    VisibleCell,  // per cell in the viewport at the start
    PageImage,    // per image cell on the page (scenarios that scroll or zoom)
    PageCell,     // per cell on the page
    // The next two are ceilings on the TOTAL over the scenario, not per frame
    // (perUnit x the quantity): work that happens once per image or cell the
    // first time it is seen (a scroll or zoom reveals it), however many frames follow.
    TotalPerPageImage,
    TotalPerPageCell,
};

struct CounterCeiling {
    std::string_view scenario;
    render::stats::Counter counter;
    double perUnit; // the ceiling per frame is perUnit x (the Per quantity; 1 for Frame)
    Per per;
};

using render::stats::Counter;

// A label is laid out once per worker thread that draws it (the text layout
// cache is per thread): the most the default pool can have.
inline constexpr double kWorkers = 8;

// Ceilings per frame (the counter's total over the scenario divided by its
// frames), except the Total kinds. Decoding once sets every image,
// text-layout and large-value row of a scenario that repaints what it already
// painted to 0: the first paint decodes the visible images and lays out the
// visible labels, and nothing after it does, whatever caused the frame; a
// scenario that reveals new cells (pan, zoom) pays once per image and cell
// (Total kinds). Tiles set cellsPainted (a cell recorded), recordsBuilt,
// cellsReplayed and tilesRasterized of hover, selection, drag and a full
// repaint to 0, and viewportFullUpdates of every scenario to 0. Hover,
// selection and drag must not rebuild the page view (PageView::build is O(cells)).
// viewportFullUpdates is the number of whole-viewport update requests per
// frame; a handler that cannot report its overlay bounds shows up here.
// Workers: tiles are rasterised by worker threads, so the counters that
// count work done by workers (tilesRasterized, textLayouts: a label is laid out
// once per worker thread that draws it, imagesDecoded) depend on how far the
// workers got and have loose ceilings (never exact); the deterministic gate of
// the package is tilesRasterizedSync, the tiles the GUI thread rasterised while
// a frame waited: ~0 in every scenario that does not change content under the
// user's hands (an edit rasterises the few cheap tiles it touches at once, at
// most TileScheduler::kSyncMaxTiles).
// Active layer: cells under the user's hands are lifted out of the
// tiles and replayed live, so a move frame replays them (activeReplayed, not
// cellsReplayed, which stays the tile replay) and records the edges that follow
// (recordsBuilt: one per re-routed edge); it still rasterises no tile and
// builds no page view. The lift itself (dragLift) rasterises the few tiles
// under the lifted cells once, on the GUI thread when they are cheap.
// largeValueCopies: the document is parsed once (a copy per image style when
// its page view is first built) and an edit copies at most the text of the
// edited cell.
inline constexpr CounterCeiling kCounterCeilings[] = {
    {"open", Counter::largeValueCopies, 1.0, Per::PageImage},
    // Recording every cell once: the cells of the page (and the image decodes and label layouts it needs).
    {"open", Counter::recordsBuilt, 1.0, Per::PageCell},

    // The first paint rasterises the visible tiles and records nothing (open did).
    {"firstPaint", Counter::imagesDecoded, 1.0, Per::VisibleImage},
    {"firstPaint", Counter::imagesResolved, 1.0, Per::VisibleImage},
    {"firstPaint", Counter::cellsPainted, 0, Per::Frame},
    {"firstPaint", Counter::recordsBuilt, 0, Per::Frame},
    {"firstPaint", Counter::tilesRasterized, 24, Per::Frame},
    {"firstPaint", Counter::cellsReplayed, 1.0, Per::PageCell},
    {"firstPaint", Counter::pageViewBuilds, 0, Per::Frame},
    {"firstPaint", Counter::largeValueCopies, 0, Per::Frame},

    // A full repaint of an unchanged view blits cached tiles: nothing is
    // recorded, replayed or rasterised.
    {"fullRepaint", Counter::imagesDecoded, 0, Per::Frame},
    {"fullRepaint", Counter::imagesResolved, 0, Per::Frame},
    {"fullRepaint", Counter::bytesDecoded, 0, Per::Frame},
    {"fullRepaint", Counter::textLayouts, 0, Per::Frame},
    {"fullRepaint", Counter::largeValueCopies, 0, Per::Frame},
    {"fullRepaint", Counter::imageScales, 0, Per::Frame},
    {"fullRepaint", Counter::cellsPainted, 0, Per::Frame},
    {"fullRepaint", Counter::recordsBuilt, 0, Per::Frame},
    {"fullRepaint", Counter::cellsReplayed, 0, Per::Frame},
    {"fullRepaint", Counter::tilesRasterized, 0, Per::Frame},
    {"fullRepaint", Counter::tilesRasterizedSync, 0, Per::Frame},
    {"fullRepaint", Counter::pageViewBuilds, 0, Per::Frame},
    {"fullRepaint", Counter::viewportFullUpdates, 0, Per::Frame},

    // A pan blits what is cached and rasterises the tiles that come into view
    // (a step reveals one strip: at most a few tiles).
    {"pan", Counter::imagesDecoded, 1.0, Per::TotalPerPageImage},
    {"pan", Counter::imagesResolved, 1.0, Per::TotalPerPageImage},
    {"pan", Counter::textLayouts, kWorkers, Per::TotalPerPageCell},
    {"pan", Counter::largeValueCopies, 0, Per::Frame},
    {"pan", Counter::cellsPainted, 0, Per::Frame},
    {"pan", Counter::recordsBuilt, 0, Per::Frame},
    {"pan", Counter::tilesRasterized, 12, Per::Frame},
    {"pan", Counter::tilesRasterizedSync, 0.5, Per::Frame},
    {"pan", Counter::pageViewBuilds, 0, Per::Frame},
    {"pan", Counter::viewportFullUpdates, 0, Per::Frame},

    // A zoom step shows the tiles of an existing bucket scaled, or the next
    // bucket's tiles when the neighbour prefetch has them; workers rasterise
    // the new bucket meanwhile. The cells are not recorded again and the GUI
    // thread rasterises nothing.
    {"zoomStep", Counter::imagesDecoded, 1.0, Per::TotalPerPageImage},
    {"zoomStep", Counter::imagesResolved, 1.0, Per::TotalPerPageImage},
    {"zoomStep", Counter::textLayouts, kWorkers, Per::TotalPerPageCell},
    {"zoomStep", Counter::largeValueCopies, 0, Per::Frame},
    {"zoomStep", Counter::cellsPainted, 0, Per::Frame},
    {"zoomStep", Counter::recordsBuilt, 0, Per::Frame},
    {"zoomStep", Counter::tilesRasterized, 24, Per::Frame},
    {"zoomStep", Counter::tilesRasterizedSync, 0.25, Per::Frame},
    {"zoomStep", Counter::pageViewBuilds, 0, Per::Frame},
    {"zoomStep", Counter::viewportFullUpdates, 0, Per::Frame},

    {"zoomContinuous", Counter::imagesDecoded, 1.0, Per::TotalPerPageImage},
    {"zoomContinuous", Counter::imagesResolved, 1.0, Per::TotalPerPageImage},
    {"zoomContinuous", Counter::textLayouts, kWorkers, Per::TotalPerPageCell},
    {"zoomContinuous", Counter::largeValueCopies, 0, Per::Frame},
    {"zoomContinuous", Counter::cellsPainted, 0, Per::Frame},
    {"zoomContinuous", Counter::recordsBuilt, 0, Per::Frame},
    {"zoomContinuous", Counter::tilesRasterized, 24, Per::Frame},
    {"zoomContinuous", Counter::tilesRasterizedSync, 0.25, Per::Frame},
    {"zoomContinuous", Counter::pageViewBuilds, 0, Per::Frame},
    {"zoomContinuous", Counter::viewportFullUpdates, 0, Per::Frame},

    // Hover, selection and a drag step repaint the overlay only: no cell is
    // recorded or replayed, no tile is rasterised, no update covers the whole viewport.
    {"hover", Counter::imagesDecoded, 0, Per::Frame},
    {"hover", Counter::imagesResolved, 0, Per::Frame},
    {"hover", Counter::bytesDecoded, 0, Per::Frame},
    {"hover", Counter::textLayouts, 0, Per::Frame},
    {"hover", Counter::largeValueCopies, 0, Per::Frame},
    {"hover", Counter::cellsPainted, 0, Per::Frame},
    {"hover", Counter::recordsBuilt, 0, Per::Frame},
    {"hover", Counter::cellsReplayed, 0, Per::Frame},
    {"hover", Counter::tilesRasterized, 0, Per::Frame},
    {"hover", Counter::tilesRasterizedSync, 0, Per::Frame},
    {"hover", Counter::pageViewBuilds, 0, Per::Frame},
    {"hover", Counter::pageViewUpdates, 0, Per::Frame},
    {"hover", Counter::viewportFullUpdates, 0, Per::Frame},

    {"selectionChange", Counter::imagesDecoded, 0, Per::Frame},
    {"selectionChange", Counter::imagesResolved, 0, Per::Frame},
    {"selectionChange", Counter::bytesDecoded, 0, Per::Frame},
    {"selectionChange", Counter::textLayouts, 0, Per::Frame},
    {"selectionChange", Counter::largeValueCopies, 0, Per::Frame},
    {"selectionChange", Counter::cellsPainted, 0, Per::Frame},
    {"selectionChange", Counter::recordsBuilt, 0, Per::Frame},
    {"selectionChange", Counter::cellsReplayed, 0, Per::Frame},
    {"selectionChange", Counter::tilesRasterized, 0, Per::Frame},
    {"selectionChange", Counter::tilesRasterizedSync, 0, Per::Frame},
    {"selectionChange", Counter::pageViewBuilds, 0, Per::Frame},
    {"selectionChange", Counter::pageViewUpdates, 0, Per::Frame},
    {"selectionChange", Counter::viewportFullUpdates, 0, Per::Frame},

    // After the lift: the lifted cells are replayed live and the edges that follow
    // them are recorded again; nothing else.
    {"dragMove", Counter::imagesDecoded, 0, Per::Frame},
    {"dragMove", Counter::imagesResolved, 0, Per::Frame},
    {"dragMove", Counter::bytesDecoded, 0, Per::Frame},
    {"dragMove", Counter::textLayouts, 0, Per::Frame},
    {"dragMove", Counter::largeValueCopies, 0, Per::Frame},
    {"dragMove", Counter::cellsPainted, 4, Per::Frame},
    {"dragMove", Counter::recordsBuilt, 4, Per::Frame},
    {"dragMove", Counter::cellsReplayed, 0, Per::Frame},
    {"dragMove", Counter::activeReplayed, 8, Per::Frame},
    {"dragMove", Counter::activeBitmaps, 0, Per::Frame},
    {"dragMove", Counter::tilesRasterized, 0, Per::Frame},
    {"dragMove", Counter::tilesRasterizedSync, 0, Per::Frame},
    {"dragMove", Counter::pageViewBuilds, 0, Per::Frame},
    {"dragMove", Counter::pageViewUpdates, 0, Per::Frame},
    {"dragMove", Counter::viewportFullUpdates, 0, Per::Frame},

    // The first move past the click tolerance: the tiles under the lifted
    // cells are rasterised without them (at most the few cheap ones on the GUI thread).
    {"dragLift", Counter::imagesDecoded, 0, Per::Frame},
    {"dragLift", Counter::imagesResolved, 0, Per::Frame},
    {"dragLift", Counter::bytesDecoded, 0, Per::Frame},
    {"dragLift", Counter::largeValueCopies, 0, Per::Frame},
    {"dragLift", Counter::recordsBuilt, 4, Per::Frame},
    {"dragLift", Counter::tilesRasterizedSync, 4, Per::Frame},
    {"dragLift", Counter::pageViewBuilds, 0, Per::Frame},
    {"dragLift", Counter::viewportFullUpdates, 0, Per::Frame},

    // One vertex resized: a record of the previewed geometry per frame (and its edges).
    {"resizeMove", Counter::imagesDecoded, 0, Per::Frame},
    {"resizeMove", Counter::imagesResolved, 0, Per::Frame},
    {"resizeMove", Counter::bytesDecoded, 0, Per::Frame},
    {"resizeMove", Counter::largeValueCopies, 0, Per::Frame},
    {"resizeMove", Counter::cellsPainted, 4, Per::Frame},
    {"resizeMove", Counter::recordsBuilt, 4, Per::Frame},
    {"resizeMove", Counter::cellsReplayed, 0, Per::Frame},
    {"resizeMove", Counter::activeReplayed, 8, Per::Frame},
    {"resizeMove", Counter::tilesRasterized, 0, Per::Frame},
    {"resizeMove", Counter::tilesRasterizedSync, 0, Per::Frame},
    {"resizeMove", Counter::pageViewBuilds, 0, Per::Frame},
    {"resizeMove", Counter::viewportFullUpdates, 0, Per::Frame},

    // A large lifted set: replayed live (at most the cells in view) or, above
    // the cost estimate's budget, one bitmap made once and blitted.
    {"dragMany", Counter::imagesDecoded, 0, Per::Frame},
    {"dragMany", Counter::imagesResolved, 0, Per::Frame},
    {"dragMany", Counter::largeValueCopies, 0, Per::Frame},
    {"dragMany", Counter::cellsReplayed, 0, Per::Frame},
    {"dragMany", Counter::activeReplayed, 1.0, Per::VisibleCell},
    {"dragMany", Counter::recordsBuilt, 1.0, Per::VisibleCell},
    {"dragMany", Counter::tilesRasterized, 0, Per::Frame},
    {"dragMany", Counter::tilesRasterizedSync, 0, Per::Frame},
    {"dragMany", Counter::pageViewBuilds, 0, Per::Frame},
    {"dragMany", Counter::viewportFullUpdates, 0, Per::Frame},
    {"dragManyLive", Counter::activeBitmaps, 0, Per::Frame},
    {"dragManyLive", Counter::activeReplayed, 1.0, Per::VisibleCell},
    {"dragManyLive", Counter::tilesRasterized, 0, Per::Frame},
    {"dragManyLive", Counter::pageViewBuilds, 0, Per::Frame},
    {"dragManyBitmap", Counter::activeBitmaps, 0.25, Per::Frame},
    {"dragManyBitmap", Counter::activeReplayed, 1.0, Per::VisibleCell},
    {"dragManyBitmap", Counter::tilesRasterized, 0, Per::Frame},
    {"dragManyBitmap", Counter::pageViewBuilds, 0, Per::Frame},

    // An edit records the cells it changed (the moved cell and the edges
    // re-routed with it) and rasterises the tiles those touch, before and after.
    {"dragCommit", Counter::activeReplayed, 8, Per::Frame},
    {"dragCommit", Counter::imagesDecoded, 0, Per::Frame},
    {"dragCommit", Counter::imagesResolved, 0, Per::Frame},
    {"dragCommit", Counter::textLayouts, kWorkers, Per::Frame},
    {"dragCommit", Counter::largeValueCopies, 2, Per::Frame},
    {"dragCommit", Counter::cellsPainted, 4, Per::Frame},
    {"dragCommit", Counter::recordsBuilt, 4, Per::Frame},
    {"dragCommit", Counter::tilesRasterized, 12, Per::Frame},
    {"dragCommit", Counter::tilesRasterizedSync, 4, Per::Frame},
    {"dragCommit", Counter::pageViewBuilds, 0, Per::Frame},
    {"dragCommit", Counter::pageViewUpdates, 1, Per::Frame},
    {"dragCommit", Counter::viewportFullUpdates, 0, Per::Frame},

    {"editCell", Counter::imagesDecoded, 0, Per::Frame},
    {"editCell", Counter::imagesResolved, 0, Per::Frame},
    {"editCell", Counter::bytesDecoded, 0, Per::Frame},
    {"editCell", Counter::textLayouts, kWorkers, Per::Frame},
    {"editCell", Counter::largeValueCopies, 2, Per::Frame},
    {"editCell", Counter::cellsPainted, 2, Per::Frame},
    {"editCell", Counter::recordsBuilt, 2, Per::Frame},
    {"editCell", Counter::tilesRasterized, 12, Per::Frame},
    {"editCell", Counter::tilesRasterizedSync, 4, Per::Frame},
    {"editCell", Counter::pageViewBuilds, 0, Per::Frame},
    {"editCell", Counter::pageViewUpdates, 1, Per::Frame},
    {"editCell", Counter::viewportFullUpdates, 0, Per::Frame},
};

// Over the whole run: every image is decoded once, not once per scenario.
inline constexpr double kWholeRunImagesDecodedPerPageImage = 1.0;

struct TimeBudget {
    std::string_view scenario;
    double releaseP95Ms; // measured p95 in a Release build; the gate is 3 x this
};

// p95 of one frame in ms, Release, on the reference machine (BENCHMARKS.md, "Canvas paint benchmark").
inline constexpr TimeBudget kTimeBudgets[] = {
    {"firstPaint", 5},
    {"fullRepaint", 1},
    {"pan", 5}, // 15 frames without warm-up: the first ones rasterise cold tiles
    {"zoomStep", 2.5},
    {"zoomContinuous", 5},
    {"hover", 0.6},
    {"selectionChange", 0.6},
    {"dragLift", 3}, // the lift rasterises the tiles under the cells at once: 2.6 ms on the synthetic page at 100 %
    {"dragMove", 0.6},
    {"resizeMove", 1},
    {"dragMany", 4},
    {"dragManyLive", 4},
    {"dragManyBitmap", 3},
    {"dragCommit", 6},
    {"editCell", 2},
};

inline constexpr double kSlackFactor = 3;
// A Debug build runs the project's own code unoptimised; Qt itself is the
// distribution's optimised build, so the factor is small.
inline constexpr double kDebugFactor = 3;

} // namespace qraw::bench::budgets
