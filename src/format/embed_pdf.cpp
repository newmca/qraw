// Port of js/diagramly/Editor.js extractGraphModelFromPdf (draw.io, Apache-2.0), modified.
// Port of js/diagramly/Editor.js writeGraphModelToPdf (draw.io, Apache-2.0), modified.

#include "qraw/format/embed.h"

#include "qraw/format/compression.h"
#include "embed_internal.h"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qraw::format {
namespace {

constexpr std::size_t kNpos = std::string_view::npos;

bool isPdfSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\0'; }
bool isDigit(char c) { return c >= '0' && c <= '9'; }

// Reads a PDF literal string whose '(' is just before `pos`, resolving
// escapes. Returns the value (balanced parentheses are kept) or nullopt when
// the string is not closed.
std::optional<std::string> readLiteral(std::string_view pdf, std::size_t pos)
{
    std::string out;
    int depth = 1;
    while (pos < pdf.size()) {
        const char c = pdf[pos++];
        if (c == '\\') {
            if (pos >= pdf.size())
                break;
            const char escaped = pdf[pos++];
            switch (escaped) {
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case '\r':
                if (pos < pdf.size() && pdf[pos] == '\n')
                    ++pos;
                break; // line continuation
            case '\n':
                break; // line continuation
            default:
                if (escaped >= '0' && escaped <= '7') {
                    int value = escaped - '0';
                    for (int digits = 0; digits < 2 && pos < pdf.size() && pdf[pos] >= '0' && pdf[pos] <= '7'; ++digits)
                        value = value * 8 + (pdf[pos++] - '0');
                    out += static_cast<char>(value & 0xFF);
                } else {
                    out += escaped; // \( \) \\ and unknown escapes
                }
            }
        } else if (c == '(') {
            ++depth;
            out += c;
        } else if (c == ')') {
            if (--depth == 0)
                return out;
            out += c;
        } else {
            out += c;
        }
    }
    return std::nullopt;
}

// The position after the literal string that starts at `pos` ('('), or npos.
std::size_t skipLiteral(std::string_view pdf, std::size_t pos)
{
    int depth = 0;
    for (std::size_t i = pos; i < pdf.size(); ++i) {
        if (pdf[i] == '\\') {
            ++i;
        } else if (pdf[i] == '(') {
            ++depth;
        } else if (pdf[i] == ')' && --depth == 0) {
            return i + 1;
        }
    }
    return kNpos;
}

// The `/Subject (...)` value of the last such entry, if it holds a diagram.
std::optional<std::string> subjectDiagram(std::string_view pdf)
{
    constexpr std::string_view key = "/Subject (";
    const std::size_t at = pdf.rfind(key);
    if (at == kNpos)
        return std::nullopt;
    std::optional<std::string> value = readLiteral(pdf, at + key.size());
    if (!value)
        return std::nullopt;
    if (!value->empty() && value->front() == '%')
        *value = decodeUriComponent(*value);
    if (!isDiagramXml(*value))
        return std::nullopt;
    return value;
}

// PDF 1.7 EmbeddedFile stream with /Subtype /application#2Fvnd.jgraph.mxfile.
std::optional<std::string> embeddedFileDiagram(std::string_view pdf)
{
    constexpr std::string_view key = "/Subtype /application#2Fvnd.jgraph.mxfile";
    const std::size_t at = pdf.find(key);
    if (at == kNpos)
        return std::nullopt;
    const std::size_t keyword = pdf.find("stream", at + key.size());
    if (keyword == kNpos)
        return std::nullopt;

    std::size_t start = keyword + 6;
    if (pdf.substr(start).starts_with("\r\n"))
        start += 2;
    else if (pdf.substr(start).starts_with("\n"))
        start += 1;
    std::size_t end = pdf.find("endstream", start);
    if (end == kNpos)
        end = pdf.size();
    const std::string_view body = pdf.substr(start, end - start);

    for (const int windowBits : {-15, 15}) {
        std::optional<std::string> inflated = inflateBytes(body, windowBits);
        if (inflated && isDiagramXml(*inflated))
            return inflated;
    }
    return std::nullopt;
}

// --- object syntax helpers for writing ------------------------------------

bool isBoundary(char c) { return isPdfSpace(c) || c == '/' || c == '<' || c == '>' || c == '[' || c == ']' || c == '(' || c == ')'; }

// An unsigned integer after optional white space, with its end position.
std::optional<std::pair<std::uint64_t, std::size_t>> parseUInt(std::string_view pdf, std::size_t pos)
{
    while (pos < pdf.size() && isPdfSpace(pdf[pos]))
        ++pos;
    std::size_t end = pos;
    std::uint64_t value = 0;
    while (end < pdf.size() && isDigit(pdf[end]) && end - pos < 18) {
        value = value * 10 + static_cast<std::uint64_t>(pdf[end] - '0');
        ++end;
    }
    if (end == pos)
        return std::nullopt;
    return std::pair{value, end};
}

struct ObjectRef {
    std::uint64_t number = 0;
    std::uint64_t generation = 0;
};

// "num gen R" at pos.
std::optional<ObjectRef> parseRef(std::string_view pdf, std::size_t pos)
{
    const auto number = parseUInt(pdf, pos);
    if (!number)
        return std::nullopt;
    const auto generation = parseUInt(pdf, number->second);
    if (!generation)
        return std::nullopt;
    std::size_t end = generation->second;
    while (end < pdf.size() && isPdfSpace(pdf[end]))
        ++end;
    if (end >= pdf.size() || pdf[end] != 'R')
        return std::nullopt;
    return ObjectRef{number->first, generation->first};
}

// The value right after `key` (e.g. "/Size") inside a dictionary.
std::optional<std::size_t> valueAfter(std::string_view dict, std::string_view key)
{
    const std::size_t at = dict.find(key);
    if (at == kNpos)
        return std::nullopt;
    return at + key.size();
}

// Position of the ">>" that closes the "<<" at `open`, or npos.
std::size_t closeDictionary(std::string_view pdf, std::size_t open)
{
    int depth = 0;
    for (std::size_t i = open; i + 1 < pdf.size(); ++i) {
        if (pdf[i] == '(') {
            i = skipLiteral(pdf, i);
            if (i == kNpos)
                return kNpos;
            --i;
        } else if (pdf[i] == '<' && pdf[i + 1] == '<') {
            ++depth;
            ++i;
        } else if (pdf[i] == '>' && pdf[i + 1] == '>') {
            if (--depth == 0)
                return i;
            ++i;
        }
    }
    return kNpos;
}

// Splits the body of a dictionary (between its outer << and >>) into
// top-level entries, each "/Name value" as written.
std::vector<std::string_view> splitEntries(std::string_view body)
{
    std::vector<std::string_view> entries;
    std::size_t start = kNpos;
    int depth = 0;
    for (std::size_t i = 0; i < body.size(); ++i) {
        const char c = body[i];
        if (c == '(') {
            i = skipLiteral(body, i);
            if (i == kNpos)
                break;
            --i;
        } else if (c == '<' && i + 1 < body.size() && body[i + 1] == '<') {
            ++depth;
            ++i;
        } else if (c == '>' && i + 1 < body.size() && body[i + 1] == '>') {
            --depth;
            ++i;
        } else if (c == '<') {
            const std::size_t close = body.find('>', i);
            if (close == kNpos)
                break;
            i = close;
        } else if (c == '[') {
            ++depth;
        } else if (c == ']') {
            --depth;
        } else if (c == '/' && depth == 0) {
            if (start != kNpos)
                entries.push_back(body.substr(start, i - start));
            start = i;
        }
    }
    if (start != kNpos)
        entries.push_back(body.substr(start));
    for (std::string_view& entry : entries) {
        while (!entry.empty() && isPdfSpace(entry.back()))
            entry.remove_suffix(1);
    }
    return entries;
}

std::string_view entryName(std::string_view entry)
{
    std::size_t end = 1;
    while (end < entry.size() && !isBoundary(entry[end]))
        ++end;
    return entry.substr(1, end - 1);
}

// Position of the "num gen obj" header of the last object with that number.
std::optional<std::size_t> findObject(std::string_view pdf, ObjectRef ref)
{
    const std::string header = std::to_string(ref.number) + " " + std::to_string(ref.generation) + " obj";
    std::optional<std::size_t> found;
    for (std::size_t at = pdf.find(header); at != kNpos; at = pdf.find(header, at + 1)) {
        if (at == 0 || !isDigit(pdf[at - 1]))
            found = at;
    }
    return found;
}

// The entries of the dictionary of an existing Info object, without /Subject.
std::optional<std::vector<std::string_view>> infoEntries(std::string_view pdf, ObjectRef info)
{
    const auto header = findObject(pdf, info);
    if (!header)
        return std::nullopt;
    const std::size_t open = pdf.find("<<", *header);
    if (open == kNpos)
        return std::nullopt;
    const std::size_t close = closeDictionary(pdf, open);
    if (close == kNpos)
        return std::nullopt;

    std::vector<std::string_view> kept;
    for (const std::string_view entry : splitEntries(pdf.substr(open + 2, close - open - 2))) {
        if (entryName(entry) != "Subject")
            kept.push_back(entry);
    }
    return kept;
}

std::string escapeLiteral(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        if (c == '\\' || c == '(' || c == ')')
            out += '\\';
        out += c;
    }
    return out;
}

std::string tenDigits(std::uint64_t value)
{
    std::string digits = std::to_string(value);
    return std::string(digits.size() < 10 ? 10 - digits.size() : 0, '0') + digits;
}

} // namespace

std::optional<std::string> extractFromPdf(QByteArrayView pdf)
{
    const std::string_view bytes(pdf.data(), static_cast<std::size_t>(pdf.size()));
    if (std::optional<std::string> xml = subjectDiagram(bytes))
        return xml;
    return embeddedFileDiagram(bytes);
}

std::optional<QByteArray> embedInPdf(QByteArrayView pdf, std::string_view xml)
{
    const std::string_view bytes(pdf.data(), static_cast<std::size_t>(pdf.size()));

    const std::size_t startxrefAt = bytes.rfind("startxref");
    if (startxrefAt == kNpos)
        return std::nullopt;
    const auto previousXref = parseUInt(bytes, startxrefAt + std::string_view("startxref").size());
    if (!previousXref)
        return std::nullopt;

    const std::size_t trailerAt = bytes.rfind("trailer");
    if (trailerAt == kNpos)
        return std::nullopt;
    const std::size_t open = bytes.find("<<", trailerAt);
    if (open == kNpos)
        return std::nullopt;
    const std::size_t close = closeDictionary(bytes, open);
    if (close == kNpos)
        return std::nullopt;
    const std::string_view trailer = bytes.substr(open, close + 2 - open);

    const auto sizeAt = valueAfter(trailer, "/Size");
    const auto rootAt = valueAfter(trailer, "/Root");
    if (!sizeAt || !rootAt)
        return std::nullopt;
    const auto size = parseUInt(trailer, *sizeAt);
    const auto root = parseRef(trailer, *rootAt);
    if (!size || !root)
        return std::nullopt;

    std::vector<std::string_view> entries;
    if (const auto infoAt = valueAfter(trailer, "/Info")) {
        const auto info = parseRef(trailer, *infoAt);
        if (!info)
            return std::nullopt;
        const auto kept = infoEntries(bytes, *info);
        if (!kept)
            return std::nullopt;
        entries = *kept;
    }

    const std::uint64_t infoNumber = size->first;
    std::string output(bytes);
    output += '\n';
    const std::size_t infoOffset = output.size();
    output += std::to_string(infoNumber) + " 0 obj\n<<";
    for (const std::string_view entry : entries) {
        output += ' ';
        output += entry;
    }
    output += " /Subject (" + escapeLiteral(encodeUriComponent(xml)) + ") >>\nendobj\n";

    const std::size_t xrefOffset = output.size();
    output += "xref\n0 1\n0000000000 65535 f \n";
    output += std::to_string(infoNumber) + " 1\n";
    output += tenDigits(infoOffset) + " 00000 n \n";

    output += "trailer\n<< /Size " + std::to_string(infoNumber + 1) + " /Root " + std::to_string(root->number) + " " +
              std::to_string(root->generation) + " R /Info " + std::to_string(infoNumber) +
              " 0 R /Prev " + std::to_string(previousXref->first) + " >>\n";
    output += "startxref\n" + std::to_string(xrefOffset) + "\n%%EOF\n";
    return QByteArray(output.data(), static_cast<qsizetype>(output.size()));
}

} // namespace qraw::format
