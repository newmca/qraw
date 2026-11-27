// Insert > Advanced > From Text (import_text.cpp): lists and diagrams, without a document.
#include "import_test_support.h"

using namespace import_test;
using imports::TextLayout;
using imports::TextType;

namespace {

imports::Result list(std::string_view text, TextLayout layout = TextLayout::VerticalTree)
{
    return imports::importText(text, TextType::List, layout);
}

imports::Result diagram(std::string_view text, TextLayout layout = TextLayout::VerticalFlow)
{
    return imports::importText(text, TextType::Diagram, layout);
}

} // namespace

class ImportTextTest : public QObject {
    Q_OBJECT

private slots:
    // --- list ---

    void aListIsATreeByIndentation()
    {
        const imports::Result r = list("Root\n  A\n    A1\n    A2\n  B\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(vertices(r).size(), std::size_t(5));
        QCOMPARE(edges(r).size(), std::size_t(4));
        QCOMPARE(edgesBetween(r, "Root", "A").size(), std::size_t(1));
        QCOMPARE(edgesBetween(r, "A", "A1").size(), std::size_t(1));
        QCOMPARE(edgesBetween(r, "A", "A2").size(), std::size_t(1));
        QCOMPARE(edgesBetween(r, "Root", "B").size(), std::size_t(1));
        QVERIFY(edgesBetween(r, "A", "B").empty());
        QVERIFY(wellFormed(r));
        QVERIFY(noOverlaps(r));
    }

    void aLineIsTheChildOfTheNearestLineWithLessIndentation()
    {
        // The indentation need not be regular (EditorUi.parseOutline).
        const imports::Result r = list("a\n      deep\n   shallow\n b\n");
        QVERIFY(r.ok());
        QCOMPARE(edgesBetween(r, "a", "deep").size(), std::size_t(1));
        QCOMPARE(edgesBetween(r, "a", "shallow").size(), std::size_t(1));
        QVERIFY(edgesBetween(r, "a", "b").size() == 1); // b has indentation 1 > 0: child of a
        QCOMPARE(edges(r).size(), std::size_t(3));
    }

    void aTabCountsAsFourSpaces()
    {
        const imports::Result r = list("a\n\tb\n    c\n");
        QVERIFY(r.ok());
        // b and c have the same indentation, so they are siblings.
        QCOMPARE(edgesBetween(r, "a", "b").size(), std::size_t(1));
        QCOMPARE(edgesBetween(r, "a", "c").size(), std::size_t(1));
        QVERIFY(edgesBetween(r, "b", "c").empty());
    }

    void blankLinesAreIgnoredAndTwoQuotesAreAnEmptyLabel()
    {
        const imports::Result r = list("\n\nroot\n\n  \"\"\n   \n  child\r\n");
        QVERIFY(r.ok());
        QCOMPARE(vertices(r).size(), std::size_t(3));
        QVERIFY(cellWithValue(r, "") != nullptr);
        QVERIFY(cellWithValue(r, "child") != nullptr);
    }

    void severalRootsAreSeparateTrees()
    {
        const imports::Result r = list("a\n  a1\nb\n  b1\n");
        QVERIFY(r.ok());
        QCOMPARE(edges(r).size(), std::size_t(2));
        QVERIFY(noOverlaps(r));
    }

    void labelsAreEscapedForHtml()
    {
        const imports::Result r = list("a < b & \"c\"\n");
        QVERIFY(r.ok());
        QCOMPARE(q(vertices(r).front()->value()), QStringLiteral("a &lt; b &amp; &quot;c&quot;"));
        QVERIFY(vertices(r).front()->style().value("html", "") == "1");
    }

    void verticalTreeGoesDownHorizontalTreeGoesRight()
    {
        const std::string text = "Root\n  A\n  B\n    B1\n";
        const imports::Result down = list(text, TextLayout::VerticalTree);
        const Rect rootDown = boundsOf(*cellWithValue(down, "Root"));
        QVERIFY(boundsOf(*cellWithValue(down, "A")).y > rootDown.y + rootDown.height);
        QVERIFY(boundsOf(*cellWithValue(down, "B1")).y > boundsOf(*cellWithValue(down, "B")).y);

        const imports::Result right = list(text, TextLayout::HorizontalTree);
        const Rect rootRight = boundsOf(*cellWithValue(right, "Root"));
        QVERIFY(boundsOf(*cellWithValue(right, "A")).x > rootRight.x + rootRight.width);
        QVERIFY(noOverlaps(right));
    }

    void aRadialTreePutsTheRootInTheMiddle()
    {
        const imports::Result r = list("Root\n  A\n  B\n  C\n  D\n", TextLayout::RadialTree);
        QVERIFY(r.ok());
        QVERIFY(noOverlaps(r));
        const Rect root = boundsOf(*cellWithValue(r, "Root"));
        for (const char* name : {"A", "B", "C", "D"}) {
            const Rect b = boundsOf(*cellWithValue(r, name));
            QVERIFY(b.x != root.x || b.y != root.y);
        }
    }

    void theTopLeftIsTheOrigin()
    {
        const imports::Result r = list("Root\n  A\n  B\n");
        double left = 1e9;
        double top = 1e9;
        for (const Cell* v : vertices(r)) {
            left = std::min(left, boundsOf(*v).x);
            top = std::min(top, boundsOf(*v).y);
        }
        QCOMPARE(left, 0.0);
        QCOMPARE(top, 0.0);
    }

    void aLabelWithoutMeasureGetsAnEstimateAndTheMeasureOverridesIt()
    {
        const imports::Result estimated = list("a very long label indeed, much longer than the others\n");
        QVERIFY(boundsOf(*vertices(estimated).front()).width > 300);
        imports::Options options;
        options.measure = [](std::string_view, std::string_view) { return qraw::core::Point{500, 90}; };
        const imports::Result measured = imports::importText("x\n", TextType::List, TextLayout::VerticalTree, options);
        QCOMPARE(boundsOf(*vertices(measured).front()).width, 500.0);
        QCOMPARE(boundsOf(*vertices(measured).front()).height, 90.0);
    }

    void theSameTextGivesTheSameCells()
    {
        const std::string text = "Root\n  A\n    A1\n  B\n  C\n";
        QVERIFY(list(text).cells == list(text).cells);
        QVERIFY(diagram("A -> B\nB -> C\nA -> C\n").cells == diagram("A -> B\nB -> C\nA -> C\n").cells);
    }

    void emptyTextIsAnError()
    {
        for (const char* text : {"", "\n\n  \n"}) {
            QVERIFY(!list(text).ok());
            QVERIFY(!list(text).errors.empty());
            QVERIFY(!diagram(text).ok());
        }
    }

    void tooManyLinesIsAnError()
    {
        std::string text;
        for (int i = 0; i < 10001; ++i)
            text += "n" + std::to_string(i) + "\n";
        const imports::Result r = list(text);
        QVERIFY(!r.ok());
        QVERIFY(r.cells.empty());
        QVERIFY(q(r.errors.front().message).contains("10000"));
    }

    // --- diagram ---

    void aChainIsOneConnectionPerArrow()
    {
        const imports::Result r = diagram("A -> B -> C\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(vertices(r).size(), std::size_t(3));
        QCOMPARE(edges(r).size(), std::size_t(2));
        QCOMPARE(edgesBetween(r, "A", "B").size(), std::size_t(1));
        QCOMPARE(edgesBetween(r, "B", "C").size(), std::size_t(1));
        QVERIFY(wellFormed(r));
    }

    void theSameNameIsTheSameVertex()
    {
        const imports::Result r = diagram("A -> B\nB -> C\nC -> A\nA -> B\n");
        QVERIFY(r.ok());
        QCOMPARE(vertices(r).size(), std::size_t(3));
        QCOMPARE(edges(r).size(), std::size_t(4));
        QCOMPARE(edgesBetween(r, "A", "B").size(), std::size_t(2)); // twice written, twice drawn
    }

    void aLineWithoutArrowIsAVertex()
    {
        const imports::Result r = diagram("Lonely\nA -> B\n");
        QVERIFY(r.ok());
        QCOMPARE(vertices(r).size(), std::size_t(3));
        QVERIFY(noOverlaps(r));
    }

    void arrowsHaveDirectionsAndOpenForms()
    {
        const imports::Result r = diagram("A -> B\nC <- D\nE <-> F\nG -- H\nI --> J\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(edges(r).size(), std::size_t(5));
        const auto style = [&](const char* from, const char* to) { return edgesBetween(r, from, to).front()->style(); };
        QVERIFY(style("A", "B").value("endArrow", "classic") == "classic");
        QVERIFY(style("A", "B").value("startArrow", "none") == "none");
        // "C <- D" keeps the order of the names; the arrowhead is at C.
        QVERIFY(style("C", "D").value("startArrow", "") == "classic");
        QVERIFY(style("C", "D").value("endArrow", "") == "none");
        QVERIFY(style("E", "F").value("startArrow", "") == "classic");
        QVERIFY(style("G", "H").value("endArrow", "") == "none");
        QVERIFY(style("G", "H").value("startArrow", "none") == "none");
        QVERIFY(style("I", "J").value("endArrow", "classic") == "classic");
    }

    void aLabelAfterAColonLabelsTheConnectionsOfTheLine()
    {
        const imports::Result r = diagram("A -> B -> C: step <1>\n");
        QVERIFY(r.ok());
        QCOMPARE(q(edgesBetween(r, "A", "B").front()->value()), QStringLiteral("step &lt;1&gt;"));
        QCOMPARE(q(edgesBetween(r, "B", "C").front()->value()), QStringLiteral("step &lt;1&gt;"));
    }

    void namesInQuotesMayHoldArrowsAndColons()
    {
        const imports::Result r = diagram("\"a -> b: c\" -> \"d\"\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(vertices(r).size(), std::size_t(2));
        QCOMPARE(edgesBetween(r, "a -&gt; b: c", "d").size(), std::size_t(1));
    }

    void commentsAndBlankLinesAreSkipped()
    {
        const imports::Result r = diagram("// a comment\n\nA -> B\n   // another\n");
        QVERIFY(r.ok());
        QCOMPARE(vertices(r).size(), std::size_t(2));
    }

    void aLabelWithoutConnectionIsAWarning()
    {
        const imports::Result r = diagram("A: nothing to label\nB -> C\n");
        QVERIFY(r.ok());
        QCOMPARE(r.warnings.size(), std::size_t(1));
        QCOMPARE(r.warnings.front().line, 1);
    }

    void verticalFlowGoesDownAndHorizontalFlowGoesRight()
    {
        const std::string text = "A -> B -> C\nA -> C\n";
        const imports::Result down = diagram(text, TextLayout::VerticalFlow);
        QVERIFY(boundsOf(*cellWithValue(down, "B")).y > boundsOf(*cellWithValue(down, "A")).y);
        QVERIFY(boundsOf(*cellWithValue(down, "C")).y > boundsOf(*cellWithValue(down, "B")).y);
        QVERIFY(noOverlaps(down));
        const imports::Result right = diagram(text, TextLayout::HorizontalFlow);
        QVERIFY(boundsOf(*cellWithValue(right, "B")).x > boundsOf(*cellWithValue(right, "A")).x);
        QVERIFY(boundsOf(*cellWithValue(right, "C")).x > boundsOf(*cellWithValue(right, "B")).x);
        QVERIFY(edgesBetween(right, "A", "B").front()->style().value("edgeStyle", "") == "orthogonalEdgeStyle");
    }

    void aLayoutOfTheOtherTypeFallsBackToTheTypesDefault()
    {
        QVERIFY(diagram("A -> B\n", TextLayout::RadialTree).ok());
        QVERIFY(list("A\n B\n", TextLayout::HorizontalFlow).ok());
        QCOMPARE(imports::textTypeOf(TextLayout::RadialTree), TextType::List);
        QCOMPARE(imports::textTypeOf(TextLayout::HorizontalFlow), TextType::Diagram);
    }

    // --- errors ---

    void aMissingNameIsAnErrorWithLineAndColumn()
    {
        imports::Result r = diagram("A -> B\nC ->\n");
        QVERIFY(!r.ok());
        QVERIFY(r.cells.empty());
        QCOMPARE(r.errors.size(), std::size_t(1));
        QCOMPARE(r.errors.front().line, 2);
        QVERIFY(r.errors.front().column >= 4);
        QVERIFY(q(imports::describe(r.errors.front())).startsWith(QStringLiteral("Line 2, column ")));

        r = diagram("  -> B\n");
        QVERIFY(!r.ok());
        QCOMPARE(r.errors.front().line, 1);

        r = diagram("A -> -> B\n");
        QVERIFY(!r.ok());
    }

    void anUnclosedQuoteIsAnError()
    {
        const imports::Result r = diagram("A -> B\n\"C -> D\n");
        QVERIFY(!r.ok());
        QCOMPARE(r.errors.front().line, 2);
        QCOMPARE(r.errors.front().column, 1);
    }

    void textAfterAClosingQuoteIsAnError()
    {
        const imports::Result r = diagram("\"A\" B -> C\n");
        QVERIFY(!r.ok());
        QCOMPARE(r.errors.front().line, 1);
    }

    void prefixesNeverCrash()
    {
        const std::string listText = "Root\n  Child one\n    \"\"\n\tTabbed\nOther root\n";
        const std::string diagramText = "A -> B -> C: label\n\"q -> x\" <- D\nE -- F\n// c\nG <-> H\n";
        truncationsAreSafe(listText, [](std::string_view t) { return list(t); });
        truncationsAreSafe(diagramText, [](std::string_view t) { return diagram(t); });
        // And with every direction.
        truncationsAreSafe(listText, [](std::string_view t) { return list(t, TextLayout::RadialTree); });
        truncationsAreSafe(diagramText, [](std::string_view t) { return diagram(t, TextLayout::HorizontalFlow); });
    }
};

QTEST_APPLESS_MAIN(ImportTextTest)
#include "tst_import_text.moc"
