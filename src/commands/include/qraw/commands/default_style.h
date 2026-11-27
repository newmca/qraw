#pragma once
// The default styles of "Set as Default Style" (draw.io's graph.currentVertexStyle
// and currentEdgeStyle) as pure functions on styles: which keys a cell
// contributes, and how new cells take them. Plain C++, no Qt: the UI stores
// the value (AppSettings) and calls this when it creates cells from nothing
// (palette, connectors, text); M9's Insert actions use the same helper.
//
// Port of the key lists of Graph.js (Graph.cellStyles, textStyles, edgeStyles,
// cellStyleGroups), copyCellStyles (:11260) with force=true and pasteCellStyles
// (:11362) (draw.io, Apache-2.0), modified: the values are kept apart from the
// graph, and the "styleChanged" tracking of the last edited style (not only
// Set as Default Style) is not ported (COMPATIBILITY.md).

#include "qraw/core/style.h"

namespace qraw::commands {

// What a cell is, for the purposes of default styles.
enum class StyleTarget {
    Vertex,
    Edge,
    EdgeLabel, // a text or edgeLabel vertex whose parent is an edge: takes the edge's text styles
};

struct DefaultStyles {
    core::Style vertex;
    core::Style edge;
    // The vertex default was set from a text cell: all of its styles then apply
    // to inserted text cells, otherwise only the text styles do
    // (graph.pasteStylesToText).
    bool vertexFromText = false;

    [[nodiscard]] bool empty() const { return vertex.empty() && edge.empty(); }
    friend bool operator==(const DefaultStyles&, const DefaultStyles&) = default;
};

// Set as Default Style (EditorUi.setDefaultStyle): replaces the default of the
// kind of the cell with the style keys `cellStyle` carries that are copied
// (Graph.cellStyles; never `shape` on vertices, never a transparent
// strokeColor). An EdgeLabel only merges its text styles into the edge default.
void setDefaultFrom(DefaultStyles& defaults, const core::Style& cellStyle, StyleTarget target);

// Clear Default Style (EditorUi.clearDefaultStyle).
inline void clearDefaults(DefaultStyles& defaults) { defaults = DefaultStyles{}; }

// How the style of a new cell meets the defaults.
enum class InsertKind {
    Template, // palette entry: a style key the entry defines is kept
    Connector, // a new connector: the keys that Graph.createCurrentEdgeStyle takes
               // (edgeStyle, curved, rounded...) come from the defaults alone
};

// The style of a new cell: `entry` with the defaults applied
// (pasteCellStyles without force). Keys that `entry` defines are kept, and so
// are the other keys of their group (fillColor, gradientColor and
// gradientDirection go together). Text cells (`text` in the style) only take
// text styles, unless the vertex default came from a text cell. Keys of edges
// are never assigned to vertices.
[[nodiscard]] core::Style applyDefaultStyle(const core::Style& entry, const DefaultStyles& defaults, StyleTarget target,
                                            InsertKind kind = InsertKind::Template);

} // namespace qraw::commands
