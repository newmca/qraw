// Insert > Advanced > Mermaid (import_mermaid*.cpp): flowcharts and sequence diagrams, without a document.
#include "import_test_support.h"

using namespace import_test;

namespace {

imports::Result mermaid(const std::string& text) { return imports::importMermaid(text); }

// The vertex labelled `label`.
const Cell& node(const imports::Result& r, const std::string& label)
{
    const Cell* cell = cellWithValue(r, label);
    if (cell == nullptr || !cell->isVertex()) {
        QTest::qFail(qPrintable(QStringLiteral("no vertex labelled %1").arg(q(label))), __FILE__, __LINE__);
        static const Cell none;
        return none;
    }
    return *cell;
}

bool has(const Cell& cell, const char* key, const char* value) { return cell.style().value(key, "") == value; }

// The messages (edges between lifelines) in top-to-bottom order.
std::vector<const Cell*> messages(const imports::Result& r)
{
    std::vector<const Cell*> found;
    for (const Cell* edge : edges(r)) {
        if (!edge->source().empty())
            found.push_back(edge);
    }
    std::stable_sort(found.begin(), found.end(), [](const Cell* a, const Cell* b) {
        const auto y = [](const Cell* c) { return std::stod(c->style().value("exitDy", "0")); };
        return y(a) < y(b);
    });
    return found;
}

double exitY(const Cell& message) { return std::stod(message.style().value("exitDy", "0")); }

const std::string kFlowchart =
    "%% a comment\n"
    "flowchart LR\n"
    "    A([Start]) --> B{Is it working?}\n"
    "    B -- Yes --> C[Ship it]\n"
    "    B -->|No| D[(Check the data)]\n"
    "    D -.-> E[[Debug]]\n"
    "    E ==> B\n"
    "    C --- F((Done)) & G>Flag]\n"
    "    classDef green fill:#9f6,stroke:#333,stroke-width:4px\n"
    "    class C green\n"
    "    style D fill:#f9f\n"
    "    subgraph Release[Release Train]\n"
    "        direction TB\n"
    "        F\n"
    "    end\n";

const std::string kSequence =
    "sequenceDiagram\n"
    "    autonumber\n"
    "    participant A as Alice\n"
    "    actor B as Bob\n"
    "    A->>+B: Hello Bob\n"
    "    Note right of B: Bob thinks<br/>about it\n"
    "    B-->>-A: Hi Alice\n"
    "    A-xB: lost\n"
    "    B--)A: async\n"
    "    A->>A: to myself\n"
    "    loop Every minute\n"
    "        A->>B: ping\n"
    "    end\n"
    "    alt ok\n"
    "        B->A: yes\n"
    "    else not ok\n"
    "        B-->A: no\n"
    "    end\n"
    "    Note over A,B: done\n";

} // namespace

class ImportMermaidTest : public QObject {
    Q_OBJECT

private slots:
    // --- the type ---

    void otherDiagramTypesAreAnErrorNamingTheSupportedOnes()
    {
        for (const char* type : {"classDiagram", "stateDiagram-v2", "erDiagram", "gantt", "pie", "journey", "gitGraph", "mindmap", "timeline", "xychart-beta"}) {
            const imports::Result r = mermaid(std::string("%% comment\n\n") + type + "\n  a --> b\n");
            QVERIFY2(!r.ok(), type);
            QVERIFY(r.cells.empty());
            QCOMPARE(r.errors.size(), std::size_t(1));
            QCOMPARE(r.errors.front().line, 3);
            QVERIFY2(q(r.errors.front().message).contains(type), type);
            QVERIFY(q(r.errors.front().message).contains("flowchart (graph) and sequenceDiagram"));
        }
    }

    void emptyTextAndOnlyAHeaderAreErrors()
    {
        QVERIFY(!mermaid("").ok());
        QVERIFY(!mermaid("%% just a comment\n").ok());
        QVERIFY(!mermaid("graph TD\n").ok());
        QVERIFY(!mermaid("sequenceDiagram\n").ok());
        QVERIFY(!mermaid("sequenceDiagram extra\nA->>B: x\n").ok());
    }

    void frontMatterAndDirectivesAreIgnoredWithAWarning()
    {
        const imports::Result r = mermaid("---\ntitle: T\n---\n%%{init: {\"theme\": \"dark\"}}%%\ngraph TD\nA-->B\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(r.warnings.size(), std::size_t(2));
    }

    // --- flowchart: nodes ---

    void everyNodeShapeHasADrawioShape()
    {
        struct Case {
            const char* text;
            const char* key;
            const char* value;
        };
        const Case cases[] = {
            {"A[text]", "rounded", "0"},
            {"A(text)", "rounded", "1"},
            {"A([text])", "arcSize", "50"},
            {"A[[text]]", "shape", "process"},
            {"A[(text)]", "shape", "cylinder3"},
            {"A((text))", "aspect", "fixed"},
            {"A(((text)))", "shape", "doubleEllipse"},
            {"A>text]", "shape", "step"},
            {"A{text}", "", "rhombus"},
            {"A{{text}}", "shape", "hexagon"},
            {"A[/text/]", "shape", "parallelogram"},
            {"A[\\text\\]", "flipH", "1"},
            {"A[/text\\]", "shape", "trapezoid"},
            {"A[\\text/]", "flipV", "1"},
        };
        for (const Case& c : cases) {
            const imports::Result r = mermaid(std::string("graph TD\n") + c.text + "\n");
            QVERIFY2(r.ok(), c.text);
            QVERIFY2(vertices(r).size() == 1, c.text);
            const Cell& cell = *vertices(r).front();
            QCOMPARE(q(cell.value()), QStringLiteral("text"));
            if (std::string_view(c.key).empty())
                QVERIFY2(cell.style().hasName(c.value), c.text);
            else
                QVERIFY2(has(cell, c.key, c.value), c.text);
        }
    }

    void aNodeWithoutTextIsLabelledByItsId()
    {
        const imports::Result r = mermaid("graph TD\nfoo_1 --> Bär\n");
        QVERIFY(r.ok());
        QVERIFY(cellWithValue(r, "foo_1") != nullptr);
        QVERIFY(cellWithValue(r, "Bär") != nullptr);
    }

    void aLaterDefinitionChangesTheLabelAndShape()
    {
        const imports::Result r = mermaid("graph TD\nA --> B\nA{Decide}\nB[(Store)]\n");
        QVERIFY(r.ok());
        QCOMPARE(vertices(r).size(), std::size_t(2));
        QVERIFY(node(r, "Decide").style().hasName("rhombus"));
        QVERIFY(has(node(r, "Store"), "shape", "cylinder3"));
    }

    void textMayBeQuotedAndHoldLineBreaksAndEntities()
    {
        const imports::Result r = mermaid("graph TD\nA[\"a [b] (c) --> d\"] --> B[\"one<br/>two #quot;q#quot; #35;1 <i>x</i>\"]\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QVERIFY(cellWithValue(r, "a [b] (c) --&gt; d") != nullptr);
        QVERIFY2(cellWithValue(r, "one<br>two &quot;q&quot; #1 &lt;i&gt;x&lt;/i&gt;") != nullptr, "entities, <br> and escaping");
    }

    void sizesFollowTheLabelAndTheShape()
    {
        const imports::Result r = mermaid("graph TD\nA[Hi] --> B{Hi}\nB --> C((Hi))\n");
        const Rect rect = boundsOf(node(r, "Hi"));
        QVERIFY(rect.width >= 70 && rect.height >= 40);
        QVERIFY(noOverlaps(r));
        const imports::Result wide = mermaid("graph TD\nA[a label that is quite a lot longer than the other one]\n");
        QVERIFY(boundsOf(*vertices(wide).front()).width > 300);
    }

    // --- flowchart: links ---

    void arrowsAndTheirForms()
    {
        struct Case {
            const char* arrow;
            const char* key;
            const char* value;
        };
        const Case cases[] = {
            {"-->", "endArrow", "classic"},  {"---", "endArrow", "none"},   {"----->", "endArrow", "classic"},
            {"-.->", "dashed", "1"},          {"-..->", "dashed", "1"},       {"-.-", "endArrow", "none"},
            {"==>", "strokeWidth", "3"},      {"===", "endArrow", "none"},    {"--x", "endArrow", "cross"},
            {"--o", "endArrow", "oval"},      {"<-->", "startArrow", "classic"}, {"x--x", "startArrow", "cross"},
            {"o--o", "startArrow", "oval"},   {"~~~", "strokeColor", "none"}, {"<-.->", "startArrow", "classic"},
            {"<==>", "startArrow", "classic"},
        };
        for (const Case& c : cases) {
            const imports::Result r = mermaid(std::string("graph TD\nA ") + c.arrow + " B\n");
            QVERIFY2(r.ok(), c.arrow);
            QVERIFY2(edges(r).size() == 1, c.arrow);
            const Cell& edge = *edges(r).front();
            // classic is the default and is not written.
            const std::string actual = edge.style().value(c.key, std::string_view(c.value) == "classic" ? "classic" : "");
            QVERIFY2(actual == c.value, c.arrow);
        }
    }

    void linkTextCanComeBeforeOrAfterTheArrow()
    {
        const imports::Result r = mermaid("graph LR\nA -- t1 --> B\nA -->|t2| B\nA -. t3 .-> B\nA == t4 ==> B\nA -- t5 --- B\nA --\"t6\"--> B\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(edges(r).size(), std::size_t(6));
        const char* labels[] = {"t1", "t2", "t3", "t4", "t5", "t6"};
        for (std::size_t i = 0; i < 6; ++i)
            QCOMPARE(q(edges(r)[i]->value()), QString::fromLatin1(labels[i]));
        QVERIFY(has(*edges(r)[2], "dashed", "1"));
        QVERIFY(has(*edges(r)[3], "strokeWidth", "3"));
        QCOMPARE(q(edges(r)[1]->style().value("labelBackgroundColor", "")), QStringLiteral("default"));
    }

    void chainsAndGroupsMakeAllTheEdges()
    {
        const imports::Result r = mermaid("graph TD\nA --> B --> C\nA & B --> D & E\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(vertices(r).size(), std::size_t(5));
        QCOMPARE(edges(r).size(), std::size_t(2 + 4));
        QCOMPARE(edgesBetween(r, "A", "D").size(), std::size_t(1));
        QCOMPARE(edgesBetween(r, "B", "E").size(), std::size_t(1));
    }

    void semicolonsSeparateStatements()
    {
        const imports::Result r = mermaid("graph TD; A-->B; B-->C;\nC-->\"D;E\"[x];\n");
        // "D;E" in quotes is not a node id: the statement is an error, but the first ones are fine.
        QVERIFY(!r.ok());
        const imports::Result ok = mermaid("graph TD; A-->B; B-->C;\nC-->D;");
        QVERIFY2(ok.ok(), qPrintable(errorText(ok)));
        QCOMPARE(edges(ok).size(), std::size_t(3));
    }

    // --- flowchart: layout ---

    void directionsDecideWhereTheEdgesPoint()
    {
        const auto place = [](const char* direction) { return mermaid(std::string("graph ") + direction + "\nA --> B --> C\nA --> C\n"); };
        const auto c = [](const imports::Result& r, const char* name) { return boundsOf(node(r, name)); };
        for (const char* direction : {"TD", "TB"}) {
            const imports::Result r = place(direction);
            QVERIFY(c(r, "B").y > c(r, "A").y);
            QVERIFY(c(r, "C").y > c(r, "B").y);
        }
        const imports::Result bt = place("BT");
        QVERIFY(c(bt, "B").y < c(bt, "A").y);
        const imports::Result lr = place("LR");
        QVERIFY(c(lr, "B").x > c(lr, "A").x);
        const imports::Result rl = place("RL");
        QVERIFY(c(rl, "B").x < c(rl, "A").x);
        for (const char* direction : {"TD", "BT", "LR", "RL"})
            QVERIFY(noOverlaps(place(direction)));
        QVERIFY(!mermaid("graph XX\nA-->B\n").ok());
        QVERIFY(mermaid("graph\nA-->B\n").ok()); // no direction: top down
        QVERIFY(c(mermaid("graph\nA-->B\n"), "B").y > c(mermaid("graph\nA-->B\n"), "A").y);
    }

    void subgraphsAreContainersThatHoldTheirNodes()
    {
        const imports::Result r = mermaid("graph TD\nsubgraph one [First]\n  A --> B\nend\nsubgraph two\n  C\n  subgraph inner\n    D\n  end\nend\nB --> C\nA --> one\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QVERIFY(wellFormed(r));
        const Cell& one = node(r, "First");
        QVERIFY(has(one, "container", "1"));
        QCOMPARE(node(r, "A").parent(), one.id());
        QCOMPARE(node(r, "B").parent(), one.id());
        const Cell& two = node(r, "two");
        QCOMPARE(node(r, "C").parent(), two.id());
        const Cell& inner = node(r, "inner");
        QCOMPARE(inner.parent(), two.id());
        QCOMPARE(node(r, "D").parent(), inner.id());
        // The children are inside their container (relative coordinates).
        for (const char* child : {"A", "B"}) {
            const Rect c = boundsOf(node(r, child));
            const Rect g = boundsOf(one);
            QVERIFY(c.x >= 0 && c.y >= 30 && c.x + c.width <= g.width && c.y + c.height <= g.height);
        }
        QVERIFY(noOverlaps(r)); // the containers at the top level do not overlap
        // An edge may end at a subgraph.
        QCOMPARE(edgesBetween(r, "A", "First").size(), std::size_t(1));
        QCOMPARE(edgesBetween(r, "B", "C").size(), std::size_t(1));
    }

    void aNodeMentionedBeforeItsSubgraphMovesIntoIt()
    {
        const imports::Result r = mermaid("graph TD\nA --> B\nsubgraph s\n  A\nend\n");
        QVERIFY(r.ok());
        QCOMPARE(node(r, "A").parent(), node(r, "s").id());
        QVERIFY(node(r, "B").parent().empty());
    }

    void aSubgraphDirectionDecidesItsOwnLayout()
    {
        const imports::Result r = mermaid("graph TD\nsubgraph s\n  direction LR\n  A --> B\nend\n");
        QVERIFY(r.ok());
        QVERIFY(boundsOf(node(r, "B")).x > boundsOf(node(r, "A")).x);
        QCOMPARE(boundsOf(node(r, "B")).y, boundsOf(node(r, "A")).y);
    }

    // --- flowchart: styles ---

    void classDefClassAndStyleSetColours()
    {
        const imports::Result r = mermaid(kFlowchart);
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        const Cell& ship = node(r, "Ship it");
        QVERIFY(has(ship, "fillColor", "#9f6"));
        QVERIFY(has(ship, "strokeColor", "#333"));
        QVERIFY(has(ship, "strokeWidth", "4"));
        QVERIFY(has(node(r, "Check the data"), "fillColor", "#f9f")); // `style D fill:#f9f`
        QVERIFY(has(node(r, "Check the data"), "shape", "cylinder3")); // the shape stays
    }

    void classShorthandDefaultClassAndOtherProperties()
    {
        const imports::Result r = mermaid(
            "graph TD\nA:::hot --> B\nclassDef default fill:#eee,color:#123456,font-weight:bold,font-size:14px,stroke-dasharray: 5 5\n"
            "classDef hot fill:rgb(255, 0, 0),opacity:0.5\nclass B,A cold\nclassDef cold stroke:#00f\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        const Cell& a = node(r, "A");
        QVERIFY(has(a, "fillColor", "#ff0000")); // rgb() becomes hex, the later class wins over `default`
        QVERIFY(has(a, "fontColor", "#123456"));
        QVERIFY(has(a, "fontStyle", "1"));
        QVERIFY(has(a, "fontSize", "14"));
        QVERIFY(has(a, "dashed", "1"));
        QVERIFY(has(a, "dashPattern", "5 5"));
        QVERIFY(has(a, "opacity", "50"));
        QVERIFY(has(a, "strokeColor", "#00f"));
        QVERIFY(has(node(r, "B"), "fillColor", "#eee"));
    }

    void linkStyleColoursEdgesByNumber()
    {
        const imports::Result r = mermaid("graph TD\nA --> B\nB --> C\nlinkStyle 1 stroke:#f00,stroke-width:3px\nlinkStyle default stroke:#0f0\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QVERIFY(has(*edges(r)[1], "strokeColor", "#f00"));
        QVERIFY(has(*edges(r)[1], "strokeWidth", "3"));
        QVERIFY(has(*edges(r)[0], "strokeColor", "#0f0"));
    }

    void unsupportedStylePropertiesAndStatementsWarnOnce()
    {
        const imports::Result r = mermaid("graph TD\nA --> B\nclassDef x fill:#fff,rx:5,rx:6\nclass A x\nclick A href \"http://x\"\nstyle Z fill:#000\nclass Q x\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        int rx = 0;
        for (const imports::Diagnostic& d : r.warnings) {
            if (q(d.message).contains("'rx'"))
                ++rx;
        }
        QCOMPARE(rx, 1);
        QCOMPARE(r.warnings.size(), std::size_t(4)); // rx, click, style Z, class Q
    }

    // --- flowchart: errors ---

    void flowchartErrorsHaveLineAndColumn()
    {
        struct Case {
            const char* text;
            int line;
            int column;
        };
        const Case cases[] = {
            {"graph TD\nA --> B\nB [oops --> C\n", 3, 3},            // the text of the node is not closed
            {"graph TD\nA --> B\nA --> \n", 3, 6},                   // a node is missing after the arrow
            {"graph TD\nA B\n", 2, 3},                               // two nodes without an arrow
            {"graph TD\nA -- text B\n", 2, 3},                       // the link text is not closed
            {"graph TD\nA -->|oops B\n", 2, 6},                      // the |text| is not closed
            {"graph TD\nA[\"oops] --> B\n", 2, 3},                   // the quote is not closed
            {"graph TD\nsubgraph s\nA --> B\n", 2, 0},               // no matching end (reported on the line that opened it)
            {"graph TD\nA --> B\nend\n", 3, 1},                      // end without a subgraph
            {"graph TD\nA --> B\n  --> C\n", 3, 3},                  // starts with an arrow
            {"graph TD\nA@{ shape: rect } --> B\n", 2, 2},           // the new shape syntax
            {"graph TD\nA:::\n", 2, 5},                              // class name missing
            {"graph TD\nclassDef onlyname\n", 2, 1},
        };
        for (const Case& c : cases) {
            const imports::Result r = mermaid(c.text);
            QVERIFY2(!r.ok(), c.text);
            QVERIFY2(r.cells.empty(), c.text);
            QCOMPARE(r.errors.size(), std::size_t(1));
            QVERIFY2(r.errors.front().line == c.line, c.text);
            if (c.column > 0)
                QVERIFY2(r.errors.front().column == c.column, qPrintable(QStringLiteral("%1: column %2 (wanted %3)").arg(q(c.text)).arg(r.errors.front().column).arg(c.column)));
        }
    }

    void tooManyNodesIsAnError()
    {
        std::string text = "graph TD\n";
        for (int i = 0; i < 10001; ++i)
            text += "n" + std::to_string(i) + "\n";
        const imports::Result r = mermaid(text);
        QVERIFY(!r.ok());
        QVERIFY(q(r.errors.front().message).contains("10000"));
    }

    // --- flowchart: the whole ---

    void theFlowchartFixtureImports()
    {
        const imports::Result r = mermaid(kFlowchart);
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QVERIFY(wellFormed(r));
        QCOMPARE(edges(r).size(), std::size_t(7));
        QVERIFY(noOverlaps(r));
        // LR: Start is at the left of the decision.
        QVERIFY(boundsOf(node(r, "Start")).x < boundsOf(node(r, "Is it working?")).x);
        // F is in the subgraph; the edge from C to F crosses its border.
        QCOMPARE(node(r, "Done").parent(), node(r, "Release Train").id());
        // Top-left is the origin.
        double left = 1e9;
        double top = 1e9;
        for (const Cell* v : vertices(r)) {
            if (v->parent().empty()) {
                left = std::min(left, boundsOf(*v).x);
                top = std::min(top, boundsOf(*v).y);
            }
        }
        QVERIFY(left == 0 && top == 0);
    }

    void theExampleOfTheDialogImports()
    {
        const imports::Result r = imports::importMermaid(imports::mermaidExample());
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QVERIFY2(r.warnings.empty(), "the example uses only supported syntax");
        QVERIFY(vertices(r).size() >= 7);
    }

    void theSameTextGivesTheSameCells()
    {
        QVERIFY(mermaid(kFlowchart).cells == mermaid(kFlowchart).cells);
        QVERIFY(mermaid(kSequence).cells == mermaid(kSequence).cells);
    }

    // --- sequence diagrams ---

    void participantsKeepTheirOrderAndAliases()
    {
        const imports::Result r = mermaid("sequenceDiagram\nparticipant B as Bob\nparticipant A\nactor C as Carol Smith\nA->>D: hi\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        std::vector<const Cell*> lifelines;
        for (const Cell* v : vertices(r)) {
            if (v->style().value("shape", "") == "umlLifeline")
                lifelines.push_back(v);
        }
        QCOMPARE(lifelines.size(), std::size_t(4)); // D is made by its first message
        const char* labels[] = {"Bob", "A", "Carol Smith", "D"};
        for (std::size_t i = 0; i < 4; ++i) {
            QCOMPARE(q(lifelines[i]->value()), QString::fromLatin1(labels[i]));
            if (i > 0)
                QVERIFY(boundsOf(*lifelines[i]).x > boundsOf(*lifelines[i - 1]).x + boundsOf(*lifelines[i - 1]).width);
            QCOMPARE(boundsOf(*lifelines[i]).y, 0.0);
        }
        QVERIFY(has(*lifelines[2], "participant", "umlActor"));
        QVERIFY(!lifelines[0]->style().has("participant"));
        QCOMPARE(boundsOf(*lifelines[0]).height, boundsOf(*lifelines[3]).height);
        QVERIFY(wellFormed(r));
    }

    void everyArrowHasItsEndAndLineStyle()
    {
        struct Case {
            const char* arrow;
            const char* endArrow;
            bool dashed;
        };
        const Case cases[] = {
            {"->>", "block", false}, {"-->>", "block", true}, {"->", "none", false}, {"-->", "none", true},
            {"-x", "cross", false},  {"--x", "cross", true},  {"-)", "open", false}, {"--)", "open", true},
        };
        for (const Case& c : cases) {
            const imports::Result r = mermaid(std::string("sequenceDiagram\nA") + c.arrow + "B: text\n");
            QVERIFY2(r.ok(), c.arrow);
            const std::vector<const Cell*> m = messages(r);
            QVERIFY2(m.size() == 1, c.arrow);
            QVERIFY2(has(*m[0], "endArrow", c.endArrow), c.arrow);
            QVERIFY2(m[0]->style().value("dashed", "0") == (c.dashed ? "1" : "0"), c.arrow);
            QCOMPARE(q(m[0]->value()), QStringLiteral("text"));
        }
        const imports::Result both = mermaid("sequenceDiagram\nA<<->>B: x\n");
        QVERIFY(both.ok());
        QVERIFY(has(*messages(both)[0], "startArrow", "block"));
    }

    void messagesAreConnectedHorizontalAndInOrder()
    {
        const imports::Result r = mermaid("sequenceDiagram\nA->>B: one\nB->>A: two\nA->>C: three\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        const std::vector<const Cell*> m = messages(r);
        QCOMPARE(m.size(), std::size_t(3));
        QCOMPARE(q(m[0]->value()), QStringLiteral("one"));
        QCOMPARE(q(m[1]->value()), QStringLiteral("two"));
        QCOMPARE(q(m[2]->value()), QStringLiteral("three"));
        QVERIFY(exitY(*m[1]) > exitY(*m[0]));
        QVERIFY(exitY(*m[2]) > exitY(*m[1]));
        for (const Cell* message : m) {
            // Leaves and enters at the same height, in the middle of the lifelines (no perimeter).
            QCOMPARE(message->style().value("entryDy", "x"), message->style().value("exitDy", "y"));
            QCOMPARE(q(message->style().value("exitPerimeter", "")), QStringLiteral("0"));
            QCOMPARE(q(message->style().value("exitX", "")), QStringLiteral("0.5"));
            QVERIFY(!message->source().empty());
            QVERIFY(!message->target().empty());
        }
        QVERIFY(m[0]->source() != m[0]->target());
        QCOMPARE(m[0]->target(), m[1]->source());
    }

    void columnsAreWideEnoughForTheMessageText()
    {
        const imports::Result r = mermaid("sequenceDiagram\nA->>B: a message with quite a lot of text in it\n");
        QVERIFY(r.ok());
        double a = 0, b = 0;
        for (const Cell* v : vertices(r)) {
            (v->value() == "A" ? a : b) = boundsOf(*v).x + boundsOf(*v).width / 2;
        }
        QVERIFY2(b - a > 7 * 40, "the columns are spread for the text");
    }

    void aSelfMessageLoopsBackOnTheSameLifeline()
    {
        const imports::Result r = mermaid("sequenceDiagram\nA->>A: think\nB->>A: next\n");
        QVERIFY(r.ok());
        const std::vector<const Cell*> m = messages(r);
        QVERIFY(m[0]->source() == m[0]->target());
        const std::vector<qraw::core::Point> points = m[0]->geometry()->points();
        QCOMPARE(points.size(), std::size_t(2));
        QVERIFY(points[1].y > points[0].y);
        QCOMPARE(points[0].x, points[1].x);
        QVERIFY(exitY(*m[1]) > std::stod(m[0]->style().value("entryDy", "0"))); // the next row is below the loop
    }

    void activationBarsFromShorthandAndStatements()
    {
        const imports::Result r = mermaid("sequenceDiagram\nA->>+B: call\nB->>+C: nested\nC-->>-B: back\nB-->>-A: done\nactivate A\nA->>B: x\ndeactivate A\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        std::vector<const Cell*> bars;
        for (const Cell* v : vertices(r)) {
            if (v->value().empty() && v->style().value("shape", "").empty() && v->style().hasName("html") == false && v->style().has("points"))
                bars.push_back(v);
        }
        QCOMPARE(bars.size(), std::size_t(3)); // B, C and the one of `activate A`
        for (const Cell* bar : bars) {
            QCOMPARE(boundsOf(*bar).width, 10.0);
            QVERIFY(boundsOf(*bar).height >= 10);
        }
        // B is active from "call" to "done": the arrows of "call" arrive at the edge of the bar.
        const std::vector<const Cell*> m = messages(r);
        QVERIFY(std::stod(m[0]->style().value("entryDx", "0")) < 0); // from the left, at the left edge of the bar
        QVERIFY(std::stod(m[1]->style().value("exitDx", "0")) > 0);  // B leaves to the right from its bar
    }

    void deactivateWithoutActivateIsAnError()
    {
        const imports::Result r = mermaid("sequenceDiagram\nA->>B: x\ndeactivate B\n");
        QVERIFY(!r.ok());
        QCOMPARE(r.errors.front().line, 3);
    }

    void notesSitNextToOrOverTheirParticipants()
    {
        const imports::Result r = mermaid("sequenceDiagram\nA->>B: x\nNote left of A: left\nNote right of B: right\nNote over A: over a\nNote over A,B: both\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        const auto center = [&](const char* label) {
            const Rect b = boundsOf(node(r, label));
            return b.x + b.width / 2;
        };
        const Rect a = boundsOf(node(r, "A"));
        const Rect b = boundsOf(node(r, "B"));
        const double ac = a.x + a.width / 2;
        const double bc = b.x + b.width / 2;
        QVERIFY(boundsOf(node(r, "left")).x + boundsOf(node(r, "left")).width <= ac);
        QVERIFY(boundsOf(node(r, "right")).x >= bc);
        QVERIFY(qAbs(center("over a") - ac) < 1);
        QVERIFY(qAbs(center("both") - (ac + bc) / 2) < 1);
        QVERIFY(boundsOf(node(r, "both")).width >= bc - ac);
        QVERIFY(node(r, "left").style().hasName("note") || has(node(r, "left"), "shape", "note"));
    }

    void noteLineBreaksAndAutonumber()
    {
        const imports::Result r = mermaid("sequenceDiagram\nautonumber\nA->>B: first\nB->>A: second\nNote over A: two<br/>lines\n");
        QVERIFY(r.ok());
        const std::vector<const Cell*> m = messages(r);
        QCOMPARE(q(m[0]->value()), QStringLiteral("1. first"));
        QCOMPARE(q(m[1]->value()), QStringLiteral("2. second"));
        QVERIFY(cellWithValue(r, "two<br>lines") != nullptr);
    }

    void blocksAreFramesThatSpanTheirMessages()
    {
        const imports::Result r = mermaid(
            "sequenceDiagram\nA->>B: before\nloop Every minute\n  B->>C: ping\n  opt maybe\n    C->>B: pong\n  end\nend\nalt ok\n  A->>B: yes\nelse not ok\n  A->>B: no\nend\nB->>C: after\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        std::vector<const Cell*> frames;
        for (const Cell* v : vertices(r)) {
            if (v->style().value("shape", "") == "umlFrame")
                frames.push_back(v);
        }
        QCOMPARE(frames.size(), std::size_t(3)); // loop, opt, alt
        QCOMPARE(q(frames[0]->value()), QStringLiteral("loop"));
        QCOMPARE(q(frames[1]->value()), QStringLiteral("opt"));
        QCOMPARE(q(frames[2]->value()), QStringLiteral("alt"));
        QVERIFY(cellWithValue(r, "[Every minute]") != nullptr);
        QVERIFY(cellWithValue(r, "[maybe]") != nullptr);
        QVERIFY(cellWithValue(r, "[not ok]") != nullptr);
        // The loop holds B and C (and not A); the opt is inside the loop.
        const Rect loop = boundsOf(*frames[0]);
        const Rect opt = boundsOf(*frames[1]);
        QVERIFY(opt.x >= loop.x && opt.x + opt.width <= loop.x + loop.width);
        QVERIFY(opt.y >= loop.y && opt.y + opt.height <= loop.y + loop.height);
        const std::vector<const Cell*> m = messages(r);
        QVERIFY(exitY(*m[0]) < loop.y); // "before" is above the loop
        QVERIFY(exitY(*m[1]) > loop.y && exitY(*m[1]) < loop.y + loop.height);
        // The else divider is a dashed line across the alt.
        int dividers = 0;
        for (const Cell* e : edges(r)) {
            if (e->source().empty() && e->geometry()->sourcePoint() && e->geometry()->targetPoint())
                ++dividers;
        }
        QCOMPARE(dividers, 1);
    }

    void parAndCriticalDividersAndIgnoredBlocks()
    {
        const imports::Result r = mermaid("sequenceDiagram\npar a\n A->>B: x\nand b\n A->>C: y\nend\ncritical c\n A->>B: z\noption o\n B->>A: w\nend\nrect rgb(0,255,0)\n A->>B: v\nend\nbox Aqua Group\nparticipant Q\nend\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(r.warnings.size(), std::size_t(2)); // rect and box
        int frames = 0;
        for (const Cell* v : vertices(r))
            frames += v->style().value("shape", "") == "umlFrame";
        QCOMPARE(frames, 2);
    }

    void sequenceErrorsHaveLineAndColumn()
    {
        struct Case {
            const char* text;
            int line;
            int column;
        };
        const Case cases[] = {
            {"sequenceDiagram\nA->>B: ok\nA ~> B: bad\n", 3, 3},
            {"sequenceDiagram\nA->>\n", 2, 5},
            {"sequenceDiagram\nA->>B nocolon\n", 2, 7},
            {"sequenceDiagram\nloop forever\nA->>B: x\n", 2, 0},
            {"sequenceDiagram\nend\n", 2, 1},
            {"sequenceDiagram\nelse x\n", 2, 1},
            {"sequenceDiagram\nloop a\nelse b\nend\n", 3, 1},
            {"sequenceDiagram\nNote above A: x\n", 2, 1},
            {"sequenceDiagram\nNote over A\n", 2, 1},
            {"sequenceDiagram\nNote left of A,B: x\n", 2, 1},
            {"sequenceDiagram\nparticipant\n", 2, 1},
            {"sequenceDiagram\nparticipant A is B\n", 2, 1},
            {"sequenceDiagram\nactivate\n", 2, 1},
            {"sequenceDiagram\nhello\n", 2, 1},
        };
        for (const Case& c : cases) {
            const imports::Result r = mermaid(c.text);
            QVERIFY2(!r.ok(), c.text);
            QCOMPARE(r.errors.size(), std::size_t(1));
            QVERIFY2(r.errors.front().line == c.line, qPrintable(QStringLiteral("%1: line %2").arg(q(c.text)).arg(r.errors.front().line)));
            if (c.column > 0)
                QVERIFY2(r.errors.front().column == c.column, qPrintable(QStringLiteral("%1: column %2").arg(q(c.text)).arg(r.errors.front().column)));
        }
    }

    void theSequenceFixtureImports()
    {
        const imports::Result r = mermaid(kSequence);
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QVERIFY(wellFormed(r));
        QVERIFY(r.warnings.empty());
        QCOMPARE(messages(r).size(), std::size_t(8));
        double left = 1e9;
        double top = 1e9;
        for (const Cell& c : r.cells) {
            if (c.isVertex() && c.parent().empty()) {
                left = std::min(left, boundsOf(c).x);
                top = std::min(top, boundsOf(c).y);
            }
        }
        QVERIFY(left >= 0 && top >= 0);
        QVERIFY(left == 0 || top == 0);
    }

    void unsupportedSequenceStatementsWarn()
    {
        const imports::Result r = mermaid("sequenceDiagram\ntitle My title\nA->>B: x\ndestroy B\nlinks A: {}\n");
        QVERIFY2(r.ok(), qPrintable(errorText(r)));
        QCOMPARE(r.warnings.size(), std::size_t(3));
    }

    // --- robustness ---

    void prefixesNeverCrash()
    {
        truncationsAreSafe(kFlowchart, [](std::string_view t) { return imports::importMermaid(t); });
        truncationsAreSafe(kSequence, [](std::string_view t) { return imports::importMermaid(t); });
        truncationsAreSafe(imports::mermaidExample(), [](std::string_view t) { return imports::importMermaid(t); });
    }

    void everyCutOfASmallFlowchartIsSafe()
    {
        // Every prefix, character by character, of a text that uses each kind of token.
        const std::string text = "graph LR\nA[\"q x\"] -- t --> B{{h}} & C((c))\nB -.-> D>f]\nsubgraph s [T]\nD\nend\nclassDef c fill:#fff\nclass A c\nA:::c==>B\n";
        for (std::size_t length = 0; length <= text.size(); ++length) {
            const imports::Result r = mermaid(text.substr(0, length));
            QVERIFY2(r.ok() || !r.errors.empty(), qPrintable(QString::number(length)));
        }
    }
};

QTEST_APPLESS_MAIN(ImportMermaidTest)
#include "tst_import_mermaid.moc"
