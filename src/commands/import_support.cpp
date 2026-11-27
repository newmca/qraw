// Text helpers, label sizes and a JSON reader shared by the importers.
#include "import_support.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string>

namespace qraw::commands::imports {

std::string describe(const Diagnostic& diagnostic)
{
    std::string text;
    if (diagnostic.line > 0) {
        text = "Line " + std::to_string(diagnostic.line);
        if (diagnostic.column > 0)
            text += ", column " + std::to_string(diagnostic.column);
        text += ": ";
    }
    return text + diagnostic.message;
}

} // namespace qraw::commands::imports

namespace qraw::commands::imports::detail {

// --- text ----------------------------------------------------------------------------

namespace {

bool isSpace(char c)
{
    return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\f' || c == '\v';
}

} // namespace

std::string_view trim(std::string_view text)
{
    while (!text.empty() && isSpace(text.front()))
        text.remove_prefix(1);
    while (!text.empty() && isSpace(text.back()))
        text.remove_suffix(1);
    return text;
}

std::vector<std::string_view> splitLines(std::string_view text)
{
    std::vector<std::string_view> lines;
    std::size_t start = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        if (text[i] != '\n' && text[i] != '\r')
            continue;
        lines.push_back(text.substr(start, i - start));
        if (text[i] == '\r' && i + 1 < text.size() && text[i + 1] == '\n')
            ++i;
        start = i + 1;
    }
    lines.push_back(text.substr(start));
    return lines;
}

std::string escapeHtml(std::string_view text)
{
    std::string out;
    out.reserve(text.size());
    for (const char c : text) {
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"': out += "&quot;"; break;
        default: out += c; break;
        }
    }
    return out;
}

namespace {

void appendUtf8(std::string& out, unsigned long code)
{
    if (code < 0x80) {
        out += static_cast<char>(code);
    } else if (code < 0x800) {
        out += static_cast<char>(0xC0 | (code >> 6));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else if (code < 0x10000) {
        out += static_cast<char>(0xE0 | (code >> 12));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (code >> 18));
        out += static_cast<char>(0x80 | ((code >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((code >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (code & 0x3F));
    }
}

} // namespace

std::string plainText(std::string_view html)
{
    std::string out;
    for (std::size_t i = 0; i < html.size(); ++i) {
        const char c = html[i];
        if (c == '<') {
            const std::size_t close = html.find('>', i);
            if (close == std::string_view::npos) {
                out += c;
                continue;
            }
            const std::string tag = lower(trim(html.substr(i + 1, close - i - 1)));
            if (tag == "br" || tag == "br/" || tag == "br /" || startsWith(tag, "/div") || startsWith(tag, "/p"))
                out += '\n';
            i = close;
        } else if (c == '&') {
            const std::size_t semi = html.find(';', i);
            if (semi == std::string_view::npos || semi - i > 10) {
                out += c;
                continue;
            }
            const std::string_view name = html.substr(i + 1, semi - i - 1);
            if (name == "lt") {
                out += '<';
            } else if (name == "gt") {
                out += '>';
            } else if (name == "amp") {
                out += '&';
            } else if (name == "quot") {
                out += '"';
            } else if (name == "nbsp") {
                out += ' ';
            } else if (name.size() > 1 && name[0] == '#' && std::isdigit(static_cast<unsigned char>(name[1]))) {
                appendUtf8(out, std::strtoul(std::string(name.substr(1)).c_str(), nullptr, 10));
            } else {
                out += c;
                continue;
            }
            i = semi;
        } else {
            out += c;
        }
    }
    return out;
}

std::string lower(std::string_view text)
{
    std::string out(text);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

bool startsWith(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && text.compare(0, prefix.size(), prefix) == 0;
}

bool startsWithNoCase(std::string_view text, std::string_view prefix)
{
    return text.size() >= prefix.size() && lower(text.substr(0, prefix.size())) == lower(prefix);
}

std::size_t characterCount(std::string_view text)
{
    std::size_t count = 0;
    for (const char c : text) {
        if ((static_cast<unsigned char>(c) & 0xC0) != 0x80)
            ++count;
    }
    return count;
}

// --- sizes ---------------------------------------------------------------------------

core::Point measureLabel(const Options& options, std::string_view style, std::string_view label)
{
    if (options.measure)
        return options.measure(style, label);
    const bool html = style.find("html=1") != std::string_view::npos;
    const std::string text = html ? plainText(label) : std::string(label);
    if (text.empty())
        return {40, 40}; // Graph.getPreferredSizeForCell of an empty label: 4 grid steps square
    std::size_t widest = 0;
    std::size_t lines = 0;
    for (const std::string_view line : splitLines(text)) {
        widest = std::max(widest, characterCount(line));
        ++lines;
    }
    return {static_cast<double>(widest) * 7 + 14, static_cast<double>(lines) * 18 + 8};
}

// --- JSON ----------------------------------------------------------------------------

const Json* Json::find(std::string_view key) const
{
    if (type != Type::Object)
        return nullptr;
    for (const auto& [name, value] : object) {
        if (name == key)
            return &value;
    }
    return nullptr;
}

std::string Json::text() const
{
    switch (type) {
    case Type::String: return string;
    case Type::Bool: return boolean ? "true" : "false";
    case Type::Number: {
        // Whole numbers without a fraction, as JSON.stringify and String(number) write them.
        if (number == static_cast<double>(static_cast<long long>(number)))
            return std::to_string(static_cast<long long>(number));
        std::string text = std::to_string(number);
        while (!text.empty() && text.back() == '0')
            text.pop_back();
        return text;
    }
    default: return {};
    }
}

namespace {

class JsonReader {
public:
    JsonReader(std::string_view text, std::string& error) : text_(text), error_(error) {}

    bool parse(Json& out)
    {
        if (!value(out, 0))
            return false;
        skipSpace();
        if (pos_ != text_.size())
            return fail("unexpected text after the value");
        return true;
    }

private:
    static constexpr int kMaxDepth = 64;

    bool fail(const std::string& message)
    {
        if (error_.empty())
            error_ = message + " (at character " + std::to_string(pos_ + 1) + ")";
        return false;
    }

    void skipSpace()
    {
        while (pos_ < text_.size() && isSpace(text_[pos_]))
            ++pos_;
    }

    bool literal(std::string_view word)
    {
        if (text_.compare(pos_, word.size(), word) != 0)
            return false;
        pos_ += word.size();
        return true;
    }

    bool value(Json& out, int depth)
    {
        if (depth > kMaxDepth)
            return fail("nested too deeply");
        skipSpace();
        if (pos_ >= text_.size())
            return fail("unexpected end of the text");
        const char c = text_[pos_];
        if (c == '{')
            return object(out, depth);
        if (c == '[')
            return array(out, depth);
        if (c == '"') {
            out.type = Json::Type::String;
            return string(out.string);
        }
        if (literal("true")) {
            out.type = Json::Type::Bool;
            out.boolean = true;
            return true;
        }
        if (literal("false")) {
            out.type = Json::Type::Bool;
            return true;
        }
        if (literal("null")) {
            out.type = Json::Type::Null;
            return true;
        }
        if (c == '-' || std::isdigit(static_cast<unsigned char>(c)))
            return number(out);
        return fail(std::string("unexpected '") + c + "'");
    }

    bool number(Json& out)
    {
        const std::size_t start = pos_;
        if (text_[pos_] == '-')
            ++pos_;
        while (pos_ < text_.size() && (std::isdigit(static_cast<unsigned char>(text_[pos_])) || text_[pos_] == '.' ||
                                       text_[pos_] == 'e' || text_[pos_] == 'E' || text_[pos_] == '+' || text_[pos_] == '-'))
            ++pos_;
        const std::string token(text_.substr(start, pos_ - start));
        char* end = nullptr;
        const double parsed = std::strtod(token.c_str(), &end);
        if (token == "-" || end != token.c_str() + token.size()) {
            pos_ = start;
            return fail("invalid number");
        }
        out.type = Json::Type::Number;
        out.number = parsed;
        return true;
    }

    bool hex4(unsigned long& code)
    {
        if (pos_ + 4 > text_.size())
            return fail("incomplete \\u escape");
        code = 0;
        for (int i = 0; i < 4; ++i) {
            const char h = text_[pos_ + static_cast<std::size_t>(i)];
            if (!std::isxdigit(static_cast<unsigned char>(h)))
                return fail("invalid \\u escape");
            code = code * 16 + static_cast<unsigned long>(std::isdigit(static_cast<unsigned char>(h)) ? h - '0' : (std::tolower(h) - 'a' + 10));
        }
        pos_ += 4;
        return true;
    }

    bool string(std::string& out)
    {
        ++pos_; // the opening quote
        out.clear();
        while (pos_ < text_.size()) {
            const char c = text_[pos_++];
            if (c == '"')
                return true;
            if (c != '\\') {
                out += c;
                continue;
            }
            if (pos_ >= text_.size())
                break;
            const char escape = text_[pos_++];
            switch (escape) {
            case '"': out += '"'; break;
            case '\\': out += '\\'; break;
            case '/': out += '/'; break;
            case 'b': out += '\b'; break;
            case 'f': out += '\f'; break;
            case 'n': out += '\n'; break;
            case 'r': out += '\r'; break;
            case 't': out += '\t'; break;
            case 'u': {
                unsigned long code = 0;
                if (!hex4(code))
                    return false;
                if (code >= 0xD800 && code < 0xDC00 && text_.compare(pos_, 2, "\\u") == 0) {
                    const std::size_t save = pos_;
                    pos_ += 2;
                    unsigned long low = 0;
                    if (hex4(low) && low >= 0xDC00 && low < 0xE000)
                        code = 0x10000 + ((code - 0xD800) << 10) + (low - 0xDC00);
                    else
                        pos_ = save;
                }
                appendUtf8(out, code);
                break;
            }
            default: return fail(std::string("invalid escape \\") + escape);
            }
        }
        return fail("the string is not closed");
    }

    bool array(Json& out, int depth)
    {
        out.type = Json::Type::Array;
        ++pos_;
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == ']') {
            ++pos_;
            return true;
        }
        for (;;) {
            out.array.emplace_back();
            if (!value(out.array.back(), depth + 1))
                return false;
            skipSpace();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == ']') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or ']'");
        }
    }

    bool object(Json& out, int depth)
    {
        out.type = Json::Type::Object;
        ++pos_;
        skipSpace();
        if (pos_ < text_.size() && text_[pos_] == '}') {
            ++pos_;
            return true;
        }
        for (;;) {
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != '"')
                return fail("expected a name in double quotes");
            std::string key;
            if (!string(key))
                return false;
            skipSpace();
            if (pos_ >= text_.size() || text_[pos_] != ':')
                return fail("expected ':'");
            ++pos_;
            Json member;
            if (!value(member, depth + 1))
                return false;
            out.object.emplace_back(std::move(key), std::move(member));
            skipSpace();
            if (pos_ < text_.size() && text_[pos_] == ',') {
                ++pos_;
                continue;
            }
            if (pos_ < text_.size() && text_[pos_] == '}') {
                ++pos_;
                return true;
            }
            return fail("expected ',' or '}'");
        }
    }

    std::string_view text_;
    std::string& error_;
    std::size_t pos_ = 0;
};

} // namespace

bool parseJson(std::string_view text, Json& out, std::string& error)
{
    error.clear();
    out = Json{};
    return JsonReader(text, error).parse(out);
}

} // namespace qraw::commands::imports::detail
