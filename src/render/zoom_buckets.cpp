// ZoomBuckets (zoom_buckets.h).

#include "qraw/render/zoom_buckets.h"

#include <algorithm>
#include <cmath>

namespace qraw::render {

namespace {
constexpr double kEpsilon = 1e-9; // relative: a zoom a hair below a bucket still is that bucket
}

ZoomBuckets::ZoomBuckets(const std::vector<double>& steps, double minZoom)
{
    if (steps.empty()) {
        ladder_ = {1};
        return;
    }
    std::vector<double> below;
    for (double z = steps.front() / kMaxRatio; z > minZoom * (1 + kEpsilon); z /= kMaxRatio)
        below.push_back(z);
    below.push_back(minZoom);
    std::sort(below.begin(), below.end());
    below.erase(std::unique(below.begin(), below.end()), below.end());
    below.erase(std::remove_if(below.begin(), below.end(), [&](double z) { return z >= steps.front(); }), below.end());
    ladder_ = below;
    for (double step : steps) {
        if (!ladder_.empty() && step > ladder_.back() * (1 + kEpsilon)) {
            const double ratio = step / ladder_.back();
            const int parts = std::max(1, static_cast<int>(std::ceil(std::log(ratio) / std::log(kMaxRatio) - kEpsilon)));
            const double from = ladder_.back();
            for (int i = 1; i < parts; ++i)
                ladder_.push_back(from * std::pow(ratio, double(i) / parts));
            ladder_.push_back(step);
        } else if (ladder_.empty()) {
            ladder_.push_back(step);
        }
    }
}

double ZoomBuckets::ceiling(double zoom) const
{
    const auto it = std::lower_bound(ladder_.begin(), ladder_.end(), zoom * (1 - kEpsilon));
    return it == ladder_.end() ? ladder_.back() : *it;
}

std::optional<double> ZoomBuckets::above(double bucket) const
{
    const auto it = std::upper_bound(ladder_.begin(), ladder_.end(), bucket * (1 + kEpsilon));
    return it == ladder_.end() ? std::nullopt : std::optional<double>(*it);
}

std::optional<double> ZoomBuckets::below(double bucket) const
{
    const auto it = std::lower_bound(ladder_.begin(), ladder_.end(), bucket * (1 - kEpsilon));
    return it == ladder_.begin() ? std::nullopt : std::optional<double>(*(it - 1));
}

bool isBucket(const ZoomBuckets& buckets, double zoom)
{
    return zoom > 0 && std::abs(buckets.ceiling(zoom) / zoom - 1) < kEpsilon;
}

double chooseDisplayBucket(const ZoomBuckets& buckets, double zoom, std::optional<double> displayed, bool settled,
                           bool targetReady, bool exactReady)
{
    const double target = buckets.ceiling(zoom);
    if (settled && displayed && std::abs(*displayed / zoom - 1) < kEpsilon)
        return *displayed;
    if (settled && exactReady)
        return zoom;
    if (!displayed || settled || targetReady)
        return target;
    const double ratio = zoom / *displayed;
    return ratio > kMaxStaleScale || ratio < 1 / kMaxStaleScale ? target : *displayed;
}

} // namespace qraw::render
