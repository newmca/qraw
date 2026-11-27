// Port of mxgraph/src/io/mxStylesheetCodec.js (draw.io, Apache-2.0), modified.
// Port of mxgraph/src/view/mxStylesheet.js (draw.io, Apache-2.0), modified.
// Port of js/grapheditor/Graph.js (draw.io, Apache-2.0), modified.

#include "qraw/render/stylesheet.h"

#include "qraw/format/xml_io.h"

#include <QColor>
#include <QFile>
#include <QString>

#include <array>
#include <cstddef>
#include <initializer_list>
#include <utility>
#include <vector>

namespace qraw::render {
namespace {

using core::Diagnostic;
using core::XmlNode;
using Severity = Diagnostic::Severity;

constexpr std::string_view kDefaultVertex = "defaultVertex";
constexpr std::string_view kDefaultEdge = "defaultEdge";

// Graph.inheritedLabelStyles (Graph.js:2069).
constexpr std::array<std::string_view, 10> kInheritedLabelStyles = {
    "fontFamily",       "fontSize",         "fontColor",         "fontStyle",
    "textOpacity",      "labelBorderColor", "labelBorderWidth",  "labelBackgroundColor",
    "defaultLabelBackgroundColor", "labelPadding",
};
// Graph.inheritLabelStyleKey (Graph.js:2078) is `inheritText`. The M3
// contract and its tests name `inheritLabelStyle`, so both keys are read and
// both are erased after resolution.
constexpr std::string_view kInheritTextKey = "inheritText";
constexpr std::string_view kInheritLabelStyleKey = "inheritLabelStyle";

void warn(std::vector<Diagnostic>* diagnostics, std::string message, int line)
{
    if (!diagnostics)
        return;
    Diagnostic d;
    d.severity = Severity::Warning;
    d.message = std::move(message);
    d.line = line;
    diagnostics->push_back(std::move(d));
}

StyleMap makeStyle(std::initializer_list<std::pair<std::string_view, std::string_view>> entries)
{
    StyleMap style;
    for (const auto& [key, value] : entries)
        style.set(key, value);
    return style;
}

// The defaultVertex and defaultEdge entries of resources/styles/default.xml,
// used when the vendored resource cannot be loaded.
Stylesheet builtinFallback()
{
    Stylesheet sheet;
    sheet.setNamed(std::string(kDefaultVertex),
                   makeStyle({{"shape", "label"},
                              {"perimeter", "rectanglePerimeter"},
                              {"fontSize", "12"},
                              {"fontFamily", "Helvetica"},
                              {"align", "center"},
                              {"verticalAlign", "middle"},
                              {"fillColor", "default"},
                              {"strokeColor", "default"},
                              {"fontColor", "default"}}));
    sheet.setNamed(std::string(kDefaultEdge),
                   makeStyle({{"shape", "connector"},
                              {"labelBackgroundColor", "default"},
                              {"endArrow", "classic"},
                              {"fontSize", "11"},
                              {"fontFamily", "Helvetica"},
                              {"align", "center"},
                              {"verticalAlign", "middle"},
                              {"rounded", "1"},
                              {"strokeColor", "default"},
                              {"fontColor", "default"}}));
    return sheet;
}

Stylesheet loadDrawioDefault()
{
    QFile file(QStringLiteral(":/qraw/styles/default.xml"));
    if (file.open(QIODevice::ReadOnly)) {
        const QByteArray xml = file.readAll();
        auto sheet = Stylesheet::fromXml(xml);
        if (sheet && sheet->named(kDefaultVertex) && sheet->named(kDefaultEdge))
            return std::move(*sheet);
    }
    return builtinFallback();
}

// mxStylesheetCodec: one <add as="name"> element.
StyleMap readNamedStyle(const Stylesheet& sheet, const XmlNode& add, std::vector<Diagnostic>* diagnostics)
{
    StyleMap style;
    if (const std::string* base = add.attributes.find("extend")) {
        if (const StyleMap* found = sheet.named(*base))
            style = *found;
        else
            warn(diagnostics, "style extends undefined style \"" + *base + "\"", add.sourceLine);
    }

    for (const XmlNode& entry : add.children) {
        if (!entry.isElement())
            continue;
        const std::string* key = entry.attributes.find("as");
        if (!key) {
            warn(diagnostics, "<" + entry.name + "> without as in style is ignored", entry.sourceLine);
            continue;
        }
        if (entry.name == "add") {
            const std::string* value = entry.attributes.find("value");
            if (!value) {
                // Upstream evaluates the element text as JavaScript; not supported.
                warn(diagnostics, "entry \"" + *key + "\" has no value and is skipped", entry.sourceLine);
                continue;
            }
            style.set(*key, *value);
        } else if (entry.name == "remove") {
            style.erase(*key);
        } else {
            warn(diagnostics, "<" + entry.name + "> in style is ignored", entry.sourceLine);
        }
    }
    return style;
}

// --- cell style resolution (mxStylesheet.getCellStyle) ----------------------

bool startsWithSemicolon(const core::Style& style)
{
    const auto& tokens = style.tokens();
    return !tokens.empty() && tokens.front().key.empty() && !tokens.front().hasValue;
}

StyleMap baseStyle(const core::Style& style, CellKind kind, const Stylesheet& sheet)
{
    if (startsWithSemicolon(style))
        return {};
    const StyleMap* base = sheet.named(kind == CellKind::Edge ? kDefaultEdge : kDefaultVertex);
    return base ? *base : StyleMap{};
}

void applyTokens(StyleMap& style, const std::vector<core::Style::Token>& tokens, const Stylesheet& sheet)
{
    for (const core::Style::Token& token : tokens) {
        if (token.key.empty())
            continue;
        if (token.hasValue) {
            if (token.value == "none")
                style.erase(token.key);
            else
                style.setShared(token.key, token.value);
        } else if (const StyleMap* named = sheet.named(token.key)) {
            style.merge(*named);
        }
    }
}

// --- inherited label styles (Graph.resolveInheritedLabelStyles) -------------

std::vector<std::string_view> inheritedLabelKeys(const StyleMap& style)
{
    std::vector<std::string_view> listed;
    for (std::string_view inheritKey : {kInheritTextKey, kInheritLabelStyleKey}) {
        const std::string* value = style.find(inheritKey);
        if (!value)
            continue;
        if (*value == "1") {
            listed.insert(listed.end(), kInheritedLabelStyles.begin(), kInheritedLabelStyles.end());
            continue;
        }
        std::string_view rest = *value;
        while (!rest.empty()) {
            const std::size_t comma = rest.find(',');
            listed.push_back(rest.substr(0, comma));
            if (comma == std::string_view::npos)
                break;
            rest.remove_prefix(comma + 1);
        }
    }

    std::vector<std::string_view> keys;
    for (std::string_view key : kInheritedLabelStyles) {
        bool inherit = false;
        for (std::string_view listedKey : listed)
            inherit = inherit || listedKey == key;
        const std::string* value = style.find(key);
        inherit = inherit || (value && *value == "inherit");
        if (inherit)
            keys.push_back(key);
    }
    return keys;
}

void inheritOne(StyleMap& style, const ResolvedStyle& parent, std::string_view key)
{
    const std::string* value = parent.map().find(key);
    if (value && *value != "none")
        style.setShared(key, *parent.map().findShared(key));
    else
        style.erase(key);
}

void resolveInheritedLabelStyles(StyleMap& style, const ResolvedStyle& parentEdge)
{
    // The keys are computed before any value changes, as upstream does.
    for (std::string_view key : inheritedLabelKeys(style)) {
        inheritOne(style, parentEdge, key);
        if (key == "fontFamily")
            inheritOne(style, parentEdge, "fontSource");
    }
    style.erase(kInheritTextKey);
    style.erase(kInheritLabelStyleKey);
}

// --- default colors (Graph.replaceDefaultColors) ---------------------------

std::string capitalizedDefaultKey(std::string_view key)
{
    char first = key.front();
    if (first >= 'a' && first <= 'z')
        first = static_cast<char>(first - 'a' + 'A');
    return "default" + std::string(1, first) + std::string(key.substr(1));
}

// Replaces `default` with `plain`, or with `inverted` when default<Key>=invert.
void replaceDefaultColor(StyleMap& style, std::string_view key, const QColor& plain, const QColor& inverted)
{
    const std::string* value = style.find(key);
    if (!value || *value != "default")
        return;
    const std::string* invert = style.find(capitalizedDefaultKey(key));
    const QColor& chosen = (invert && *invert == "invert") ? inverted : plain;
    style.set(key, chosen.name(QColor::HexRgb).toStdString());
}

// Graph.replaceLabelGap (Graph.js:18956).
void replaceLabelGap(StyleMap& style)
{
    const std::string* gap = style.find("defaultLabelBackgroundColor");
    if (!gap || (*gap != "page" && *gap != "gap"))
        return;
    const std::string* background = style.find("labelBackgroundColor");
    if (background && *background == "default") {
        style.erase("labelBackgroundColor");
        style.set("defaultLabelBackgroundColor", "page");
    } else {
        style.erase("defaultLabelBackgroundColor");
    }
}

void replaceDefaultColors(StyleMap& style, const Theme& theme)
{
    const QColor& fg = theme.foreground;
    const QColor& bg = theme.background;
    replaceDefaultColor(style, "fontColor", fg, bg);
    replaceDefaultColor(style, "fillColor", bg, fg);
    replaceDefaultColor(style, "gradientColor", fg, bg);
    replaceDefaultColor(style, "strokeColor", fg, bg);
    replaceDefaultColor(style, "imageBorder", fg, bg);
    replaceDefaultColor(style, "imageBackground", bg, fg);
    replaceDefaultColor(style, "labelBorderColor", fg, bg);
    replaceDefaultColor(style, "swimlaneFillColor", bg, fg);
    replaceLabelGap(style);
    replaceDefaultColor(style, "labelBackgroundColor", bg, fg);
}

// --- light-dark() ------------------------------------------------------------

void selectLightDark(StyleMap& style, bool dark)
{
    std::vector<std::pair<std::string, std::string>> replacements;
    for (const auto& [key, value] : style.values()) {
        if (auto picked = pickLightDark(value, dark))
            replacements.emplace_back(key, std::move(*picked));
    }
    for (const auto& [key, value] : replacements)
        style.set(key, value);
}

// --- dark scheme (mxSvgCanvas2D.getLightDarkColor) --------------------------------

// The keys of a style that hold a color: strokeColor, fillColor2, ... and the
// two image colors.
bool isColorKey(std::string_view key)
{
    if (key == "imageBorder" || key == "imageBackground")
        return true;
    while (!key.empty() && key.back() >= '2' && key.back() <= '4')
        key.remove_suffix(1);
    return key.size() > 5 && key.substr(key.size() - 5) == "Color";
}

// Inverts the explicit colors of a dark Auto scheme. `default`, `none`,
// light-dark() pairs (which name their dark color) and names that are no
// color (defaultFillColor=invert) stay as they are.
void adaptExplicitColors(StyleMap& style, const Theme& theme)
{
    if (!theme.dark || theme.adaptive != AdaptiveColors::Auto)
        return;
    std::vector<std::pair<std::string, std::string>> replacements;
    for (const auto& [key, value] : style.values()) {
        const std::string& text = value.str();
        if (!isColorKey(key) || text.empty() || text == "none" || text == "default" || text.starts_with("light-dark("))
            continue;
        const QColor color = parseStyleColor(text, QColor());
        if (color.isValid())
            replacements.emplace_back(key, styleColorText(theme.adapt(color)));
    }
    for (const auto& [key, value] : replacements)
        style.set(key, value);
}

} // namespace

Theme Theme::darkScheme()
{
    Theme theme;
    theme.foreground = QColor(0xff, 0xff, 0xff);
    theme.background = QColor(0x12, 0x12, 0x12);
    theme.dark = true;
    return theme;
}

Theme Theme::forPage(std::optional<AdaptiveColors> setting) const
{
    Theme theme = *this;
    theme.adaptive = setting.value_or(AdaptiveColors::Auto);
    if (dark && theme.adaptive == AdaptiveColors::None) {
        theme = Theme();
        theme.adaptive = AdaptiveColors::None;
    }
    return theme;
}

QColor Theme::adapt(const QColor& color) const
{
    return dark && adaptive == AdaptiveColors::Auto ? inverseColor(color) : color;
}

QColor Theme::pageBackground(std::string_view backgroundValue) const
{
    std::string value(backgroundValue);
    if (const auto picked = pickLightDark(value, dark))
        value = *picked;
    if (!value.empty() && value != "none") {
        const QColor explicitColor = parseStyleColor(value, QColor());
        if (explicitColor.isValid() && explicitColor.alpha() > 0)
            return value == std::string(backgroundValue) ? adapt(explicitColor) : explicitColor;
    }
    return background;
}

const Stylesheet& Stylesheet::drawioDefault()
{
    static const Stylesheet instance = loadDrawioDefault();
    return instance;
}

std::optional<Stylesheet> Stylesheet::fromXml(QByteArrayView xml, std::vector<core::Diagnostic>* diagnostics)
{
    format::XmlParseResult parsed = format::parseXml(xml);
    if (diagnostics)
        diagnostics->insert(diagnostics->end(), parsed.warnings.begin(), parsed.warnings.end());
    if (!parsed.document) {
        if (diagnostics) {
            Diagnostic error = parsed.error;
            error.severity = Severity::Error;
            diagnostics->push_back(std::move(error));
        }
        return std::nullopt;
    }

    const XmlNode& root = parsed.document->root;
    if (root.name != "mxStylesheet") {
        if (diagnostics) {
            Diagnostic error;
            error.severity = Severity::Error;
            error.message = "stylesheet root is <" + root.name + ">, expected <mxStylesheet>";
            error.line = root.sourceLine;
            diagnostics->push_back(std::move(error));
        }
        return std::nullopt;
    }

    Stylesheet sheet;
    for (const XmlNode& child : root.children) {
        if (!child.isElement())
            continue;
        if (child.name != "add") {
            warn(diagnostics, "<" + child.name + "> in stylesheet is ignored", child.sourceLine);
            continue;
        }
        const std::string* name = child.attributes.find("as");
        if (!name) {
            warn(diagnostics, "<add> without as in stylesheet is ignored", child.sourceLine);
            continue;
        }
        sheet.setNamed(*name, readNamedStyle(sheet, child, diagnostics));
    }
    return sheet;
}

const StyleMap* Stylesheet::named(std::string_view name) const
{
    const auto it = styles_.find(name);
    return it == styles_.end() ? nullptr : &it->second;
}

std::vector<std::string> Stylesheet::names() const
{
    std::vector<std::string> result;
    result.reserve(styles_.size());
    for (const auto& [name, style] : styles_)
        result.push_back(name);
    return result;
}

void Stylesheet::setNamed(std::string name, StyleMap style) { styles_.insert_or_assign(std::move(name), std::move(style)); }

ResolvedStyle Stylesheet::resolve(const core::Style& style, CellKind kind, const ResolveOptions& options) const
{
    StyleMap values = baseStyle(style, kind, *this);
    applyTokens(values, style.tokens(), *this);
    if (options.parentEdgeStyle)
        resolveInheritedLabelStyles(values, *options.parentEdgeStyle);
    adaptExplicitColors(values, options.theme);
    replaceDefaultColors(values, options.theme);
    selectLightDark(values, options.theme.dark);
    // No fontColor at all is black text, which a dark scheme shows inverted.
    if (options.theme.dark && !values.has("fontColor"))
        values.set("fontColor", styleColorText(options.theme.adapt(QColor(0, 0, 0))));
    return ResolvedStyle(std::move(values), kind == CellKind::Edge);
}

} // namespace qraw::render
