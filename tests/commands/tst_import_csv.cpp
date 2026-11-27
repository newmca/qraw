// Insert > Advanced > CSV (import_csv.cpp), without a document. The fixture is draw.io's own example.
#include "import_test_support.h"

using namespace import_test;

namespace {

imports::Result csv(const std::string& text) { return imports::importCsv(text); }

// A header with the directives, then the CSV.
std::string withHeader(std::string header, const std::string& rows)
{
    while (!header.empty() && header.back() == '\n')
        header.pop_back();
    return header + "\n" + rows;
}

QString attribute(const Cell& cell, const char* name)
{
    if (cell.wrapper() == nullptr)
        return QStringLiteral("<no object>");
    const std::string* value = cell.wrapper()->attributes.find(name);
    return value == nullptr ? QStringLiteral("<none>") : q(*value);
}

const Cell& named(const imports::Result& r, const std::string& name)
{
    for (const Cell& cell : r.cells) {
        if (cell.isVertex() && cell.wrapper() != nullptr && cell.wrapper()->attributes.value("name") == name)
            return cell;
    }
    QTest::qFail(qPrintable(QStringLiteral("no cell named %1").arg(q(name))), __FILE__, __LINE__);
    static const Cell none;
    return none;
}

} // namespace

class ImportCsvTest : public QObject {
    Q_OBJECT

private slots:
    // --- draw.io's example ---

    void theDefaultExampleImportsWithoutWarnings()
    {
        const imports::Result r = imports::importCsv(imports::csvExample());
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QVERIFY2(r.warnings.empty(), qPrintable(q(imports::describe(r.warnings.empty() ? imports::Diagnostic{} : r.warnings.front()))));
        QCOMPARE(vertices(r).size(), std::size_t(4));
        // 3 "manages" connections (manager -> employee, inverted) and the 2 of "refs".
        QCOMPARE(edges(r).size(), std::size_t(5));
        QVERIFY(wellFormed(r));
        QVERIFY(noOverlaps(r));
    }

    void theExamplesCellsAreUserObjectsWithTheColumns()
    {
        const imports::Result r = imports::importCsv(imports::csvExample());
        const Cell& tessa = named(r, "Tessa Miller");
        QVERIFY(tessa.hasWrapper());
        QCOMPARE(q(tessa.wrapper()->tag), QStringLiteral("UserObject"));
        QCOMPARE(attribute(tessa, "position"), QStringLiteral("CFO"));
        QCOMPARE(attribute(tessa, "location"), QStringLiteral("Office 1"));
        QCOMPARE(attribute(tessa, "email"), QStringLiteral("me@example.com"));
        QCOMPARE(attribute(tessa, "placeholders"), QStringLiteral("1"));
        // The label is the template: the renderer replaces the placeholders.
        QVERIFY(q(tessa.value()).startsWith(QStringLiteral("%name%<br><i style=\"color:gray;\">%position%</i><br>")));
        // `# ignore: id,image,fill,stroke,refs,manager` removes those columns, `# link: url` renames one.
        for (const char* name : {"image", "fill", "stroke", "refs", "manager"})
            QCOMPARE(attribute(tessa, name), QStringLiteral("<none>"));
        QVERIFY(!tessa.id().empty()); // `id` is in the list too, but the id of the cell is not a property
        QCOMPARE(attribute(tessa, "link"), QStringLiteral("https://app.diagrams.net"));
        QCOMPARE(attribute(tessa, "url"), QStringLiteral("<none>"));
    }

    void thePlaceholdersOfTheStyleAreReplacedOnce()
    {
        const imports::Result r = imports::importCsv(imports::csvExample());
        const qraw::core::Style style = named(r, "Edward Morrison").style();
        QCOMPARE(q(style.value("strokeColor", "")), QStringLiteral("#82b366"));
        QCOMPARE(q(style.value("fillColor", "")), QStringLiteral("default"));
        QVERIFY(q(style.value("image", "")).endsWith(QStringLiteral("matureman1-782.svg")));
        QVERIFY(style.hasName("label"));
        QVERIFY(!q(named(r, "Edward Morrison").styleText()).contains('%'));
    }

    void connectionsFollowTheConnectDirectives()
    {
        const imports::Result r = imports::importCsv(imports::csvExample());
        const Cell& tessa = named(r, "Tessa Miller");
        const Cell& edward = named(r, "Edward Morrison");
        const Cell& alison = named(r, "Alison Donovan");
        const Cell& evan = named(r, "Evan Valet");
        int manages = 0;
        int refs = 0;
        for (const Cell* edge : edges(r)) {
            if (edge->value() == "manages") {
                ++manages;
                QCOMPARE(edge->source(), tessa.id()); // "invert": the manager is the source
                QVERIFY(edge->target() == edward.id() || edge->target() == alison.id() || edge->target() == evan.id());
                QVERIFY(edge->style().value("curved", "") == "1");
                QVERIFY(edge->style().value("endArrow", "") == "blockThin");
            } else {
                ++refs;
                QCOMPARE(edge->source(), alison.id()); // Alison refers to "emo" (Edward) and "tva" (Evan)
                QVERIFY(edge->target() == edward.id() || edge->target() == evan.id());
            }
        }
        QCOMPARE(manages, 3);
        QCOMPARE(refs, 2);
    }

    void autoLayoutOfTheExampleIsALayeredLayoutWithTheManagerOnTop()
    {
        const imports::Result r = imports::importCsv(imports::csvExample());
        const double tessaBottom = boundsOf(named(r, "Tessa Miller")).y + boundsOf(named(r, "Tessa Miller")).height;
        for (const char* name : {"Edward Morrison", "Alison Donovan", "Evan Valet"})
            QVERIFY(boundsOf(named(r, name)).y >= tessaBottom + 100 - 1); // `# levelspacing: 100`
        // The padding of -12 makes the cells 12 px narrower than their label.
        QVERIFY(boundsOf(named(r, "Tessa Miller")).width > 20);
        QCOMPARE(q(r.cells.front().style().value("x", "none")), QStringLiteral("none"));
    }

    void theExampleIsStableAndAnyPrefixIsSafe()
    {
        QVERIFY(imports::importCsv(imports::csvExample()).cells == imports::importCsv(imports::csvExample()).cells);
        truncationsAreSafe(imports::csvExample(), [](std::string_view t) { return imports::importCsv(t); });
    }

    // --- rows ---

    void quotedFieldsKeepCommasAndDoubledQuotes()
    {
        const imports::Result r = csv("name,note\n\"Smith, John\",\"say \"\"hi\"\"\"\nPlain,\"\"\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        const Cell& smith = named(r, "Smith, John");
        QCOMPARE(attribute(smith, "note"), QStringLiteral("say \"hi\""));
        QCOMPARE(attribute(named(r, "Plain"), "note"), QStringLiteral(""));
    }

    void theDefaultLabelIsTheFirstColumn()
    {
        const imports::Result r = csv("First Name,Other\nAda,1\n");
        QVERIFY(r.ok());
        QCOMPARE(q(vertices(r).front()->value()), QStringLiteral("%First_Name%"));
        QCOMPARE(attribute(*vertices(r).front(), "First_Name"), QStringLiteral("Ada")); // the column as an XML attribute name
        QCOMPARE(attribute(*vertices(r).front(), "Other"), QStringLiteral("1"));
    }

    void columnNamesBecomeAttributeNames()
    {
        const imports::Result r = csv("name,1st place,Weird -- Name!\nA,B,C\n");
        QVERIFY(r.ok());
        const Cell& cell = *vertices(r).front();
        QCOMPARE(attribute(cell, "st_place"), QStringLiteral("B"));
        QCOMPARE(attribute(cell, "Weird_Name"), QStringLiteral("C"));
    }

    void rowsWithoutTheirOwnLineAreSkippedAndExtraValuesWarn()
    {
        const imports::Result r = csv("a,b\n1,2\n\n3,4,5\n");
        QVERIFY(r.ok());
        QCOMPARE(vertices(r).size(), std::size_t(2));
        QCOMPARE(r.warnings.size(), std::size_t(1));
        QCOMPARE(r.warnings.front().line, 4);
    }

    void rowsAreStackedWithTheNodeSpacingWithoutALayout()
    {
        const imports::Result r = csv(withHeader("# layout: none\n# width: 100\n# height: 30\n# nodespacing: 10", "name\nA\nB\nC\n"));
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        const std::vector<const Cell*> v = vertices(r);
        QCOMPARE(boundsOf(*v[0]).y, 0.0);
        QCOMPARE(boundsOf(*v[1]).y, 40.0);
        QCOMPARE(boundsOf(*v[2]).y, 80.0);
        QCOMPARE(boundsOf(*v[0]).width, 100.0);
        QCOMPARE(boundsOf(*v[0]).height, 30.0);
    }

    // --- the header ---

    void aBackslashContinuesADirectiveOnTheNextCommentLine()
    {
        const imports::Result r = csv("# connect: {\"from\": \"to\", \"to\": \"name\", \\\n#   \"label\": \"goes\"}\nname,to\nA,B\nB,\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(edges(r).size(), std::size_t(1));
        QCOMPARE(q(edges(r).front()->value()), QStringLiteral("goes"));
    }

    void doubleHashLinesAreComments()
    {
        const imports::Result r = csv("## label: ignored\n##\n# label: Hello %name%\nname\nA\n");
        QVERIFY(r.ok());
        QCOMPARE(q(vertices(r).front()->value()), QStringLiteral("Hello %name%"));
        QVERIFY(r.warnings.empty());
    }

    void styleStylenameStylesAndVars()
    {
        const std::string header =
            "# style: rounded=%round%;fillColor=%color%;\n"
            "# stylename: kind\n"
            "# styles: {\"big\": \"shape=hexagon;fillColor=%color%;\", \"x\": \"ellipse;\"}\n"
            "# vars: {\"color\": \"#ff0000\"}\n";
        const imports::Result r = csv(withHeader(header, "name,kind,round\nA,big,1\nB,other,0\nC,x,1\n"));
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        const qraw::core::Style a = named(r, "A").style();
        QCOMPARE(q(a.value("shape", "")), QStringLiteral("hexagon"));
        QCOMPARE(q(a.value("fillColor", "")), QStringLiteral("#ff0000")); // from vars
        const qraw::core::Style b = named(r, "B").style(); // an unknown stylename value: the default `style`
        QCOMPARE(q(b.value("rounded", "")), QStringLiteral("0"));
        QCOMPARE(q(b.value("fillColor", "")), QStringLiteral("#ff0000"));
        QVERIFY(named(r, "C").style().hasName("ellipse"));
    }

    void anUnknownPlaceholderStaysAsItIs()
    {
        const imports::Result r = csv(withHeader("# style: fillColor=%nothing%;\n# parentstyle: a=%b%", "name\nA\n"));
        QVERIFY(r.ok());
        QCOMPARE(q(vertices(r).front()->style().value("fillColor", "")), QStringLiteral("%nothing%"));
    }

    void labelnameAndLabelsChooseTheLabelOfARow()
    {
        const std::string header = "# labelname: kind\n# labels: {\"a\": \"Alpha %name%\", \"b\": \"Beta\"}\n";
        const imports::Result r = csv(withHeader(header, "name,kind\nX,a\nY,b\nZ,c\n"));
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(q(named(r, "X").value()), QStringLiteral("Alpha %name%"));
        QCOMPARE(q(named(r, "Y").value()), QStringLiteral("Beta"));
        QCOMPARE(q(named(r, "Z").value()), QStringLiteral("%name%")); // no label for "c": the default
    }

    void widthAndHeightAcceptNumbersAutoColumnsAndWidth()
    {
        const std::string header = "# width: @w\n# height: width\n# layout: none\n";
        const imports::Result r = csv(withHeader(header, "name,w\nA,120\nB,auto\nC,\n"));
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(boundsOf(named(r, "A")).width, 120.0);
        QCOMPARE(boundsOf(named(r, "A")).height, 120.0);
        QVERIFY(boundsOf(named(r, "B")).width > 0); // auto: the label's size
    }

    void leftAndTopPlaceTheCellsWithoutALayout()
    {
        const imports::Result r = csv(withHeader("# left: x\n# top: y\n# layout: none\n# width: 10\n# height: 10", "name,x,y\nA,100,200\nB,300,50\n"));
        QVERIFY(r.ok());
        // Moved to the origin together: only the distances are kept.
        QCOMPARE(boundsOf(named(r, "B")).x - boundsOf(named(r, "A")).x, 200.0);
        QCOMPARE(boundsOf(named(r, "B")).y - boundsOf(named(r, "A")).y, -150.0);
    }

    void collapsedMarksTheCells()
    {
        const imports::Result r = csv(withHeader("# collapsed: true", "name\nA\n"));
        QVERIFY(vertices(r).front()->isCollapsed());
        QVERIFY(!csv("name\nA\n").cells.front().isCollapsed());
    }

    void ignoreRemovesPropertiesAfterTheConnectionsUsedThem()
    {
        const imports::Result r = csv(withHeader("# ignore: next\n# connect: {\"from\":\"next\",\"to\":\"name\"}", "name,next\nA,B\nB,\n"));
        QVERIFY(r.ok());
        QCOMPARE(edges(r).size(), std::size_t(1));
        QCOMPARE(attribute(named(r, "A"), "next"), QStringLiteral("<none>"));
    }

    void linkMovesAColumnToTheLinkProperty()
    {
        const imports::Result r = csv(withHeader("# link: homepage", "name,homepage\nA,https://example.com\n"));
        QVERIFY(r.ok());
        QCOMPARE(attribute(named(r, "A"), "link"), QStringLiteral("https://example.com"));
        QCOMPARE(attribute(named(r, "A"), "homepage"), QStringLiteral("<none>"));
    }

    // --- parents and identity ---

    void parentColumnsPutCellsInsideCellsByIdentity()
    {
        const std::string header = "# identity: id\n# parent: parent\n# parentstyle: swimlane;horizontal=1;startSize=30;\n"
                                   "# layout: none\n";
        const imports::Result r = csv(withHeader(header, "name,id,parent\nGroup,g,\nOne,a,g\nTwo,b,g\nOutside,c,\n"));
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        const Cell& group = named(r, "Group");
        QCOMPARE(named(r, "One").parent(), group.id());
        QCOMPARE(named(r, "Two").parent(), group.id());
        QVERIFY(named(r, "Outside").parent().empty());
        QVERIFY(group.style().hasName("swimlane")); // the parent style replaces the style
        QVERIFY(wellFormed(r));
        // The automatic size of the group holds its children and the title strip.
        const Rect g = boundsOf(group);
        for (const char* child : {"One", "Two"}) {
            const Rect c = boundsOf(named(r, child));
            QVERIFY(c.x >= 0 && c.y >= 30 && c.x + c.width <= g.width && c.y + c.height <= g.height);
        }
    }

    void withoutIdentityTheParentIsTheNumberOfAnEarlierRow()
    {
        const imports::Result r = csv(withHeader("# parent: parent\n# layout: none", "name,parent\nGroup,\nChild,1\nLater,5\n"));
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(named(r, "Child").parent(), named(r, "Group").id());
        QVERIFY(named(r, "Later").parent().empty()); // row 5 does not exist
    }

    void aRowWithTheSameIdentityAddsNoCellButItsConnections()
    {
        const std::string header = "# identity: id\n# connect: {\"from\":\"to\",\"to\":\"id\"}\n";
        const imports::Result r = csv(withHeader(header, "name,id,to\nA,a,\nB,b,a\nB again,b,\n"));
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(vertices(r).size(), std::size_t(2));
        QCOMPARE(edges(r).size(), std::size_t(1));
    }

    // --- connections ---

    void connectionLabelsAndStylesTakeDataFromBothEnds()
    {
        const std::string header =
            "# connect: {\"from\":\"to\",\"to\":\"name\",\"label\":\"-\",\"fromlabel\":\"kind\",\"tolabel\":\"kind\","
            "\"sourcelabel\":\"[%name%]\",\"targetlabel\":\"(%name%)\",\"style\":\"strokeColor=%color%;\",\"placeholders\":\"target\"}\n"
            "# layout: none\n";
        const imports::Result r = csv(withHeader(header, "name,to,kind,color\nA,B,x,red\nB,,y,blue\n"));
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(edges(r).size(), std::size_t(1));
        // As doImportCsv builds it: sourcelabel, fromlabel, label, tolabel, targetlabel (its own comment lists
        // fromlabel first, but its code puts each of the first two in front of what is there).
        QCOMPARE(q(edges(r).front()->value()), QStringLiteral("[A]x-y(B)"));
        QCOMPARE(q(edges(r).front()->style().value("strokeColor", "")), QStringLiteral("blue")); // placeholders=target
    }

    void aConnectionHasTheCurrentEdgeStyleWithoutAStyle()
    {
        const imports::Result r = csv(withHeader("# connect: {\"from\":\"to\",\"to\":\"name\"}", "name,to\nA,B\nB,\n"));
        QCOMPARE(q(edges(r).front()->style().value("edgeStyle", "")), QStringLiteral("orthogonalEdgeStyle"));
    }

    void referencesCanBeListedAndInverted()
    {
        const imports::Result r = csv(withHeader("# connect: {\"from\":\"refs\",\"to\":\"name\",\"invert\":true}", "name,refs\nA,\"B,C\"\nB,\nC,\n"));
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(edges(r).size(), std::size_t(2));
        for (const Cell* edge : edges(r))
            QCOMPARE(edge->target(), named(r, "A").id());
    }

    void extraLabelsAndDataBecomeChildrenAndProperties()
    {
        const std::string header = "# connect: {\"from\":\"to\",\"to\":\"name\",\"labels\":[{\"label\":\"start\",\"x\":-0.8,\"y\":5,\"dx\":2,\"dy\":3},"
                                   "{\"label\":\"%name%\",\"placeholders\":\"source\"}],\"data\":{\"weight\":3,\"kind\":\"strong\"}}\n";
        const imports::Result r = csv(withHeader(header, "name,to\nA,B\nB,\n"));
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        const Cell& edge = *edges(r).front();
        QCOMPARE(attribute(edge, "weight"), QStringLiteral("3"));
        QCOMPARE(attribute(edge, "kind"), QStringLiteral("strong"));
        std::vector<const Cell*> labels;
        for (const Cell& cell : r.cells) {
            if (cell.parent() == edge.id())
                labels.push_back(&cell);
        }
        QCOMPARE(labels.size(), std::size_t(2));
        QCOMPARE(q(labels[0]->value()), QStringLiteral("start"));
        QVERIFY(labels[0]->geometry()->relative());
        QCOMPARE(labels[0]->geometry()->x(), -0.8);
        QCOMPARE(labels[0]->geometry()->offset()->x, 2.0);
        QVERIFY(!labels[0]->isConnectable());
        QCOMPARE(q(labels[1]->value()), QStringLiteral("A")); // placeholders=source: the cell of the row
    }

    void unknownStyleMakesShapesForMissingReferences()
    {
        const imports::Result without = csv(withHeader("# connect: {\"from\":\"to\",\"to\":\"name\"}", "name,to\nA,Nowhere\n"));
        QVERIFY(without.ok());
        QCOMPARE(edges(without).size(), std::size_t(0)); // ignored
        const imports::Result with = csv(withHeader("# unknownStyle: ellipse;whiteSpace=wrap;html=1;\n# connect: {\"from\":\"to\",\"to\":\"name\"}",
                                                    "name,to\nA,Nowhere\nB,Nowhere\n"));
        QVERIFY2(with.ok(), qPrintable(errorText(with)));
        QCOMPARE(vertices(with).size(), std::size_t(3)); // one shared "Nowhere"
        QCOMPARE(edges(with).size(), std::size_t(2));
        QVERIFY(cellWithValue(with, "Nowhere") != nullptr);
        QVERIFY(cellWithValue(with, "Nowhere")->style().hasName("ellipse"));
    }

    // --- layouts ---

    void layoutNamesChooseTheArrangement()
    {
        const std::string rows = "name,to\nA,\"B,C\"\nB,\nC,\n";
        const std::string connect = "# connect: {\"from\":\"to\",\"to\":\"name\"}\n";
        const auto place = [&](const char* layout) { return csv(withHeader(connect + "# layout: " + layout, rows)); };
        const auto y = [](const imports::Result& r, const char* name) { return boundsOf(named(r, name)).y; };
        const auto x = [](const imports::Result& r, const char* name) { return boundsOf(named(r, name)).x; };

        for (const char* name : {"auto", "verticaltree", "verticalflow"}) {
            const imports::Result r = place(name);
            QVERIFY2(r.ok(), name);
            QVERIFY2(y(r, "B") > y(r, "A"), name);
            QVERIFY(noOverlaps(r));
        }
        for (const char* name : {"horizontaltree", "horizontalflow"}) {
            const imports::Result r = place(name);
            QVERIFY2(x(r, "B") > x(r, "A"), name);
        }
        for (const char* name : {"orgchart", "elkRadial", "organic", "circle", "elkOrganic"}) {
            const imports::Result r = place(name);
            QVERIFY2(r.ok() && r.warnings.empty(), name);
            QVERIFY2(noOverlaps(r), name);
        }
        const imports::Result none = place("none");
        QVERIFY(noOverlaps(none));
        QCOMPARE(boundsOf(named(none, "A")).x, boundsOf(named(none, "B")).x); // a column
        QVERIFY(place("auto").cells == place("verticaltree").cells || place("auto").cells == place("verticalflow").cells);
    }

    void spacingDirectivesReachTheLayout()
    {
        const std::string rows = "name,to\nA,B\nB,\n";
        const std::string connect = "# connect: {\"from\":\"to\",\"to\":\"name\"}\n# layout: verticaltree\n# width: 100\n# height: 40\n";
        const auto gap = [&](const std::string& extra) {
            const imports::Result r = csv(withHeader(connect + extra, rows));
            return boundsOf(named(r, "B")).y - boundsOf(named(r, "A")).y - 40;
        };
        QCOMPARE(gap("# levelspacing: 100"), 100.0);
        QCOMPARE(gap("# levelspacing: 30"), 30.0);
    }

    void severalRootsWithConnectionsChooseTheOrganicLayout()
    {
        const imports::Result r = csv(withHeader("# connect: {\"from\":\"to\",\"to\":\"name\"}", "name,to\nA,B\nB,\nC,D\nD,\n"));
        QVERIFY(r.ok());
        QVERIFY(noOverlaps(r));
        QVERIFY(r.warnings.empty());
    }

    void theForceDirectedLayoutIsLimitedToFiveHundredShapes()
    {
        std::string rows = "name,to\n";
        for (int i = 0; i < 501; ++i)
            rows += "n" + std::to_string(i) + (i >= 2 ? ",n" + std::to_string(i - 2) : std::string(",")) + "\n";
        const std::string connect = "# connect: {\"from\":\"to\",\"to\":\"name\"}\n";
        // Two roots with connections: auto would choose organic, which is too slow above 500 shapes.
        const imports::Result automatic = csv(connect + rows);
        QVERIFY2(automatic.ok(), qPrintable(errorText(automatic)));
        QVERIFY(automatic.warnings.empty());
        QVERIFY(boundsOf(named(automatic, "n0")).y > boundsOf(named(automatic, "n2")).y); // a layered layout: n2 -> n0 points down
        const imports::Result asked = csv(connect + "# layout: organic\n" + rows);
        QVERIFY(asked.ok());
        QCOMPARE(asked.warnings.size(), std::size_t(1));
        QVERIFY(q(asked.warnings.front().message).contains("500"));
    }

    // --- not supported ---

    void unknownAndUnsupportedDirectivesWarnWithTheirLine()
    {
        const imports::Result r = csv("# bogus: 1\n# container: true\n# list: items\n# layout: libavoid\n# nodespacing: x\n# label: %name%\nname,items\nA,x|y\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(r.warnings.size(), std::size_t(5));
        QCOMPARE(r.warnings[0].line, 1);
        QVERIFY(q(r.warnings[0].message).contains("bogus"));
        QCOMPARE(r.warnings[1].line, 2);
        QCOMPARE(r.warnings[2].line, 3);
        QCOMPARE(r.warnings[3].line, 5); // the invalid number, in the header; the layout warning comes last
        QCOMPARE(r.warnings[4].line, 0);
    }

    void aJsonLayoutWarnsAndLeavesTheColumn()
    {
        const imports::Result r = csv("# layout: [{\"layout\":\"x\"}]\nname\nA\nB\n");
        QVERIFY(r.ok());
        QVERIFY(!r.warnings.empty());
        QVERIFY(noOverlaps(r));
    }

    // --- errors ---

    void invalidJsonIsAnErrorOnItsLineAndNothingIsImported()
    {
        for (const char* directive : {"labels", "styles", "vars", "connect"}) {
            const imports::Result r = csv("# label: x\n# " + std::string(directive) + ": {\"a\": }\nname\nA\n");
            QVERIFY2(!r.ok(), directive);
            QVERIFY(r.cells.empty());
            QCOMPARE(r.errors.size(), std::size_t(1));
            QCOMPARE(r.errors.front().line, 2);
            QVERIFY(q(r.errors.front().message).contains(directive));
        }
    }

    void aConnectThatIsNotAnObjectIsAnError()
    {
        const imports::Result r = csv("# connect: [1]\nname\nA\n");
        QVERIFY(!r.ok());
        QCOMPARE(r.errors.front().line, 1);
    }

    void anUnclosedQuoteIsAnErrorWithItsLine()
    {
        const imports::Result r = csv("name,note\nA,\"fine\"\nB,\"broken\nC,x\n");
        QVERIFY(!r.ok());
        QCOMPARE(r.errors.size(), std::size_t(1));
        QCOMPARE(r.errors.front().line, 3);
        QVERIFY(q(r.errors.front().message).contains("B,"));
    }

    void aMissingHeaderRowIsAnError()
    {
        for (const char* text : {"", "# label: x\n", "# label: x\n\n", "\n"}) {
            const imports::Result r = csv(text);
            QVERIFY2(!r.ok(), text);
            QVERIFY(!r.errors.empty());
        }
        QVERIFY(!csv("name\n").ok()); // no rows
    }

    void tooManyRowsIsAnError()
    {
        std::string text = "name\n";
        for (int i = 0; i < 10001; ++i)
            text += "n" + std::to_string(i) + "\n";
        const imports::Result r = csv(text);
        QVERIFY(!r.ok());
        QVERIFY(r.cells.empty());
        QVERIFY(q(r.errors.front().message).contains("10000"));
    }

    void windowsLineBreaksAreAccepted()
    {
        const imports::Result r = csv("# layout: none\r\nname,to\r\nA,B\r\nB,\r\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(vertices(r).size(), std::size_t(2));
        QCOMPARE(attribute(named(r, "A"), "to"), QStringLiteral("B"));
    }

    void prefixesOfEveryDirectiveAreSafe()
    {
        const std::string text =
            "# label: %name%<br>%role%\n# style: rounded=1;fillColor=%c%;\n# parentstyle: swimlane;\n# unknownStyle: ellipse;\n# stylename: k\n"
            "# styles: {\"a\":\"ellipse;\"}\n# vars: {\"c\":\"#fff\"}\n# labelname: k\n# labels: {\"a\":\"L\"}\n# identity: id\n# parent: p\n"
            "# namespace: ns-\n# width: @w\n# height: auto\n# left: x\n# top: y\n# ignore: w\n# link: u\n# padding: 4\n# edgespacing: 10\n"
            "# nodespacing: 20\n# levelspacing: 50\n# layout: horizontalflow\n# connect: {\"from\":\"to\",\"to\":\"id\",\"labels\":[{\"label\":\"a\"}],\"data\":{\"k\":1}}\n"
            "name,id,p,to,k,c,w,x,y,u,role\n\"A, a\",a,,b,a,#f00,100,1,2,http://x,boss\nB,b,a,c,,#0f0,auto,3,4,,dev\nC,c,,,,,,,,,\n";
        truncationsAreSafe(text, [](std::string_view t) { return imports::importCsv(t); });
        QVERIFY2(csv(text).ok(), qPrintable(errorText(csv(text))));
    }
};

QTEST_APPLESS_MAIN(ImportCsvTest)
#include "tst_import_csv.moc"
