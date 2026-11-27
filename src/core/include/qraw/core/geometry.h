#pragma once

#include "qraw/core/attributes.h"
#include "qraw/core/xml_node.h"

#include <optional>
#include <string_view>
#include <vector>

namespace qraw::core {

struct Point {
    double x = 0;
    double y = 0;
    friend bool operator==(const Point&, const Point&) = default;
};

struct Rect {
    double x = 0;
    double y = 0;
    double width = 0;
    double height = 0;
    friend bool operator==(const Rect&, const Rect&) = default;
};

// An <mxGeometry> element.
//
// The ordered attributes and the child elements (<mxPoint as="..."/>,
// <Array as="points">, anything unknown) are the source of truth; the typed
// accessors read and edit them in place. Values that are never touched keep
// their original text (e.g. "10.50"), which keeps saving lossless.
class Geometry {
public:
    // A new geometry with `as="geometry"`, as draw.io writes it.
    static Geometry create();
    static Geometry create(const Rect& bounds);

    [[nodiscard]] double x() const;
    [[nodiscard]] double y() const;
    [[nodiscard]] double width() const;
    [[nodiscard]] double height() const;
    [[nodiscard]] Rect bounds() const { return {x(), y(), width(), height()}; }
    void setX(double v);
    void setY(double v);
    void setWidth(double v);
    void setHeight(double v);
    void setBounds(const Rect& r);

    // Edge labels and children of edges use relative coordinates.
    [[nodiscard]] bool relative() const;
    void setRelative(bool relative);

    [[nodiscard]] std::optional<Point> offset() const { return namedPoint("offset"); }
    [[nodiscard]] std::optional<Point> sourcePoint() const { return namedPoint("sourcePoint"); }
    [[nodiscard]] std::optional<Point> targetPoint() const { return namedPoint("targetPoint"); }
    void setOffset(std::optional<Point> p) { setNamedPoint("offset", p); }
    void setSourcePoint(std::optional<Point> p) { setNamedPoint("sourcePoint", p); }
    void setTargetPoint(std::optional<Point> p) { setNamedPoint("targetPoint", p); }

    // Edge waypoints (<Array as="points">).
    // The <mxRectangle as="alternateBounds"> child (the size a collapsed
    // container swaps with, mxGeometry.alternateBounds); nullopt when absent.
    // The setter edits in place (unknown attributes survive) or appends the
    // child; nullopt removes it.
    [[nodiscard]] std::optional<Rect> alternateBounds() const;
    void setAlternateBounds(std::optional<Rect> bounds);

    [[nodiscard]] std::vector<Point> points() const;
    // Replaces the waypoints. An empty list removes the <Array>.
    void setPoints(const std::vector<Point>& points);

    [[nodiscard]] Attributes& attributes() { return attributes_; }
    [[nodiscard]] const Attributes& attributes() const { return attributes_; }
    [[nodiscard]] std::vector<XmlNode>& children() { return children_; }
    [[nodiscard]] const std::vector<XmlNode>& children() const { return children_; }

    friend bool operator==(const Geometry&, const Geometry&) = default;

private:
    void setNumber(std::string_view name, double v);
    [[nodiscard]] const XmlNode* childWithRole(std::string_view role) const;
    [[nodiscard]] std::optional<Point> namedPoint(std::string_view role) const;
    void setNamedPoint(std::string_view role, std::optional<Point> p);

    Attributes attributes_;
    std::vector<XmlNode> children_;
};

// Reads x/y of an <mxPoint> element (missing values are 0).
[[nodiscard]] Point pointFromXml(const XmlNode& node);
// Builds an <mxPoint> element; `role` becomes the `as` attribute when set.
[[nodiscard]] XmlNode pointToXml(const Point& p, std::string_view role = {});

} // namespace qraw::core
