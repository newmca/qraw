#pragma once
// Deterministic benchmark documents: one page with a requested number of
// cells (about 65% vertices, 35% edges). The same count always produces the
// same bytes, on every platform (own PRNG, no std distributions).

#include <QByteArray>

#include <cstddef>

namespace qraw::bench {

// The saved bytes (format::writeDocument) of a one-page document with
// exactly `cellCount` cells besides the root and the layer: a grid of
// vertices in five shapes (about 20% with html labels), every 50th grid
// slot a swimlane with three children, a few small tables, and edges between
// grid neighbours in three routing styles. Returns an empty array if the
// count is too small to hold the fixed parts (below 100).
[[nodiscard]] QByteArray generateDocument(std::size_t cellCount);

// The synthetic image-heavy diagram: one page of 150 cells (with the root and
// the layer), 32 of them embedded PNGs of about 1000 x 600 px stored as
// `image=data:image/png,<base64>`, about 1.2 MB of base64 each (about 37 MB
// in all), generated from a fixed seed. The pixels are deterministic; the
// bytes of the PNG streams depend on the zlib version. Written as XML (not
// through format::writeDocument, to save a second pass over that much
// text); the benchmark reads it like any file.
[[nodiscard]] QByteArray generateImageHeavyDocument();

inline constexpr std::size_t kImageHeavyImageCount = 32;
inline constexpr std::size_t kImageHeavyCellCount = 150; // with the root and the layer

} // namespace qraw::bench
