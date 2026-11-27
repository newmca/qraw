#pragma once

// The effective style of one cell after stylesheet resolution, with typed
// accessors. Defaults below are draw.io's (mxConstants, mxShape, mxText);
// each accessor names its style key.

#include "qraw/core/number.h"
#include "qraw/core/shared_value.h"
#include "qraw/geometry/label_layout.h"
#include "qraw/geometry/perimeter.h"
#include "qraw/geometry/polyline.h"
#include "qraw/geometry/routing.h"
#include "qraw/render/color.h"

#include <QColor>

#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace qraw::render {

// Flat key -> value map after resolution. Values are the raw style strings;
// a large one (an embedded image) is a shared buffer, so copying the map or
// resolving a style from the tokens of a cell never copies it.
class StyleMap {
public:
    using Map = std::map<std::string, core::SharedValue, std::less<>>;

    [[nodiscard]] const std::string* find(std::string_view key) const
    {
        const auto it = values_.find(key);
        return it == values_.end() ? nullptr : &it->second.str();
    }
    // The value with its sharing handle, or nullptr.
    [[nodiscard]] const core::SharedValue* findShared(std::string_view key) const
    {
        const auto it = values_.find(key);
        return it == values_.end() ? nullptr : &it->second;
    }
    [[nodiscard]] bool has(std::string_view key) const { return values_.find(key) != values_.end(); }
    void set(std::string_view key, std::string_view value) { values_.insert_or_assign(std::string(key), core::SharedValue(value)); }
    void setShared(std::string_view key, const core::SharedValue& value)
    {
        values_.insert_or_assign(std::string(key), value);
    }
    void erase(std::string_view key)
    {
        if (const auto it = values_.find(key); it != values_.end())
            values_.erase(it);
    }
    // Copies every entry of `other` over this map (named-style merge).
    void merge(const StyleMap& other)
    {
        for (const auto& [k, v] : other.values_)
            values_.insert_or_assign(k, v);
    }
    [[nodiscard]] const Map& values() const { return values_; }
    [[nodiscard]] bool empty() const { return values_.empty(); }
    friend bool operator==(const StyleMap&, const StyleMap&) = default;

private:
    Map values_;
};

// mxConstants font style bits (`fontStyle`).
enum FontStyleBit : int {
    FontBold = 1,
    FontItalic = 2,
    FontUnderline = 4,
    FontStrikethrough = 8,
};

// draw.io defaults that are not in the stylesheet.
inline constexpr double kDefaultFontSize = 11;          // mxConstants.DEFAULT_FONTSIZE
inline constexpr std::string_view kDefaultFontFamily = "Arial,Helvetica"; // mxConstants.DEFAULT_FONTFAMILY
inline constexpr double kDefaultMarkerSize = 6;         // mxConstants.DEFAULT_MARKERSIZE
inline constexpr double kRectangleRoundingFactor = 0.15; // mxConstants.RECTANGLE_ROUNDING_FACTOR
inline constexpr double kDefaultStartSize = 40;         // mxConstants.DEFAULT_STARTSIZE
inline constexpr double kShadowOpacity = 0.25;          // Graph.js:140
inline constexpr double kShadowOffsetX = 2;             // mxConstants.SHADOW_OFFSET_X
inline constexpr double kShadowOffsetY = 3;             // mxConstants.SHADOW_OFFSET_Y
inline constexpr double kLineHeight = 1.2;              // mxConstants.LINE_HEIGHT

// The resolved style. Copyable value type; string_view results point into
// this object and live as long as it does.
//
// Colors: "none", "" and absence all mean "no color" (nullopt). "default"
// never reaches this class: Stylesheet::resolve replaces it with the theme
// colors. Unparsable colors are treated as absent.
class ResolvedStyle {
public:
    ResolvedStyle() = default;
    ResolvedStyle(StyleMap values, bool edge) : values_(std::move(values)), edge_(edge) {}

    [[nodiscard]] const StyleMap& map() const { return values_; }
    [[nodiscard]] StyleMap& map() { return values_; }
    [[nodiscard]] bool isEdge() const { return edge_; }

    // --- raw access -----------------------------------------------------
    [[nodiscard]] bool has(std::string_view key) const { return values_.has(key); }
    [[nodiscard]] std::string_view string(std::string_view key, std::string_view fallback = {}) const
    {
        const std::string* v = values_.find(key);
        return v ? std::string_view(*v) : fallback;
    }
    // Absent or unparsable -> fallback.
    [[nodiscard]] double number(std::string_view key, double fallback) const
    {
        const std::string* v = values_.find(key);
        return v ? core::parseNumber(*v, fallback) : fallback;
    }
    // "1" and "true" are true, every other present value is false.
    [[nodiscard]] bool flag(std::string_view key, bool fallback) const
    {
        const std::string* v = values_.find(key);
        if (!v)
            return fallback;
        return *v == "1" || *v == "true";
    }
    [[nodiscard]] std::optional<QColor> color(std::string_view key) const
    {
        const std::string* v = values_.find(key);
        if (!v || v->empty() || *v == "none")
            return std::nullopt;
        const QColor c = parseStyleColor(*v, QColor());
        if (!c.isValid())
            return std::nullopt;
        return c;
    }

    // --- shape ------------------------------------------------------------
    [[nodiscard]] std::string_view shape() const { return string("shape"); }
    [[nodiscard]] std::string_view perimeter() const { return string("perimeter"); }
    [[nodiscard]] geometry::Direction direction() const { return geometry::parseDirection(string("direction")); }
    [[nodiscard]] double rotation() const { return number("rotation", 0); }
    [[nodiscard]] bool flipH() const { return flag("flipH", false); }
    [[nodiscard]] bool flipV() const { return flag("flipV", false); }
    // Shape-specific relative or absolute size parameter (`size`).
    [[nodiscard]] double size(double fallback) const { return number("size", fallback); }
    [[nodiscard]] bool fixedSize() const { return flag("fixedSize", false); }

    // --- fill / stroke ----------------------------------------------------
    [[nodiscard]] std::optional<QColor> fillColor() const { return color("fillColor"); }
    [[nodiscard]] std::optional<QColor> gradientColor() const { return color("gradientColor"); }
    // `gradientDirection`: south (default), north, east, west, radial.
    [[nodiscard]] std::string_view gradientDirection() const { return string("gradientDirection", "south"); }
    [[nodiscard]] std::optional<QColor> strokeColor() const { return color("strokeColor"); }
    [[nodiscard]] double strokeWidth() const { return number("strokeWidth", 1); }
    [[nodiscard]] bool dashed() const { return flag("dashed", false); }
    // `dashPattern`, space-separated multiples of the stroke width; empty = "3 3".
    [[nodiscard]] std::string_view dashPattern() const { return string("dashPattern"); }
    [[nodiscard]] bool fixDash() const { return flag("fixDash", false); }
    // 0..100
    [[nodiscard]] double opacity() const { return number("opacity", 100); }
    [[nodiscard]] double fillOpacity() const { return number("fillOpacity", 100); }
    [[nodiscard]] double strokeOpacity() const { return number("strokeOpacity", 100); }
    [[nodiscard]] double textOpacity() const { return number("textOpacity", 100); }
    [[nodiscard]] bool rounded() const { return flag("rounded", false); }
    // `arcSize`: percent of the smaller side for vertices (default 15 =
    // RECTANGLE_ROUNDING_FACTOR * 100), absolute for edges (default 20 =
    // LINE_ARCSIZE) and with absoluteArcSize=1 (then default 20... see
    // mxShape.getArcSize, mxgraph/src/shape/mxShape.js:1424).
    [[nodiscard]] double arcSize() const
    {
        return number("arcSize", edge_ ? geometry::kLineArcSize : kRectangleRoundingFactor * 100);
    }
    [[nodiscard]] bool absoluteArcSize() const { return flag("absoluteArcSize", false); }
    [[nodiscard]] bool shadow() const { return flag("shadow", false); }
    [[nodiscard]] bool glass() const { return flag("glass", false); }
    [[nodiscard]] std::string_view lineCap() const { return string("linecap"); }   // flat|round|square
    [[nodiscard]] std::string_view lineJoin() const { return string("linejoin"); } // miter|round|bevel

    // --- text -------------------------------------------------------------
    [[nodiscard]] std::string_view fontFamily() const { return string("fontFamily", kDefaultFontFamily); }
    [[nodiscard]] double fontSize() const { return number("fontSize", kDefaultFontSize); }
    // Bitmask of FontStyleBit.
    [[nodiscard]] int fontStyle() const { return static_cast<int>(number("fontStyle", 0)); }
    [[nodiscard]] std::optional<QColor> fontColor() const { return color("fontColor"); }
    [[nodiscard]] std::optional<QColor> labelBackgroundColor() const { return color("labelBackgroundColor"); }
    [[nodiscard]] std::optional<QColor> labelBorderColor() const { return color("labelBorderColor"); }
    [[nodiscard]] double labelPadding() const { return number("labelPadding", 0); }
    // `html=1`: the value is an HTML fragment.
    [[nodiscard]] bool html() const { return flag("html", false); }
    // `whiteSpace=wrap`: wrap at the label box width.
    [[nodiscard]] bool wrap() const { return string("whiteSpace") == "wrap"; }
    [[nodiscard]] bool horizontal() const { return flag("horizontal", true); }
    [[nodiscard]] bool noLabel() const { return flag("noLabel", false); }
    [[nodiscard]] geometry::Overflow overflow() const { return geometry::parseOverflow(string("overflow")); }
    [[nodiscard]] geometry::LabelParams labelParams() const
    {
        geometry::LabelParams p;
        p.align = geometry::parseHAlign(string("align"), geometry::HAlign::Center);
        p.verticalAlign = geometry::parseVAlign(string("verticalAlign"), geometry::VAlign::Middle);
        p.labelPosition = geometry::parseHAlign(string("labelPosition"), geometry::HAlign::Center);
        p.verticalLabelPosition = geometry::parseVAlign(string("verticalLabelPosition"), geometry::VAlign::Middle);
        p.spacing = number("spacing", 2);
        p.spacingTop = number("spacingTop", 0);
        p.spacingLeft = number("spacingLeft", 0);
        p.spacingBottom = number("spacingBottom", 0);
        p.spacingRight = number("spacingRight", 0);
        if (has("labelWidth"))
            p.labelWidth = number("labelWidth", 0);
        p.horizontal = horizontal();
        p.rotation = edge_ ? 0 : rotation();
        p.overflow = overflow();
        p.blockSpacing = flag("blockSpacing", false);
        return p;
    }

    // --- images (shape=image, shape=label) ---------------------------------
    [[nodiscard]] std::string_view image() const { return string("image"); }
    // The `image` value with its sharing handle (content hash memoised), or nullptr.
    [[nodiscard]] const core::SharedValue* imageValue() const { return values_.findShared("image"); }
    [[nodiscard]] double imageWidth(double fallback) const { return number("imageWidth", fallback); }
    [[nodiscard]] double imageHeight(double fallback) const { return number("imageHeight", fallback); }
    [[nodiscard]] bool imageAspect() const { return flag("imageAspect", true); }
    [[nodiscard]] std::optional<QColor> imageBackground() const { return color("imageBackground"); }
    [[nodiscard]] std::optional<QColor> imageBorder() const { return color("imageBorder"); }

    // --- containers ---------------------------------------------------------
    // `startSize` of swimlanes/tables (title size); default 40 (DEFAULT_STARTSIZE).
    [[nodiscard]] double startSize() const { return number("startSize", kDefaultStartSize); }
    [[nodiscard]] std::optional<QColor> swimlaneFillColor() const { return color("swimlaneFillColor"); }
    [[nodiscard]] bool swimlaneLine() const { return flag("swimlaneLine", true); }
    [[nodiscard]] bool container() const { return flag("container", false); }

    // --- edges --------------------------------------------------------------
    // Marker names (`startArrow`/`endArrow`); "" and "none" mean no marker.
    [[nodiscard]] std::string_view startArrow() const { return string("startArrow"); }
    [[nodiscard]] std::string_view endArrow() const { return string("endArrow"); }
    // NB: for vertices `startSize` is the swimlane title size (startSize()).
    [[nodiscard]] double startMarkerSize() const { return number("startSize", kDefaultMarkerSize); }
    [[nodiscard]] double endMarkerSize() const { return number("endSize", kDefaultMarkerSize); }
    [[nodiscard]] bool startFill() const { return flag("startFill", true); }
    [[nodiscard]] bool endFill() const { return flag("endFill", true); }
    [[nodiscard]] geometry::EdgeStyleKind edgeStyle() const { return geometry::parseEdgeStyle(string("edgeStyle")); }
    [[nodiscard]] geometry::ElbowAxis elbow() const
    {
        return string("elbow") == "vertical" ? geometry::ElbowAxis::Vertical : geometry::ElbowAxis::Horizontal;
    }
    [[nodiscard]] bool curved() const { return flag("curved", false); }
    // Line jumps (Graph.js): jumpStyle none|arc|gap|sharp|line, jumpSize
    // (default Graph.defaultJumpSize = 6), noJump (other edges do not jump
    // over this one), jumpLayers=0 (jump only edges of the same layer).
    [[nodiscard]] std::string_view jumpStyle() const { return string("jumpStyle", "none"); }
    [[nodiscard]] double jumpSize() const { return number("jumpSize", 6); }
    [[nodiscard]] bool noJump() const { return flag("noJump", false); }
    [[nodiscard]] bool jumpAcrossLayers() const { return flag("jumpLayers", true); }
    // `jettySize`; nullopt for "auto" or absent.
    [[nodiscard]] std::optional<double> jettySize(std::string_view key = "jettySize") const
    {
        const std::string* v = values_.find(key);
        if (!v || *v == "auto")
            return std::nullopt;
        return core::parseNumber(*v);
    }
    [[nodiscard]] double perimeterSpacing() const { return number("perimeterSpacing", 0); }
    [[nodiscard]] double sourcePerimeterSpacing() const { return number("sourcePerimeterSpacing", 0); }
    [[nodiscard]] double targetPerimeterSpacing() const { return number("targetPerimeterSpacing", 0); }
    // exitX/exitY[/exitDx/exitDy/exitPerimeter] (source) or entry* (target).
    // nullopt unless both X and Y are present.
    [[nodiscard]] std::optional<geometry::ConnectionConstraint> constraint(bool source) const
    {
        const std::string_view p = source ? "exit" : "entry";
        const auto key = [&](std::string_view suffix) { return std::string(p) + std::string(suffix); };
        if (!has(key("X")) || !has(key("Y")))
            return std::nullopt;
        geometry::ConnectionConstraint c;
        c.point = {number(key("X"), 0), number(key("Y"), 0)};
        c.dx = number(key("Dx"), 0);
        c.dy = number(key("Dy"), 0);
        c.perimeter = flag(key("Perimeter"), true);
        return c;
    }

    friend bool operator==(const ResolvedStyle&, const ResolvedStyle&) = default;

private:
    StyleMap values_;
    bool edge_ = false;
};

} // namespace qraw::render
