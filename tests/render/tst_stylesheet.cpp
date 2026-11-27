// Style resolution: the default stylesheet, cell style tokens, theme colors,
// inherited label styles and <mxStylesheet> parsing.

#include "qraw/core/style.h"
#include "qraw/render/resolved_style.h"
#include "qraw/render/stylesheet.h"

#include <QColor>
#include <QString>
#include <QTest>

#include <optional>
#include <string>
#include <string_view>
#include <vector>

using namespace qraw;
using render::CellKind;
using render::ResolvedStyle;
using render::ResolveOptions;
using render::StyleMap;
using render::Stylesheet;
using render::Theme;

namespace {

ResolvedStyle resolve(std::string_view text, CellKind kind, const ResolveOptions& options = {})
{
    return Stylesheet::drawioDefault().resolve(text, kind, options);
}

ResolvedStyle resolveVertex(std::string_view text, const Theme& theme = {})
{
    ResolveOptions options;
    options.theme = theme;
    return resolve(text, CellKind::Vertex, options);
}

ResolvedStyle resolveEdge(std::string_view text, const Theme& theme = {})
{
    ResolveOptions options;
    options.theme = theme;
    return resolve(text, CellKind::Edge, options);
}

QString valueOf(const StyleMap& map, std::string_view key)
{
    const std::string* value = map.find(key);
    return value ? QString::fromStdString(*value) : QString();
}

QString colorName(const std::optional<QColor>& color) { return color ? color->name() : QString(); }

} // namespace

class StylesheetTest : public QObject
{
    Q_OBJECT

private slots:
    void drawioDefaultHasCoreStyles()
    {
        const Stylesheet& sheet = Stylesheet::drawioDefault();
        for (const char* name : {"defaultVertex", "defaultEdge", "text", "ellipse", "swimlane"})
            QVERIFY2(sheet.named(name) != nullptr, name);
        QCOMPARE(valueOf(*sheet.named("defaultVertex"), "shape"), QStringLiteral("label"));
        QCOMPARE(valueOf(*sheet.named("defaultVertex"), "fontSize"), QStringLiteral("12"));
    }

    void ellipseNameSetsPerimeter()
    {
        const ResolvedStyle style = resolveVertex("ellipse;html=1");
        QCOMPARE(valueOf(style.map(), "shape"), QStringLiteral("ellipse"));
        QCOMPARE(valueOf(style.map(), "perimeter"), QStringLiteral("ellipsePerimeter"));
        QCOMPARE(valueOf(style.map(), "html"), QStringLiteral("1"));
    }

    void shapeKeyKeepsDefaultPerimeter()
    {
        const ResolvedStyle style = resolveVertex("shape=ellipse");
        QCOMPARE(valueOf(style.map(), "shape"), QStringLiteral("ellipse"));
        QCOMPARE(valueOf(style.map(), "perimeter"), QStringLiteral("rectanglePerimeter"));
    }

    void unknownNameChangesNothing()
    {
        QVERIFY(resolveVertex("process").map() == resolveVertex("").map());
    }

    void leadingSemicolonStartsFromEmptyStyle()
    {
        const ResolvedStyle style = resolveVertex(";fillColor=#ff0000");
        QCOMPARE(style.map().values().size(), std::size_t(1));
        QCOMPARE(colorName(style.fillColor()), QStringLiteral("#ff0000"));
    }

    void textNameClearsFillAndIsOverridden()
    {
        const ResolvedStyle style = resolveVertex("text;strokeColor=#f00");
        QCOMPARE(valueOf(style.map(), "fillColor"), QStringLiteral("none"));
        QCOMPARE(valueOf(style.map(), "align"), QStringLiteral("left"));
        QCOMPARE(colorName(style.strokeColor()), QStringLiteral("#ff0000"));
        QVERIFY(!style.fillColor());
    }

    void noneErasesKey()
    {
        const ResolvedStyle style = resolveVertex("fillColor=none");
        QVERIFY(!style.map().has("fillColor"));
        QVERIFY(!style.fillColor());
    }

    void vertexDefaultColorsFollowTheme()
    {
        const ResolvedStyle style = resolveVertex("");
        QCOMPARE(colorName(style.fillColor()), QStringLiteral("#ffffff"));
        QCOMPARE(colorName(style.strokeColor()), QStringLiteral("#000000"));
        QCOMPARE(colorName(style.fontColor()), QStringLiteral("#000000"));
    }

    void edgeDefaults()
    {
        const ResolvedStyle style = resolveEdge("");
        QCOMPARE(valueOf(style.map(), "endArrow"), QStringLiteral("classic"));
        QCOMPARE(valueOf(style.map(), "rounded"), QStringLiteral("1"));
        QCOMPARE(colorName(style.labelBackgroundColor()), QStringLiteral("#ffffff"));
        QVERIFY(style.isEdge());
    }

    void defaultInvertSwapsTheThemeColor()
    {
        const ResolvedStyle style = resolveVertex("defaultFillColor=invert");
        QCOMPARE(colorName(style.fillColor()), QStringLiteral("#000000"));
    }

    void lightDarkPicksByTheme()
    {
        const ResolvedStyle light = resolveVertex("fillColor=light-dark(#111111, #eeeeee)");
        QCOMPARE(valueOf(light.map(), "fillColor"), QStringLiteral("#111111"));

        Theme dark;
        dark.dark = true;
        const ResolvedStyle darkStyle = resolveVertex("fillColor=light-dark(#111111, #eeeeee)", dark);
        QCOMPARE(valueOf(darkStyle.map(), "fillColor"), QStringLiteral("#eeeeee"));

        const ResolvedStyle malformed = resolveVertex("fillColor=light-dark(#111111)");
        QCOMPARE(valueOf(malformed.map(), "fillColor"), QStringLiteral("light-dark(#111111)"));
    }

    void labelGapErasesLabelBackground()
    {
        const ResolvedStyle style = resolveEdge("defaultLabelBackgroundColor=page");
        QVERIFY(!style.map().has("labelBackgroundColor"));
        QCOMPARE(valueOf(style.map(), "defaultLabelBackgroundColor"), QStringLiteral("page"));
    }

    void inheritedLabelStyleFromEdge()
    {
        const ResolvedStyle parent = resolveEdge("fontSize=20");
        ResolveOptions options;
        options.parentEdgeStyle = &parent;
        const ResolvedStyle label = resolve("inheritLabelStyle=1", CellKind::Vertex, options);
        QCOMPARE(valueOf(label.map(), "fontSize"), QStringLiteral("20"));
        QVERIFY(!label.map().has("inheritLabelStyle"));
    }

    void inheritValueWithoutParentKeyErasesKey()
    {
        const ResolvedStyle parent = resolveEdge("fontColor=none");
        ResolveOptions options;
        options.parentEdgeStyle = &parent;
        const ResolvedStyle label = resolve("fontColor=inherit", CellKind::Vertex, options);
        QVERIFY(!label.map().has("fontColor"));
        QVERIFY(!label.map().has("inheritLabelStyle"));
    }

    void fromXmlExtendsAndRemoves()
    {
        const char* xml = R"(<mxStylesheet>
  <add as="base"><add as="fillColor" value="#ff0000"/><add as="strokeColor" value="#000000"/></add>
  <add as="derived" extend="base"><remove as="strokeColor"/><add as="fontSize" value="9"/></add>
</mxStylesheet>)";
        std::vector<core::Diagnostic> diagnostics;
        const auto sheet = Stylesheet::fromXml(xml, &diagnostics);
        QVERIFY(sheet.has_value());
        const StyleMap* derived = sheet->named("derived");
        QVERIFY(derived);
        QCOMPARE(valueOf(*derived, "fillColor"), QStringLiteral("#ff0000"));
        QCOMPARE(valueOf(*derived, "fontSize"), QStringLiteral("9"));
        QVERIFY(!derived->has("strokeColor"));
        QVERIFY(sheet->names() == (std::vector<std::string>{"base", "derived"}));
        QVERIFY(diagnostics.empty());
    }

    void fromXmlSkipsEntriesWithoutValue()
    {
        std::vector<core::Diagnostic> diagnostics;
        const auto sheet = Stylesheet::fromXml(
            R"(<mxStylesheet><add as="scripted"><add as="fillColor"/></add></mxStylesheet>)", &diagnostics);
        QVERIFY(sheet.has_value());
        QVERIFY(!sheet->named("scripted")->has("fillColor"));
        QVERIFY(!diagnostics.empty());
        QVERIFY(diagnostics.front().severity == core::Diagnostic::Severity::Warning);
    }

    void fromXmlRejectsMalformedAndWrongRoot()
    {
        std::vector<core::Diagnostic> malformed;
        QVERIFY(!Stylesheet::fromXml("<mxStylesheet><add as=\"x\">", &malformed).has_value());
        QVERIFY(!malformed.empty());
        QVERIFY(malformed.back().severity == core::Diagnostic::Severity::Error);

        std::vector<core::Diagnostic> wrongRoot;
        QVERIFY(!Stylesheet::fromXml("<mxGraphModel/>", &wrongRoot).has_value());
        QVERIFY(wrongRoot.back().severity == core::Diagnostic::Severity::Error);
    }
};

QTEST_MAIN(StylesheetTest)
#include "tst_stylesheet.moc"
