// Dark diagram mode (mxGraphModel@adaptiveColors, mxUtils.getInverseColor,
// light-dark() colors): the colour mapping of style resolution and labels, the
// page theme, and what a dark export draws.

#include "qraw/core/page.h"
#include "qraw/format/codec.h"
#include "qraw/render/color.h"
#include "qraw/render/export.h"
#include "qraw/render/label_colors.h"
#include "qraw/render/page_view.h"
#include "qraw/render/stylesheet.h"

#include <QImage>
#include <QTest>

#include <string>

using namespace qraw;
using render::AdaptiveColors;
using render::Theme;

namespace {

QString name(const std::optional<QColor>& color) { return color ? color->name() : QString(); }

Theme darkTheme(AdaptiveColors adaptive = AdaptiveColors::Auto)
{
    Theme theme = Theme::darkScheme();
    theme.adaptive = adaptive;
    return theme;
}

render::ResolvedStyle resolve(std::string_view style, const Theme& theme)
{
    render::ResolveOptions options;
    options.theme = theme;
    return render::Stylesheet::drawioDefault().resolve(style, render::CellKind::Vertex, options);
}

// One red box (explicit colors) and one default box; `modelAttributes` goes on <mxGraphModel>.
core::Document sampleDocument(const char* modelAttributes = "")
{
    const QByteArray xml =
        QByteArray("<mxfile><diagram id=\"p\" name=\"p\"><mxGraphModel ") + modelAttributes +
        "><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
        "<mxCell id=\"red\" value=\"\" style=\"rounded=0;fillColor=#dce6f2;strokeColor=none;\" vertex=\"1\" parent=\"1\">"
        "<mxGeometry x=\"20\" y=\"20\" width=\"60\" height=\"40\" as=\"geometry\"/></mxCell>"
        "<mxCell id=\"plain\" value=\"\" style=\"rounded=0;strokeColor=none;\" vertex=\"1\" parent=\"1\">"
        "<mxGeometry x=\"120\" y=\"20\" width=\"60\" height=\"40\" as=\"geometry\"/></mxCell>"
        "</root></mxGraphModel></diagram></mxfile>";
    auto read = format::readDocument(xml);
    if (!read.ok())
        qFatal("fixture does not parse");
    return std::move(*read.document);
}

render::RenderContext contextWith(const Theme& theme)
{
    render::RenderContext context = render::RenderContext::defaults();
    context.theme = theme;
    return context;
}

} // namespace

class AdaptiveColorsTest : public QObject {
    Q_OBJECT

private slots:
    void inverseColorPortsGetInverseColor_data()
    {
        QTest::addColumn<QString>("light");
        QTest::addColumn<QString>("dark");
        // Black and white are not exact opposites (93 % inversion) and white
        // lands on draw.io's dark page color.
        QTest::newRow("black") << "#000000" << "#ededed";
        QTest::newRow("white") << "#ffffff" << "#121212";
        QTest::newRow("red") << "#ff0000" << "#ff9090";
        QTest::newRow("light blue") << "#dce6f2" << "#202933";
        QTest::newRow("gray") << "#808080" << "#7f7f7f";
    }
    void inverseColorPortsGetInverseColor()
    {
        QFETCH(QString, light);
        QFETCH(QString, dark);
        QCOMPARE(render::inverseColor(QColor(light)).name(), dark);
    }

    void inverseColorKeepsAlphaAndLeavesTransparentAndInvalid()
    {
        QCOMPARE(render::inverseColor(QColor(255, 255, 255, 100)).alpha(), 100);
        QVERIFY(render::inverseColor(QColor(Qt::transparent)).alpha() == 0);
        QVERIFY(!render::inverseColor(QColor()).isValid());
        QCOMPARE(render::styleColorText(QColor(18, 18, 18, 128)), std::string("#12121280"));
    }

    void adaptiveColorsNames()
    {
        QCOMPARE(render::parseAdaptiveColors("auto"), std::optional(AdaptiveColors::Auto));
        QCOMPARE(render::parseAdaptiveColors("simple"), std::optional(AdaptiveColors::Simple));
        QCOMPARE(render::parseAdaptiveColors("none"), std::optional(AdaptiveColors::None));
        QVERIFY(!render::parseAdaptiveColors("default"));
        QVERIFY(!render::parseAdaptiveColors("").has_value());
    }

    void pickLightDarkSplitsAtTheTopLevelComma()
    {
        QCOMPARE(render::pickLightDark("light-dark(#000000,#FFFFFF)", false), std::optional<std::string>("#000000"));
        QCOMPARE(render::pickLightDark("light-dark(#000000,#FFFFFF)", true), std::optional<std::string>("#FFFFFF"));
        QCOMPARE(render::pickLightDark("light-dark(rgb(1, 2, 3), rgb(4, 5, 6))", true),
                 std::optional<std::string>("rgb(4, 5, 6)"));
        QVERIFY(!render::pickLightDark("light-dark(#111111)", true));
        QVERIFY(!render::pickLightDark("#111111", true));
    }

    // --- style resolution in each mode ------------------------------------

    void lightThemeKeepsEveryColor()
    {
        const auto style = resolve("fillColor=#dce6f2;strokeColor=#ff0000;fontColor=#000000", Theme());
        QCOMPARE(name(style.fillColor()), QStringLiteral("#dce6f2"));
        QCOMPARE(name(style.strokeColor()), QStringLiteral("#ff0000"));
        QCOMPARE(name(style.fontColor()), QStringLiteral("#000000"));
    }

    void autoInvertsExplicitColorsAndSwapsTheDefaults()
    {
        const auto style = resolve("fillColor=#dce6f2;strokeColor=#ff0000;gradientColor=#ffffff;fontColor=default", darkTheme());
        QCOMPARE(name(style.fillColor()), QStringLiteral("#202933"));
        QCOMPARE(name(style.strokeColor()), QStringLiteral("#ff9090"));
        QCOMPARE(name(style.gradientColor()), QStringLiteral("#121212"));
        // `default` is the dark scheme's own color, inverted once only.
        QCOMPARE(name(style.fontColor()), QStringLiteral("#ffffff"));
        const auto defaults = resolve("", darkTheme());
        QCOMPARE(name(defaults.fillColor()), QStringLiteral("#121212"));
        QCOMPARE(name(defaults.strokeColor()), QStringLiteral("#ffffff"));
    }

    void autoKeepsNoneAndInvertsKeysWithASuffix()
    {
        const auto style = resolve("fillColor=none;fillColor2=#ffffff;imageBorder=#000000;defaultFillColor=invert", darkTheme());
        QVERIFY(!style.fillColor());
        QCOMPARE(QString::fromStdString(std::string(style.string("fillColor2"))), QStringLiteral("#121212"));
        QCOMPARE(QString::fromStdString(std::string(style.string("imageBorder"))), QStringLiteral("#ededed"));
        QCOMPARE(QString::fromStdString(std::string(style.string("defaultFillColor"))), QStringLiteral("invert"));
    }

    void autoKeepsTheAlphaOfAnInvertedColor()
    {
        const auto style = resolve("fillColor=#ffffff80", darkTheme());
        QCOMPARE(style.fillColor()->name(), QStringLiteral("#121212"));
        QCOMPARE(style.fillColor()->alpha(), 128);
    }

    void simpleKeepsExplicitColorsButAdaptsDefaultsAndLightDark()
    {
        const auto style =
            resolve("fillColor=#dce6f2;strokeColor=light-dark(#111111, #eeeeee);fontColor=default", darkTheme(AdaptiveColors::Simple));
        QCOMPARE(name(style.fillColor()), QStringLiteral("#dce6f2"));
        QCOMPARE(name(style.strokeColor()), QStringLiteral("#eeeeee"));
        QCOMPARE(name(style.fontColor()), QStringLiteral("#ffffff"));
    }

    void lightDarkPairsAreNeverInverted()
    {
        const auto style = resolve("fillColor=light-dark(#ffffff,#ff0000)", darkTheme());
        QCOMPARE(name(style.fillColor()), QStringLiteral("#ff0000"));
    }

    void noneForcesTheLightSchemeOnAPage()
    {
        const Theme page = darkTheme().forPage(AdaptiveColors::None);
        QVERIFY(!page.dark);
        const auto style = resolve("fillColor=#dce6f2;strokeColor=light-dark(#111111, #eeeeee);fontColor=default", page);
        QCOMPARE(name(style.fillColor()), QStringLiteral("#dce6f2"));
        QCOMPARE(name(style.strokeColor()), QStringLiteral("#111111"));
        QCOMPARE(name(style.fontColor()), QStringLiteral("#000000"));
    }

    void withoutAFontColorTheTextIsTheInvertedBlack()
    {
        QCOMPARE(name(resolve(";shape=rectangle", darkTheme()).fontColor()), QStringLiteral("#ededed"));
        QCOMPARE(name(resolve(";shape=rectangle", darkTheme(AdaptiveColors::Simple)).fontColor()), QStringLiteral("#000000"));
        QVERIFY(!resolve(";shape=rectangle", Theme()).fontColor());
    }

    void pageBackgroundByScheme()
    {
        QCOMPARE(Theme().pageBackground("").name(), QStringLiteral("#ffffff"));
        QCOMPARE(darkTheme().pageBackground("").name(), QStringLiteral("#121212"));
        QCOMPARE(darkTheme().pageBackground("none").name(), QStringLiteral("#121212"));
        QCOMPARE(darkTheme().pageBackground("#dce6f2").name(), QStringLiteral("#202933"));
        QCOMPARE(darkTheme(AdaptiveColors::Simple).pageBackground("#dce6f2").name(), QStringLiteral("#dce6f2"));
        QCOMPARE(darkTheme().pageBackground("light-dark(#ffffff,#222222)").name(), QStringLiteral("#222222"));
    }

    // --- labels -----------------------------------------------------------

    void labelColorsAreInvertedInDarkAuto()
    {
        const std::string html = "<div style=\"color: #000000; font-size: 9px; background-color: #ffff00;\">"
                                 "a <font color=\"#ff0000\">b</font></div>";
        const std::string dark = render::adaptLabelColors(html, darkTheme());
        QVERIFY2(dark.find("color: #ededed;") != std::string::npos, dark.c_str());
        QVERIFY2(dark.find("font-size: 9px;") != std::string::npos, dark.c_str());
        QVERIFY2(dark.find("color=\"#ff9090\"") != std::string::npos, dark.c_str());
        QVERIFY2(dark.find("#ffff00") == std::string::npos, dark.c_str());
        QCOMPARE(render::adaptLabelColors(html, Theme()), html);
        QCOMPARE(render::adaptLabelColors(html, darkTheme(AdaptiveColors::Simple)), html);
    }

    void labelLightDarkPairsPickTheirSideInBothSchemes()
    {
        const std::string html = "<span style=\"color: light-dark(#111111, #eeeeee);\">x</span>";
        QVERIFY(render::adaptLabelColors(html, Theme()).find("color: #111111;") != std::string::npos);
        QVERIFY(render::adaptLabelColors(html, darkTheme()).find("color: #eeeeee;") != std::string::npos);
    }

    // --- the page ----------------------------------------------------------

    void adaptiveColorsRoundTripsLosslesslyAtItsPlace()
    {
        const QByteArray text = "<mxfile><diagram id=\"p\" name=\"p\"><mxGraphModel dx=\"1\" math=\"0\" shadow=\"0\" adaptiveColors=\"simple\">"
                                "<root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/></root></mxGraphModel></diagram></mxfile>";
        auto read = format::readDocument(text);
        QVERIFY(read.ok());
        QCOMPARE(read.document->page(0)->adaptiveColors(), std::string("simple"));
        const QByteArray written = format::writeDocument(*read.document);
        QVERIFY(written.contains("dx=\"1\" math=\"0\" shadow=\"0\" adaptiveColors=\"simple\""));
        auto again = format::readDocument(written);
        QVERIFY(again.ok());
        QCOMPARE(format::writeDocument(*again.document), written);
    }

    void pageViewFollowsTheContextThemeAsThePageAllows()
    {
        const core::Document plain = sampleDocument();
        const render::PageView dark = render::PageView::build(*plain.page(0), contextWith(Theme::darkScheme()));
        QVERIFY(dark.theme().dark);
        QCOMPARE(name(dark.find("red")->style.fillColor()), QStringLiteral("#202933"));
        QCOMPARE(name(dark.find("plain")->style.fillColor()), QStringLiteral("#121212"));

        const core::Document none = sampleDocument("adaptiveColors=\"none\"");
        const render::PageView light = render::PageView::build(*none.page(0), contextWith(Theme::darkScheme()));
        QVERIFY(!light.theme().dark);
        QCOMPARE(name(light.find("red")->style.fillColor()), QStringLiteral("#dce6f2"));
        QCOMPARE(name(light.find("plain")->style.fillColor()), QStringLiteral("#ffffff"));

        const core::Document simple = sampleDocument("adaptiveColors=\"simple\"");
        const render::PageView simpleView = render::PageView::build(*simple.page(0), contextWith(Theme::darkScheme()));
        QCOMPARE(name(simpleView.find("red")->style.fillColor()), QStringLiteral("#dce6f2"));
        QCOMPARE(name(simpleView.find("plain")->style.fillColor()), QStringLiteral("#121212"));
    }

    // --- export ------------------------------------------------------------

    void aDarkPngIsPaintedInTheDarkScheme()
    {
        const core::Document doc = sampleDocument();
        render::ExportOptions options;
        options.format = render::ExportFormat::Png;
        options.border = 10;
        const auto light = render::exportPage(doc, 0, options);
        options.dark = true;
        const auto dark = render::exportPage(doc, 0, options);
        QImage lightImage, darkImage;
        QVERIFY(lightImage.loadFromData(light.data));
        QVERIFY(darkImage.loadFromData(dark.data));
        QCOMPARE(lightImage.pixelColor(2, 2), QColor("#ffffff")); // the border: the page color
        QCOMPARE(darkImage.pixelColor(2, 2), QColor("#121212"));
        // The first box, in the middle, in its inverted fill.
        QCOMPARE(lightImage.pixelColor(10 + 30, 10 + 20), QColor("#dce6f2"));
        QCOMPARE(darkImage.pixelColor(10 + 30, 10 + 20), QColor("#202933"));
    }

    void aDarkExportOfANoneDiagramStaysLight()
    {
        const core::Document doc = sampleDocument("adaptiveColors=\"none\"");
        render::ExportOptions options;
        options.format = render::ExportFormat::Png;
        options.dark = true;
        QImage image;
        QVERIFY(image.loadFromData(render::exportPage(doc, 0, options).data));
        QCOMPARE(image.pixelColor(30, 20), QColor("#dce6f2"));
    }

    void aDarkSvgWithoutABackgroundStaysTransparent()
    {
        const core::Document doc = sampleDocument();
        render::ExportOptions options;
        options.format = render::ExportFormat::Svg;
        options.dark = true;
        const QByteArray svg = render::exportPage(doc, 0, options).data;
        QVERIFY(svg.contains("#202933"));
        QVERIFY(!svg.contains("background-color"));
    }
};

QTEST_MAIN(AdaptiveColorsTest)
#include "tst_adaptive_colors.moc"
