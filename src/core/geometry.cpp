#include "qraw/core/geometry.h"

#include "qraw/core/number.h"

#include <algorithm>

namespace qraw::core {

namespace {
constexpr std::initializer_list<std::string_view> kGeometryOrder = {"x", "y", "width", "height", "relative", "as"};
constexpr std::initializer_list<std::string_view> kPointOrder = {"x", "y", "as"};
constexpr std::initializer_list<std::string_view> kRectOrder = {"x", "y", "width", "height", "as"};

void setPointCoordinate(XmlNode& node, std::string_view name, double v)
{
    if (v == 0 && !node.attributes.has(name))
        return; // draw.io omits zero coordinates
    node.attributes.setOrdered(name, formatNumber(v), kPointOrder);
}
} // namespace

Point pointFromXml(const XmlNode& node)
{
    return {parseNumber(node.attributes.value("x"), 0), parseNumber(node.attributes.value("y"), 0)};
}

XmlNode pointToXml(const Point& p, std::string_view role)
{
    XmlNode node = XmlNode::element("mxPoint");
    setPointCoordinate(node, "x", p.x);
    setPointCoordinate(node, "y", p.y);
    if (!role.empty())
        node.attributes.setOrdered("as", role, kPointOrder);
    return node;
}

Geometry Geometry::create()
{
    Geometry g;
    g.attributes_.set("as", "geometry");
    return g;
}

Geometry Geometry::create(const Rect& bounds)
{
    Geometry g = create();
    g.setBounds(bounds);
    return g;
}

double Geometry::x() const { return parseNumber(attributes_.value("x"), 0); }
double Geometry::y() const { return parseNumber(attributes_.value("y"), 0); }
double Geometry::width() const { return parseNumber(attributes_.value("width"), 0); }
double Geometry::height() const { return parseNumber(attributes_.value("height"), 0); }

void Geometry::setNumber(std::string_view name, double v)
{
    if (v == 0 && !attributes_.has(name))
        return; // draw.io omits zero values
    attributes_.setOrdered(name, formatNumber(v), kGeometryOrder);
}

void Geometry::setX(double v) { setNumber("x", v); }
void Geometry::setY(double v) { setNumber("y", v); }
void Geometry::setWidth(double v) { setNumber("width", v); }
void Geometry::setHeight(double v) { setNumber("height", v); }

void Geometry::setBounds(const Rect& r)
{
    setX(r.x);
    setY(r.y);
    setWidth(r.width);
    setHeight(r.height);
}

bool Geometry::relative() const { return attributes_.value("relative") == "1"; }

void Geometry::setRelative(bool relative)
{
    if (relative)
        attributes_.setOrdered("relative", "1", kGeometryOrder);
    else
        attributes_.remove("relative");
}

const XmlNode* Geometry::childWithRole(std::string_view role) const
{
    for (const auto& c : children_) {
        if (c.isElement() && c.attributes.value("as") == role)
            return &c;
    }
    return nullptr;
}

std::optional<Point> Geometry::namedPoint(std::string_view role) const
{
    const XmlNode* node = childWithRole(role);
    if (!node || node->name != "mxPoint")
        return std::nullopt;
    return pointFromXml(*node);
}

void Geometry::setNamedPoint(std::string_view role, std::optional<Point> p)
{
    auto it = std::find_if(children_.begin(), children_.end(), [&](const XmlNode& c) {
        return c.isElement("mxPoint") && c.attributes.value("as") == role;
    });
    if (!p) {
        if (it != children_.end())
            children_.erase(it);
        return;
    }
    if (it == children_.end()) {
        children_.push_back(pointToXml(*p, role));
        return;
    }
    // Edit in place so that unknown attributes on the point survive.
    setPointCoordinate(*it, "x", p->x);
    setPointCoordinate(*it, "y", p->y);
}

std::optional<Rect> Geometry::alternateBounds() const
{
    const XmlNode* node = childWithRole("alternateBounds");
    if (!node || node->name != "mxRectangle")
        return std::nullopt;
    return Rect{parseNumber(node->attributes.value("x"), 0), parseNumber(node->attributes.value("y"), 0),
                parseNumber(node->attributes.value("width"), 0), parseNumber(node->attributes.value("height"), 0)};
}

void Geometry::setAlternateBounds(std::optional<Rect> bounds)
{
    auto it = std::find_if(children_.begin(), children_.end(), [](const XmlNode& c) {
        return c.isElement("mxRectangle") && c.attributes.value("as") == "alternateBounds";
    });
    if (!bounds) {
        if (it != children_.end())
            children_.erase(it);
        return;
    }
    if (it == children_.end()) {
        children_.push_back(XmlNode::element("mxRectangle"));
        it = std::prev(children_.end());
        it->attributes.setOrdered("as", "alternateBounds", kRectOrder);
    }
    const auto set = [&](std::string_view name, double v) {
        if (v == 0 && !it->attributes.has(name))
            return; // draw.io omits zero values
        it->attributes.setOrdered(name, formatNumber(v), kRectOrder);
    };
    set("x", bounds->x);
    set("y", bounds->y);
    set("width", bounds->width);
    set("height", bounds->height);
}

std::vector<Point> Geometry::points() const
{
    std::vector<Point> out;
    const XmlNode* array = childWithRole("points");
    if (!array || array->name != "Array")
        return out;
    for (const auto& c : array->children) {
        if (c.isElement("mxPoint"))
            out.push_back(pointFromXml(c));
    }
    return out;
}

void Geometry::setPoints(const std::vector<Point>& points)
{
    auto it = std::find_if(children_.begin(), children_.end(), [](const XmlNode& c) {
        return c.isElement("Array") && c.attributes.value("as") == "points";
    });
    if (points.empty()) {
        if (it != children_.end())
            children_.erase(it);
        return;
    }
    if (it == children_.end()) {
        children_.push_back(XmlNode::element("Array", {{"as", "points"}}));
        it = std::prev(children_.end());
    }
    it->children.clear();
    for (const auto& p : points)
        it->children.push_back(pointToXml(p));
}

} // namespace qraw::core
