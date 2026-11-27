// Tile rasterisation (tile_rasterizer.h) and the page decoration painter,
// which DiagramScene::drawBackground used to be (js/grapheditor/Graph.js
// geDiagramBackdrop; mxGraph page and grid colours).

#include "qraw/render/tile_rasterizer.h"

#include "qraw/render/level_of_detail.h"
#include "qraw/render/perf_scope.h"
#include "qraw/render/stats.h"

#include <QLineF>
#include <QList>
#include <QPen>
#include <QTransform>

#include <algorithm>
#include <cmath>

namespace qraw::render {

namespace {

const QColor kBackdrop(0xf0, 0xf0, 0xf0);
const QColor kPageBorder(0xd0, 0xd0, 0xd0);
const QColor kGridMinor(0xe8, 0xe8, 0xe8);
const QColor kGridMajor(0xd8, 0xd8, 0xd8);
// The same for the dark scheme (a page of Editor.darkColor on a slightly darker backdrop).
const QColor kBackdropDark(0x0a, 0x0a, 0x0a);
const QColor kPageBorderDark(0x40, 0x40, 0x40);
const QColor kGridMinorDark(0x20, 0x20, 0x20);
const QColor kGridMajorDark(0x2e, 0x2e, 0x2e);
constexpr int kMajorGridEvery = 4;   // a major line every 4 * gridSize
constexpr double kMinGridPixels = 4; // lines closer than this (logical pixels) are skipped

QPen cosmeticPen(const QColor& color, double width)
{
    QPen pen(color, width);
    pen.setCosmetic(true);
    return pen;
}

// Lines every `step` inside `area`, aligned to multiples of `step` in scene
// coordinates; every kMajorGridEvery-th line is major. Minor lines are
// skipped when they would be closer than kMinGridPixels on screen.
void drawGrid(QPainter& painter, const QRectF& area, double step, double zoom, double lineWidth, bool dark)
{
    const bool showMinor = step * zoom >= kMinGridPixels;
    if (!showMinor && step * kMajorGridEvery * zoom < kMinGridPixels)
        return;
    QList<QLineF> minor;
    QList<QLineF> major;
    const auto collect = [&](double from, double to, auto makeLine) {
        const auto first = static_cast<long long>(std::ceil(from / step));
        const auto last = static_cast<long long>(std::floor(to / step));
        for (long long i = first; i <= last; ++i) {
            const bool isMajor = i % kMajorGridEvery == 0;
            if (isMajor || showMinor)
                (isMajor ? major : minor).append(makeLine(double(i) * step));
        }
    };
    collect(area.left(), area.right(), [&](double x) { return QLineF(x, area.top(), x, area.bottom()); });
    collect(area.top(), area.bottom(), [&](double y) { return QLineF(area.left(), y, area.right(), y); });
    painter.setPen(cosmeticPen(dark ? kGridMinorDark : kGridMinor, lineWidth));
    painter.drawLines(minor);
    painter.setPen(cosmeticPen(dark ? kGridMajorDark : kGridMajor, lineWidth));
    painter.drawLines(major);
}

void paintDecor(QPainter& painter, const PageDecor& decor, const QRectF& rect, const SnapshotPaint& options)
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, false);
    const QColor& backdrop = decor.dark ? kBackdropDark : kBackdrop;
    if (!decor.hasPage) {
        painter.fillRect(rect, backdrop);
        painter.restore();
        return;
    }
    QRectF gridArea = rect;
    if (decor.pageVisible) {
        const QRectF& area = decor.pageArea;
        painter.fillRect(rect, backdrop);
        painter.fillRect(area.intersected(rect), decor.background);
        painter.setPen(cosmeticPen(decor.dark ? kPageBorderDark : kPageBorder, options.lineWidth));
        painter.setBrush(Qt::NoBrush);
        const QSizeF tile = decor.pageTile;
        if (tile.width() > 0 && tile.height() > 0) {
            const int x0 = static_cast<int>(std::lround(area.left() / tile.width()));
            const int y0 = static_cast<int>(std::lround(area.top() / tile.height()));
            const int x1 = static_cast<int>(std::lround(area.right() / tile.width()));
            const int y1 = static_cast<int>(std::lround(area.bottom() / tile.height()));
            for (int ty = y0; ty < y1; ++ty) {
                for (int tx = x0; tx < x1; ++tx) {
                    const QRectF tileRect(tx * tile.width(), ty * tile.height(), tile.width(), tile.height());
                    if (tileRect.intersects(rect))
                        painter.drawRect(tileRect);
                }
            }
        }
        gridArea = rect.intersected(area);
    } else {
        painter.fillRect(rect, decor.background);
    }
    if (decor.gridEnabled && decor.gridSize > 0 && !gridArea.isEmpty())
        drawGrid(painter, gridArea, decor.gridSize, options.gridZoom, options.lineWidth, decor.dark);
    painter.restore();
}

} // namespace

QRect tileDeviceRect(const TileKey& key)
{
    return QRect(key.tx * kTileSize, key.ty * kTileSize, kTileSize, kTileSize);
}

QRectF tileSceneRect(const TileKey& key)
{
    const QRect device = tileDeviceRect(key);
    return QRectF(device.left() / key.scale, device.top() / key.scale, device.width() / key.scale,
                  device.height() / key.scale);
}

TileRange tilesCoveringDevice(const QRectF& deviceRect)
{
    TileRange range;
    if (!(deviceRect.width() >= 0) || !(deviceRect.height() >= 0))
        return range;
    // Half-open: a rectangle that ends exactly on a tile border does not reach the next tile.
    range.tx0 = static_cast<int>(std::floor(deviceRect.left() / kTileSize));
    range.ty0 = static_cast<int>(std::floor(deviceRect.top() / kTileSize));
    range.tx1 = static_cast<int>(std::ceil(deviceRect.right() / kTileSize));
    range.ty1 = static_cast<int>(std::ceil(deviceRect.bottom() / kTileSize));
    return range;
}

TileRange tilesCovering(const QRectF& sceneRect, double scale)
{
    const QRectF device(sceneRect.left() * scale - kAntialiasSlackPx, sceneRect.top() * scale - kAntialiasSlackPx,
                        sceneRect.width() * scale + 2 * kAntialiasSlackPx,
                        sceneRect.height() * scale + 2 * kAntialiasSlackPx);
    return tilesCoveringDevice(device);
}

void paintSnapshot(QPainter& painter, const RenderSnapshot& snapshot, const QRectF& sceneRect,
                   const SnapshotPaint& options)
{
    {
        QRAW_PERF_SCOPE(SceneBackground);
        paintDecor(painter, snapshot.decor(), sceneRect, options);
    }
    for (const std::uint32_t index : snapshot.query(sceneRect)) {
        if (options.cancel != nullptr && options.cancel->load(std::memory_order_relaxed))
            return;
        const CellRecord& record = *snapshot.records()[index];
        if (record.display.empty())
            continue;
        replayRecord(painter, record, options.imageQuality, options.deviceScale);
    }
}

namespace {

// Fitted to release-build timings of rasterizeTile on a page of 1,500
// rounded rectangles with a label each (a tile at 100 % with 63 records
// takes 0.74 ms, one with 20 records 0.36 ms, an empty one 0.035 ms; 1,500
// records at 10 % 9.7 ms; Fedora 44, one core of the reference machine): a
// fixed part (allocation, decoration) and per record, op, glyph and image terms.
constexpr double kTileBaseUs = 50;
constexpr double kRecordUs = 3;
constexpr double kOpUs = 0.5;
constexpr double kGlyphUs = 0.35;
constexpr double kImageUs = 60;
constexpr double kSkippedUs = 0.2; // a record the level of detail skips: a bounds test

} // namespace

double estimateRecordCostUs(const DisplayList& list, double deviceScale)
{
    const lod::Detail detail = lod::detailFor(list, deviceScale);
    if (!detail.cell)
        return kSkippedUs;
    const DisplayListCost& c = list.cost();
    const double ops = detail.label ? double(c.ops) : double(list.labelBegin());
    const double glyphs = detail.label ? double(c.glyphs) : 0;
    return kRecordUs + kOpUs * ops + kGlyphUs * glyphs + (c.imagePixels > 0 ? kImageUs : 0);
}

double estimateTileCostUs(const RenderSnapshot& snapshot, const TileKey& key, double limitUs)
{
    const double slack = kAntialiasSlackPx / key.scale;
    double cost = kTileBaseUs;
    for (const std::uint32_t index : snapshot.query(tileSceneRect(key).adjusted(-slack, -slack, slack, slack))) {
        cost += estimateRecordCostUs(snapshot.records()[index]->display, key.scale);
        if (cost > limitUs)
            break;
    }
    return cost;
}

void replayRecord(QPainter& painter, const CellRecord& record, ImageQuality quality, double deviceScale, bool live)
{
    const lod::Detail detail = lod::detailFor(record.display, deviceScale);
    if (!detail.cell) {
        stats::add(stats::Counter::cellsSkippedLod);
        return;
    }
    stats::add(live ? stats::Counter::activeReplayed : stats::Counter::cellsReplayed);
    // A canvas of its own per cell, as the cell item had: the state of one
    // cell never reaches the next.
    PainterCanvas canvas(painter);
    canvas.setImageQuality(quality);
    record.display.replayShape(canvas);
    if (record.labelHidden)
        return;
    if (detail.label)
        record.display.replayLabel(canvas);
    else if (record.display.cost().labelFontPx > 0)
        stats::add(stats::Counter::labelsSkippedLod);
}

QImage rasterizeTile(const RenderSnapshot& snapshot, const TileKey& key, double dpr, const std::atomic<bool>* cancel)
{
    QRAW_PERF_SCOPE(TileRaster);
    QImage image(kTileSize, kTileSize, QImage::Format_ARGB32_Premultiplied);
    image.fill(kBackdrop); // opaque: the decoration covers it, and the blit can copy
    {
        QPainter painter(&image);
        const QRect device = tileDeviceRect(key);
        QTransform transform;
        transform.translate(-device.left(), -device.top());
        transform.scale(key.scale, key.scale);
        painter.setTransform(transform);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        const QRectF area = tileSceneRect(key).adjusted(-kAntialiasSlackPx / key.scale, -kAntialiasSlackPx / key.scale,
                                                        kAntialiasSlackPx / key.scale, kAntialiasSlackPx / key.scale);
        SnapshotPaint options;
        options.gridZoom = key.scale / dpr;
        options.lineWidth = dpr;
        options.deviceScale = key.scale;
        options.cancel = cancel;
        paintSnapshot(painter, snapshot, area, options);
    }
    if (cancel != nullptr && cancel->load(std::memory_order_relaxed))
        return {}; // the owner counts it (tilesDiscarded)
    stats::add(stats::Counter::tilesRasterized);
    image.setDevicePixelRatio(dpr);
    return image;
}

} // namespace qraw::render
