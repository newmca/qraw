#include "qraw/ui/image_embed.h"

#include "qraw/render/image_source.h"

#include <QBuffer>
#include <QCoreApplication>
#include <QFile>
#include <QImageReader>
#include <QLocale>
#include <QSvgRenderer>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::ImageEmbed)
};

struct Detected {
    QByteArray subtype; // the MIME subtype: "png", "svg+xml"
    QSize size;
};

// The format of `bytes`, from the bytes themselves: Qt's image readers for the
// bitmaps, the SVG renderer for vector images.
std::optional<Detected> detect(const QByteArray& bytes)
{
    QBuffer buffer;
    buffer.setData(bytes);
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer);
    reader.setAutoTransform(false);
    const QByteArray format = reader.format();
    if (!format.isEmpty() && format != "svg" && format != "svgz") {
        // The reader reads the header only: the size costs no decoding.
        const QSize size = reader.size();
        const QByteArray subtype = format == "jpg" ? QByteArray("jpeg") : format;
        return Detected{subtype, size};
    }
    QSvgRenderer svg(bytes);
    if (svg.isValid())
        return Detected{QByteArray("svg+xml"), svg.defaultSize()};
    return std::nullopt;
}

std::optional<EmbeddedImage> embed(const QByteArray& bytes, const Detected& detected)
{
    static const QByteArray prefix = "data:image/";
    const QByteArray base64 = bytes.toBase64();
    EmbeddedImage image;
    // One allocation: the style value is built in place, not through a copy of the base64 text.
    image.value.reserve(static_cast<std::size_t>(prefix.size() + detected.subtype.size() + 1 + base64.size()));
    image.value.append(prefix.constData(), static_cast<std::size_t>(prefix.size()));
    image.value.append(detected.subtype.constData(), static_cast<std::size_t>(detected.subtype.size()));
    image.value.push_back(',');
    image.value.append(base64.constData(), static_cast<std::size_t>(base64.size()));
    image.size = detected.size;
    image.bytes = bytes.size();
    return image;
}

std::optional<EmbeddedImage> fail(QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
    return std::nullopt;
}

QString tooLarge(qint64 size)
{
    return Text::tr("The image is %1, more than the %2 that can be embedded in a diagram.")
        .arg(QLocale().formattedDataSize(size), QLocale().formattedDataSize(maxEmbeddedImageBytes()));
}

} // namespace

qint64 maxEmbeddedImageBytes()
{
    return render::ImageResolveContext().maxBytes;
}

std::optional<EmbeddedImage> embedImageBytes(const QByteArray& bytes, QString* error)
{
    if (bytes.isEmpty())
        return fail(error, Text::tr("The image is empty."));
    if (bytes.size() > maxEmbeddedImageBytes()) {
        return fail(error, tooLarge(bytes.size()));
    }
    const std::optional<Detected> detected = detect(bytes);
    if (!detected)
        return fail(error, Text::tr("This is not an image that can be shown (PNG, JPEG, GIF, BMP, WebP or SVG)."));
    return embed(bytes, *detected);
}

std::optional<EmbeddedImage> embedImageFile(const QString& path, QString* error)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return fail(error, Text::tr("Cannot read %1: %2").arg(path, file.errorString()));
    // Checked before reading, so that a huge file is not loaded to be refused.
    if (file.size() > maxEmbeddedImageBytes())
        return fail(error, tooLarge(file.size()));
    return embedImageBytes(file.readAll(), error);
}

std::optional<EmbeddedImage> embedQImage(const QImage& image, QString* error)
{
    if (image.isNull())
        return fail(error, Text::tr("There is no image on the clipboard."));
    QByteArray png;
    QBuffer buffer(&png);
    buffer.open(QIODevice::WriteOnly);
    if (!image.save(&buffer, "PNG"))
        return fail(error, Text::tr("The image cannot be encoded."));
    return embedImageBytes(png, error);
}

QSize naturalImageSize(const std::string& imageValue, const QString& baseDirectory)
{
    render::ImageResolveContext context;
    context.baseDirectory = baseDirectory;
    const std::optional<render::ImageData> data = render::resolveImage(imageValue, context);
    if (!data || data->bytes.isEmpty())
        return {};
    const std::optional<Detected> detected = detect(data->bytes);
    return detected ? detected->size : QSize();
}

} // namespace qraw::ui
