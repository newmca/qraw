// Label layout caches (text_layout_cache.h).

#include "qraw/render/text_layout_cache.h"

#include "app_exit_cleanup.h"
#include "qraw/core/shared_value.h"
#include "qraw/render/perf_scope.h"
#include "qraw/render/stats.h"
#include "qraw/render/text_metrics.h"

#include <QAbstractTextDocumentLayout>
#include <QFontMetricsF>
#include <QTextBlock>
#include <QTextLayout>
#include <QTextTable>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <list>
#include <mutex>
#include <string_view>
#include <unordered_map>

namespace qraw::render {

namespace {

constexpr std::size_t kMetricsCapacity = 16384;
// Rough memory of an entry apart from its content, for the byte limit.
constexpr std::size_t kEntryBaseCost = 4096;
constexpr std::size_t kMaxCostBytes = 96u * 1024 * 1024;

// Everything of a TextBlock that the layout depends on.
struct LayoutKey {
    bool html = false;
    std::string_view content;
    QStringView family;
    int pixelSize = 1;
    int fontStyle = 0;
    int align = 0; // geometry::HAlign
    bool wrap = false;
    bool fill = false;    // a table label that fills its box (see fillsBox)
    double wrapWidth = 0; // only when wrap or fill
    double fillHeight = 0; // only when fill
    double lineHeight = 0;
};

// An HTML label with a table in a box with overflow=fill: draw.io's text div is as large as the box,
// so the table's width and height of 100 % are the box's. Other labels keep the ideal width of their
// text (alignment across lines of different length).
bool fillsBox(const TextBlock& b, double boxWidth)
{
    return b.text.html && boxWidth > 0 && b.overflow == geometry::Overflow::Fill &&
           b.text.content.find("<table") != std::string::npos;
}

LayoutKey keyFor(const TextBlock& b, double scale, double boxWidth)
{
    LayoutKey k;
    k.html = b.text.html;
    k.content = b.text.content;
    k.family = b.fontFamily;
    k.pixelSize = std::max(1, static_cast<int>(std::lround(b.fontSize * scale)));
    k.fontStyle = b.fontStyle;
    k.align = static_cast<int>(b.align);
    k.wrap = b.wrap && boxWidth > 0;
    k.fill = fillsBox(b, boxWidth);
    k.wrapWidth = k.wrap || k.fill ? boxWidth : 0;
    k.fillHeight = k.fill ? b.height * scale : 0;
    k.lineHeight = b.lineHeight;
    return k;
}

std::uint64_t mix(std::uint64_t h, std::uint64_t v)
{
    h ^= v + 0x9E3779B97F4A7C15ull + (h << 6) + (h >> 2);
    return h;
}

std::uint64_t doubleBits(double v)
{
    std::uint64_t bits;
    std::memcpy(&bits, &v, sizeof bits);
    return bits;
}

std::uint64_t hashOf(const LayoutKey& k)
{
    std::uint64_t h = core::hashBytes(k.content);
    h = mix(h, static_cast<std::uint64_t>(qHash(k.family)));
    h = mix(h, static_cast<std::uint64_t>(k.pixelSize) | (static_cast<std::uint64_t>(k.fontStyle) << 32));
    h = mix(h, static_cast<std::uint64_t>(k.align) | (static_cast<std::uint64_t>(k.html) << 8) |
                   (static_cast<std::uint64_t>(k.wrap) << 9) | (static_cast<std::uint64_t>(k.fill) << 10));
    h = mix(h, doubleBits(k.wrapWidth));
    h = mix(h, doubleBits(k.fillHeight));
    h = mix(h, doubleBits(k.lineHeight));
    return h;
}

// The owned copy of a key.
struct StoredKey {
    bool html = false;
    std::string content;
    QString family;
    int pixelSize = 1;
    int fontStyle = 0;
    int align = 0;
    bool wrap = false;
    bool fill = false;
    double wrapWidth = 0;
    double fillHeight = 0;
    double lineHeight = 0;

    explicit StoredKey(const LayoutKey& k)
        : html(k.html), content(k.content), family(k.family.toString()), pixelSize(k.pixelSize),
          fontStyle(k.fontStyle), align(k.align), wrap(k.wrap), fill(k.fill), wrapWidth(k.wrapWidth), fillHeight(k.fillHeight), lineHeight(k.lineHeight)
    {
    }
    [[nodiscard]] bool matches(const LayoutKey& k) const
    {
        return html == k.html && pixelSize == k.pixelSize && fontStyle == k.fontStyle && align == k.align &&
               wrap == k.wrap && fill == k.fill && wrapWidth == k.wrapWidth && fillHeight == k.fillHeight && lineHeight == k.lineHeight &&
               std::string_view(content) == k.content && QStringView(family) == k.family;
    }
    [[nodiscard]] std::size_t cost() const { return kEntryBaseCost + content.size() * 8; }
};

double hFraction(geometry::HAlign a)
{
    return a == geometry::HAlign::Left ? 0 : a == geometry::HAlign::Center ? 0.5 : 1;
}
double vFraction(geometry::VAlign a)
{
    return a == geometry::VAlign::Top ? 0 : a == geometry::VAlign::Middle ? 0.5 : 1;
}

// The vertical placement of a laid-out document (see LabelPlacement).
LabelPlacement placementOf(const TextBlock& b, const QTextDocument& doc, int pixelSize, double contentHeight)
{
    int lines = 0;
    QTextLine first;
    double firstTopMargin = 0;
    double lastBottomMargin = 0;
    for (QTextBlock paragraph = doc.begin(); paragraph.isValid(); paragraph = paragraph.next()) {
        const QTextLayout* layout = paragraph.layout();
        if (layout == nullptr || layout->lineCount() == 0)
            continue;
        if (lines == 0) {
            first = layout->lineAt(0);
            firstTopMargin = paragraph.blockFormat().topMargin();
        }
        lastBottomMargin = paragraph.blockFormat().bottomMargin();
        lines += layout->lineCount();
    }
    LabelPlacement placement;
    placement.blockHeight = contentHeight;
    if (lines == 0)
        return placement;

    const double size = pixelSize;
    // Where Qt put the first baseline in its block. A line of Qt's own height has it one ascent down; a line
    // of a fixed height (see configureTextDocument) has it wherever the fixed height puts it, which depends
    // on the font's leading. Read from the layout, so what is painted is what is placed.
    const double firstBaseline = first.y() + first.ascent();
    if (!b.text.html) {
        // mxSvgCanvas2D.plainText: the block is `size + (lines - 1) * lh` high with lh = round(size *
        // LINE_HEIGHT), its first baseline is at `size - 1` below its top, and a bottom anchored block
        // moves up one more pixel.
        const double lh = std::round(std::max(b.lineHeight, 1.0) * size);
        placement.blockHeight = size + (lines - 1) * lh;
        placement.shift = (size - 1) - firstBaseline;
        placement.bottomGap = 1;
    } else if (b.lineHeight > 0) {
        // CSS: a line box of `line-height` puts half of what the font's ascent and descent leave over
        // above the glyphs. The first line's own ascent and descent count, so larger text in it is taken
        // into account. Where the lines have an exact pitch the line box is `line-height` itself; otherwise
        // it is the label's line-height scaled the way the document scaled the line (see
        // configureTextDocument).
        double lineBox = b.lineHeight * size;
        if (!hasExactLinePitch(doc)) {
            const double natural = naturalLineHeight(doc.defaultFont());
            if (natural > 0)
                lineBox = lineBox * first.height() / natural;
        }
        // Qt does not lay out the margin above the first block and below the last; the label box of a
        // browser contains them (they do not collapse through it).
        placement.blockHeight += firstTopMargin + lastBottomMargin;
        const double cssBaseline = firstTopMargin + (lineBox - (first.ascent() + first.descent())) / 2 + first.ascent();
        placement.shift = cssBaseline - firstBaseline;
    }
    return placement;
}

// `height: 100%` of a table: Qt's HTML importer ignores it and its layout has no table height, so the
// rows are made taller by the padding of their cells (a browser shares the extra height between the
// rows alike). A table that is already as tall as the box stays as it is. Run with the text width set.
void fillTables(QTextDocument& doc, double height)
{
    for (QTextFrame* frame : doc.rootFrame()->childFrames()) {
        auto* table = qobject_cast<QTextTable*>(frame);
        if (table == nullptr || table->rows() == 0)
            continue;
        const double current = doc.documentLayout()->frameBoundingRect(table).height();
        const double extraPerRow = (height - current) / table->rows();
        if (extraPerRow <= 0)
            continue;
        for (int row = 0; row < table->rows(); ++row) {
            for (int column = 0; column < table->columns(); ++column) {
                QTextTableCell cell = table->cellAt(row, column);
                if (cell.row() != row || cell.column() != column)
                    continue; // covered by a span; the cell is handled at its origin
                QTextCharFormat format = cell.format();
                auto cellFormat = format.toTableCellFormat();
                // A spanning cell takes the extra of every row it covers.
                const double extra = extraPerRow * cell.rowSpan();
                // A cell without padding of its own uses the table's cellpadding; setting one replaces it.
                const double top = format.hasProperty(QTextFormat::TableCellTopPadding) ? cellFormat.topPadding()
                                                                                        : table->format().cellPadding();
                const double bottom = format.hasProperty(QTextFormat::TableCellBottomPadding)
                                          ? cellFormat.bottomPadding()
                                          : table->format().cellPadding();
                cellFormat.setTopPadding(top + extra / 2);
                cellFormat.setBottomPadding(bottom + extra / 2);
                cell.setFormat(cellFormat);
            }
        }
    }
}

} // namespace

// ------------------------------------------------------------ per-thread cache

struct TextLayoutCache::Entry {
    std::uint64_t hash = 0;
    StoredKey key;
    LaidOutLabel label;
    Entry(std::uint64_t h, const LayoutKey& k) : hash(h), key(k) {}
};

TextLayoutCache::TextLayoutCache() = default;
TextLayoutCache::~TextLayoutCache() = default;

TextLayoutCache& TextLayoutCache::forThisThread()
{
    thread_local TextLayoutCache cache;
    thread_local const bool registered = [] {
        clearOnApplicationExit([] { TextLayoutCache::forThisThread().clear(); });
        return true;
    }();
    (void)registered;
    return cache;
}

void TextLayoutCache::clear()
{
    index_.clear();
    lru_.clear();
    costBytes_ = 0;
}

void TextLayoutCache::setCapacity(std::size_t entries)
{
    capacity_ = entries;
    trimToCapacity();
}

void TextLayoutCache::trimToCapacity()
{
    while (lru_.size() > 1 && (lru_.size() > capacity_ || costBytes_ > kMaxCostBytes)) {
        const auto oldest = lru_.begin();
        const auto range = index_.equal_range(oldest->hash);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second == oldest) {
                index_.erase(it);
                break;
            }
        }
        costBytes_ -= oldest->key.cost();
        lru_.erase(oldest);
    }
}

const LaidOutLabel& TextLayoutCache::layout(const TextBlock& block, double scale, double boxWidth)
{
    const LayoutKey key = keyFor(block, scale, boxWidth);
    const std::uint64_t hash = hashOf(key);

    const auto range = index_.equal_range(hash);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second->key.matches(key)) {
            lru_.splice(lru_.end(), lru_, it->second); // most recently used; iterators stay valid
            return lru_.back().label;
        }
    }

    Entry entry(hash, key);
    {
        QRAW_PERF_SCOPE(TextLayout);
        stats::add(stats::Counter::textLayouts);
        auto doc = std::make_unique<QTextDocument>();
        configureTextDocument(*doc, block, scale, boxWidth);
        // Unwrapped text uses its ideal width so that alignment works across
        // lines of different length.
        double width = boxWidth;
        if (key.wrap || key.fill) {
            doc->setTextWidth(boxWidth);
            if (key.fill)
                fillTables(*doc, key.fillHeight);
        } else {
            doc->setTextWidth(-1);
            width = doc->idealWidth();
            doc->setTextWidth(width);
        }
        entry.label.width = width;
        entry.label.height = doc->size().height();
        entry.label.placement = placementOf(block, *doc, key.pixelSize, entry.label.height);
        entry.label.document = std::move(doc);
    }
    costBytes_ += entry.key.cost();
    lru_.push_back(std::move(entry));
    index_.emplace(hash, std::prev(lru_.end()));
    trimToCapacity();
    return lru_.back().label;
}

// ------------------------------------------------------------ shared metrics

namespace {

struct MetricsEntry {
    std::uint64_t hash = 0;
    StoredKey key;
    LabelMetrics metrics;
};

struct MetricsCache {
    std::mutex mutex;
    std::list<MetricsEntry> lru; // most recently used last
    std::unordered_multimap<std::uint64_t, std::list<MetricsEntry>::iterator> index;
    std::uint64_t hits = 0;
    std::uint64_t misses = 0;
};

MetricsCache& metricsCache()
{
    static MetricsCache cache;
    return cache;
}

LabelMetrics measure(const TextBlock& block, double scale, double boxWidth)
{
    const LaidOutLabel& label = TextLayoutCache::forThisThread().layout(block, scale, boxWidth);
    LabelMetrics metrics;
    metrics.size = QSizeF(label.width, label.height);
    metrics.placement = label.placement;
    QAbstractTextDocumentLayout* layout = label.document->documentLayout();
    for (QTextBlock paragraph = label.document->begin(); paragraph.isValid(); paragraph = paragraph.next()) {
        const QTextLayout* lines = paragraph.layout();
        if (lines == nullptr)
            continue;
        const QPointF origin = layout->blockBoundingRect(paragraph).topLeft();
        for (int i = 0; i < lines->lineCount(); ++i)
            metrics.lines.push_back(lines->lineAt(i).rect().translated(origin));
    }
    return metrics;
}

} // namespace

LabelMetrics labelMetrics(const TextBlock& block, double scale, double boxWidth)
{
    if (block.text.empty())
        return {};
    const LayoutKey key = keyFor(block, scale, boxWidth);
    const std::uint64_t hash = hashOf(key);
    MetricsCache& cache = metricsCache();
    {
        std::lock_guard lock(cache.mutex);
        const auto range = cache.index.equal_range(hash);
        for (auto it = range.first; it != range.second; ++it) {
            if (it->second->key.matches(key)) {
                ++cache.hits;
                cache.lru.splice(cache.lru.end(), cache.lru, it->second);
                return cache.lru.back().metrics;
            }
        }
    }

    // Laid out outside the lock (on this thread's own documents); two threads
    // that miss together both measure, which is harmless: the numbers agree.
    LabelMetrics metrics = measure(block, scale, boxWidth);
    std::lock_guard lock(cache.mutex);
    ++cache.misses;
    const auto range = cache.index.equal_range(hash);
    for (auto it = range.first; it != range.second; ++it) {
        if (it->second->key.matches(key))
            return metrics;
    }
    cache.lru.push_back(MetricsEntry{hash, StoredKey(key), metrics});
    cache.index.emplace(hash, std::prev(cache.lru.end()));
    while (cache.lru.size() > kMetricsCapacity) {
        const auto oldest = cache.lru.begin();
        const auto old = cache.index.equal_range(oldest->hash);
        for (auto it = old.first; it != old.second; ++it) {
            if (it->second == oldest) {
                cache.index.erase(it);
                break;
            }
        }
        cache.lru.erase(oldest);
    }
    return metrics;
}

QRectF labelTextRect(const TextBlock& block)
{
    const LabelMetrics metrics = labelMetrics(block, 1, block.width);
    const double w = metrics.size.width();
    const double h = metrics.size.height();
    return QRectF(block.x - w * hFraction(block.align), metrics.placement.contentTop(block.y, vFraction(block.verticalAlign)),
                  w, h);
}

LabelCacheStats labelCacheStats()
{
    MetricsCache& cache = metricsCache();
    std::lock_guard lock(cache.mutex);
    return {cache.hits, cache.misses, cache.lru.size()};
}

void clearLabelMetrics()
{
    MetricsCache& cache = metricsCache();
    std::lock_guard lock(cache.mutex);
    cache.index.clear();
    cache.lru.clear();
    cache.hits = cache.misses = 0;
}

} // namespace qraw::render
