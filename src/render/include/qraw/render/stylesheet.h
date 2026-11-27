#pragma once

// draw.io stylesheets (<mxStylesheet>) and cell style resolution
// (mxStylesheet.getCellStyle + Graph.postProcessCellStyle).

#include "qraw/core/diagnostic.h"
#include "qraw/core/style.h"
#include "qraw/render/resolved_style.h"

#include <QByteArrayView>
#include <QColor>

#include <map>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::render {

// The scheme a diagram is shown in: the two colors `default` resolves to
// (Graph.shapeForegroundColor / shapeBackgroundColor) and how explicit colors
// follow the scheme. Export uses the light scheme unless asked for the dark
// one, as draw.io does.
struct Theme {
    QColor foreground = QColor(0, 0, 0);
    QColor background = QColor(255, 255, 255);
    bool dark = false; // picks the second color of light-dark(a, b)
    // The page's adaptiveColors (see forPage); decides what happens to explicit colors when dark.
    AdaptiveColors adaptive = AdaptiveColors::Auto;

    // The dark scheme: Editor.lightColor (#ffffff) text and lines on Editor.darkColor (#121212).
    [[nodiscard]] static Theme darkScheme();
    // The scheme of a page with this adaptiveColors attribute (nullopt: absent, so Auto). A dark
    // theme turns into the light scheme for a page that says None.
    [[nodiscard]] Theme forPage(std::optional<AdaptiveColors> setting) const;
    // The color an explicit (light) color has in this scheme: inverted by
    // dark Auto, unchanged otherwise (mxUtils.getLightDarkColor with a single color).
    [[nodiscard]] QColor adapt(const QColor& color) const;
    // The paper color of a page with this mxGraphModel@background (empty or "none": the scheme's background).
    [[nodiscard]] QColor pageBackground(std::string_view backgroundValue) const;

    friend bool operator==(const Theme&, const Theme&) = default;
};

enum class CellKind { Vertex, Edge };

struct ResolveOptions {
    Theme theme;
    // For a vertex whose parent is an edge (an edge label cell): the
    // resolved style of that edge, for Graph.resolveInheritedLabelStyles.
    // nullptr otherwise.
    const ResolvedStyle* parentEdgeStyle = nullptr;
};

class Stylesheet {
public:
    // draw.io's default stylesheet (resources/styles/default.xml, compiled
    // in as :/qraw/styles/default.xml). Parsed once, thread-safe. If the
    // resource is missing or broken this is a built-in copy of
    // defaultVertex and defaultEdge (never empty).
    [[nodiscard]] static const Stylesheet& drawioDefault();

    // Parses an <mxStylesheet> document (mxStylesheetCodec semantics):
    //   <add as="name" [extend="base"]> <add as="key" value="v"/>
    //   <remove as="key"/> </add>
    // `extend` starts from a copy of the already defined `base` style.
    // Entries without a `value` attribute (upstream evaluates their text as
    // JS) are skipped with a warning. Returns nullopt (with an error
    // diagnostic) when the root is not <mxStylesheet> or the XML is malformed.
    [[nodiscard]] static std::optional<Stylesheet> fromXml(QByteArrayView xml,
                                                           std::vector<core::Diagnostic>* diagnostics = nullptr);

    // A named style ("defaultVertex", "text", "ellipse", ...), or nullptr.
    [[nodiscard]] const StyleMap* named(std::string_view name) const;
    [[nodiscard]] std::vector<std::string> names() const;
    void setNamed(std::string name, StyleMap style);

    // Resolves a cell style:
    //  1. mxStylesheet.getCellStyle (mxgraph/src/view/mxStylesheet.js:217):
    //     start from a copy of defaultVertex/defaultEdge (an empty map when
    //     the style text starts with ';'); apply tokens left to right:
    //     key=value sets the key, key=none erases it; a bare token merges the
    //     named style of that name (unknown names are ignored).
    //  2. Graph.resolveInheritedLabelStyles (js/grapheditor/Graph.js:18538)
    //     when options.parentEdgeStyle is set: the keys listed by the cell's
    //     `inheritLabelStyle` value (1 = all of Graph.inheritedLabelStyles,
    //     else a comma list) plus keys whose value is "inherit" are copied
    //     from the edge (fontFamily also copies fontSource) or erased when
    //     the edge lacks them; inheritLabelStyle itself is erased.
    //  3. Graph.replaceDefaultColors (Graph.js:18812): value "default" of
    //     fontColor, strokeColor, gradientColor, imageBorder,
    //     labelBorderColor -> theme.foreground; fillColor, imageBackground,
    //     swimlaneFillColor, labelBackgroundColor -> theme.background
    //     (written as "#rrggbb"). default<Key>=invert inverts that color.
    //  4. In a dark theme with adaptive Auto (before 3.), every explicit color
    //     value of a color key (fillColor, strokeColor, fontColor, ...) is
    //     inverted (inverseColor); Simple keeps them. An absent fontColor is
    //     the inverted black then.
    //  5. Any color value "light-dark(a, b)" becomes a (or b when theme.dark).
    [[nodiscard]] ResolvedStyle resolve(const core::Style& style, CellKind kind, const ResolveOptions& options = {}) const;
    [[nodiscard]] ResolvedStyle resolve(std::string_view styleText, CellKind kind,
                                        const ResolveOptions& options = {}) const
    {
        return resolve(core::Style(styleText), kind, options);
    }

private:
    std::map<std::string, StyleMap, std::less<>> styles_;
};

} // namespace qraw::render
