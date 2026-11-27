#pragma once

// Tiles: the content layer of the canvas (ARCHITECTURE.md, "Canvas rendering").
//
// The page is rasterised at one raster scale (zoom x device pixel ratio) into
// square tiles of kTileSize device pixels. Tile (tx, ty) covers the device
// pixels [tx * kTileSize, (tx + 1) * kTileSize) of the scene scaled by the
// raster scale, so tile origins are integer device pixels and neighbours meet
// without a seam; the compositor puts them on the screen at integer device
// positions. rasterizeTile() is a pure function of an immutable snapshot and a
// key: it touches no global state of the editor and can run on any thread
// (it runs on the worker threads of TileRasterPool).

#include "qraw/render/painter_canvas.h"
#include "qraw/render/render_snapshot.h"

#include <QImage>
#include <QPainter>
#include <QRect>
#include <QRectF>

#include <atomic>
#include <cstddef>
#include <functional>
#include <limits>

namespace qraw::render {

// Edge of a tile in device pixels.
inline constexpr int kTileSize = 512;
// Everything that can reach one device pixel beyond a rectangle (antialiased
// edges, a hairline that snaps outward) is painted with it: tile queries and
// damage grow their rectangles by this many device pixels.
inline constexpr double kAntialiasSlackPx = 1;

struct TileKey {
    double scale = 1; // raster scale: zoom x device pixel ratio
    int tx = 0;
    int ty = 0;
    friend bool operator==(const TileKey&, const TileKey&) = default;
};

struct TileKeyHash {
    [[nodiscard]] std::size_t operator()(const TileKey& key) const noexcept
    {
        std::size_t h = std::hash<double>{}(key.scale);
        h ^= std::hash<int>{}(key.tx) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        h ^= std::hash<int>{}(key.ty) + 0x9e3779b97f4a7c15ULL + (h << 6) + (h >> 2);
        return h;
    }
};

// A block of tiles [tx0, tx1) x [ty0, ty1).
struct TileRange {
    int tx0 = 0;
    int ty0 = 0;
    int tx1 = 0;
    int ty1 = 0;
    [[nodiscard]] bool empty() const { return tx1 <= tx0 || ty1 <= ty0; }
    [[nodiscard]] long long count() const
    {
        return empty() ? 0 : static_cast<long long>(tx1 - tx0) * (ty1 - ty0);
    }
    [[nodiscard]] bool contains(int tx, int ty) const { return tx >= tx0 && tx < tx1 && ty >= ty0 && ty < ty1; }
};

// The device pixels tile `key` covers (scene scaled by key.scale).
[[nodiscard]] QRect tileDeviceRect(const TileKey& key);
// The scene area behind those pixels, without slack.
[[nodiscard]] QRectF tileSceneRect(const TileKey& key);
// The tiles whose pixels meet `deviceRect` (device pixels at the raster scale, half-open).
[[nodiscard]] TileRange tilesCoveringDevice(const QRectF& deviceRect);
// The tiles that show any part of `sceneRect` at `scale`, with the
// antialiasing slack: the tiles a change to that area has to rasterise again.
[[nodiscard]] TileRange tilesCovering(const QRectF& sceneRect, double scale);

struct SnapshotPaint {
    // Logical zoom (raster scale / device pixel ratio): the grid skips lines
    // closer than 4 logical pixels.
    double gridZoom = 1;
    // Cosmetic line width in device pixels: one logical pixel.
    double lineWidth = 1;
    ImageQuality imageQuality = ImageQuality::Interactive;
    // Device pixels per canvas unit of the painter's transform, for level of
    // detail (level_of_detail.h); 0: everything is drawn, as the direct
    // renderer does.
    double deviceScale = 0;
    // Checked between cells; when it becomes true the painting stops (a tile
    // job that is no longer wanted). Null: never.
    const std::atomic<bool>* cancel = nullptr;
};

// Paints the decoration of `snapshot` and its records that meet `sceneRect`
// (already grown by the caller's slack), in paint order, with the painter's
// current transform. The painter's device is the clip. Records are replayed
// through a PainterCanvas, so this is exactly what the direct renderer would
// paint, except that cells that cannot reach `sceneRect` are skipped.
void paintSnapshot(QPainter& painter, const RenderSnapshot& snapshot, const QRectF& sceneRect,
                   const SnapshotPaint& options);

// Replays one record (shape, then label unless its editor is open) through a
// PainterCanvas of its own on `painter`, with the painter's transform.
// `deviceScale` (device pixels per canvas unit; 0 = off) applies the level of
// detail: a cell below a device pixel and a label too small to read are skipped.
// `live`: the active layer replays it (stats activeReplayed), not a tile (cellsReplayed).
void replayRecord(QPainter& painter, const CellRecord& record, ImageQuality quality, double deviceScale = 0,
                  bool live = false);

// A cheap estimate of what rasterising the tile costs, in microseconds, from
// the records that meet it (the display lists' op, glyph and image counts):
// the scheduler decides with it whether a small edit is rasterised at once or
// by a worker. Calibrated with the paint benchmark (BENCHMARKS.md).
// It stops adding once the sum passes `limitUs` (the caller only compares with
// a budget), so that a tile with thousands of records costs one pass over them
// at most.
[[nodiscard]] double estimateTileCostUs(const RenderSnapshot& snapshot, const TileKey& key,
                                        double limitUs = std::numeric_limits<double>::infinity());

// The same estimate for replaying one record, in microseconds, without the
// tile's fixed part (the active layer chooses between replaying its cells live
// every frame and a bitmap with it).
// `deviceScale` (0: none) applies the level of detail: a skipped cell costs
// next to nothing, a skipped label costs no glyphs and no label ops.
[[nodiscard]] double estimateRecordCostUs(const DisplayList& list, double deviceScale = 0);

// The tile `key` of `snapshot`: Format_ARGB32_Premultiplied, kTileSize square
// device pixels, devicePixelRatio `dpr` (so a blit is 1:1), opaque. Thread
// safe (a pure function of its arguments and the shared caches). When `cancel`
// turns true before the tile is finished the result is a null image.
[[nodiscard]] QImage rasterizeTile(const RenderSnapshot& snapshot, const TileKey& key, double dpr,
                                   const std::atomic<bool>* cancel = nullptr);

} // namespace qraw::render
