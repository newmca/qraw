// Set as Default Style as pure style functions: which keys a cell contributes
// and how new cells take them (commands/default_style.h).

#include "qraw/commands/default_style.h"

#include <QtTest>

using namespace qraw;
using commands::DefaultStyles;
using commands::InsertKind;
using commands::StyleTarget;
using core::Style;

namespace {

DefaultStyles vertexDefault(const char* cellStyle)
{
    DefaultStyles defaults;
    commands::setDefaultFrom(defaults, Style(cellStyle), StyleTarget::Vertex);
    return defaults;
}

DefaultStyles edgeDefault(const char* cellStyle)
{
    DefaultStyles defaults;
    commands::setDefaultFrom(defaults, Style(cellStyle), StyleTarget::Edge);
    return defaults;
}

QString applied(const char* entry, const DefaultStyles& defaults, StyleTarget target = StyleTarget::Vertex,
                InsertKind kind = InsertKind::Template)
{
    return QString::fromStdString(commands::applyDefaultStyle(Style(entry), defaults, target, kind).toString());
}

} // namespace

class TestDefaultStyle : public QObject {
    Q_OBJECT

private slots:
    void vertexDefaultKeepsStyleKeysOnly()
    {
        const DefaultStyles defaults =
            vertexDefault("ellipse;whiteSpace=wrap;html=1;fillColor=#FF0000;fontSize=18;shape=cylinder3;"
                          "strokeColor=none;rounded=1;points=[[0,0]];");
        QCOMPARE(QString::fromStdString(defaults.vertex.value("fillColor").value_or("")), QStringLiteral("#FF0000"));
        QCOMPARE(QString::fromStdString(defaults.vertex.value("fontSize").value_or("")), QStringLiteral("18"));
        QVERIFY(defaults.vertex.has("rounded"));
        // Cell-specific keys, the shape, a transparent stroke and bare names stay behind.
        for (const char* key : {"whiteSpace", "html", "shape", "strokeColor", "points"})
            QVERIFY2(!defaults.vertex.has(key), key);
        QVERIFY(!defaults.vertex.hasName("ellipse"));
        QVERIFY(defaults.edge.empty());
        QVERIFY(!defaults.vertexFromText);
    }

    void edgeDefaultKeepsEdgeKeysAndShape()
    {
        const DefaultStyles defaults =
            edgeDefault("edgeStyle=elbowEdgeStyle;endArrow=block;strokeColor=#0000FF;html=1;shape=link;exitX=1;");
        for (const char* key : {"edgeStyle", "endArrow", "strokeColor", "shape"})
            QVERIFY2(defaults.edge.has(key), key);
        for (const char* key : {"html", "exitX"})
            QVERIFY2(!defaults.edge.has(key), key);
        QVERIFY(defaults.vertex.empty());
    }

    void settingAgainReplacesTheDefaultOfItsKind()
    {
        DefaultStyles defaults = vertexDefault("fillColor=#FF0000;fontSize=18;");
        commands::setDefaultFrom(defaults, Style("fillColor=#00FF00;"), StyleTarget::Vertex);
        QCOMPARE(QString::fromStdString(defaults.vertex.toString()), QStringLiteral("fillColor=#00FF00;"));
        commands::setDefaultFrom(defaults, Style("endArrow=none;"), StyleTarget::Edge);
        QVERIFY(defaults.vertex.has("fillColor")); // the other kind is untouched
        commands::clearDefaults(defaults);
        QVERIFY(defaults.empty());
    }

    void edgeLabelMergesTextStylesIntoTheEdgeDefault()
    {
        DefaultStyles defaults = edgeDefault("endArrow=block;fontSize=11;");
        commands::setDefaultFrom(defaults, Style("edgeLabel;html=1;fontSize=16;fontColor=#FF0000;fillColor=#FFFFFF;"),
                                 StyleTarget::EdgeLabel);
        QCOMPARE(QString::fromStdString(defaults.edge.value("endArrow").value_or("")), QStringLiteral("block"));
        QCOMPARE(QString::fromStdString(defaults.edge.value("fontSize").value_or("")), QStringLiteral("16"));
        QVERIFY(defaults.edge.has("fontColor"));
        QVERIFY(!defaults.edge.has("fillColor"));
        QVERIFY(defaults.vertex.empty());
    }

    void noDefaultsLeavesTheEntryUntouched()
    {
        const char* entry = "rounded=0;whiteSpace=wrap;html=1;";
        QCOMPARE(applied(entry, DefaultStyles{}), QString::fromLatin1(entry));
    }

    void vertexTakesFillAndFont()
    {
        const DefaultStyles defaults = vertexDefault("fillColor=#FF0000;fontSize=18;");
        const Style styled = commands::applyDefaultStyle(Style("rounded=0;whiteSpace=wrap;html=1;"), defaults,
                                                         StyleTarget::Vertex);
        QCOMPARE(QString::fromStdString(styled.value("fillColor").value_or("")), QStringLiteral("#FF0000"));
        QCOMPARE(QString::fromStdString(styled.value("fontSize").value_or("")), QStringLiteral("18"));
        QCOMPARE(QString::fromStdString(styled.value("whiteSpace").value_or("")), QStringLiteral("wrap"));
    }

    void keysTheEntryDefinesAreKept()
    {
        const DefaultStyles defaults = vertexDefault("fillColor=#FF0000;strokeColor=#00FF00;rounded=1;");
        const Style styled = commands::applyDefaultStyle(Style("rounded=0;fillColor=#123456;"), defaults,
                                                         StyleTarget::Vertex);
        QCOMPARE(QString::fromStdString(styled.value("fillColor").value_or("")), QStringLiteral("#123456"));
        QCOMPARE(QString::fromStdString(styled.value("rounded").value_or("")), QStringLiteral("0"));
        QCOMPARE(QString::fromStdString(styled.value("strokeColor").value_or("")), QStringLiteral("#00FF00"));
    }

    void definingOneKeyOfAGroupKeepsTheWholeGroup()
    {
        const DefaultStyles defaults = vertexDefault("fillColor=#FF0000;gradientColor=#0000FF;gradientDirection=east;");
        // A gradient of its own: the default fill and gradient direction do not mix in.
        const Style styled = commands::applyDefaultStyle(Style("gradientColor=#FFFFFF;"), defaults, StyleTarget::Vertex);
        QVERIFY(!styled.has("fillColor"));
        QVERIFY(!styled.has("gradientDirection"));
        QCOMPARE(QString::fromStdString(styled.value("gradientColor").value_or("")), QStringLiteral("#FFFFFF"));
    }

    void vertexNeverTakesKeysOfEdges()
    {
        const DefaultStyles defaults = vertexDefault("fillColor=#FF0000;endArrow=block;edgeStyle=elbowEdgeStyle;");
        const QString styled = applied("rounded=0;", defaults);
        QVERIFY(styled.contains("fillColor=#FF0000"));
        QVERIFY(!styled.contains("endArrow"));
        QVERIFY(!styled.contains("edgeStyle"));
    }

    void textCellsTakeTextStylesUnlessTheDefaultCameFromText()
    {
        const char* entry = "text;html=1;fontSize=12;";
        DefaultStyles defaults = vertexDefault("fillColor=#FF0000;fontSize=18;fontColor=#00FF00;");
        // Text styles replace the entry's own, other styles stay out.
        const QString text = applied(entry, defaults);
        QVERIFY(text.contains("fontSize=18"));
        QVERIFY(text.contains("fontColor=#00FF00"));
        QVERIFY(!text.contains("fillColor"));

        defaults = vertexDefault("text;fillColor=#FF0000;fontSize=18;");
        QVERIFY(defaults.vertexFromText);
        QVERIFY(applied(entry, defaults).contains("fillColor=#FF0000"));
    }

    void fontFamilyReplacesTheWebFontOfTheEntry()
    {
        const DefaultStyles defaults = vertexDefault("fontFamily=Courier New;");
        const QString styled = applied("fontSource=https://fonts.example/x;html=1;", defaults);
        QVERIFY(styled.contains("fontFamily=Courier New"));
        QVERIFY(!styled.contains("fontSource"));
    }

    void edgeTakesEdgeKeysButKeepsItsOwn()
    {
        const DefaultStyles defaults = edgeDefault("edgeStyle=elbowEdgeStyle;endArrow=block;strokeColor=#0000FF;");
        const QString styled = applied("edgeStyle=orthogonalEdgeStyle;html=1;", defaults, StyleTarget::Edge);
        QVERIFY(styled.contains("edgeStyle=orthogonalEdgeStyle")); // a palette edge keeps its routing
        QVERIFY(styled.contains("endArrow=block"));
        QVERIFY(styled.contains("strokeColor=#0000FF"));
        // A vertex default does not reach edges.
        QCOMPARE(applied("html=1;", vertexDefault("fillColor=#FF0000;"), StyleTarget::Edge), QStringLiteral("html=1;"));
    }

    void roundedEdgesAreNotCurved()
    {
        const DefaultStyles defaults = edgeDefault("rounded=1;");
        QVERIFY(!applied("curved=1;", defaults, StyleTarget::Edge).contains("curved"));
    }

    void edgeLabelsTakeTheTextStylesOfTheEdge()
    {
        const DefaultStyles defaults = edgeDefault("endArrow=block;fontSize=16;strokeColor=#0000FF;");
        const QString styled = applied("edgeLabel;html=1;", defaults, StyleTarget::EdgeLabel);
        QVERIFY(styled.contains("fontSize=16"));
        QVERIFY(!styled.contains("endArrow"));
        QVERIFY(!styled.contains("strokeColor"));
    }

    void connectorTakesTheConnectorKeysFromTheDefault()
    {
        const char* connector = "edgeStyle=orthogonalEdgeStyle;rounded=0;orthogonalLoop=1;jettySize=auto;html=1;";
        const DefaultStyles elbow = edgeDefault("edgeStyle=elbowEdgeStyle;strokeColor=#0000FF;");
        const QString styled = applied(connector, elbow, StyleTarget::Edge, InsertKind::Connector);
        QVERIFY(styled.contains("edgeStyle=elbowEdgeStyle"));
        QVERIFY(!styled.contains("orthogonalEdgeStyle"));
        QVERIFY(!styled.contains("rounded=0")); // not part of the default
        QVERIFY(styled.contains("strokeColor=#0000FF"));
        QVERIFY(styled.contains("jettySize=auto")); // not a connector key: kept

        // A straight default makes straight connectors.
        const DefaultStyles straight = edgeDefault("strokeColor=#0000FF;");
        QVERIFY(applied(connector, straight, StyleTarget::Edge, InsertKind::Connector).contains("edgeStyle=none"));
        // Without an edge default the connector is as it was.
        QCOMPARE(applied(connector, vertexDefault("fillColor=#FF0000;"), StyleTarget::Edge, InsertKind::Connector),
                 QString::fromLatin1(connector));
    }
};

QTEST_APPLESS_MAIN(TestDefaultStyle)
#include "tst_default_style.moc"
