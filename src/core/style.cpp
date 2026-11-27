#include "qraw/core/style.h"

#include <algorithm>

namespace qraw::core {

namespace {
Style::Token tokenFromRaw(std::string_view raw)
{
    Style::Token t;
    if (const auto eq = raw.find('='); eq != std::string_view::npos) {
        t.key = raw.substr(0, eq);
        t.value = raw.substr(eq + 1);
        t.hasValue = true;
    } else {
        t.key = raw;
    }
    return t;
}
} // namespace

Style::Style(std::string_view text)
{
    if (text.empty())
        return;
    std::size_t start = 0;
    while (true) {
        const auto pos = text.find(';', start);
        tokens_.push_back(tokenFromRaw(text.substr(start, pos == std::string_view::npos ? pos : pos - start)));
        if (pos == std::string_view::npos)
            break;
        start = pos + 1;
    }
}

std::string Style::toString() const
{
    std::string out;
    std::size_t total = tokens_.size();
    for (const Token& t : tokens_)
        total += t.key.size() + t.value.size() + 1;
    out.reserve(total);
    for (std::size_t i = 0; i < tokens_.size(); ++i) {
        if (i > 0)
            out += ';';
        out += tokens_[i].key;
        if (tokens_[i].hasValue) {
            out += '=';
            out += tokens_[i].value.str();
        }
    }
    return out;
}

std::vector<Style::Token>::iterator Style::lastKey(std::string_view key)
{
    auto it = std::find_if(tokens_.rbegin(), tokens_.rend(),
                           [&](const Token& t) { return t.hasValue && t.key == key; });
    return it == tokens_.rend() ? tokens_.end() : std::prev(it.base());
}

bool Style::has(std::string_view key) const { return value(key).has_value(); }

const SharedValue* Style::findValue(std::string_view key) const
{
    for (auto it = tokens_.rbegin(); it != tokens_.rend(); ++it) {
        if (it->hasValue && it->key == key)
            return &it->value;
    }
    return nullptr;
}

std::optional<std::string> Style::value(std::string_view key) const
{
    if (const SharedValue* found = findValue(key))
        return found->str();
    return std::nullopt;
}

std::string Style::value(std::string_view key, std::string_view fallback) const
{
    auto v = value(key);
    return v ? *v : std::string(fallback);
}

void Style::insertToken(Token token)
{
    if (tokens_.empty()) {
        // draw.io writes styles with a trailing ';'.
        tokens_.push_back(std::move(token));
        tokens_.push_back({});
        return;
    }
    const bool trailingSeparator = tokens_.back().key.empty() && !tokens_.back().hasValue;
    tokens_.insert(trailingSeparator ? std::prev(tokens_.end()) : tokens_.end(), std::move(token));
}

void Style::set(std::string_view key, std::string_view value)
{
    if (auto it = lastKey(key); it != tokens_.end()) {
        it->value = value;
        return;
    }
    insertToken({std::string(key), std::string(value), true});
}

bool Style::remove(std::string_view key)
{
    const auto removed = std::erase_if(tokens_, [&](const Token& t) { return t.hasValue && t.key == key; });
    if (tokens_.size() == 1 && tokens_.front() == Token{})
        tokens_.clear(); // only the trailing separator is left
    return removed > 0;
}

std::vector<std::string> Style::names() const
{
    std::vector<std::string> out;
    for (const auto& t : tokens_) {
        if (t.isName())
            out.push_back(t.key);
    }
    return out;
}

bool Style::hasName(std::string_view name) const
{
    return std::any_of(tokens_.begin(), tokens_.end(), [&](const Token& t) { return t.isName() && t.key == name; });
}

void Style::addName(std::string_view name)
{
    if (name.empty() || hasName(name))
        return;
    if (tokens_.empty()) {
        insertToken({std::string(name), {}, false});
        return;
    }
    // Insert after the last leading name.
    auto pos = std::find_if(tokens_.begin(), tokens_.end(), [](const Token& t) { return !t.isName(); });
    tokens_.insert(pos, {std::string(name), {}, false});
}

bool Style::removeName(std::string_view name)
{
    const auto removed = std::erase_if(tokens_, [&](const Token& t) { return t.isName() && t.key == name; });
    if (tokens_.size() == 1 && tokens_.front() == Token{})
        tokens_.clear();
    return removed > 0;
}

} // namespace qraw::core
