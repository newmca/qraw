#pragma once

#include "qraw/core/shared_value.h"

#include <cstddef>
#include <initializer_list>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::core {

struct Attribute {
    std::string name;
    SharedValue value; // converts to const std::string& and std::string_view

    friend bool operator==(const Attribute&, const Attribute&) = default;
};

// Ordered attribute list. Document order is kept so that unknown attributes
// and the original ordering survive a load/save round trip.
// Lookups are linear: XML elements carry few attributes.
class Attributes {
public:
    using const_iterator = std::vector<Attribute>::const_iterator;

    Attributes() = default;
    Attributes(std::initializer_list<Attribute> items) : items_(items) {}

    [[nodiscard]] bool has(std::string_view name) const;
    // Returns the value, or an empty string when the attribute is absent.
    [[nodiscard]] const std::string& value(std::string_view name) const;
    [[nodiscard]] const std::string* find(std::string_view name) const;
    // The value with its sharing handle (see SharedValue), or nullptr.
    [[nodiscard]] const SharedValue* findValue(std::string_view name) const;

    // Edits the value in place, or appends a new attribute at the end.
    void set(std::string_view name, std::string_view value);
    // set() for a value that is already a SharedValue: a large one is shared, not copied.
    void setShared(std::string_view name, const SharedValue& value);
    // set() taking the bytes of `value` over (the XML reader's path).
    void setOwned(std::string_view name, std::string&& value);

    // Edits the value in place. A new attribute is inserted before the first
    // existing attribute that comes later in `canonicalOrder`, so attributes
    // created through setters follow draw.io's canonical order. Names that
    // are not in `canonicalOrder` are appended.
    void setOrdered(std::string_view name, std::string_view value,
                    std::initializer_list<std::string_view> canonicalOrder);

    // Inserts (or edits) `name` so that it lands right before `anchor`
    // when `anchor` exists; appends otherwise.
    void setBefore(std::string_view name, std::string_view value, std::string_view anchor);

    bool remove(std::string_view name);
    void clear() { items_.clear(); }

    [[nodiscard]] std::size_t size() const { return items_.size(); }
    [[nodiscard]] bool empty() const { return items_.empty(); }
    [[nodiscard]] const Attribute& operator[](std::size_t i) const { return items_[i]; }
    [[nodiscard]] const_iterator begin() const { return items_.begin(); }
    [[nodiscard]] const_iterator end() const { return items_.end(); }

    friend bool operator==(const Attributes&, const Attributes&) = default;

private:
    std::vector<Attribute>::iterator findIt(std::string_view name);
    std::vector<Attribute> items_;
};

} // namespace qraw::core
