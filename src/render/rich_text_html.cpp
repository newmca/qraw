#include "rich_text_html.h"

#include <algorithm>
#include <array>
#include <cstdint>
#include <initializer_list>

namespace qraw::render::html {

namespace {

constexpr std::size_t kMaxDepth = 200; // open elements; a deeper element is dropped, its content stays in its parent

bool isSpace(char c) { return c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f'; }
bool isAlpha(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z'); }
char toLower(char c) { return (c >= 'A' && c <= 'Z') ? static_cast<char>(c - 'A' + 'a') : c; }

std::string lower(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        c = toLower(c);
    return out;
}

void appendUtf8(std::string& out, std::uint32_t cp)
{
    if (cp == 0 || cp > 0x10FFFF || (cp >= 0xD800 && cp <= 0xDFFF))
        cp = 0xFFFD;
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else if (cp < 0x10000) {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xF0 | (cp >> 18));
        out += static_cast<char>(0x80 | ((cp >> 12) & 0x3F));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

struct NamedEntity {
    std::string_view name;
    std::uint32_t codePoint;
};

constexpr std::array kEntities = std::to_array<NamedEntity>({
    {"amp", '&'},      {"lt", '<'},        {"gt", '>'},        {"quot", '"'},     {"apos", '\''},
    {"nbsp", 0xA0},    {"copy", 0xA9},     {"reg", 0xAE},      {"trade", 0x2122}, {"hellip", 0x2026},
    {"mdash", 0x2014}, {"ndash", 0x2013},  {"laquo", 0xAB},    {"raquo", 0xBB},   {"bull", 0x2022},
    {"middot", 0xB7},  {"deg", 0xB0},      {"euro", 0x20AC},   {"lsquo", 0x2018}, {"rsquo", 0x2019},
    {"ldquo", 0x201C}, {"rdquo", 0x201D},  {"plusmn", 0xB1},   {"times", 0xD7},   {"divide", 0xF7},
    {"para", 0xB6},    {"sect", 0xA7},     {"cent", 0xA2},     {"pound", 0xA3},   {"yen", 0xA5},
    {"frac12", 0xBD},  {"larr", 0x2190},   {"rarr", 0x2192},   {"uarr", 0x2191},  {"darr", 0x2193},
});

// Appends the entity whose text (between '&' and ';') is `body`. Returns
// false for unknown ones.
bool decodeEntity(std::string_view body, std::string& out)
{
    if (body.empty())
        return false;
    if (body[0] == '#') {
        const bool hex = body.size() > 1 && (body[1] == 'x' || body[1] == 'X');
        const std::string_view digits = body.substr(hex ? 2 : 1);
        if (digits.empty() || digits.size() > 8)
            return false;
        std::uint32_t cp = 0;
        for (const char c : digits) {
            int v = -1;
            if (c >= '0' && c <= '9')
                v = c - '0';
            else if (hex && c >= 'a' && c <= 'f')
                v = c - 'a' + 10;
            else if (hex && c >= 'A' && c <= 'F')
                v = c - 'A' + 10;
            if (v < 0)
                return false;
            cp = cp * (hex ? 16 : 10) + static_cast<std::uint32_t>(v);
        }
        appendUtf8(out, cp);
        return true;
    }
    for (const auto& e : kEntities) {
        if (e.name == body) {
            appendUtf8(out, e.codePoint);
            return true;
        }
    }
    return false;
}

// Removes what XML 1.0 cannot carry: control characters other than tab and newline, carriage returns, and
// U+FFFE / U+FFFF. The serializer drops them anyway; doing it here means every filter sees the text that will be
// written, which keeps sanitizeHtml idempotent (a font size of "\x17" is not a size, and not an empty one either).
void dropXmlInvalid(std::string& text)
{
    std::size_t kept = 0;
    for (std::size_t i = 0; i < text.size(); ++i) {
        const auto c = static_cast<unsigned char>(text[i]);
        if (c < 0x20 && c != '\t' && c != '\n')
            continue;
        if (c == 0xEF && i + 2 < text.size() && static_cast<unsigned char>(text[i + 1]) == 0xBF &&
            static_cast<unsigned char>(text[i + 2]) >= 0xBE) { // U+FFFE, U+FFFF
            i += 2;
            continue;
        }
        text[kept++] = text[i];
    }
    text.resize(kept);
}

std::string decodeEntities(std::string_view in)
{
    std::string out;
    out.reserve(in.size());
    for (std::size_t i = 0; i < in.size(); ++i) {
        if (in[i] == '&') {
            const std::size_t semi = in.find(';', i + 1);
            // Entity names are short; a far-away ';' is not part of one.
            if (semi != std::string_view::npos && semi - i <= 12 && decodeEntity(in.substr(i + 1, semi - i - 1), out)) {
                i = semi;
                continue;
            }
        }
        out += in[i];
    }
    dropXmlInvalid(out);
    return out;
}

struct Token {
    enum class Type { Text, Start, End } type = Type::Text;
    std::string name; // Start/End: lowercase tag name; Text: decoded text
    std::vector<Attribute> attributes;
    bool selfClosing = false;
};

class Tokenizer {
public:
    explicit Tokenizer(std::string_view in) : in_(in) {}

    bool next(Token& t)
    {
        t = {};
        if (!rawTextEnd_.empty())
            skipRawText();
        while (pos_ < in_.size()) {
            if (in_[pos_] != '<')
                return readText(t);
            const char c1 = peek(1);
            if (in_.substr(pos_, 4) == "<!--") {
                const std::size_t end = in_.find("-->", pos_ + 4);
                pos_ = end == std::string_view::npos ? in_.size() : end + 3;
            } else if (c1 == '!' || c1 == '?' || (c1 == '/' && !isAlpha(peek(2)))) {
                const std::size_t end = in_.find('>', pos_);
                pos_ = end == std::string_view::npos ? in_.size() : end + 1;
            } else if (c1 == '/') {
                return readEndTag(t);
            } else if (isAlpha(c1)) {
                return readStartTag(t);
            } else { // a lone '<' is text
                t.type = Token::Type::Text;
                t.name = "<";
                ++pos_;
                return true;
            }
        }
        return false;
    }

private:
    [[nodiscard]] char peek(std::size_t offset) const
    {
        return pos_ + offset < in_.size() ? in_[pos_ + offset] : '\0';
    }

    bool readText(Token& t)
    {
        const std::size_t end = std::min(in_.find('<', pos_), in_.size());
        t.type = Token::Type::Text;
        t.name = decodeEntities(in_.substr(pos_, end - pos_));
        pos_ = end;
        return true;
    }

    std::string readTagName()
    {
        const std::size_t start = pos_;
        while (pos_ < in_.size() && !isSpace(in_[pos_]) && in_[pos_] != '>' && in_[pos_] != '/')
            ++pos_;
        return lower(in_.substr(start, pos_ - start));
    }

    bool readEndTag(Token& t)
    {
        pos_ += 2;
        t.type = Token::Type::End;
        t.name = readTagName();
        const std::size_t end = in_.find('>', pos_);
        pos_ = end == std::string_view::npos ? in_.size() : end + 1;
        return true;
    }

    // Returns false at the end of input inside the tag (the truncated tag is dropped).
    bool readStartTag(Token& t)
    {
        ++pos_;
        t.type = Token::Type::Start;
        t.name = readTagName();
        for (;;) {
            skipSpaces();
            if (pos_ >= in_.size())
                return false;
            const char c = in_[pos_];
            if (c == '>') {
                ++pos_;
                break;
            }
            if (c == '/') {
                ++pos_;
                if (pos_ < in_.size() && in_[pos_] == '>') {
                    t.selfClosing = true;
                    ++pos_;
                    break;
                }
                continue;
            }
            const std::size_t start = pos_;
            while (pos_ < in_.size() && !isSpace(in_[pos_]) && in_[pos_] != '=' && in_[pos_] != '>' &&
                   in_[pos_] != '/')
                ++pos_;
            if (pos_ == start) { // a stray '='
                ++pos_;
                continue;
            }
            Attribute attr;
            attr.name = lower(in_.substr(start, pos_ - start));
            skipSpaces();
            if (pos_ < in_.size() && in_[pos_] == '=') {
                ++pos_;
                skipSpaces();
                if (!readAttributeValue(attr.value))
                    return false;
            }
            const bool duplicate = std::any_of(t.attributes.begin(), t.attributes.end(),
                                               [&](const Attribute& a) { return a.name == attr.name; });
            if (!duplicate)
                t.attributes.push_back(std::move(attr));
        }
        if (!t.selfClosing && (t.name == "script" || t.name == "style"))
            rawTextEnd_ = t.name;
        return true;
    }

    bool readAttributeValue(std::string& out)
    {
        if (pos_ >= in_.size())
            return false;
        const char quote = in_[pos_];
        if (quote == '"' || quote == '\'') {
            const std::size_t end = in_.find(quote, pos_ + 1);
            if (end == std::string_view::npos)
                return false;
            out = decodeEntities(in_.substr(pos_ + 1, end - pos_ - 1));
            pos_ = end + 1;
            return true;
        }
        const std::size_t start = pos_;
        while (pos_ < in_.size() && !isSpace(in_[pos_]) && in_[pos_] != '>')
            ++pos_;
        out = decodeEntities(in_.substr(start, pos_ - start));
        return true;
    }

    void skipSpaces()
    {
        while (pos_ < in_.size() && isSpace(in_[pos_]))
            ++pos_;
    }

    // Skips the body of <script>/<style> up to (not including) its end tag.
    void skipRawText()
    {
        const std::string needle = "</" + rawTextEnd_;
        rawTextEnd_.clear();
        while (pos_ < in_.size()) {
            const std::size_t lt = in_.find("</", pos_);
            if (lt == std::string_view::npos) {
                pos_ = in_.size();
                return;
            }
            pos_ = lt;
            if (lower(in_.substr(lt, needle.size())) == needle)
                return;
            pos_ += 2;
        }
    }

    std::string_view in_;
    std::size_t pos_ = 0;
    std::string rawTextEnd_;
};

bool isOneOf(std::string_view name, std::initializer_list<std::string_view> names)
{
    return std::find(names.begin(), names.end(), name) != names.end();
}

bool isHeading(std::string_view n) { return n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6'; }

bool isTableFamily(std::string_view n)
{
    return isOneOf(n, {"table", "thead", "tbody", "tfoot", "tr", "td", "th"});
}

class TreeBuilder {
public:
    explicit TreeBuilder(Node& root) { stack_.push_back(&root); }

    void text(const std::string& data)
    {
        Node& parent = *stack_.back();
        if (!parent.children.empty() && parent.children.back()->kind == Node::Kind::Text) {
            parent.children.back()->text += data;
            return;
        }
        auto node = std::make_unique<Node>();
        node->kind = Node::Kind::Text;
        node->text = data;
        parent.children.push_back(std::move(node));
    }

    void start(Token& t)
    {
        impliedEndTags(t.name);
        // Past the depth limit the element is dropped (its content is kept). Keeping it as an empty leaf
        // would serialize as <x></x>, whose end tag closes an ancestor when the output is parsed again.
        if (!isVoidElement(t.name) && stack_.size() >= kMaxDepth)
            return;
        auto node = std::make_unique<Node>();
        node->text = t.name;
        node->attributes = std::move(t.attributes);
        Node* raw = node.get();
        stack_.back()->children.push_back(std::move(node));
        if (!isVoidElement(t.name) && !t.selfClosing)
            stack_.push_back(raw);
    }

    void end(const std::string& name)
    {
        if (name == "br") { // `</br>` is a line break, as in browsers
            Token t;
            t.name = "br";
            start(t);
            return;
        }
        for (std::size_t i = stack_.size(); i-- > 1;) {
            const std::string& open = stack_[i]->text;
            if (open == name) {
                stack_.resize(i);
                return;
            }
            // An end tag never reaches across a table cell it is not part of.
            if (isOneOf(open, {"table", "td", "th"}) && !isTableFamily(name))
                return;
        }
    }

private:
    // Pops up to and including the nearest open element named in `targets`,
    // unless an element in `boundaries` is met first.
    void closeNearest(std::initializer_list<std::string_view> targets,
                      std::initializer_list<std::string_view> boundaries)
    {
        for (std::size_t i = stack_.size(); i-- > 1;) {
            const std::string& open = stack_[i]->text;
            if (isOneOf(open, targets)) {
                stack_.resize(i);
                return;
            }
            if (isOneOf(open, boundaries))
                return;
        }
    }

    void impliedEndTags(std::string_view name)
    {
        if (name == "li")
            closeNearest({"li"}, {"ul", "ol"});
        else if (name == "td" || name == "th")
            closeNearest({"td", "th"}, {"tr", "table"});
        else if (name == "tr")
            closeNearest({"tr"}, {"table"});
        else if (name == "thead" || name == "tbody" || name == "tfoot")
            closeNearest({"thead", "tbody", "tfoot"}, {"table"});
        if (isOneOf(name, {"p", "div", "ul", "ol", "li", "table", "pre", "blockquote", "hr"}) || isHeading(name))
            closeNearest({"p"}, {"table", "td", "th"});
        // After the <p> is closed, as in the HTML spec: `<h1><p><h1>` must give two headings, not a nested pair
        // (which the next pass would split, so the sanitizer would not be idempotent).
        if (isHeading(name) && isHeading(stack_.back()->text))
            stack_.pop_back();
    }

    std::vector<Node*> stack_;
};

} // namespace

const std::string* Node::attribute(std::string_view name) const
{
    for (const Attribute& a : attributes) {
        if (a.name == name)
            return &a.value;
    }
    return nullptr;
}

bool isVoidElement(std::string_view name)
{
    return isOneOf(name, {"area", "base", "br", "col", "embed", "hr", "img", "input", "link", "meta", "param",
                          "source", "track", "wbr"});
}

Node parse(std::string_view input)
{
    Node root;
    TreeBuilder builder(root);
    Tokenizer tokenizer(input);
    Token token;
    while (tokenizer.next(token)) {
        switch (token.type) {
        case Token::Type::Text:
            builder.text(token.name);
            break;
        case Token::Type::Start:
            builder.start(token);
            break;
        case Token::Type::End:
            builder.end(token.name);
            break;
        }
    }
    return root;
}

} // namespace qraw::render::html
