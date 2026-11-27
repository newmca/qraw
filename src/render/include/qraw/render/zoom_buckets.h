#pragma once

// Zoom buckets (ARCHITECTURE.md, "Canvas rendering", "Zoom buckets").
//
// Tiles are rasterised at a few scales only: the zoom steps of the view
// (draw.io's x1.2 steps, so a toolbar or wheel step lands exactly on a bucket
// and blits 1:1 at whole device pixels), with intermediate buckets inserted
// wherever two steps are more than x1.2 apart (the low end of the ladder), so
// that a zoom between two buckets is shown from the bucket above it scaled
// down by at most 1.2. Few distinct scales also bound Qt's per-thread,
// per-matrix glyph caches, which are never trimmed on worker threads.
//
// Pure and Qt-free: the scheduler asks, nothing is stored here.

#include <optional>
#include <vector>

namespace qraw::render {

class ZoomBuckets {
public:
    // Largest ratio between two neighbouring buckets (Chrome's snap ratio, and
    // mxGraph.zoomFactor).
    static constexpr double kMaxRatio = 1.2;

    // `steps`: ascending zoom stops (the view's). The ladder extends below the
    // first stop by factors of kMaxRatio down to `minZoom`, and every gap wider
    // than kMaxRatio is filled with equal ratios.
    ZoomBuckets(const std::vector<double>& steps, double minZoom);

    [[nodiscard]] const std::vector<double>& ladder() const { return ladder_; }
    // The smallest bucket at or above `zoom` (the last one when zoom is above all).
    [[nodiscard]] double ceiling(double zoom) const;
    // The next bucket above / below `bucket` (nullopt at the ends).
    [[nodiscard]] std::optional<double> above(double bucket) const;
    [[nodiscard]] std::optional<double> below(double bucket) const;

private:
    std::vector<double> ladder_;
};

// Hysteresis of the displayed bucket (the tiling the compositor shows at the
// given zoom).
//  * settled, and the tiling on screen is already at exactly this zoom: it stays.
//  * settled, and `exactReady` (every visible tile of a tiling at exactly the
//    zoom is current): that tiling, "the transient exact-scale tiling": a zoom
//    between two steps (Fit, continuous zoom) is then drawn 1:1 instead of the
//    bucket above scaled down, which is softer and costs a scaled blit per
//    repaint. It is at most one tiling and the next zoom change drops it.
//  * `settled` (the zoom has been stable for the settle delay) or
//    `targetReady` (every visible tile of the target bucket is current): the
//    target, ceiling(zoom).
//  * during a gesture: the tiling on screen stays, scaled, as long as it is
//    within a factor of 2 of the zoom; beyond that the target takes over (and
//    its rasterisation starts).
//  * nothing displayed yet: the target.
[[nodiscard]] double chooseDisplayBucket(const ZoomBuckets& buckets, double zoom, std::optional<double> displayed,
                                         bool settled, bool targetReady, bool exactReady = false);
// True when `zoom` is (within rounding) one of the buckets.
[[nodiscard]] bool isBucket(const ZoomBuckets& buckets, double zoom);

// The ratio beyond which an unsettled zoom abandons the tiling on screen.
inline constexpr double kMaxStaleScale = 2;

} // namespace qraw::render
