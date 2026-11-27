#include "qraw/core/attributes.h"

#include <algorithm>

namespace qraw::core {

namespace {
const std::string kEmpty;

std::size_t rankOf(std::string_view name, std::initializer_list<std::string_view> order)
{
    std::size_t rank = 0;
    for (auto key : order) {
        if (key == name)
            return rank;
        ++rank;
    }
    return order.size();
}
} // namespace

std::vector<Attribute>::iterator Attributes::findIt(std::string_view name)
{
    return std::find_if(items_.begin(), items_.end(), [&](const Attribute& a) { return a.name == name; });
}

bool Attributes::has(std::string_view name) const { return find(name) != nullptr; }

const SharedValue* Attributes::findValue(std::string_view name) const
{
    for (const auto& a : items_) {
        if (a.name == name)
            return &a.value;
    }
    return nullptr;
}

const std::string* Attributes::find(std::string_view name) const
{
    const SharedValue* v = findValue(name);
    return v ? &v->str() : nullptr;
}

const std::string& Attributes::value(std::string_view name) const
{
    const std::string* v = find(name);
    return v ? *v : kEmpty;
}

void Attributes::set(std::string_view name, std::string_view value) { setShared(name, SharedValue(value)); }

void Attributes::setOwned(std::string_view name, std::string&& value) { setShared(name, SharedValue(std::move(value))); }

void Attributes::setShared(std::string_view name, const SharedValue& value)
{
    if (auto it = findIt(name); it != items_.end()) {
        it->value = value;
        return;
    }
    items_.push_back({std::string(name), value});
}

void Attributes::setOrdered(std::string_view name, std::string_view value,
                            std::initializer_list<std::string_view> canonicalOrder)
{
    if (auto it = findIt(name); it != items_.end()) {
        it->value = value;
        return;
    }
    const std::size_t rank = rankOf(name, canonicalOrder);
    auto pos = items_.end();
    if (rank < canonicalOrder.size()) {
        pos = std::find_if(items_.begin(), items_.end(), [&](const Attribute& a) {
            const std::size_t other = rankOf(a.name, canonicalOrder);
            return other < canonicalOrder.size() && other > rank;
        });
    }
    items_.insert(pos, {std::string(name), SharedValue(value)});
}

void Attributes::setBefore(std::string_view name, std::string_view value, std::string_view anchor)
{
    if (auto it = findIt(name); it != items_.end()) {
        it->value = value;
        return;
    }
    items_.insert(findIt(anchor), {std::string(name), SharedValue(value)});
}

bool Attributes::remove(std::string_view name)
{
    if (auto it = findIt(name); it != items_.end()) {
        items_.erase(it);
        return true;
    }
    return false;
}

} // namespace qraw::core
