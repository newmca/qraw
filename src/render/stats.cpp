#include "qraw/render/stats.h"

namespace qraw::render::stats {

std::string_view name(Counter counter) noexcept
{
    switch (counter) {
    case Counter::cellsPainted: return "cellsPainted";
    case Counter::labelsPainted: return "labelsPainted";
    case Counter::textLayouts: return "textLayouts";
    case Counter::imagesResolved: return "imagesResolved";
    case Counter::imagesDecoded: return "imagesDecoded";
    case Counter::bytesDecoded: return "bytesDecoded";
    case Counter::imageScales: return "imageScales";
    case Counter::pageViewBuilds: return "pageViewBuilds";
    case Counter::viewportFullUpdates: return "viewportFullUpdates";
    case Counter::largeValueCopies: return "largeValueCopies";
    case Counter::recordsBuilt: return "recordsBuilt";
    case Counter::tilesRasterized: return "tilesRasterized";
    case Counter::tilesBlitted: return "tilesBlitted";
    case Counter::tilesRasterizedSync: return "tilesRasterizedSync";
    case Counter::tilesSubstituted: return "tilesSubstituted";
    case Counter::tilesDiscarded: return "tilesDiscarded";
    case Counter::cellsReplayed: return "cellsReplayed";
    case Counter::activeReplayed: return "activeReplayed";
    case Counter::activeBitmaps: return "activeBitmaps";
    case Counter::activeBitmapsDrawn: return "activeBitmapsDrawn";
    case Counter::cellsSkippedLod: return "cellsSkippedLod";
    case Counter::labelsSkippedLod: return "labelsSkippedLod";
    case Counter::pageViewUpdates: return "pageViewUpdates";
    case Counter::Count: break;
    }
    return {};
}

Snapshot snapshot() noexcept
{
    Snapshot values{};
    for (std::size_t i = 0; i < kCounterCount; ++i)
        values[i] = detail::counters[i].load(std::memory_order_relaxed);
    values[static_cast<std::size_t>(Counter::largeValueCopies)] = core::largeValueCopies();
    return values;
}

void reset() noexcept
{
    for (auto& counter : detail::counters)
        counter.store(0, std::memory_order_relaxed);
    core::resetLargeValueCopies();
}

} // namespace qraw::render::stats
