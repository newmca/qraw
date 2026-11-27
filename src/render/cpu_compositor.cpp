// CpuCompositor (compositor.h): tiles blitted with QPainter::drawImage.

#include "qraw/render/compositor.h"

#include "qraw/render/painter_canvas.h"
#include "qraw/render/perf_scope.h"
#include "qraw/render/stats.h"

#include <QPainter>

#include <cmath>

namespace qraw::render {

CpuCompositor::CpuCompositor(QPainter& painter) : painter_(painter) {}

void CpuCompositor::present(const FrameDescription& frame)
{
    QRAW_PERF_SCOPE(Composite);
    QPainter& painter = painter_;
    const double dpr = frame.devicePixelRatio > 0 ? frame.devicePixelRatio : 1;
    painter.save();
    painter.resetTransform();
    const QPainter::RenderHints hints = painter.renderHints();
    painter.setRenderHint(QPainter::Antialiasing, false); // whole device pixels: no blended edges in Source mode
    if (!frame.clip.isEmpty())
        painter.setClipRect(QRectF(QPointF(frame.clip.topLeft()) / dpr, QSizeF(frame.clip.size()) / dpr), Qt::IntersectClip);

    // Tiles are opaque: copy them (the memcpy/SIMD path), no blending.
    painter.setCompositionMode(QPainter::CompositionMode_Source);
    for (const TileRef& tile : frame.tiles) {
        if (tile.image.isNull())
            continue;
        stats::add(stats::Counter::tilesBlitted);
        // Device pixel (x, y) is the logical position (x, y) / dpr; the image
        // carries the same ratio, so the engine maps it 1:1 when the sizes agree.
        const bool scaled = !tile.destSize.isEmpty() && QSizeF(tile.destSize) != tile.source.size();
        if (!scaled) {
            painter.drawImage(QPointF(tile.dest) / dpr, tile.image, tile.source);
            continue;
        }
        painter.setRenderHint(QPainter::SmoothPixmapTransform, true);
        painter.drawImage(QRectF(QPointF(tile.dest) / dpr, QSizeF(tile.destSize) / dpr), tile.image, tile.source);
    }
    painter.setCompositionMode(QPainter::CompositionMode_SourceOver);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform, false);
    painter.setRenderHints(hints, true);

    // What the painter will not show is not replayed: the clip is in logical
    // pixels, the items are in device pixels.
    const QRectF visible = painter.clipBoundingRect();
    const QRectF deviceVisible(visible.topLeft() * dpr, visible.size() * dpr);
    for (const ActiveItem& item : frame.active) {
        QRectF bounds;
        if (item.record)
            bounds = item.transform.mapRect(item.record->visualBounds);
        else if (!item.bitmap.isNull())
            bounds = item.transform.mapRect(QRectF(QPointF(0, 0), QSizeF(item.bitmap.size())));
        else
            continue;
        if (!bounds.adjusted(-kAntialiasSlackPx, -kAntialiasSlackPx, kAntialiasSlackPx, kAntialiasSlackPx)
                 .intersects(deviceVisible))
            continue;
        painter.save();
        painter.setOpacity(item.opacity);
        painter.setTransform(item.transform * QTransform::fromScale(1 / dpr, 1 / dpr));
        if (item.record) {
            replayRecord(painter, *item.record, ImageQuality::Interactive,
                         std::hypot(item.transform.m11(), item.transform.m12()), true);
        } else {
            // A bitmap of device pixels at a whole device position: a copy.
            stats::add(stats::Counter::activeBitmapsDrawn);
            painter.setRenderHint(QPainter::SmoothPixmapTransform, false);
            painter.drawImage(QPointF(0, 0), item.bitmap);
        }
        painter.restore();
    }
    painter.restore();

    if (frame.overlay)
        frame.overlay(painter);
}

} // namespace qraw::render
