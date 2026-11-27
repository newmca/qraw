#pragma once

// Resolves the `image` style value of image/label shapes into bytes.
// Nothing is ever fetched from the network.

#include "qraw/render/canvas.h"

#include <QString>

#include <optional>
#include <string_view>

namespace qraw::render {

struct ImageResolveContext {
    // Directory of the document, for relative file references. Empty: no
    // relative lookups.
    QString baseDirectory;
    // Local file references (absolute, file:// or relative) are read only
    // when true. Export from the CLI enables it; previews of untrusted
    // files may not.
    bool allowLocalFiles = true;
    // Larger payloads are rejected (bytes after decoding).
    qsizetype maxBytes = 32 * 1024 * 1024;
};

// Accepts:
//  - data:<mime>[;base64],<payload>  (percent-decoded when not base64)
//  - draw.io's non-standard data:image/svg+xml,<base64> (no ";base64"
//    marker, payload not starting with '<' or '%3C'): base64-decoded
//  - absolute paths, file:// URLs and relative paths (with allowLocalFiles)
// Rejects (nullopt): http(s)/ftp and every other scheme, draw.io bundled
// paths that do not exist locally (img/lib/...), oversized or empty data.
// The MIME type comes from the data URI or the file extension
// (png, jpg/jpeg, gif, svg, webp, bmp); sniffed from the bytes if unknown.
[[nodiscard]] std::optional<ImageData> resolveImage(std::string_view source, const ImageResolveContext& context);

// What a source refers to, found without reading its content: the image cache
// keys on it. nullopt when resolveImage() would fail for a reason that does not
// need the content (empty, unsupported scheme, local files not allowed, missing
// or too large file).
struct ImageSourceInfo {
    bool isData = false;       // a data: URI (the source text is the content)
    QString absolutePath;      // otherwise: the file
    qint64 fileSize = 0;
    qint64 fileModifiedMs = 0; // milliseconds since the epoch
};
[[nodiscard]] std::optional<ImageSourceInfo> inspectImageSource(std::string_view source,
                                                                const ImageResolveContext& context);

} // namespace qraw::render
