// Label text preparation (text.h): the HTML sanitizer, the XHTML serializer
// and the line extraction. Parsing lives in rich_text_html.cpp.
//
// The sanitizer is an allow-list over the parsed tree, not a filter over the
// markup: whatever is not explicitly allowed is not in the output, and the
// output is produced by the serializer below, so it is well-formed XHTML by
// construction.

#include "qraw/render/text.h"

#include "rich_text_html.h"

#include <QString>

#include <algorithm>
#include <cctype>
#include <cstring>
#include <initializer_list>

namespace qraw::render {

namespace {

using html::Node;

bool isOneOf(std::string_view value, std::initializer_list<std::string_view> values)
{
    return std::find(values.begin(), values.end(), value) != values.end();
}

std::string lowerAscii(std::string_view s)
{
    std::string out(s);
    for (char& c : out)
        c = static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    return out;
}

std::string_view trim(std::string_view s)
{
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.front())))
        s.remove_prefix(1);
    while (!s.empty() && std::isspace(static_cast<unsigned char>(s.back())))
        s.remove_suffix(1);
    return s;
}

bool startsWith(std::string_view s, std::string_view prefix) { return s.substr(0, prefix.size()) == prefix; }

// Replaces invalid UTF-8 by U+FFFD so every later stage sees valid text.
std::string normalizeUtf8(std::string_view s)
{
    return QString::fromUtf8(s.data(), static_cast<qsizetype>(s.size())).toUtf8().toStdString();
}

// ---------------------------------------------------------------- whitelist

bool isAllowedElement(std::string_view n)
{
    if (n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6')
        return true;
    return isOneOf(n, {"b",  "strong", "i",  "em",         "u",  "s",  "strike", "del",   "ins",   "sub",
                       "sup", "font",  "span", "div",       "p",  "br", "ul",     "ol",    "li",    "a",
                       "pre", "code",  "blockquote", "hr",  "table", "thead", "tbody", "tr", "td", "th", "img"});
}

// Removed together with everything inside.
bool isDroppedWithContent(std::string_view n)
{
    return isOneOf(n, {"script", "style", "iframe", "object", "embed", "svg", "math", "template", "noscript"});
}

bool isBlockElement(std::string_view n)
{
    if (n.size() == 2 && n[0] == 'h' && n[1] >= '1' && n[1] <= '6')
        return true;
    return isOneOf(n, {"div", "p", "li", "tr", "pre", "blockquote", "ul", "ol", "table", "thead", "tbody", "hr"});
}

// ---------------------------------------------------------------- style

bool isAllowedCssProperty(std::string_view p)
{
    if (isOneOf(p, {"color", "background-color", "font-size", "font-family", "font-weight", "font-style",
                    "text-decoration", "text-align", "vertical-align", "line-height", "white-space", "width",
                    "height", "display"}))
        return true;
    return startsWith(p, "margin") || startsWith(p, "padding") || startsWith(p, "border");
}

bool isSafeCssValue(std::string_view property, std::string_view value)
{
    if (value.empty())
        return false;
    const std::string v = lowerAscii(value);
    for (const char* bad : {"url(", "expression(", "javascript:", "@import", "\\", "/*", "<", ">"}) {
        if (v.find(bad) != std::string::npos)
            return false;
    }
    if (property == "display")
        return isOneOf(trim(v), {"none", "block", "inline", "inline-block"});
    return true;
}

// Keeps the allowed, safe declarations: "color: red; font-size: 12px;".
std::string filterStyle(std::string_view style)
{
    std::string out;
    std::size_t pos = 0;
    while (pos <= style.size()) {
        std::size_t end = style.find(';', pos);
        if (end == std::string_view::npos)
            end = style.size();
        const std::string_view declaration = style.substr(pos, end - pos);
        pos = end + 1;
        const std::size_t colon = declaration.find(':');
        if (colon == std::string_view::npos)
            continue;
        const std::string property = lowerAscii(trim(declaration.substr(0, colon)));
        std::string_view value = trim(declaration.substr(colon + 1));
        if (!isAllowedCssProperty(property) || !isSafeCssValue(property, value))
            continue;
        if (!out.empty())
            out += ' ';
        out += property + ": " + std::string(value) + ";";
    }
    return out;
}

// ---------------------------------------------------------------- attributes

bool isSafeLink(std::string_view href)
{
    // Control characters and spaces inside the scheme must not hide it
    // ("java\tscript:").
    std::string compact;
    for (const char c : href) {
        if (static_cast<unsigned char>(c) > 0x20)
            compact += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
    }
    return startsWith(compact, "http:") || startsWith(compact, "https:") || startsWith(compact, "mailto:") ||
           startsWith(compact, "#");
}

bool isImageDataUri(std::string_view src)
{
    return startsWith(lowerAscii(trim(src).substr(0, 11)), "data:image/");
}

bool isNumber(std::string_view v, bool allowUnit)
{
    if (allowUnit) {
        if (v.size() > 1 && v.back() == '%')
            v.remove_suffix(1);
        else if (v.size() > 2 && v.substr(v.size() - 2) == "px")
            v.remove_suffix(2);
    }
    return !v.empty() && std::all_of(v.begin(), v.end(), [](char c) { return std::isdigit(static_cast<unsigned char>(c)) || c == '.'; });
}

// The size of a table that is given as `style="width: 100%"` also as the width and height attributes:
// browsers read either, Qt's HTML importer reads only the attributes (without them a table of
// `width: 100%` is as wide as its text).
void addTableSizeAttributes(Node& table)
{
    const std::string* styleAttribute = table.attribute("style");
    if (styleAttribute == nullptr)
        return;
    // A copy: push_back below may reallocate the attribute list that `styleAttribute` points into.
    const std::string style = *styleAttribute;
    for (const char* property : {"width", "height"}) {
        if (table.attribute(property) != nullptr)
            continue;
        const std::string_view css = style;
        std::size_t pos = 0;
        while ((pos = css.find(property, pos)) != std::string_view::npos) {
            const bool declaration = pos == 0 || css[pos - 1] == ' ' || css[pos - 1] == ';';
            pos += std::strlen(property);
            if (!declaration || css.substr(pos, 1) != ":")
                continue;
            const std::size_t end = css.find(';', pos);
            const std::string_view value = trim(css.substr(pos + 1, end == std::string_view::npos ? end : end - pos - 1));
            if (isNumber(value, true))
                table.attributes.push_back({property, std::string(value)});
            break;
        }
    }
}

// Copies the allowed attributes of `in` to `out`. Returns false when the
// element must not survive at all (an img without a data: image source).
bool copyAllowedAttributes(const Node& in, Node& out)
{
    const std::string& tag = in.text;
    for (const html::Attribute& a : in.attributes) {
        const std::string_view value = a.value;
        bool keep = false;
        std::string kept = a.value;
        if (a.name == "style") {
            kept = filterStyle(value);
            keep = !kept.empty();
        } else if (a.name == "align") {
            kept = lowerAscii(trim(value));
            keep = isOneOf(kept, {"left", "center", "right", "justify"});
        } else if (tag == "font" && isOneOf(a.name, {"color", "size", "face"})) {
            keep = isSafeCssValue("", value);
        } else if (tag == "a" && a.name == "href") {
            keep = isSafeLink(value);
        } else if (tag == "img" && a.name == "src") {
            keep = isImageDataUri(value);
            kept = std::string(trim(value));
        } else if (tag == "img" && (a.name == "width" || a.name == "height")) {
            keep = isNumber(trim(value), true);
            kept = std::string(trim(value));
        } else if (tag == "img" && a.name == "alt") {
            keep = true;
        } else if ((tag == "td" || tag == "th") && (a.name == "colspan" || a.name == "rowspan")) {
            keep = isNumber(trim(value), false);
            kept = std::string(trim(value));
        } else if (tag == "table" && (a.name == "width" || a.name == "height")) {
            keep = isNumber(trim(value), true);
            kept = std::string(trim(value));
        } else if (tag == "table" && isOneOf(a.name, {"border", "cellpadding", "cellspacing"})) {
            keep = isNumber(trim(value), false);
            kept = std::string(trim(value));
        }
        if (keep)
            out.attributes.push_back({a.name, kept});
    }
    if (tag == "table")
        addTableSizeAttributes(out);
    return tag != "img" || out.attribute("src") != nullptr;
}

// ---------------------------------------------------------------- sanitize

constexpr int kMaxSanitizeDepth = 256;

void appendText(Node& parent, const std::string& text)
{
    if (text.empty())
        return;
    if (!parent.children.empty() && parent.children.back()->kind == Node::Kind::Text) {
        parent.children.back()->text += text;
        return;
    }
    auto node = std::make_unique<Node>();
    node->kind = Node::Kind::Text;
    node->text = text;
    parent.children.push_back(std::move(node));
}

void sanitizeChildren(const Node& in, Node& out, int depth)
{
    for (const auto& child : in.children) {
        if (child->kind == Node::Kind::Text) {
            appendText(out, child->text);
            continue;
        }
        const std::string& tag = child->text;
        if (isDroppedWithContent(tag))
            continue;
        if (!isAllowedElement(tag)) { // unknown element: keep its content
            if (depth < kMaxSanitizeDepth)
                sanitizeChildren(*child, out, depth + 1);
            continue;
        }
        auto clean = std::make_unique<Node>();
        clean->text = tag;
        if (!copyAllowedAttributes(*child, *clean))
            continue;
        if (depth < kMaxSanitizeDepth)
            sanitizeChildren(*child, *clean, depth + 1);
        out.children.push_back(std::move(clean));
    }
}

// ---------------------------------------------------------------- serialize

// XML 1.0 Char: control characters (except tab, newline), U+FFFE and U+FFFF
// cannot appear in a well-formed document; a carriage return is dropped.
void appendEscaped(std::string& out, std::string_view s, bool attribute)
{
    for (std::size_t i = 0; i < s.size(); ++i) {
        const auto c = static_cast<unsigned char>(s[i]);
        switch (c) {
        case '&': out += "&amp;"; break;
        case '<': out += "&lt;"; break;
        case '>': out += "&gt;"; break;
        case '"':
            if (attribute)
                out += "&quot;";
            else
                out += '"';
            break;
        case '\n':
        case '\t':
            if (attribute)
                out += c == '\n' ? "&#10;" : "&#9;";
            else
                out += static_cast<char>(c);
            break;
        case 0xC2:
            if (i + 1 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xA0) { // U+00A0
                out += "&#160;";
                ++i;
            } else {
                out += static_cast<char>(c);
            }
            break;
        case 0xEF:
            if (i + 2 < s.size() && static_cast<unsigned char>(s[i + 1]) == 0xBF &&
                static_cast<unsigned char>(s[i + 2]) >= 0xBE) { // U+FFFE, U+FFFF
                i += 2;
            } else {
                out += static_cast<char>(c);
            }
            break;
        default:
            if (c >= 0x20 || c >= 0x80)
                out += static_cast<char>(c);
            break; // other control characters (and '\r') are dropped
        }
    }
}

void serialize(const Node& node, std::string& out)
{
    if (node.kind == Node::Kind::Text) {
        appendEscaped(out, node.text, false);
        return;
    }
    if (!node.text.empty()) {
        out += '<' + node.text;
        for (const html::Attribute& a : node.attributes) {
            out += ' ' + a.name + "=\"";
            appendEscaped(out, a.value, true);
            out += '"';
        }
        if (html::isVoidElement(node.text)) {
            out += "/>";
            return;
        }
        out += '>';
    }
    for (const auto& child : node.children)
        serialize(*child, out);
    if (!node.text.empty())
        out += "</" + node.text + '>';
}

// ---------------------------------------------------------------- lines

// Walks the sanitized tree the way a browser lays out explicit breaks:
// whitespace collapses (except in <pre>), <br> and the boundaries of block
// elements end a line, and a boundary never creates an empty line.
class LineCollector {
public:
    std::vector<std::string> run(const Node& root)
    {
        visit(root);
        if (!current_.empty())
            lines_.push_back(std::move(current_));
        return std::move(lines_);
    }

private:
    void endLine()
    {
        lines_.push_back(std::move(current_));
        current_.clear();
        pendingSpace_ = false;
    }

    void flushLine()
    {
        if (!current_.empty())
            endLine();
        pendingSpace_ = false;
    }

    void addText(const std::string& text)
    {
        for (std::size_t i = 0; i < text.size(); ++i) {
            const char c = text[i];
            if (preDepth_ > 0) {
                if (c == '\n')
                    endLine();
                else if (c != '\r')
                    current_ += c;
            } else if (c == ' ' || c == '\t' || c == '\n' || c == '\r' || c == '\f') {
                pendingSpace_ = true;
            } else {
                if (pendingSpace_ && !current_.empty())
                    current_ += ' ';
                pendingSpace_ = false;
                current_ += c;
            }
        }
    }

    void visit(const Node& node)
    {
        if (node.kind == Node::Kind::Text) {
            addText(node.text);
            return;
        }
        const std::string& tag = node.text;
        if (tag == "br") {
            endLine();
            return;
        }
        const bool block = isBlockElement(tag);
        if (block)
            flushLine();
        if (tag == "pre")
            ++preDepth_;
        for (const auto& child : node.children)
            visit(*child);
        if (tag == "pre")
            --preDepth_;
        if (block)
            flushLine();
        else if (tag == "td" || tag == "th")
            pendingSpace_ = true;
    }

    std::vector<std::string> lines_;
    std::string current_;
    bool pendingSpace_ = false;
    int preDepth_ = 0;
};

std::vector<std::string> splitLines(std::string_view value)
{
    std::vector<std::string> lines;
    if (value.empty())
        return lines;
    std::string current;
    for (const char c : value) {
        if (c == '\n') {
            lines.push_back(std::move(current));
            current.clear();
        } else if (c != '\r') {
            current += c;
        }
    }
    lines.push_back(std::move(current));
    return lines;
}

bool isBlank(const Node& n)
{
    if (n.kind != Node::Kind::Text)
        return false;
    return std::all_of(n.text.begin(), n.text.end(), [](char c) { return std::isspace(static_cast<unsigned char>(c)) != 0; });
}

// Whether `n` draws something: text other than blanks, an image, or an element that has such content.
bool hasContent(const Node& n)
{
    if (n.kind == Node::Kind::Text)
        return !isBlank(n);
    if (n.isElement("br"))
        return false;
    if (html::isVoidElement(n.text))
        return true;
    return std::any_of(n.children.begin(), n.children.end(), [](const auto& c) { return hasContent(*c); });
}

// A <br> that ends a block, after something on its line, ends that line and adds none: browsers draw
// `<p>text<br></p>` as one line, where Qt's rich text adds an empty one (docs/FIDELITY.md class 4, a
// template with ten such rows draws them 40 % too tall). A lone <br> (`<div><br></div>`) is a blank
// line and stays, and so does the second of two. `contentBefore` is whether the enclosing inline
// elements already have content on the line.
void dropTrailingBreak(Node& block, bool contentBefore)
{
    auto& kids = block.children;
    for (std::size_t i = kids.size(); i-- > 0;) {
        Node& last = *kids[i];
        if (isBlank(last))
            continue;
        bool before = contentBefore;
        for (std::size_t j = 0; j < i && !before; ++j)
            before = hasContent(*kids[j]);
        if (last.isElement("br")) {
            if (before)
                kids.erase(kids.begin() + static_cast<std::ptrdiff_t>(i), kids.end());
        } else if (last.kind == Node::Kind::Element && !isBlockElement(last.text) && !html::isVoidElement(last.text)) {
            dropTrailingBreak(last, before);
        }
        return;
    }
}

void dropTrailingBreaks(Node& node)
{
    if (node.kind != Node::Kind::Element)
        return;
    if (node.text.empty() || isBlockElement(node.text) || node.text == "td" || node.text == "th")
        dropTrailingBreak(node, false);
    for (auto& child : node.children)
        dropTrailingBreaks(*child);
}

// `forLayout`: also drops the breaks that browsers do not draw. That is a layout rule, not a
// sanitizing one, so sanitizeHtml (idempotent: `a<br><br>` would lose a break on every pass) leaves them.
Node sanitizeTree(std::string_view htmlText, bool forLayout = false)
{
    const html::Node parsed = html::parse(normalizeUtf8(htmlText));
    Node clean;
    sanitizeChildren(parsed, clean, 0);
    if (forLayout)
        dropTrailingBreaks(clean);
    return clean;
}

} // namespace

std::string sanitizeHtml(std::string_view htmlText)
{
    const Node clean = sanitizeTree(htmlText);
    std::string out;
    serialize(clean, out);
    return out;
}

RichText makeRichText(std::string_view value, bool htmlText)
{
    RichText text;
    text.html = htmlText;
    if (!htmlText) {
        text.content = std::string(value);
        text.lines = splitLines(value);
        return text;
    }
    // mxText.paint: a literal newline in an HTML value is a line break
    // (replaceLinefeeds). A '\r' before it is dropped.
    std::string withBreaks;
    withBreaks.reserve(value.size());
    for (const char c : value) {
        if (c == '\n')
            withBreaks += "<br/>";
        else if (c != '\r')
            withBreaks += c;
    }
    const Node clean = sanitizeTree(withBreaks, true);
    serialize(clean, text.content);
    text.lines = LineCollector().run(clean);
    return text;
}

} // namespace qraw::render
