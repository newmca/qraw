#include "qraw/core/cell.h"

namespace qraw::core {

namespace {
// mxCell field order as serialized by draw.io's mxCodec.
constexpr std::initializer_list<std::string_view> kCellOrder = {
    "id", "value", "style", "vertex", "edge", "connectable", "visible", "collapsed", "parent", "source", "target"};
} // namespace

Cell::Cell(std::string id) { setId(id); }

Cell Cell::makeVertex(std::string id, std::string_view parent, std::string_view value, std::string_view style,
                      const Rect& bounds)
{
    Cell c(std::move(id));
    c.setValue(value);
    c.setStyleText(style);
    c.setVertex(true);
    c.setParent(parent);
    c.ensureGeometry().setBounds(bounds);
    return c;
}

Cell Cell::makeEdge(std::string id, std::string_view parent, std::string_view value, std::string_view style,
                    std::string_view source, std::string_view target)
{
    Cell c(std::move(id));
    c.setValue(value);
    c.setStyleText(style);
    c.setEdge(true);
    c.setParent(parent);
    c.setSource(source);
    c.setTarget(target);
    c.ensureGeometry().setRelative(true);
    return c;
}

Cell Cell::makeLayer(std::string id, std::string_view parent)
{
    Cell c(std::move(id));
    c.setParent(parent);
    return c;
}

void Cell::setCellAttribute(std::string_view name, std::string_view value)
{
    if (value.empty())
        attributes_.remove(name);
    else
        attributes_.setOrdered(name, value, kCellOrder);
}

void Cell::setFlag(std::string_view name, bool on, bool defaultValue)
{
    if (on == defaultValue)
        attributes_.remove(name); // draw.io omits defaults
    else
        attributes_.setOrdered(name, on ? "1" : "0", kCellOrder);
}

const std::string& Cell::id() const
{
    if (wrapper_ && wrapper_->attributes.has("id"))
        return wrapper_->attributes.value("id");
    return attributes_.value("id");
}

void Cell::setId(std::string_view id)
{
    if (wrapper_)
        wrapper_->attributes.set("id", id);
    else
        attributes_.setOrdered("id", id, kCellOrder);
}

const std::string& Cell::value() const
{
    return wrapper_ ? wrapper_->attributes.value("label") : attributes_.value("value");
}

void Cell::setValue(std::string_view value)
{
    if (wrapper_) {
        wrapper_->attributes.setBefore("label", value, "id");
        return;
    }
    // `value` is kept even when empty for vertices and edges, like draw.io.
    attributes_.setOrdered("value", value, kCellOrder);
}

Style Cell::style() const
{
    const SharedValue* text = attributes_.findValue("style");
    if (text == nullptr)
        return {};
    if (text->isShared()) {
        if (const auto parsed = text->buffer()->memo<Style>([&] { return Style(text->str()); }))
            return *parsed;
    }
    return Style(text->str());
}

void Cell::setParent(std::string_view parent) { setCellAttribute("parent", parent); }
void Cell::setSource(std::string_view source) { setCellAttribute("source", source); }
void Cell::setTarget(std::string_view target) { setCellAttribute("target", target); }
void Cell::setStyle(const Style& style) { setStyleText(style.toString()); }
void Cell::setStyleText(std::string_view style) { setCellAttribute("style", style); }

void Cell::setVertex(bool on) { setFlag("vertex", on, false); }
void Cell::setEdge(bool on) { setFlag("edge", on, false); }
void Cell::setConnectable(bool on) { setFlag("connectable", on, true); }
void Cell::setVisible(bool on) { setFlag("visible", on, true); }
void Cell::setCollapsed(bool on) { setFlag("collapsed", on, false); }

Geometry& Cell::ensureGeometry()
{
    if (!geometry_)
        geometry_ = Geometry::create();
    return *geometry_;
}

CellWrapper& Cell::ensureWrapper(std::string_view tag)
{
    if (wrapper_)
        return *wrapper_;
    CellWrapper w;
    w.tag = tag;
    w.attributes.set("label", attributes_.value("value"));
    w.attributes.set("id", attributes_.value("id"));
    attributes_.remove("value");
    attributes_.remove("id");
    wrapper_ = std::move(w);
    return *wrapper_;
}

void Cell::setData(std::string_view name, std::string_view value)
{
    ensureWrapper().attributes.setBefore(name, value, "id");
}

} // namespace qraw::core
