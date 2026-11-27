#include "canvas_layout.h"

#include <algorithm>
#include <cmath>

namespace qraw::ui::canvas {

namespace {

constexpr double kZoomFactor = 1.2;       // mxGraph.zoomFactor
constexpr int kZoomUnit = 20;             // stops are multiples of 1/20 (5 %)
constexpr int kMaxZoomUnits = 3200;       // 16000 %
constexpr double kZoomEpsilon = 0.000001; // Graph.getZoomStep eps

std::vector<double> computeZoomSteps()
{
    // Down from 100 %: each stop is the previous divided by the factor,
    // at least one unit smaller (prepended in upstream, so collected here
    // nearest-first and reversed).
    std::vector<double> steps;
    int t = kZoomUnit;
    while (t > 1) {
        t = std::max(std::min(static_cast<int>(std::lround(t / kZoomFactor)), t - 1), 1);
        steps.push_back(double(t) / kZoomUnit);
    }
    std::reverse(steps.begin(), steps.end());
    steps.push_back(1);
    t = kZoomUnit;
    while (t < kMaxZoomUnits) {
        t = std::min(std::max(static_cast<int>(std::lround(t * kZoomFactor)), t + 1), kMaxZoomUnits);
        steps.push_back(double(t) / kZoomUnit);
    }
    return steps;
}

} // namespace

QSizeF pageSize(const core::Page& page)
{
    const double scale = page.pageScale() > 0 ? page.pageScale() : 1;
    const double w = page.pageWidth() > 0 ? page.pageWidth() : kDefaultPageWidth;
    const double h = page.pageHeight() > 0 ? page.pageHeight() : kDefaultPageHeight;
    return {w * scale, h * scale};
}

QRectF pageArea(const core::Page& page, const std::optional<QRectF>& content)
{
    const QSizeF tile = pageSize(page);
    if (!page.pageVisible())
        return content ? *content : QRectF(QPointF(0, 0), tile);
    int x0 = 0;
    int y0 = 0;
    int x1 = 1;
    int y1 = 1;
    if (content) {
        x0 = std::min(x0, static_cast<int>(std::floor(content->left() / tile.width())));
        y0 = std::min(y0, static_cast<int>(std::floor(content->top() / tile.height())));
        x1 = std::max(x1, static_cast<int>(std::ceil(content->right() / tile.width())));
        y1 = std::max(y1, static_cast<int>(std::ceil(content->bottom() / tile.height())));
    }
    return {x0 * tile.width(), y0 * tile.height(), (x1 - x0) * tile.width(), (y1 - y0) * tile.height()};
}

const std::vector<double>& zoomSteps()
{
    static const std::vector<double> steps = computeZoomSteps();
    return steps;
}

double nextZoomStep(double zoom, bool zoomIn)
{
    const std::vector<double>& steps = zoomSteps();
    if (zoomIn) {
        for (double s : steps) {
            if (s > zoom + kZoomEpsilon)
                return s;
        }
        return steps.back();
    }
    for (auto it = steps.rbegin(); it != steps.rend(); ++it) {
        if (*it < zoom - kZoomEpsilon)
            return *it;
    }
    return steps.front();
}

double fitScale(double scale)
{
    return std::floor(scale * kZoomUnit + kZoomEpsilon) / kZoomUnit;
}

QPointF snapToGrid(const core::Page& page, QPointF p)
{
    p = QPointF(std::round(p.x()), std::round(p.y()));
    const double grid = page.gridSize();
    if (!page.gridEnabled() || grid <= 0)
        return p;
    return {std::round(p.x() / grid) * grid, std::round(p.y() / grid) * grid};
}

} // namespace qraw::ui::canvas
