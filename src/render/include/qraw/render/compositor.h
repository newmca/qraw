#pragma once

// The compositor seam (DECISIONS.md D-031; ARCHITECTURE.md, "Canvas
// rendering").
//
// A paint of the canvas is a FrameDescription: the visible tiles of the
// content layer, the items of the active layer (cells under the user's hands)
// and the overlay (hover, selection, handles, guides, rubber band).
// A Compositor presents it. CpuCompositor does it with QPainter on the
// viewport; a QRhi compositor (Vulkan, Metal, D3D, behind a Qt 6.7 check
// because QRhiWidget needs it) replaces it without touching the rest of the
// editor.
//
// Rules that keep it replaceable: tiles are immutable once published, so a
// GPU backend uploads one texture per (key, version); every resource is an
// explicit argument of the call; there is no QPainter in the interface except
// the overlay callback (the overlay stays vector code on the CPU and reaches a
// GPU backend as a bitmap it draws); no global state.

#include "qraw/render/image_cache.h"
#include "qraw/render/render_snapshot.h"
#include "qraw/render/tile_rasterizer.h"

#include <QImage>
#include <QPoint>
#include <QRect>
#include <QSize>
#include <QTransform>

#include <cstdint>
#include <functional>
#include <memory>
#include <vector>

class QPainter;

namespace qraw::render {

// One tile of the content layer, ready to put on the screen.
struct TileRef {
    TileKey key;
    std::uint64_t version = 0; // TileCache::Entry::version: a new image has a new version
    QImage image;              // immutable; devicePixelRatio is the frame's
    QPoint dest;               // top-left in viewport device pixels
    QRectF source;             // the part of the image to show, in image pixels
    // The size the part takes on the screen, in device pixels; empty: the size
    // of `source` (a 1:1 copy). A different size is drawn smooth-scaled: a tile
    // of another zoom bucket shown while the right one renders, or a bucket
    // shown downscaled between two zoom steps. Neighbouring tiles' edges are
    // rounded to the same device pixel, so they meet without a seam.
    QSize destSize;
};

// One item of the active layer: a cell record replayed live as vectors, or a
// bitmap, under a transform (scene to viewport device pixels).
struct ActiveItem {
    std::shared_ptr<const CellRecord> record; // replayed when set...
    QImage bitmap;                            // ...else drawn
    QTransform transform;
    double opacity = 1;
};

struct FrameDescription {
    double devicePixelRatio = 1;
    QSize deviceSize;          // the viewport in device pixels
    QRect clip;                // device pixels to present (the exposed area)
    std::vector<TileRef> tiles;
    std::vector<ActiveItem> active;
    // Painted after tiles and active items, in logical viewport coordinates,
    // for compositors that present the whole frame. CanvasView leaves it empty
    // and paints the overlay from drawForeground, because the scene's rotated
    // label editor (a proxy widget) has to sit between content and overlay.
    std::function<void(QPainter&)> overlay;
};

class Compositor {
public:
    virtual ~Compositor() = default;
    virtual void present(const FrameDescription& frame) = 0;
};

// Presents with QPainter. Constructed per paint on the viewport's painter; the
// painter's transform is reset to the identity (device pixels) for the call and
// restored.
class CpuCompositor final : public Compositor {
public:
    explicit CpuCompositor(QPainter& painter);
    void present(const FrameDescription& frame) override;

private:
    QPainter& painter_;
};

} // namespace qraw::render
