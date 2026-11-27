#pragma once

#include "qraw/core/shared_value.h"

#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::core {

// A draw.io cell style: `;`-separated tokens that are either bare style
// names (`ellipse`, `text`) or `key=value` pairs.
//
// Tokens are kept in order and exactly as written (including empty tokens
// such as the one after a trailing `;`), so an untouched style serializes
// byte-identically. Editing an existing key changes it in place; a new key is
// appended (before a trailing `;`, which is preserved).
class Style {
public:
    struct Token {
        std::string key;   // the whole token when it has no '='
        SharedValue value; // converts to const std::string& and std::string_view; a large value is shared
        bool hasValue = false;

        [[nodiscard]] bool isName() const { return !hasValue && !key.empty(); }
        friend bool operator==(const Token&, const Token&) = default;
    };

    Style() = default;
    explicit Style(std::string_view text);

    [[nodiscard]] std::string toString() const;
    [[nodiscard]] bool empty() const { return tokens_.empty(); }
    [[nodiscard]] const std::vector<Token>& tokens() const { return tokens_; }

    // key=value access. When a key occurs more than once the last occurrence
    // wins, as in draw.io.
    [[nodiscard]] bool has(std::string_view key) const;
    [[nodiscard]] std::optional<std::string> value(std::string_view key) const;
    // The last value of `key` without copying it (nullptr when absent): the
    // accessor for values that can be megabytes (`image`).
    [[nodiscard]] const SharedValue* findValue(std::string_view key) const;
    [[nodiscard]] std::string value(std::string_view key, std::string_view fallback) const;
    void set(std::string_view key, std::string_view value);
    // Removes every occurrence of `key`. Returns false when it was absent.
    bool remove(std::string_view key);

    // Bare style names.
    [[nodiscard]] std::vector<std::string> names() const;
    [[nodiscard]] bool hasName(std::string_view name) const;
    // Adds a bare name after the existing names (draw.io puts names first).
    void addName(std::string_view name);
    bool removeName(std::string_view name);

    friend bool operator==(const Style&, const Style&) = default;

private:
    std::vector<Token>::iterator lastKey(std::string_view key);
    void insertToken(Token token);

    std::vector<Token> tokens_;
};

} // namespace qraw::core
