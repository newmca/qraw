// The decoded-image cache (image_cache.h).

#include "qraw/render/image_cache.h"

#include "app_exit_cleanup.h"
#include "qraw/render/perf_scope.h"
#include "qraw/render/resolved_style.h"
#include "qraw/render/stats.h"

#include <QBuffer>
#include <QImageReader>
#include <QPainter>
#include <QSvgRenderer>

#include <algorithm>
#include <cmath>
#include <list>
#include <unordered_map>

namespace qraw::render {

namespace {

constexpr std::int64_t kDefaultBudgetBytes = 256LL * 1024 * 1024;
// What an entry without pixels (a cached failure) is counted as.
constexpr std::int64_t kEntryOverheadBytes = 256;
constexpr std::size_t kSvgRenderersPerThread = 32;

std::int64_t budgetFromEnvironment()
{
    bool ok = false;
    const qint64 megabytes = qEnvironmentVariableIntValue("QRAW_IMAGE_CACHE_MB", &ok);
    return ok && megabytes >= 0 ? static_cast<std::int64_t>(megabytes) * 1024 * 1024 : kDefaultBudgetBytes;
}

bool looksLikeSvg(const ImageData& image)
{
    if (image.mimeType == QLatin1String("image/svg+xml"))
        return true;
    QByteArrayView view(image.bytes);
    while (!view.isEmpty() && (view.front() == ' ' || view.front() == '\t' || view.front() == '\n' || view.front() == '\r'))
        view = view.sliced(1);
    return view.startsWith('<');
}

int ceilSize(double value) { return std::max(1, static_cast<int>(std::ceil(value - 0.01))); }

int nextPowerOfTwo(int value)
{
    int result = 1;
    while (result < value && result < (1 << 20))
        result <<= 1;
    return result;
}

// The calling thread's parsed SVGs (a QSvgRenderer is reentrant but one
// instance must not be used by two threads at once), by image key.
struct SvgRenderers {
    std::unordered_map<std::uint64_t, std::unique_ptr<QSvgRenderer>> byKey;
    void clear() { byKey.clear(); }
};

SvgRenderers& svgRenderers()
{
    thread_local SvgRenderers renderers;
    thread_local const bool registered = [] {
        clearOnApplicationExit([] { svgRenderers().clear(); });
        return true;
    }();
    (void)registered;
    return renderers;
}

} // namespace

// ------------------------------------------------------------------ mip filter

QImage halveBoxFilter(const QImage& source)
{
    Q_ASSERT(source.format() == QImage::Format_ARGB32_Premultiplied);
    const int w = source.width();
    const int h = source.height();
    const int ow = std::max(1, (w + 1) / 2);
    const int oh = std::max(1, (h + 1) / 2);
    QImage out(ow, oh, QImage::Format_ARGB32_Premultiplied);
    if (out.isNull() || source.isNull())
        return out;
    for (int y = 0; y < oh; ++y) {
        const auto* row0 = reinterpret_cast<const std::uint32_t*>(source.constScanLine(std::min(2 * y, h - 1)));
        const auto* row1 = reinterpret_cast<const std::uint32_t*>(source.constScanLine(std::min(2 * y + 1, h - 1)));
        auto* dst = reinterpret_cast<std::uint32_t*>(out.scanLine(y));
        for (int x = 0; x < ow; ++x) {
            const int x0 = std::min(2 * x, w - 1);
            const int x1 = std::min(2 * x + 1, w - 1);
            const std::uint32_t p[4] = {row0[x0], row0[x1], row1[x0], row1[x1]};
            std::uint32_t packed = 0;
            for (int shift = 0; shift < 32; shift += 8) {
                const std::uint32_t sum = ((p[0] >> shift) & 0xFF) + ((p[1] >> shift) & 0xFF) +
                                          ((p[2] >> shift) & 0xFF) + ((p[3] >> shift) & 0xFF);
                packed |= ((sum + 2) >> 2) << shift;
            }
            dst[x] = packed;
        }
    }
    return out;
}

// ----------------------------------------------------------------- CachedImage

std::shared_ptr<CachedImage> CachedImage::create(std::uint64_t key, Source source, const ImageData& resolved,
                                                 bool keepPixelFormat)
{
    std::shared_ptr<CachedImage> image(new CachedImage);
    image->key_ = key;
    image->source_ = std::move(source);
    image->mime_ = resolved.mimeType;
    image->build(resolved, keepPixelFormat);
    return image;
}

std::shared_ptr<CachedImage> CachedImage::createFailed(std::uint64_t key, Source source, const QString& mime)
{
    std::shared_ptr<CachedImage> image(new CachedImage);
    image->key_ = key;
    image->source_ = std::move(source);
    image->mime_ = mime;
    image->failed_ = true;
    image->bytes_ = kEntryOverheadBytes;
    return image;
}

void CachedImage::build(const ImageData& resolved, bool keepPixelFormat)
{
    QRAW_PERF_SCOPE(ImageDecode);
    stats::add(stats::Counter::imagesDecoded);
    stats::add(stats::Counter::bytesDecoded, static_cast<std::uint64_t>(resolved.bytes.size()));
    svg_ = looksLikeSvg(resolved);
    bytes_ = kEntryOverheadBytes;

    if (svg_) {
        QSvgRenderer renderer;
        if (!renderer.load(resolved.bytes)) {
            failed_ = true;
            return;
        }
        svgBytes_ = resolved.bytes;
        size_ = renderer.defaultSize();
        bytes_ += svgBytes_.size();
        return;
    }

    // The header says what the decode will allocate; QImageReader refuses what
    // exceeds its allocation limit, which is a failed image, not a crash.
    QBuffer buffer;
    buffer.setData(resolved.bytes); // shares the bytes
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    const QSize header = reader.size();
    const qint64 limit = QImageReader::allocationLimit();
    if (header.isValid() && limit > 0 &&
        static_cast<qint64>(header.width()) * header.height() * 4 > limit * 1024 * 1024) {
        failed_ = true;
        return;
    }
    QImage decoded = reader.read();
    if (decoded.isNull()) {
        failed_ = true;
        return;
    }
    size_ = decoded.size();
    levelZeroIsOriginal_ = keepPixelFormat || decoded.format() == QImage::Format_ARGB32_Premultiplied;

    // The level sizes are known up front, so that levelFor() needs no pixels.
    QSize s = size_;
    sizes_.push_back(s);
    while (std::max(s.width(), s.height()) > kSmallestLevel) {
        s = QSize(std::max(1, (s.width() + 1) / 2), std::max(1, (s.height() + 1) / 2));
        sizes_.push_back(s);
    }
    bytes_ += decoded.sizeInBytes();
    levels_.assign(sizes_.size(), QImage());
    levelFrames_.assign(sizes_.size(), 0);
    // Level 0 is made from the decoder's image by the first on-screen use.
    (levelZeroIsOriginal_ ? levels_[0] : original_) = std::move(decoded);
}

QSize CachedImage::levelSize(int level) const
{
    return level >= 0 && level < levelCount() ? sizes_[static_cast<std::size_t>(level)] : QSize();
}

int CachedImage::levelFor(const QSizeF& device) const
{
    if (levelCount() == 0)
        return 0;
    const int wantW = ceilSize(device.width());
    const int wantH = ceilSize(device.height());
    int level = 0;
    while (level + 1 < levelCount() && sizes_[static_cast<std::size_t>(level) + 1].width() >= wantW &&
           sizes_[static_cast<std::size_t>(level) + 1].height() >= wantH)
        ++level;
    return level;
}

QImage CachedImage::level(int level) const
{
    if (level < 0 || level >= levelCount())
        return {};
    std::lock_guard lock(mutex_);
    const std::uint64_t frame = usedFrame_.load(std::memory_order_relaxed);
    levelFrames_[0] = frame;
    if (levels_[0].isNull()) {
        // The decoder's image becomes level 0 (premultiplied) and is not kept next to it.
        levels_[0] = original_.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        bytes_.fetch_add(levels_[0].sizeInBytes() - original_.sizeInBytes(), std::memory_order_relaxed);
        original_ = QImage();
    }
    // A level is made from the one above it; the levels between are built too
    // (or again, when the cache dropped them).
    for (std::size_t i = 1; i <= static_cast<std::size_t>(level); ++i) {
        levelFrames_[i] = frame;
        if (!levels_[i].isNull())
            continue;
        QImage previous = levels_[i - 1];
        if (previous.format() != QImage::Format_ARGB32_Premultiplied)
            previous = previous.convertToFormat(QImage::Format_ARGB32_Premultiplied);
        levels_[i] = halveBoxFilter(previous);
        bytes_.fetch_add(levels_[i].sizeInBytes(), std::memory_order_relaxed);
    }
    return levels_[static_cast<std::size_t>(level)];
}

QImage CachedImage::exactImage(const QByteArray& encoded) const
{
    if (svg_ || failed_)
        return {};
    if (levelZeroIsOriginal_)
        return level(0);
    {
        std::lock_guard lock(mutex_);
        if (!original_.isNull()) {
            originalFrame_ = usedFrame_.load(std::memory_order_relaxed);
            return original_;
        }
    }
    const QByteArray bytes = encoded.isEmpty() ? encodedBytes() : encoded;
    stats::add(stats::Counter::imagesDecoded);
    stats::add(stats::Counter::bytesDecoded, static_cast<std::uint64_t>(bytes.size()));
    QImage again = QImage::fromData(bytes);
    if (!again.isNull()) {
        std::lock_guard lock(mutex_);
        if (original_.isNull()) {
            original_ = again;
            bytes_.fetch_add(again.sizeInBytes(), std::memory_order_relaxed);
        }
        originalFrame_ = usedFrame_.load(std::memory_order_relaxed);
    }
    return again;
}

QImage CachedImage::svgRaster(const QSizeF& device) const
{
    if (!svg_ || failed_ || !(device.width() > 0) || !(device.height() > 0))
        return {};
    const int w = nextPowerOfTwo(ceilSize(device.width()));
    const int h = nextPowerOfTwo(ceilSize(device.height()));
    if (static_cast<std::int64_t>(w) * h * 4 > kMaxSvgRasterBytes)
        return {};

    std::lock_guard lock(mutex_);
    for (SvgBucket& bucket : svgRasters_) {
        if (bucket.size == QSize(w, h)) {
            bucket.frame = usedFrame_.load(std::memory_order_relaxed);
            return bucket.image;
        }
    }
    QImage image(w, h, QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QSvgRenderer* renderer = nullptr;
        auto& mine = svgRenderers().byKey;
        if (auto it = mine.find(key_); it != mine.end()) {
            renderer = it->second.get();
        } else {
            if (mine.size() >= kSvgRenderersPerThread)
                mine.clear();
            auto created = std::make_unique<QSvgRenderer>(svgBytes_);
            renderer = created.get();
            mine.emplace(key_, std::move(created));
        }
        QPainter painter(&image);
        painter.setRenderHint(QPainter::Antialiasing, true);
        renderer->render(&painter, QRectF(0, 0, w, h));
    }
    bytes_.fetch_add(image.sizeInBytes(), std::memory_order_relaxed);
    svgRasters_.push_back({QSize(w, h), image, usedFrame_.load(std::memory_order_relaxed)});
    return image;
}

bool CachedImage::paintSvg(QPainter& painter, const QRectF& target) const
{
    if (!svg_ || failed_)
        return false;
    auto& mine = svgRenderers().byKey;
    auto it = mine.find(key_);
    if (it == mine.end()) {
        if (mine.size() >= kSvgRenderersPerThread)
            mine.clear();
        it = mine.emplace(key_, std::make_unique<QSvgRenderer>(svgBytes_)).first;
    }
    it->second->render(&painter, target);
    return true;
}

QByteArray CachedImage::encodedBytes() const
{
    if (svg_)
        return svgBytes_;
    const std::optional<ImageData> again = resolveImage(source_.text, source_.context);
    return again ? again->bytes : QByteArray();
}

std::int64_t CachedImage::trimUnused(std::uint64_t frame) const
{
    std::lock_guard lock(mutex_);
    std::int64_t freed = 0;
    if (!original_.isNull() && !levels_.empty() && !levels_[0].isNull() && originalFrame_ < frame) {
        freed += original_.sizeInBytes();
        original_ = QImage();
    }
    for (std::size_t i = 1; i < levels_.size(); ++i) {
        if (!levels_[i].isNull() && levelFrames_[i] < frame) {
            freed += levels_[i].sizeInBytes();
            levels_[i] = QImage();
        }
    }
    std::erase_if(svgRasters_, [&](const SvgBucket& bucket) {
        if (bucket.frame >= frame)
            return false;
        freed += bucket.image.sizeInBytes();
        return true;
    });
    bytes_.fetch_sub(freed, std::memory_order_relaxed);
    return freed;
}

QByteArray ImageData::encodedBytes() const
{
    if (!bytes.isEmpty() || !cached)
        return bytes;
    return cached->encodedBytes();
}

// ------------------------------------------------------------------ ImageCache

namespace {

struct Key {
    std::uint64_t hash = 0;
    std::uint64_t size = 0;
    std::uint64_t extra = 0; // the resolve limit; for files the modification time
    friend bool operator==(const Key&, const Key&) = default;
};

struct KeyHash {
    std::size_t operator()(const Key& key) const noexcept
    {
        return static_cast<std::size_t>(key.hash ^ (key.size * 0x9E3779B97F4A7C15ull) ^ (key.extra * 0xC2B2AE3D27D4EB4Full));
    }
};

// One cache entry. `image` is written once inside `once`, then `done` is set;
// readers outside the call_once look at `done` first.
struct Slot {
    std::once_flag once;
    std::shared_ptr<CachedImage> image; // null after done: the source could not be resolved
    std::atomic<bool> done{false};
    std::atomic<std::uint64_t> tick{0};
    std::atomic<std::uint64_t> frame{0};
};

} // namespace

struct ImageCache::Impl {
    mutable std::mutex mutex;
    std::unordered_map<Key, std::shared_ptr<Slot>, KeyHash> entries;
    std::uint64_t tick = 0;

    static std::int64_t slotBytes(const Slot& slot)
    {
        return slot.image ? slot.image->byteSize() : kEntryOverheadBytes;
    }
    std::int64_t total() const
    {
        std::int64_t bytes = 0;
        for (const auto& [key, slot] : entries) {
            if (slot->done.load(std::memory_order_acquire))
                bytes += slotBytes(*slot);
        }
        return bytes;
    }
};

ImageCache::ImageCache(std::int64_t budgetBytes) : d(std::make_unique<Impl>()), budget_(budgetBytes) {}
ImageCache::~ImageCache() = default;

ImageCache& ImageCache::instance()
{
    static ImageCache cache(budgetFromEnvironment());
    return cache;
}

void ImageCache::setBudget(std::int64_t bytes)
{
    budget_.store(bytes, std::memory_order_relaxed);
    trim();
}

std::int64_t ImageCache::usedBytes() const
{
    std::lock_guard lock(d->mutex);
    return d->total();
}

std::size_t ImageCache::entryCount() const
{
    std::lock_guard lock(d->mutex);
    return d->entries.size();
}

void ImageCache::clear()
{
    std::lock_guard lock(d->mutex);
    std::erase_if(d->entries, [](const auto& item) { return item.second->done.load(std::memory_order_acquire); });
}

void ImageCache::trim()
{
    const std::int64_t budget = budget_.load(std::memory_order_relaxed);
    const std::uint64_t frame = frame_.load(std::memory_order_relaxed);
    std::lock_guard lock(d->mutex);
    std::int64_t total = d->total();
    if (total <= budget)
        return;

    // 1. Whole images, coldest first, except those used in this frame.
    std::vector<std::pair<std::uint64_t, Key>> cold;
    for (const auto& [key, slot] : d->entries) {
        if (slot->done.load(std::memory_order_acquire) && slot->frame.load(std::memory_order_relaxed) != frame)
            cold.emplace_back(slot->tick.load(std::memory_order_relaxed), key);
    }
    std::sort(cold.begin(), cold.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (const auto& [tick, key] : cold) {
        if (total <= budget)
            return;
        total -= Impl::slotBytes(*d->entries[key]);
        d->entries.erase(key);
    }

    // 2. The mip levels and SVG rasters nothing used in this frame.
    for (const auto& [key, slot] : d->entries) {
        if (total <= budget)
            return;
        if (slot->done.load(std::memory_order_acquire) && slot->image)
            total -= slot->image->trimUnused(frame);
    }

    // 3. A hard limit of twice the budget, frame or not: a long export that
    // never calls beginFrame() (every image of the page is "this frame's")
    // must not keep them all. The newest entry stays.
    const std::int64_t hardLimit = budget * 2;
    if (total <= hardLimit)
        return;
    std::vector<std::pair<std::uint64_t, Key>> all;
    for (const auto& [key, slot] : d->entries) {
        if (slot->done.load(std::memory_order_acquire))
            all.emplace_back(slot->tick.load(std::memory_order_relaxed), key);
    }
    std::sort(all.begin(), all.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
    for (std::size_t i = 0; i + 1 < all.size() && total > hardLimit; ++i) {
        total -= Impl::slotBytes(*d->entries[all[i].second]);
        d->entries.erase(all[i].second);
    }
}

namespace {

// Resolves and decodes `source`; the result is null when it cannot be resolved.
std::shared_ptr<CachedImage> load(std::uint64_t key, const core::SharedValue& source, const ImageResolveContext& context,
                                  bool keepPixelFormat)
{
    const std::optional<ImageData> resolved = resolveImage(source, context);
    if (!resolved)
        return nullptr;
    CachedImage::Source origin{source, context};
    return CachedImage::create(key, std::move(origin), *resolved, keepPixelFormat);
}

} // namespace

std::shared_ptr<const CachedImage> ImageCache::getShared(const core::SharedValue& source, const ImageResolveContext& context)
{
    const std::optional<ImageSourceInfo> info = inspectImageSource(source, context);
    if (!info)
        return nullptr;

    Key key;
    key.extra = static_cast<std::uint64_t>(context.maxBytes);
    if (info->isData) {
        key.hash = source.contentHash();
        key.size = source.size();
    } else {
        const QByteArray path = info->absolutePath.toUtf8();
        key.hash = core::hashBytes(std::string_view(path.constData(), static_cast<std::size_t>(path.size())));
        key.size = static_cast<std::uint64_t>(info->fileSize);
        key.extra ^= static_cast<std::uint64_t>(info->fileModifiedMs) * 0x9E3779B97F4A7C15ull;
    }

    if (!enabled())
        return load(key.hash ^ key.size, source, context, true);

    std::shared_ptr<Slot> slot;
    {
        std::lock_guard lock(d->mutex);
        auto [it, inserted] = d->entries.try_emplace(key);
        if (inserted)
            it->second = std::make_shared<Slot>();
        slot = it->second;
        slot->tick.store(++d->tick, std::memory_order_relaxed);
        slot->frame.store(frame_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    bool loaded = false;
    // The decode runs outside the cache lock; a second requester of the same
    // key waits here for the first one's result instead of decoding again.
    std::call_once(slot->once, [&] {
        slot->image = load(key.hash ^ key.size, source, context, false);
        slot->done.store(true, std::memory_order_release);
        loaded = true;
    });
    if (loaded)
        trim();
    if (slot->image)
        slot->image->touch(frame_.load(std::memory_order_relaxed));
    return slot->image;
}

std::shared_ptr<const CachedImage> ImageCache::get(std::string_view source, const ImageResolveContext& context)
{
    return getShared(core::SharedValue(source), context);
}

std::shared_ptr<const CachedImage> ImageCache::fromEncoded(const QByteArray& bytes, const QString& mime)
{
    if (bytes.isEmpty())
        return nullptr;
    ImageData resolved{bytes, mime, {}};
    Key key;
    key.hash = core::hashBytes(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    key.size = static_cast<std::uint64_t>(bytes.size());
    key.extra = 1; // an encoded image of its own, apart from the sources with that content

    const auto make = [&](bool keepPixelFormat) {
        return CachedImage::create(key.hash ^ key.size, {}, resolved, keepPixelFormat);
    };
    if (!enabled())
        return make(true);

    std::shared_ptr<Slot> slot;
    {
        std::lock_guard lock(d->mutex);
        auto [it, inserted] = d->entries.try_emplace(key);
        if (inserted)
            it->second = std::make_shared<Slot>();
        slot = it->second;
        slot->tick.store(++d->tick, std::memory_order_relaxed);
        slot->frame.store(frame_.load(std::memory_order_relaxed), std::memory_order_relaxed);
    }
    bool loaded = false;
    std::call_once(slot->once, [&] {
        slot->image = make(false);
        slot->done.store(true, std::memory_order_release);
        loaded = true;
    });
    if (loaded)
        trim();
    slot->image->touch(frame_.load(std::memory_order_relaxed));
    return slot->image;
}

namespace {

std::optional<ImageData> asImageData(const std::shared_ptr<const CachedImage>& image)
{
    if (!image)
        return std::nullopt;
    ImageData data;
    data.mimeType = image->mimeType();
    data.cached = image;
    return data;
}

} // namespace

std::optional<ImageData> cachedStyleImage(const ResolvedStyle& style, const ImageResolveContext& context)
{
    const core::SharedValue* value = style.imageValue();
    if (value == nullptr)
        return std::nullopt;
    return asImageData(ImageCache::instance().getShared(*value, context));
}

std::optional<ImageData> cachedImage(std::string_view source, const ImageResolveContext& context)
{
    return asImageData(ImageCache::instance().get(source, context));
}

} // namespace qraw::render
