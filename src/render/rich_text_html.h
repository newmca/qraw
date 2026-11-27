#pragma once

// Private to rich_text*.cpp: a small lenient HTML tokenizer and tree builder
// for label markup (tag soup). It only builds a tree; deciding what is
// allowed is rich_text.cpp's job (sanitizeHtml).

#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::render::html {

struct Attribute {
    std::string name;  // lowercase
    std::string value; // entity-decoded
};

struct Node {
    enum class Kind { Element, Text };
    Kind kind = Kind::Element;
    // Element: lowercase tag name ("" for the synthetic root).
    // Text: the entity-decoded text.
    std::string text;
    std::vector<Attribute> attributes;
    std::vector<std::unique_ptr<Node>> children;

    [[nodiscard]] bool isElement(std::string_view name) const { return kind == Kind::Element && text == name; }
    [[nodiscard]] const std::string* attribute(std::string_view name) const;
};

// Parses `input` (UTF-8) into a tree under a synthetic root element. Never
// fails: unknown or broken markup degrades to text or is skipped.
//  - Tokens: text, start/end/self-closing tags, comments and other `<!`/`<?`
//    constructs (dropped), attributes quoted with " or ' or unquoted.
//    <script> and <style> contents are discarded.
//  - Entities: the named set real labels use, plus &#N; and &#xH;. Unknown
//    entities stay literal text.
//  - Structure (HTML5 basics): void elements, <p>/<li>/<td>/<th>/<tr> closing
//    their predecessors, unmatched end tags ignored, `<tag/>` closes at once
//    (XHTML style; browsers would leave it open), everything still open at
//    the end is closed.
[[nodiscard]] Node parse(std::string_view input);

[[nodiscard]] bool isVoidElement(std::string_view name);

} // namespace qraw::render::html
