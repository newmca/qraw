// Port of js/grapheditor/Editor.js extractGraphModelFromPng (draw.io, Apache-2.0), modified.
// Port of js/diagramly/Editor.js writeGraphModelToPng (draw.io, Apache-2.0), modified.

#include "qraw/format/embed.h"

#include "qraw/format/compression.h"

#include <zlib.h>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <utility>

namespace qraw::format {
namespace {

constexpr std::string_view kSignature{"\x89PNG\r\n\x1a\n", 8};
constexpr std::size_t kChunkOverhead = 12; // length + type + CRC

struct Chunk {
    std::string_view type;
    std::string_view data;
    std::size_t begin = 0; // offset of the length field
    std::size_t end = 0;   // offset just past the CRC
};

std::uint32_t readU32(std::string_view bytes, std::size_t pos)
{
    const auto at = [&](std::size_t i) { return static_cast<std::uint32_t>(static_cast<unsigned char>(bytes[pos + i])); };
    return (at(0) << 24) | (at(1) << 16) | (at(2) << 8) | at(3);
}

void appendU32(std::string& out, std::uint32_t value)
{
    out += static_cast<char>((value >> 24) & 0xFF);
    out += static_cast<char>((value >> 16) & 0xFF);
    out += static_cast<char>((value >> 8) & 0xFF);
    out += static_cast<char>(value & 0xFF);
}

bool hasSignature(std::string_view bytes) { return bytes.size() >= kSignature.size() && bytes.substr(0, kSignature.size()) == kSignature; }

// The chunk at pos, or nullopt when its length runs past the end of the data.
std::optional<Chunk> readChunk(std::string_view bytes, std::size_t pos)
{
    if (pos > bytes.size() || bytes.size() - pos < kChunkOverhead)
        return std::nullopt;
    const std::size_t length = readU32(bytes, pos);
    if (length > bytes.size() - pos - kChunkOverhead)
        return std::nullopt;
    Chunk chunk;
    chunk.begin = pos;
    chunk.type = bytes.substr(pos + 4, 4);
    chunk.data = bytes.substr(pos + 8, length);
    chunk.end = pos + kChunkOverhead + length;
    return chunk;
}

bool isDiagramKeyword(std::string_view keyword) { return keyword == "mxfile" || keyword == "mxGraphModel"; }

// A tEXt or zTXt chunk whose keyword names a diagram.
bool isDiagramTextChunk(const Chunk& chunk)
{
    if (chunk.type != "tEXt" && chunk.type != "zTXt")
        return false;
    const std::size_t nul = chunk.data.find('\0');
    return nul != std::string_view::npos && isDiagramKeyword(chunk.data.substr(0, nul));
}

// The text of a diagram tEXt or zTXt chunk, or nullopt when it is corrupt.
std::optional<std::string> decodeDiagramText(const Chunk& chunk)
{
    const std::string_view payload = chunk.data.substr(chunk.data.find('\0') + 1);
    if (chunk.type == "tEXt")
        return std::string(payload);

    // zTXt: one compression method byte, then the zlib stream. Upstream
    // writes zlib data; raw deflate is accepted as a fallback.
    if (payload.empty())
        return std::nullopt;
    const std::string_view compressed = payload.substr(1);
    std::optional<std::string> text = inflateBytes(compressed, 15);
    if (!text)
        text = inflateBytes(compressed, -15);
    if (!text)
        return std::nullopt;
    for (char& c : *text) {
        if (c == '+')
            c = ' ';
    }
    return text;
}

// Values starting with '%' are URI-encoded (up to twice, as upstream does).
std::optional<std::string> finishText(std::string text)
{
    if (!text.empty() && text.front() == '%')
        text = decodeUriComponent(text);
    if (!text.empty() && text.front() == '%')
        text = decodeUriComponent(text);
    if (text.empty())
        return std::nullopt;
    return text;
}

std::string makeChunk(std::string_view type, std::string_view data)
{
    std::string out;
    out.reserve(kChunkOverhead + data.size());
    appendU32(out, static_cast<std::uint32_t>(data.size()));
    out += type;
    out += data;

    uLong crc = crc32(0L, nullptr, 0);
    crc = crc32(crc, reinterpret_cast<const Bytef*>(type.data()), static_cast<uInt>(type.size()));
    crc = crc32(crc, reinterpret_cast<const Bytef*>(data.data()), static_cast<uInt>(data.size()));
    appendU32(out, static_cast<std::uint32_t>(crc));
    return out;
}

std::string diagramTextChunk(std::string_view xml)
{
    std::string data = "mxfile";
    data += '\0';
    data += encodeUriComponent(xml);
    return makeChunk("tEXt", data);
}

} // namespace

std::optional<std::string> extractFromPng(QByteArrayView png)
{
    const std::string_view bytes(png.data(), static_cast<std::size_t>(png.size()));
    if (!hasSignature(bytes))
        return std::nullopt;

    std::size_t pos = kSignature.size();
    while (const auto chunk = readChunk(bytes, pos)) {
        if (chunk->type == "IDAT" || chunk->type == "IEND")
            break;
        if (isDiagramTextChunk(*chunk)) {
            std::optional<std::string> text = decodeDiagramText(*chunk);
            return text ? finishText(std::move(*text)) : std::nullopt;
        }
        pos = chunk->end;
    }
    return std::nullopt;
}

std::optional<QByteArray> embedInPng(QByteArrayView png, std::string_view xml)
{
    const std::string_view bytes(png.data(), static_cast<std::size_t>(png.size()));
    if (!hasSignature(bytes))
        return std::nullopt;
    const auto first = readChunk(bytes, kSignature.size());
    if (!first || first->type != "IHDR")
        return std::nullopt;

    std::string out(kSignature);
    bool insertedDiagram = false;
    std::size_t pos = kSignature.size();
    while (const auto chunk = readChunk(bytes, pos)) {
        if (chunk->type == "IDAT" && !insertedDiagram) {
            out += diagramTextChunk(xml);
            insertedDiagram = true;
        }
        if (!isDiagramTextChunk(*chunk))
            out.append(bytes.substr(chunk->begin, chunk->end - chunk->begin));
        pos = chunk->end;
        if (chunk->type == "IEND")
            break;
    }
    if (!insertedDiagram)
        return std::nullopt;
    out.append(bytes.substr(pos));
    return QByteArray(out.data(), static_cast<qsizetype>(out.size()));
}

} // namespace qraw::format
