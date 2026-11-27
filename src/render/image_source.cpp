// Resolves the `image` style of image/label shapes into bytes: data: URIs
// and local files only, nothing is fetched from the network.
//
// Modeled on how draw.io resolves image URLs (mxSvgCanvas2D.image,
// Graph.js data URI handling), modified: remote and bundled `img/lib` paths
// are never resolved.

#include "qraw/render/image_source.h"

#include "qraw/render/perf_scope.h"
#include "qraw/render/stats.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QUrl>

#include <algorithm>
#include <array>
#include <cstdint>

namespace qraw::render {

namespace {

constexpr QByteArrayView kPng("\x89PNG", 4);

QString normalizeMime(QString mime)
{
    mime = mime.trimmed().toLower();
    if (mime == QLatin1String("image/jpg"))
        return QStringLiteral("image/jpeg");
    if (mime == QLatin1String("image/svg"))
        return QStringLiteral("image/svg+xml");
    return mime;
}

// MIME type from the file extension; empty if unknown.
QString mimeFromExtension(const QString& path)
{
    const QString suffix = QFileInfo(path).suffix().toLower();
    if (suffix == QLatin1String("png"))
        return QStringLiteral("image/png");
    if (suffix == QLatin1String("jpg") || suffix == QLatin1String("jpeg"))
        return QStringLiteral("image/jpeg");
    if (suffix == QLatin1String("gif"))
        return QStringLiteral("image/gif");
    if (suffix == QLatin1String("svg"))
        return QStringLiteral("image/svg+xml");
    if (suffix == QLatin1String("webp"))
        return QStringLiteral("image/webp");
    if (suffix == QLatin1String("bmp"))
        return QStringLiteral("image/bmp");
    return {};
}

// MIME type from the magic bytes; empty if unknown.
QString sniffMime(const QByteArray& bytes)
{
    const QByteArrayView view(bytes);
    if (view.startsWith(kPng))
        return QStringLiteral("image/png");
    if (view.startsWith(QByteArrayView("\xFF\xD8", 2)))
        return QStringLiteral("image/jpeg");
    if (view.startsWith(QByteArrayView("GIF8", 4)))
        return QStringLiteral("image/gif");
    if (view.startsWith(QByteArrayView("BM", 2)))
        return QStringLiteral("image/bmp");
    if (view.size() >= 12 && view.startsWith(QByteArrayView("RIFF", 4)) && view.mid(8, 4) == QByteArrayView("WEBP", 4))
        return QStringLiteral("image/webp");

    // SVG: optional BOM and whitespace, then <svg or an XML declaration.
    QByteArrayView text = view.first(std::min<qsizetype>(view.size(), 512));
    if (text.startsWith(QByteArrayView("\xEF\xBB\xBF", 3)))
        text = text.sliced(3);
    while (!text.isEmpty() && (text.front() == ' ' || text.front() == '\t' || text.front() == '\n' || text.front() == '\r'))
        text = text.sliced(1);
    if (text.startsWith(QByteArrayView("<svg", 4)) || text.startsWith(QByteArrayView("<?xml", 5)))
        return QStringLiteral("image/svg+xml");
    return {};
}

bool isBase64Alphabet(QByteArrayView payload)
{
    if (payload.size() < 4)
        return false;
    return std::all_of(payload.begin(), payload.end(), [](char ch) {
        return (ch >= 'A' && ch <= 'Z') || (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '+' ||
               ch == '/' || ch == '=' || ch == '-' || ch == '_' || ch == '\n' || ch == '\r';
    });
}

bool startsWithMarkup(QByteArrayView payload)
{
    return payload.startsWith('<') || payload.startsWith("%3C") || payload.startsWith("%3c");
}

// Standard padded base64 at memory speed (QByteArray::fromBase64 takes about
// 6 ms for the 1.2 MB of a typical embedded PNG, a cold first paint of 32
// images pays it 32 times). Only input that is valid in the standard
// alphabet, a multiple of four characters long with '=' padding at the end,
// is decoded here; anything else (white space, base64url, missing padding,
// garbage) returns nullopt and goes through Qt's lenient decoder.
struct Base64Table {
    std::array<std::int8_t, 256> value{};
    constexpr Base64Table()
    {
        for (auto& v : value)
            v = -1;
        constexpr std::string_view alphabet = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        for (std::size_t i = 0; i < alphabet.size(); ++i)
            value[static_cast<unsigned char>(alphabet[i])] = static_cast<std::int8_t>(i);
    }
};

std::optional<QByteArray> decodeBase64Strict(QByteArrayView in)
{
    static constexpr Base64Table table;
    const auto n = static_cast<std::size_t>(in.size());
    if (n == 0 || n % 4 != 0)
        return std::nullopt;
    const auto* src = reinterpret_cast<const unsigned char*>(in.data());
    std::size_t padding = 0;
    if (src[n - 1] == '=')
        padding = src[n - 2] == '=' ? 2 : 1;
    const std::size_t outSize = n / 4 * 3 - padding;
    QByteArray out(static_cast<qsizetype>(outSize), Qt::Uninitialized);
    auto* dst = reinterpret_cast<unsigned char*>(out.data());

    const std::size_t quads = n / 4;
    std::size_t o = 0;
    for (std::size_t q = 0; q + 1 < quads; ++q, o += 3) {
        const int a = table.value[src[q * 4]];
        const int b = table.value[src[q * 4 + 1]];
        const int c = table.value[src[q * 4 + 2]];
        const int d = table.value[src[q * 4 + 3]];
        if ((a | b | c | d) < 0)
            return std::nullopt;
        const std::uint32_t bits = (static_cast<std::uint32_t>(a) << 18) | (static_cast<std::uint32_t>(b) << 12) |
                                   (static_cast<std::uint32_t>(c) << 6) | static_cast<std::uint32_t>(d);
        dst[o] = static_cast<unsigned char>(bits >> 16);
        dst[o + 1] = static_cast<unsigned char>(bits >> 8);
        dst[o + 2] = static_cast<unsigned char>(bits);
    }
    // The last quad, with its padding.
    const unsigned char* last = src + (quads - 1) * 4;
    const int a = table.value[last[0]];
    const int b = table.value[last[1]];
    const int c = padding == 2 ? 0 : table.value[last[2]];
    const int d = padding >= 1 ? 0 : table.value[last[3]];
    if ((a | b | c | d) < 0)
        return std::nullopt;
    const std::uint32_t bits = (static_cast<std::uint32_t>(a) << 18) | (static_cast<std::uint32_t>(b) << 12) |
                               (static_cast<std::uint32_t>(c) << 6) | static_cast<std::uint32_t>(d);
    dst[o++] = static_cast<unsigned char>(bits >> 16);
    if (padding < 2)
        dst[o++] = static_cast<unsigned char>(bits >> 8);
    if (padding < 1)
        dst[o++] = static_cast<unsigned char>(bits);
    return out;
}

std::optional<ImageData> finish(QByteArray bytes, QString mime, const ImageResolveContext& context)
{
    if (bytes.isEmpty() || bytes.size() > context.maxBytes)
        return std::nullopt;
    mime = normalizeMime(std::move(mime));
    if (!mime.startsWith(QLatin1String("image/"))) {
        mime = sniffMime(bytes);
        if (mime.isEmpty())
            return std::nullopt;
    }
    return ImageData{std::move(bytes), std::move(mime), {}};
}

// data:<mime>[;params][;base64],<payload>
std::optional<ImageData> resolveDataUri(QByteArrayView uri, const ImageResolveContext& context)
{
    const qsizetype comma = uri.indexOf(',');
    if (comma < 0)
        return std::nullopt;
    const QList<QByteArray> header = QByteArray(uri.sliced(5, comma - 5)).split(';');
    const QString mime = QString::fromLatin1(header.value(0));
    bool base64 = false;
    for (qsizetype i = 1; i < header.size(); ++i)
        base64 = base64 || header[i].trimmed().toLower() == "base64";
    const QByteArrayView payload = uri.sliced(comma + 1).trimmed();
    stats::add(stats::Counter::imagesResolved);
    stats::add(stats::Counter::bytesDecoded, static_cast<std::uint64_t>(payload.size()));

    // draw.io writes data URIs into styles, where ';' separates the keys, so
    // it drops ";base64": `data:image/png,iVBOR...` and
    // `data:image/svg+xml,PHN2Zy...`. A payload without markup that only
    // uses the base64 alphabet is therefore base64, for every image type.
    // A payload the strict decoder accepts is base64 by definition (and the
    // alphabet check is then not needed: the decode did it).
    std::optional<QByteArray> fast;
    const bool guess = !base64 && mime.startsWith(QLatin1String("image/")) && !startsWithMarkup(payload);
    if (base64 || guess)
        fast = decodeBase64Strict(payload);
    if (guess && (fast || isBase64Alphabet(payload)))
        base64 = true;

    QByteArray bytes;
    if (base64) {
        if (fast) {
            bytes = std::move(*fast);
        } else {
            // Wrapped lines (MIME style) and the like: white space is not part of the data.
            QByteArray compact(payload);
            compact.removeIf([](char c) { return c == '\n' || c == '\r' || c == ' '; });
            if (std::optional<QByteArray> again = decodeBase64Strict(compact)) {
                bytes = std::move(*again);
            } else if (const auto decoded = QByteArray::fromBase64Encoding(
                           compact, QByteArray::Base64Option::AbortOnBase64DecodingErrors |
                                        QByteArray::Base64Option::Base64Encoding)) {
                bytes = *decoded;
            } else {
                // Base64url, or sloppy padding: be lenient like browsers are.
                bytes = QByteArray::fromBase64(compact, QByteArray::Base64UrlEncoding |
                                                            QByteArray::IgnoreBase64DecodingErrors);
            }
        }
    } else {
        bytes = QByteArray::fromPercentEncoding(QByteArray(payload));
    }
    return finish(std::move(bytes), mime, context);
}

// "scheme:" with at least two characters (a single letter is a drive letter).
bool hasScheme(const QString& source)
{
    const qsizetype colon = source.indexOf(u':');
    if (colon < 2)
        return false;
    for (qsizetype i = 0; i < colon; ++i) {
        const QChar ch = source[i];
        const bool ok = ch.isLetter() || (i > 0 && (ch.isDigit() || ch == u'+' || ch == u'-' || ch == u'.'));
        if (!ok)
            return false;
    }
    return true;
}

// The file `pathText` names (relative to the context's directory), if it may
// be read and is a plausible image file.
std::optional<QFileInfo> localFileInfo(const QString& pathText, const ImageResolveContext& context)
{
    if (!context.allowLocalFiles || pathText.isEmpty())
        return std::nullopt;
    QFileInfo info(pathText);
    if (info.isRelative()) {
        if (context.baseDirectory.isEmpty())
            return std::nullopt;
        info = QFileInfo(QDir(context.baseDirectory), pathText);
    }
    if (!info.isFile() || info.size() <= 0 || info.size() > context.maxBytes)
        return std::nullopt;
    return info;
}

std::optional<ImageData> resolveLocalFile(const QString& pathText, const ImageResolveContext& context)
{
    const std::optional<QFileInfo> found = localFileInfo(pathText, context);
    if (!found)
        return std::nullopt;
    const QFileInfo& info = *found;

    QFile file(info.absoluteFilePath());
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    // Read one byte more than allowed so that a file that grew since the
    // size check is still rejected.
    QByteArray bytes = file.read(context.maxBytes + 1);
    stats::add(stats::Counter::imagesResolved);
    stats::add(stats::Counter::bytesDecoded, static_cast<std::uint64_t>(bytes.size()));
    QString mime = mimeFromExtension(info.fileName());
    if (mime.isEmpty())
        mime = sniffMime(bytes);
    return finish(std::move(bytes), std::move(mime), context);
}

bool startsWithNoCase(std::string_view text, std::string_view prefix)
{
    if (text.size() < prefix.size())
        return false;
    for (std::size_t i = 0; i < prefix.size(); ++i) {
        const char c = text[i];
        if ((c >= 'A' && c <= 'Z' ? c - 'A' + 'a' : c) != prefix[i])
            return false;
    }
    return true;
}

} // namespace

std::optional<ImageSourceInfo> inspectImageSource(std::string_view source, const ImageResolveContext& context)
{
    while (!source.empty() && (source.front() == ' ' || source.front() == '\t' || source.front() == '\n' || source.front() == '\r'))
        source.remove_prefix(1);
    if (source.empty())
        return std::nullopt;

    ImageSourceInfo info;
    if (startsWithNoCase(source, "data:")) {
        info.isData = true;
        return info;
    }
    const QString text = QString::fromUtf8(source.data(), static_cast<qsizetype>(source.size())).trimmed();
    QString path;
    if (text.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
        path = QUrl(text).toLocalFile();
    else if (!hasScheme(text))
        path = text;
    const std::optional<QFileInfo> file = localFileInfo(path, context);
    if (!file)
        return std::nullopt;
    info.absolutePath = file->absoluteFilePath();
    info.fileSize = file->size();
    info.fileModifiedMs = file->lastModified().toMSecsSinceEpoch();
    return info;
}

std::optional<ImageData> resolveImage(std::string_view source, const ImageResolveContext& context)
{
    QRAW_PERF_SCOPE(ImageResolve);
    // A data URI is decoded straight from the text (no QString of megabytes).
    std::string_view trimmed = source;
    while (!trimmed.empty() && (trimmed.front() == ' ' || trimmed.front() == '\t' || trimmed.front() == '\n' || trimmed.front() == '\r'))
        trimmed.remove_prefix(1);
    if (startsWithNoCase(trimmed, "data:"))
        return resolveDataUri(QByteArrayView(trimmed.data(), static_cast<qsizetype>(trimmed.size())), context);

    const QString text = QString::fromUtf8(source.data(), static_cast<qsizetype>(source.size())).trimmed();
    if (text.isEmpty())
        return std::nullopt;

    if (text.startsWith(QLatin1String("data:"), Qt::CaseInsensitive))
        return resolveDataUri(text.toUtf8(), context);

    if (text.startsWith(QLatin1String("file:"), Qt::CaseInsensitive))
        return resolveLocalFile(QUrl(text).toLocalFile(), context);

    // http(s), ftp, blob and every other scheme: never fetched.
    if (hasScheme(text))
        return std::nullopt;

    return resolveLocalFile(text, context);
}

} // namespace qraw::render
