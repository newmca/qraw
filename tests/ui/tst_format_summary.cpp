// StyleSummary of the Format panel: common values, mixed state, flags, numbers
// and geometry over the simple fixture (cells "a" and "b" differ in shape and
// html; "e" is an edge).

#include "support/test_support.h"

#include "qraw/render/resolved_style.h"

#include <QtTest>

#include <optional>

// QCOMPARE needs printable operands: std::optional values print their content.
namespace QTest {
template <typename T>
char* toString(const std::optional<T>& value)
{
    return value ? toString(*value) : qstrdup("nullopt");
}
} // namespace QTest

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

class TestFormatSummary : public QObject {
    Q_OBJECT

private slots:
    void counts();
    void commonAndMixedValues();
    void flagsAndNumbersUseFallbacks();
    void geometryIsCommonOrUnset();
    void emptyForUnknownIds();
};

void TestFormatSummary::counts()
{
    const core::Document document = simpleDocument();
    const StyleSummary both = StyleSummary::of(*document.page(0), {"a", "b", "e"}, render::Stylesheet::drawioDefault());
    QCOMPARE(both.cellCount(), std::size_t(3));
    QCOMPARE(both.vertexCount(), std::size_t(2));
    QCOMPARE(both.edgeCount(), std::size_t(1));
    QVERIFY(both.hasVertices());
    QVERIFY(both.hasEdges());
    QVERIFY(both.anyHtml());   // "b" has html=1
    QVERIFY(!both.anyLocked());
}

void TestFormatSummary::commonAndMixedValues()
{
    const core::Document document = simpleDocument();
    const auto& sheet = render::Stylesheet::drawioDefault();
    const StyleSummary both = StyleSummary::of(*document.page(0), {"a", "b"}, sheet);

    // Both vertices have whiteSpace=wrap (a directly, b through its own style).
    const StyleValue whiteSpace = both.value("whiteSpace");
    QVERIFY(!whiteSpace.mixed);
    QCOMPARE(QString::fromStdString(whiteSpace.value.value_or("")), QStringLiteral("wrap"));

    // Same stylesheet default fill on both.
    const StyleValue fill = both.value("fillColor");
    QVERIFY(!fill.mixed);
    QVERIFY(fill.value.has_value());

    // html=1 only on "b": the cells disagree.
    QVERIFY(both.value("html").mixed);
    QVERIFY(!both.value("noSuchKey").mixed);
    QVERIFY(!both.value("noSuchKey").value.has_value());
}

void TestFormatSummary::flagsAndNumbersUseFallbacks()
{
    const core::Document document = simpleDocument();
    const auto& sheet = render::Stylesheet::drawioDefault();

    // "a" has rounded=0 in its own style.
    const StyleSummary a = StyleSummary::of(*document.page(0), {"a"}, sheet);
    QCOMPARE(a.flag("rounded", true), std::optional<bool>(false));
    QCOMPARE(a.flag("sketch", true), std::optional<bool>(true)); // absent: fallback

    // A key absent from every cell gives the fallback.
    QCOMPARE(a.number("noSuchKey", 7), std::optional<double>(7));

    // Font style bit: no cell is bold.
    QCOMPARE(a.fontStyleBit(render::FontBold), std::optional<bool>(false));
    const StyleSummary both = StyleSummary::of(*document.page(0), {"a", "b"}, sheet);
    QCOMPARE(both.fontStyleBit(render::FontBold), std::optional<bool>(false));

    // A flag that differs between cells is mixed.
    const StyleSummary withRounded = StyleSummary::of(*document.page(0), {"a", "c1"}, sheet);
    QVERIFY(!withRounded.flag("rounded", false).has_value());
}

void TestFormatSummary::geometryIsCommonOrUnset()
{
    const core::Document document = simpleDocument();
    const auto& sheet = render::Stylesheet::drawioDefault();

    // Both at y=40 but different x (40 and 280).
    const StyleSummary both = StyleSummary::of(*document.page(0), {"a", "b"}, sheet);
    QCOMPARE(both.geometry().y, std::optional<double>(40));
    QVERIFY(!both.geometry().x.has_value());
    QCOMPARE(both.geometry().width, std::optional<double>(120));

    // An edge has no bounds for the Arrange tab.
    const StyleSummary edge = StyleSummary::of(*document.page(0), {"e"}, sheet);
    QVERIFY(!edge.geometry().x.has_value());
    QVERIFY(!edge.geometry().width.has_value());
}

void TestFormatSummary::emptyForUnknownIds()
{
    const core::Document document = simpleDocument();
    const StyleSummary none = StyleSummary::of(*document.page(0), {"missing"}, render::Stylesheet::drawioDefault());
    QVERIFY(none.empty());
    QVERIFY(!none.value("fillColor").value.has_value());
    QVERIFY(!none.value("fillColor").mixed);
}

QTEST_MAIN(TestFormatSummary)
#include "tst_format_summary.moc"
