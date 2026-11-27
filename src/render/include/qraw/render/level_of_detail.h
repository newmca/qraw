#pragma once

// Level of detail for what is drawn on the screen (ARCHITECTURE.md, "Canvas
// rendering": level of detail).
//
// Zoomed far out, cells are smaller than a pixel and labels are unreadable
// specks; replaying them costs as much as at 100 % and changes nothing anyone
// sees. Replay (tile rasterisation, the active layer, the outline view)
// skips them. The decision is made from the record's bounds and text size and
// the device scale (device pixels per canvas unit), so it is a pure function
// of the record and the zoom: deterministic, and the same on every thread.
//
// Never applied to export, print, SVG or palette thumbnails (they paint
// directly, not through replayRecord with a device scale), and never to hit
// testing, selection or the overlay: a skipped cell is still selectable.
//
// All thresholds are here, in one place.

#include "qraw/render/display_list.h"

#include <atomic>

namespace qraw::render::lod {

// A cell whose bounds are below this in BOTH directions (device pixels) is
// not drawn: it would colour at most one pixel faintly.
inline constexpr double kMinCellDevicePx = 1.0;
// A label whose font is rendered smaller than this (device pixels) is not
// shaped or drawn: the glyphs are specks, and shaping them is the expensive
// part of a zoomed-out tile (greeked bars were tried on screenshots and
// do not read better; see BENCHMARKS.md).
inline constexpr double kMinLabelFontPx = 4.0;

// Switches the level of detail off for the whole process (the benchmark's
// --no-lod, to measure what it saves; tests). On by default.
inline std::atomic<bool>& enabledFlag() noexcept
{
    static std::atomic<bool> flag{true};
    return flag;
}
inline void setEnabled(bool on) noexcept
{
    enabledFlag().store(on, std::memory_order_relaxed);
}

// What replay does with one record at `deviceScale` (device pixels per canvas
// unit; <= 0: no level of detail, everything is drawn).
struct Detail {
    bool cell = true;  // draw the shape
    bool label = true; // draw the label
};

[[nodiscard]] inline Detail detailFor(const DisplayList& list, double deviceScale)
{
    Detail detail;
    if (!(deviceScale > 0) || !enabledFlag().load(std::memory_order_relaxed))
        return detail;
    const QRectF bounds = list.bounds();
    // A cell without bounds paints nothing anyway.
    if (!bounds.isNull() && bounds.width() * deviceScale < kMinCellDevicePx
        && bounds.height() * deviceScale < kMinCellDevicePx) {
        return {false, false};
    }
    const double font = list.cost().labelFontPx;
    detail.label = !(font > 0) || font * deviceScale >= kMinLabelFontPx;
    return detail;
}

} // namespace qraw::render::lod
