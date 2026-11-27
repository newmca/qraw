#pragma once

#include "qraw/core/attributes.h"
#include "qraw/core/foreign_children.h"
#include "qraw/core/geometry.h"
#include "qraw/core/style.h"

#include <optional>
#include <string>
#include <string_view>

namespace qraw::core {

// The element that wraps an <mxCell> when a cell carries custom data:
// <object label=".." id=".." custom=".."><mxCell .../></object>.
// draw.io accepts any element name (`object`, `UserObject`, ...).
struct CellWrapper {
    std::string tag = "object";
    Attributes attributes;
    ForeignChildren foreign; // children besides the inner <mxCell>

    friend bool operator==(const CellWrapper&, const CellWrapper&) = default;
};

// One <mxCell> (optionally wrapped). The ordered attribute lists are the
// source of truth; typed accessors read and edit them in place. New
// attributes are inserted in draw.io's canonical order.
class Cell {
public:
    Cell() = default;
    explicit Cell(std::string id);

    static Cell makeVertex(std::string id, std::string_view parent, std::string_view value,
                           std::string_view style, const Rect& bounds);
    static Cell makeEdge(std::string id, std::string_view parent, std::string_view value,
                         std::string_view style, std::string_view source, std::string_view target);
    // A layer: a child of the root cell without vertex/edge flags.
    static Cell makeLayer(std::string id, std::string_view parent);

    // id and value/label live on the wrapper when there is one.
    [[nodiscard]] const std::string& id() const;
    void setId(std::string_view id);
    [[nodiscard]] const std::string& value() const;
    void setValue(std::string_view value);

    [[nodiscard]] const std::string& parent() const { return attributes_.value("parent"); }
    void setParent(std::string_view parent);
    [[nodiscard]] const std::string& source() const { return attributes_.value("source"); }
    void setSource(std::string_view source);
    [[nodiscard]] const std::string& target() const { return attributes_.value("target"); }
    void setTarget(std::string_view target);

    [[nodiscard]] const std::string& styleText() const { return attributes_.value("style"); }
    // The parsed style. A large style text (an embedded image) is parsed once
    // per text buffer and the result shared, so asking again costs a copy of
    // the token list, not a parse and not a copy of the megabytes.
    [[nodiscard]] Style style() const;
    void setStyle(const Style& style);
    void setStyleText(std::string_view style);

    [[nodiscard]] bool isVertex() const { return attributes_.value("vertex") == "1"; }
    [[nodiscard]] bool isEdge() const { return attributes_.value("edge") == "1"; }
    [[nodiscard]] bool isConnectable() const { return attributes_.value("connectable") != "0"; }
    [[nodiscard]] bool isVisible() const { return attributes_.value("visible") != "0"; }
    [[nodiscard]] bool isCollapsed() const { return attributes_.value("collapsed") == "1"; }
    void setVertex(bool on);
    void setEdge(bool on);
    void setConnectable(bool on);
    void setVisible(bool on);
    void setCollapsed(bool on);

    [[nodiscard]] Geometry* geometry() { return geometry_ ? &*geometry_ : nullptr; }
    [[nodiscard]] const Geometry* geometry() const { return geometry_ ? &*geometry_ : nullptr; }
    Geometry& ensureGeometry();
    void setGeometry(std::optional<Geometry> geometry) { geometry_ = std::move(geometry); }

    // Custom data. Creating a wrapper moves id and value onto it (`label`).
    [[nodiscard]] bool hasWrapper() const { return wrapper_.has_value(); }
    [[nodiscard]] CellWrapper* wrapper() { return wrapper_ ? &*wrapper_ : nullptr; }
    [[nodiscard]] const CellWrapper* wrapper() const { return wrapper_ ? &*wrapper_ : nullptr; }
    CellWrapper& ensureWrapper(std::string_view tag = "object");
    void setWrapper(std::optional<CellWrapper> wrapper) { wrapper_ = std::move(wrapper); }
    // Sets a custom data attribute on the wrapper (created on demand),
    // keeping `id` last as draw.io does.
    void setData(std::string_view name, std::string_view value);

    // Raw <mxCell> attributes and its children other than <mxGeometry>.
    [[nodiscard]] Attributes& attributes() { return attributes_; }
    [[nodiscard]] const Attributes& attributes() const { return attributes_; }
    [[nodiscard]] ForeignChildren& foreignChildren() { return foreign_; }
    [[nodiscard]] const ForeignChildren& foreignChildren() const { return foreign_; }

    friend bool operator==(const Cell&, const Cell&) = default;

private:
    void setCellAttribute(std::string_view name, std::string_view value);
    void setFlag(std::string_view name, bool on, bool defaultValue);

    std::optional<CellWrapper> wrapper_;
    Attributes attributes_;
    ForeignChildren foreign_;
    std::optional<Geometry> geometry_;
};

} // namespace qraw::core
