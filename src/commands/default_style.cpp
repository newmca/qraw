// Port of the key lists of js/grapheditor/Graph.js (draw.io, Apache-2.0) and of
// Graph.copyCellStyles / pasteCellStyles, modified: see default_style.h.
#include "qraw/commands/default_style.h"

#include <algorithm>
#include <array>
#include <string_view>

namespace qraw::commands {

namespace {

using core::Style;

// Graph.textStyles
constexpr std::array kTextStyles = {
    std::string_view("fontFamily"),   std::string_view("fontSource"),          std::string_view("fontSize"),
    std::string_view("fontColor"),    std::string_view("fontStyle"),           std::string_view("textOpacity"),
    std::string_view("labelBorderColor"), std::string_view("labelBorderWidth"), std::string_view("labelBackgroundColor"),
    std::string_view("defaultLabelBackgroundColor"), std::string_view("autosize"), std::string_view("resizable"),
    std::string_view("horizontal"),   std::string_view("textDirection"),       std::string_view("autosizeText"),
};

// Graph.edgeStyles
constexpr std::array kEdgeStyles = {
    std::string_view("edgeStyle"), std::string_view("elbow"), std::string_view("jumpStyle"),
    std::string_view("jumpSize"), std::string_view("startArrow"), std::string_view("startFill"),
    std::string_view("startSize"), std::string_view("endArrow"), std::string_view("endFill"),
    std::string_view("endSize"), std::string_view("flowAnimation"), std::string_view("flowAnimationDirection"),
    std::string_view("flowAnimationTimingFunction"), std::string_view("flowAnimationDuration"),
    std::string_view("flowAnimationType"), std::string_view("flowAnimationSize"), std::string_view("flowAnimationCount"),
    std::string_view("flowAnimationLoop"), std::string_view("sourcePerimeterSpacing"),
    std::string_view("targetPerimeterSpacing"), std::string_view("fixedPointSpacing"), std::string_view("curved"),
    std::string_view("curveGeometry"), std::string_view("trimCurve"), std::string_view("linecap"),
    std::string_view("linejoin"), std::string_view("libavoidRouting"), std::string_view("libavoidSpacing"),
    std::string_view("followTerminals"), std::string_view("labelPlacement"),
};

// The keys of Graph.cellStyles that are not text or edge styles.
constexpr std::array kShapeStyles = {
    std::string_view("rounded"), std::string_view("shadow"), std::string_view("glass"), std::string_view("dashed"),
    std::string_view("dashPattern"), std::string_view("comic"), std::string_view("sketch"),
    std::string_view("fillWeight"), std::string_view("hachureGap"), std::string_view("hachureAngle"),
    std::string_view("jiggle"), std::string_view("disableMultiStroke"), std::string_view("disableMultiStrokeFill"),
    std::string_view("fillStyle"), std::string_view("curveFitting"), std::string_view("simplification"),
    std::string_view("sketchStyle"), std::string_view("pointerEvents"), std::string_view("opacity"),
    std::string_view("strokeColor"), std::string_view("strokeWidth"), std::string_view("align"),
    std::string_view("verticalAlign"), std::string_view("spacingLeft"), std::string_view("spacingRight"),
    std::string_view("spacingTop"), std::string_view("spacingBottom"), std::string_view("spacing"),
    std::string_view("arcSize"), std::string_view("absoluteArcSize"), std::string_view("comicStyle"),
    std::string_view("swimlaneFillColor"), std::string_view("shadowOffsetX"), std::string_view("shadowOffsetY"),
    std::string_view("shadowBlur"), std::string_view("shadowColor"), std::string_view("shadowOpacity"),
    std::string_view("multiple"), std::string_view("multipleCount"), std::string_view("multipleDx"),
    std::string_view("multipleDy"),
    // The members of Graph.cellStyleGroups that are in no other list.
    std::string_view("fillColor"), std::string_view("gradientColor"), std::string_view("gradientDirection"),
    std::string_view("roundedPerimeter"), std::string_view("opacityGroup"),
};

// Graph.cellStyleGroups: when an entry defines one of them, none is applied.
constexpr std::array<std::array<std::string_view, 4>, 6> kGroups = {{
    {"startArrow", "startFill", "endArrow", "endFill"},
    {"startSize", "endSize", "", ""},
    {"sourcePerimeterSpacing", "targetPerimeterSpacing", "fixedPointSpacing", ""},
    {"fillColor", "gradientColor", "gradientDirection", ""},
    {"rounded", "roundedPerimeter", "", ""},
    {"opacity", "opacityGroup", "", ""},
}};

// Graph.createCurrentEdgeStyle: what a new connector takes from the edge default.
constexpr std::array kConnectorKeys = {
    std::string_view("edgeStyle"), std::string_view("shape"), std::string_view("curved"), std::string_view("rounded"),
    std::string_view("comic"), std::string_view("sketch"), std::string_view("fillWeight"),
    std::string_view("hachureGap"), std::string_view("hachureAngle"), std::string_view("jiggle"),
    std::string_view("disableMultiStroke"), std::string_view("disableMultiStrokeFill"), std::string_view("fillStyle"),
    std::string_view("curveFitting"), std::string_view("simplification"), std::string_view("comicStyle"),
    std::string_view("jumpStyle"), std::string_view("jumpSize"), std::string_view("libavoidRouting"),
    std::string_view("libavoidSpacing"),
};

template <typename Array>
bool contains(const Array& keys, std::string_view key)
{
    return std::find(keys.begin(), keys.end(), key) != keys.end();
}

// Copies one key of `cellStyle` into `target` (Graph.copyCellStyles, force).
void copyKey(Style& target, const Style& cellStyle, std::string_view key)
{
    if (const auto value = cellStyle.value(key))
        target.set(key, *value);
}

// The keys of the cell styles in Graph.cellStyles order of declaration.
template <typename Fn>
void forEachCellStyle(Fn&& fn)
{
    for (std::string_view key : kShapeStyles)
        fn(key);
    for (std::string_view key : kTextStyles)
        fn(key);
    for (std::string_view key : kEdgeStyles)
        fn(key);
}

// True when `entry` defines the key or another key of its group.
bool definedInEntry(const Style& entry, std::string_view key)
{
    if (entry.has(key))
        return true;
    for (const auto& group : kGroups) {
        if (!contains(group, key))
            continue;
        for (std::string_view member : group) {
            if (!member.empty() && entry.has(member))
                return true;
        }
    }
    return false;
}

} // namespace

void setDefaultFrom(DefaultStyles& defaults, const Style& cellStyle, StyleTarget target)
{
    auto copyCellStyles = [&](Style& into, bool textOnly, bool withShape) {
        forEachCellStyle([&](std::string_view key) {
            if (textOnly && !contains(kTextStyles, key))
                return;
            // Ignores transparent stroke colors.
            if (key == "strokeColor" && cellStyle.value(key, "none") == "none")
                return;
            copyKey(into, cellStyle, key);
        });
        if (withShape)
            copyKey(into, cellStyle, "shape");
    };

    switch (target) {
    case StyleTarget::Vertex:
        defaults.vertex = Style{};
        copyCellStyles(defaults.vertex, false, false);
        defaults.vertexFromText = cellStyle.hasName("text");
        break;
    case StyleTarget::Edge:
        defaults.edge = Style{};
        copyCellStyles(defaults.edge, false, true);
        break;
    case StyleTarget::EdgeLabel:
        copyCellStyles(defaults.edge, true, false);
        break;
    }
}

Style applyDefaultStyle(const Style& entry, const DefaultStyles& defaults, StyleTarget target, InsertKind kind)
{
    const bool edge = target == StyleTarget::Edge;
    const Style& current = target == StyleTarget::Vertex ? defaults.vertex : defaults.edge;
    if (current.empty())
        return entry;

    Style result = entry;
    if (kind == InsertKind::Connector) {
        // createCurrentEdgeStyle builds the connector from the default: the
        // connector keys it does not have are not kept, and no edgeStyle is none.
        for (std::string_view key : kConnectorKeys) {
            if (!current.has(key))
                result.remove(key);
        }
        if (!current.has("edgeStyle"))
            result.set("edgeStyle", "none");
    }
    const bool isText = entry.hasName("text");
    const bool onlyText = target == StyleTarget::EdgeLabel || (isText && !defaults.vertexFromText);
    // Text cells and edge labels take their text styles whatever the entry defines.
    const bool takesAll = isText || target == StyleTarget::EdgeLabel;

    forEachCellStyle([&](std::string_view key) {
        const std::optional<std::string> value = current.value(key);
        if (!value)
            return;
        if (onlyText && !contains(kTextStyles, key))
            return;
        // Keys of edges are never assigned to vertices.
        if (!edge && target != StyleTarget::EdgeLabel && contains(kEdgeStyles, key))
            return;
        // Keys the entry defines are kept, except that a connector takes the
        // connector keys of the default over its own.
        const bool replaces = kind == InsertKind::Connector && contains(kConnectorKeys, key);
        if (!takesAll && !replaces && definedInEntry(entry, key))
            return;
        result.set(key, *value);
        // A font family replaces the web font of the entry unless the default has one.
        if (key == "fontFamily" && !current.has("fontSource"))
            result.remove("fontSource");
        // Rounded edges are no longer curved.
        if (edge && key == "rounded" && *value == "1" && !current.has("curved"))
            result.remove("curved");
    });
    if (edge && kind == InsertKind::Connector) {
        if (const auto shape = current.value("shape"))
            result.set("shape", *shape);
    }
    return result;
}

} // namespace qraw::commands
