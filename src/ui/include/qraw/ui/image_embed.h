#pragma once
// Embedding an image in a style: `image=data:image/png,<base64>`. draw.io writes
// the data URI without ";base64" because ';' separates the style keys
// (render::resolveImage reads both forms). The payload is stored once in the
// style value, which the document shares from there on (core::SharedValue).
// Nothing is ever fetched: files and clipboard images only.

#include <QByteArray>
#include <QImage>
#include <QSize>
#include <QString>

#include <optional>
#include <string>

namespace qraw::ui {

struct EmbeddedImage {
    std::string value; // data:image/<type>,<base64>
    QSize size;        // the image's own size in pixels (the SVG's default size), invalid if unknown
    qint64 bytes = 0;  // the encoded image
};

// The largest image that is embedded: the limit the renderer reads
// (render::ImageResolveContext::maxBytes), so that an embedded image always draws.
[[nodiscard]] qint64 maxEmbeddedImageBytes();

// Embeds PNG, JPEG, GIF, BMP, WebP or SVG bytes. nullopt with a translated
// reason in `error` when the bytes are no image, or are larger than
// maxEmbeddedImageBytes().
[[nodiscard]] std::optional<EmbeddedImage> embedImageBytes(const QByteArray& bytes, QString* error = nullptr);
[[nodiscard]] std::optional<EmbeddedImage> embedImageFile(const QString& path, QString* error = nullptr);
// A clipboard or drag image, encoded as PNG.
[[nodiscard]] std::optional<EmbeddedImage> embedQImage(const QImage& image, QString* error = nullptr);

// The size of the image a style `image` value refers to (an embedded data URI or
// a local file; never a remote one), read without decoding the pixels when the
// format allows. Invalid when unknown.
[[nodiscard]] QSize naturalImageSize(const std::string& imageValue, const QString& baseDirectory);

} // namespace qraw::ui
