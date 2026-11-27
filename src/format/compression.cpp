#include "qraw/format/compression.h"

#include <QByteArray>

#include <zlib.h>

#include <algorithm>
#include <array>

namespace qraw::format {

namespace {

bool isUnreserved(unsigned char c)
{
    // encodeURIComponent leaves A-Z a-z 0-9 - _ . ! ~ * ' ( ) unescaped.
    return (c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '-' || c == '_'
        || c == '.' || c == '!' || c == '~' || c == '*' || c == '\'' || c == '(' || c == ')';
}

int hexValue(char c)
{
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    return -1;
}

bool startsWithMarkup(std::string_view s)
{
    const auto first = s.find_first_not_of(" \t\r\n");
    return first != std::string_view::npos && s[first] == '<';
}

} // namespace

std::string encodeUriComponent(std::string_view text)
{
    static constexpr char kHex[] = "0123456789ABCDEF";
    std::string out;
    out.reserve(text.size() * 3 / 2);
    for (const char ch : text) {
        const auto c = static_cast<unsigned char>(ch);
        if (isUnreserved(c)) {
            out += ch;
        } else {
            out += '%';
            out += kHex[c >> 4];
            out += kHex[c & 0xF];
        }
    }
    return out;
}

std::string decodeUriComponent(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] == '%' && i + 2 < text.size()) {
            const int hi = hexValue(text[i + 1]);
            const int lo = hexValue(text[i + 2]);
            if (hi >= 0 && lo >= 0) {
                out += static_cast<char>((hi << 4) | lo);
                i += 2;
                continue;
            }
        }
        out += text[i];
    }
    return out;
}

std::string deflateBytes(std::string_view data, int windowBits)
{
    z_stream zs{};
    if (deflateInit2(&zs, Z_DEFAULT_COMPRESSION, Z_DEFLATED, windowBits, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return {};
    std::string out;
    out.resize(deflateBound(&zs, static_cast<uLong>(data.size())));
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());
    zs.next_out = reinterpret_cast<Bytef*>(out.data());
    zs.avail_out = static_cast<uInt>(out.size());
    const int rc = deflate(&zs, Z_FINISH);
    out.resize(zs.total_out);
    deflateEnd(&zs);
    return rc == Z_STREAM_END ? out : std::string();
}

std::optional<std::string> inflateBytes(std::string_view data, int windowBits)
{
    z_stream zs{};
    if (inflateInit2(&zs, windowBits) != Z_OK)
        return std::nullopt;
    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(data.data()));
    zs.avail_in = static_cast<uInt>(data.size());

    std::string out;
    std::array<char, 64 * 1024> chunk{};
    int rc = Z_OK;
    while (rc == Z_OK) {
        zs.next_out = reinterpret_cast<Bytef*>(chunk.data());
        zs.avail_out = static_cast<uInt>(chunk.size());
        rc = inflate(&zs, Z_NO_FLUSH);
        out.append(chunk.data(), chunk.size() - zs.avail_out);
        if (rc == Z_BUF_ERROR && zs.avail_in == 0)
            break; // truncated input
    }
    inflateEnd(&zs);
    if (rc != Z_STREAM_END)
        return std::nullopt;
    return out;
}

std::string compressDiagram(std::string_view xml)
{
    const std::string deflated = deflateBytes(encodeUriComponent(xml), -MAX_WBITS);
    return QByteArray::fromRawData(deflated.data(), static_cast<qsizetype>(deflated.size())).toBase64().toStdString();
}

std::optional<std::string> decompressDiagram(std::string_view base64)
{
    QByteArray compact;
    compact.reserve(static_cast<qsizetype>(base64.size()));
    for (const char c : base64) {
        if (c != ' ' && c != '\t' && c != '\r' && c != '\n')
            compact += c;
    }
    if (compact.isEmpty())
        return std::nullopt;
    const auto decoded = QByteArray::fromBase64Encoding(compact, QByteArray::AbortOnBase64DecodingErrors);
    if (!decoded)
        return std::nullopt;
    const std::string_view bytes(decoded.decoded.constData(), static_cast<std::size_t>(decoded.decoded.size()));

    auto inflated = inflateBytes(bytes, -MAX_WBITS);
    if (!inflated)
        inflated = inflateBytes(bytes, MAX_WBITS + 32); // zlib or gzip header
    if (!inflated)
        return std::nullopt;
    if (startsWithMarkup(*inflated))
        return inflated; // legacy payload without URI encoding
    std::string xml = decodeUriComponent(*inflated);
    if (!startsWithMarkup(xml))
        return std::nullopt;
    return xml;
}

} // namespace qraw::format
