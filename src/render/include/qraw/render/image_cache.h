#pragma once

// The decoded-image cache: an image is resolved (base64 / file read) and
// decoded once, whatever number of times, on whichever thread, it is painted.
//
// Two stages are cached together, behind one key:
//   1. resolveImage(): the base64 / percent decode or the file read,
//   2. the decode: QImage::fromData converted once to
//      Format_ARGB32_Premultiplied (drawing the non-premultiplied ARGB32 that
//      PNGs with alpha decode to costs 2 to 3 times as much per paint), or a
//      parsed SVG.
// The encoded bytes are not kept; ImageData::encodedBytes() re-reads them for
// the one consumer that needs them (SVG export).
//
// A raster image also owns a lazily built chain of power-of-two mip levels
// (2x2 box filter on premultiplied pixels, written by hand so that the result
// is identical everywhere), so that an on-screen draw of a 1000 px image into
// 100 px samples a level at most 2x larger than the destination instead of
// aliasing (bilinear from the full image) or scaling megabytes per paint.
//
// Everything here is thread-safe: tiles are painted on worker threads. The
// cache hands out shared_ptr<const CachedImage>; the only mutation of an
// entry afterwards is building a mip level or an SVG raster, under its own
// mutex.
//
// Key: a 64-bit content hash plus the length of the source for data URIs; the
// absolute path, file size and modification time for files; both with the
// resolve limit. The hash of a style's `image` value is memoised on its shared
// buffer (core::SharedValue), so no paint hashes a megabyte.

#include "qraw/core/shared_value.h"
#include "qraw/render/canvas.h"
#include "qraw/render/image_source.h"

#include <QImage>
#include <QRectF>
#include <QSize>
#include <QSizeF>
#include <QString>

#include <atomic>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string_view>
#include <vector>

class QPainter;

namespace qraw::render {

class ResolvedStyle;

class CachedImage {
public:
    // Mip levels stop at this size (the larger side).
    static constexpr int kSmallestLevel = 32;

    [[nodiscard]] std::uint64_t key() const noexcept { return key_; }
    [[nodiscard]] const QString& mimeType() const noexcept { return mime_; }
    [[nodiscard]] bool isSvg() const noexcept { return svg_; }
    // Resolved but not decodable (a broken image): it draws nothing, and the
    // failure is cached so it is not retried per paint.
    [[nodiscard]] bool decodeFailed() const noexcept { return failed_; }
    // Natural size: raster pixels, or the SVG's default size.
    [[nodiscard]] QSize size() const noexcept { return size_; }
    // Bytes held now: level 0, the mip levels and SVG rasters built so far.
    [[nodiscard]] std::int64_t byteSize() const noexcept { return bytes_.load(std::memory_order_relaxed); }

    // --- raster -------------------------------------------------------------
    // Level 0 is the decoded image, level n is half of level n-1 (rounded up).
    [[nodiscard]] int levelCount() const noexcept { return static_cast<int>(sizes_.size()); }
    [[nodiscard]] QSize levelSize(int level) const;
    // The image of a level, built on first use (thread-safe, built once). Null
    // when the image is not a decodable raster image or the level is out of range.
    [[nodiscard]] QImage level(int level) const;
    // The smallest level that is at least as large as `device` (device pixels)
    // in both directions; 0 when even level 0 is smaller. Never a level more
    // than 2x larger than needed.
    [[nodiscard]] int levelFor(const QSizeF& device) const;

    // The image for exact drawing (export, print): what QImage::fromData returns,
    // in its own pixel format. QPainter's downscaling of a non-premultiplied
    // ARGB32 or RGB32 image rounds differently from the premultiplied level 0
    // (by up to a few levels), so exact drawing needs the decoder's image. The
    // entry keeps it until the first on-screen use converts it to level 0 (a
    // process that only exports never converts it and never decodes twice);
    // after that, or after trimUnused(), the next exact draw decodes the source
    // again (counted) and keeps the result until it is trimmed in turn. An
    // image whose decoder format already is premultiplied, or one from a
    // disabled cache, returns level 0. `encoded` are the encoded bytes when
    // the caller has them (an ImageData made by hand).
    [[nodiscard]] QImage exactImage(const QByteArray& encoded = {}) const;

    // --- svg ----------------------------------------------------------------
    // The SVG rasterised for a destination of `device` pixels: the size is
    // rounded up to a power of two per side (cached per bucket). Null when the
    // bucket would be larger than kMaxSvgRasterBytes or the image is not an SVG.
    static constexpr std::int64_t kMaxSvgRasterBytes = 64LL * 1024 * 1024;
    [[nodiscard]] QImage svgRaster(const QSizeF& device) const;
    // Renders the SVG as vectors into `painter` (exact path), through the
    // calling thread's own parsed renderer. False when it is not a valid SVG.
    bool paintSvg(QPainter& painter, const QRectF& target) const;

    // The encoded bytes, read again from the source (a data URI or a file).
    [[nodiscard]] QByteArray encodedBytes() const;

    // Drops the mip levels (not level 0), the decoder's image of exactImage()
    // (once level 0 exists) and SVG rasters that were not used since `frame`;
    // returns the bytes freed. Called by the cache under pressure.
    std::int64_t trimUnused(std::uint64_t frame) const;
    [[nodiscard]] std::uint64_t lastUsedFrame() const noexcept { return usedFrame_.load(std::memory_order_relaxed); }
    void touch(std::uint64_t frame) const noexcept { usedFrame_.store(frame, std::memory_order_relaxed); }

    // Built by ImageCache.
    struct Source {
        core::SharedValue text;        // the style value or path, to re-read the encoded bytes
        ImageResolveContext context;
    };
    static std::shared_ptr<CachedImage> create(std::uint64_t key, Source source, const ImageData& resolved, bool keepPixelFormat);
    static std::shared_ptr<CachedImage> createFailed(std::uint64_t key, Source source, const QString& mime);

private:
    CachedImage() = default;
    void build(const ImageData& resolved, bool keepPixelFormat);

    std::uint64_t key_ = 0;
    QString mime_;
    bool svg_ = false;
    bool failed_ = false;
    bool levelZeroIsOriginal_ = false; // level 0 is the decoder's image, as returned
    QSize size_;
    Source source_;
    QByteArray svgBytes_;                 // small: SVGs are parsed again per thread
    std::vector<QSize> sizes_;            // size of every level
    mutable std::mutex mutex_;            // guards levels_ and svgRasters_
    mutable std::vector<QImage> levels_;  // levels_[0] always set for a raster image
    mutable std::vector<std::uint64_t> levelFrames_; // the frame each level was last used in
    mutable QImage original_;                        // the decoder's image, for exactImage(); see there
    mutable std::uint64_t originalFrame_ = 0;
    struct SvgBucket {
        QSize size;
        QImage image;
        std::uint64_t frame = 0;
    };
    mutable std::vector<SvgBucket> svgRasters_;
    mutable std::atomic<std::int64_t> bytes_{0};
    mutable std::atomic<std::uint64_t> usedFrame_{0};
};

// The 2x2 box filter used for the mip levels (exposed for tests): the
// premultiplied average of every 2x2 block of `source` (Format_ARGB32_Premultiplied),
// rounded to nearest; a size that is odd repeats the last row or column.
[[nodiscard]] QImage halveBoxFilter(const QImage& source);

class ImageCache {
public:
    // The process-wide cache (what PainterCanvas and the shapes use). Budget
    // 256 MiB, or QRAW_IMAGE_CACHE_MB.
    [[nodiscard]] static ImageCache& instance();

    explicit ImageCache(std::int64_t budgetBytes);
    ~ImageCache();
    ImageCache(const ImageCache&) = delete;
    ImageCache& operator=(const ImageCache&) = delete;

    // The image of an `image` style value. nullptr when the source cannot be
    // resolved (unsupported scheme, missing file, empty data); the failure is
    // cached for data URIs (a file is looked at again when its size or time
    // changes). A resolved image that cannot be decoded is returned with
    // decodeFailed() true. `source`'s hash is memoised when it is shared.
    [[nodiscard]] std::shared_ptr<const CachedImage> getShared(const core::SharedValue& source,
                                                               const ImageResolveContext& context);
    [[nodiscard]] std::shared_ptr<const CachedImage> get(std::string_view source, const ImageResolveContext& context);
    // An already resolved image (ImageData{bytes, mime} made by hand): keyed
    // by the hash of its bytes, computed on every call.
    [[nodiscard]] std::shared_ptr<const CachedImage> fromEncoded(const QByteArray& bytes, const QString& mime);

    // A new frame begins: entries used since are not evicted until the next one.
    void beginFrame() noexcept { ++frame_; }
    [[nodiscard]] std::uint64_t frame() const noexcept { return frame_.load(std::memory_order_relaxed); }

    void setBudget(std::int64_t bytes);
    [[nodiscard]] std::int64_t budget() const noexcept { return budget_.load(std::memory_order_relaxed); }
    // Bytes held by the entries now.
    [[nodiscard]] std::int64_t usedBytes() const;
    [[nodiscard]] std::size_t entryCount() const;
    void clear();

    // Off: every get() resolves and decodes again and keeps nothing, and level 0
    // stays in the format the decoder returned. This is the pre-cache behaviour,
    // the reference the export identity test compares against.
    void setEnabled(bool on) noexcept { enabled_.store(on, std::memory_order_relaxed); }
    [[nodiscard]] bool enabled() const noexcept { return enabled_.load(std::memory_order_relaxed); }

    // Evicts until the entries fit the budget: whole cold images first
    // (least recently used, not used in the current frame), then the unused mip
    // levels of the rest. Called after every insert; exposed for tests.
    void trim();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
    std::atomic<std::uint64_t> frame_{1};
    std::atomic<std::int64_t> budget_;
    std::atomic<bool> enabled_{true};
};

// The image of `style`'s `image` value for painting (nullopt: nothing to
// resolve, draw the placeholder), through the process-wide cache.
[[nodiscard]] std::optional<ImageData> cachedStyleImage(const ResolvedStyle& style, const ImageResolveContext& context);
// The same for a plain source string (stencil `<image src>`).
[[nodiscard]] std::optional<ImageData> cachedImage(std::string_view source, const ImageResolveContext& context);

} // namespace qraw::render
