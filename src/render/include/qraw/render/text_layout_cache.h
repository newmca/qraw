#pragma once

// Label layout caches.
//
// A label used to be laid out again on every paint: a fresh QTextDocument,
// setHtml/setPlainText, layout, draw. Laying out is 35 to 60 % of a label's
// cost (docs/research/qt-canvas-performance.md, "Text"); the rest is glyph
// rasterisation, which tiles take away. Two caches:
//
//  (a) TextLayoutCache, one per thread (thread_local): the laid-out
//      QTextDocument itself, for drawing. A QTextDocument and Qt's font
//      engines and glyph caches belong to one thread, so nothing is shared;
//      the GUI thread has one and so will every persistent tile worker.
//  (b) labelMetrics(): thread-neutral numbers about the same layout (the
//      text size and the line rectangles), in one mutex-protected cache for
//      every thread. Autosize and the label editor measure through it, and so
//      will the level of detail (line rectangles) and the exact paint bounds.
//
// Both are keyed by everything that affects the layout and nothing else: the
// text or HTML, the html flag, the font family, the integer pixel size
// (fontSize x canvas scale, rounded: the layout is independent of the view
// zoom, which only enters through the painter transform), the style bits, the
// alignment, the wrap width (only when wrapping) and the line height. The
// colour is not part of the key: it is applied when drawing.

#include "qraw/render/text.h"

#include <QRectF>
#include <QSizeF>
#include <QTextDocument>

#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <unordered_map>
#include <vector>

namespace qraw::render {

// Where a laid-out label sits vertically relative to its anchor (the y of the
// TextBlock). draw.io does not anchor the Qt document's box: HTML labels are CSS
// line boxes (line-height: 1.2, the leading split evenly above and below the
// glyphs) and plain labels are SVG <text> elements positioned by
// mxSvgCanvas2D.plainText (one font size of block height per line plus the
// line pitch, the first baseline a pixel above one font size). Qt keeps the
// font ascent above the first baseline whatever the line height, which puts a
// line 1 to 5 px lower than draw.io for the same box (docs/FIDELITY.md,
// class 1). The placement turns the one into the other.
struct LabelPlacement {
    double blockHeight = 0; // the height the vertical alignment anchors (middle: half of it)
    double shift = 0;       // the document's top minus the block's top
    double bottomGap = 0;   // plainText puts a bottom anchored block one pixel higher

    // The top of the document for an anchor at `anchorY` and a vertical
    // alignment fraction (0 top, 0.5 middle, 1 bottom).
    [[nodiscard]] double contentTop(double anchorY, double vFraction) const
    {
        const double blockTop = vFraction >= 1 ? anchorY - blockHeight - bottomGap : anchorY - blockHeight * vFraction;
        return blockTop + shift;
    }
};

// A label laid out for drawing: the document and the size of its content in
// the pixels of the layout.
struct LaidOutLabel {
    std::unique_ptr<QTextDocument> document;
    double width = 0;  // the wrap width, or the ideal width of unwrapped text
    double height = 0;
    LabelPlacement placement;
};

class TextLayoutCache {
public:
    // The calling thread's cache.
    [[nodiscard]] static TextLayoutCache& forThisThread();

    // The layout of `block` at canvas `scale` for a box `boxWidth` px wide
    // (wrapping happens only when block.wrap and boxWidth > 0), laid out on a
    // miss (counted in stats textLayouts). The reference is valid until the
    // next call on this thread.
    [[nodiscard]] const LaidOutLabel& layout(const TextBlock& block, double scale, double boxWidth);

    void setCapacity(std::size_t entries);
    [[nodiscard]] std::size_t size() const { return lru_.size(); }
    void clear();

    ~TextLayoutCache();
    TextLayoutCache();
    TextLayoutCache(const TextLayoutCache&) = delete;
    TextLayoutCache& operator=(const TextLayoutCache&) = delete;

private:
    struct Entry;
    void trimToCapacity();

    std::list<Entry> lru_;                                          // most recently used last
    std::unordered_multimap<std::uint64_t, std::list<Entry>::iterator> index_; // by key hash
    std::size_t capacity_ = 8192;
    std::size_t costBytes_ = 0;
};

// Thread-neutral facts about one laid-out label (the layout at `scale`).
struct LabelMetrics {
    QSizeF size;                 // content width x height
    LabelPlacement placement;    // where the content sits for a given anchor
    std::vector<QRectF> lines;   // one rectangle per laid-out line, relative to the content's top-left corner
};

// The metrics of `block` at `scale` for a box `boxWidth` wide, from the shared
// cache (laid out on the calling thread on a miss). Empty text: size 0.
[[nodiscard]] LabelMetrics labelMetrics(const TextBlock& block, double scale = 1, double boxWidth = 0);

// The rectangle the text of `block` occupies on the page, in model units, as
// PainterCanvas::text at scale 1 paints it: anchored at (block.x, block.y)
// with the block's alignment (vertically through LabelPlacement), `block.width` as the wrap width when block.wrap
// is set. Not rotated (the block's rotation turns it around (x, y)) and
// without padding. An empty block gives an empty rectangle at the anchor.
[[nodiscard]] QRectF labelTextRect(const TextBlock& block);

struct LabelCacheStats {
    std::uint64_t metricsHits = 0;
    std::uint64_t metricsMisses = 0;
    std::size_t metricsEntries = 0;
};
[[nodiscard]] LabelCacheStats labelCacheStats();
void clearLabelMetrics();

} // namespace qraw::render
