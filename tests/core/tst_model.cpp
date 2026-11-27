#include "qraw/core/document.h"
#include "qraw/core/number.h"
#include "qraw/core/xml_node.h"

#include <QTest>

#include <algorithm>

using namespace qraw::core;

namespace {
std::vector<std::string> names(const Attributes& attrs)
{
    std::vector<std::string> out;
    for (const auto& a : attrs)
        out.push_back(a.name);
    return out;
}

std::vector<std::string> ids(const std::vector<const Cell*>& cells)
{
    std::vector<std::string> out;
    for (const Cell* c : cells)
        out.push_back(c->id());
    return out;
}

// 0 -> 1 (layer) -> {a, e(edge a->b), g(group) -> b}; 0 -> L2 (layer)
Page samplePage()
{
    Page page;
    page.setId("p1");
    page.addCell(Cell("0"));
    page.addCell(Cell::makeLayer("1", "0"));
    page.addCell(Cell::makeVertex("a", "1", "A", "rounded=1;", {10, 20, 80, 40}));
    page.addCell(Cell::makeEdge("e", "1", "", "endArrow=classic;", "a", "b"));
    page.addCell(Cell::makeVertex("g", "1", "", "group;", {0, 0, 200, 200}));
    page.addCell(Cell::makeVertex("b", "g", "B", "", {100, 100, 80, 40}));
    page.addCell(Cell::makeLayer("L2", "0"));
    return page;
}
} // namespace

class TestModel : public QObject {
    Q_OBJECT

private slots:
    void xmlNodeEqualityIgnoresSourceLine()
    {
        XmlNode a = XmlNode::element("foo", {{"b", "1"}, {"a", "2"}});
        a.children.push_back(XmlNode::textNode("hi"));
        XmlNode b = a;
        b.sourceLine = 42;
        QVERIFY(a == b);
        b.attributes = {{"a", "2"}, {"b", "1"}}; // order is significant
        QVERIFY(!(a == b));
        QCOMPARE(a.textContent(), std::string("hi"));
    }

    void attributesKeepOrderAndInsertCanonically()
    {
        Attributes attrs{{"zeta", "1"}, {"id", "x"}, {"parent", "1"}};
        attrs.set("zeta", "2"); // in place
        attrs.setOrdered("style", "s", {"id", "value", "style", "parent"});
        attrs.set("custom", "c"); // appended
        QCOMPARE(names(attrs), (std::vector<std::string>{"zeta", "id", "style", "parent", "custom"}));
        QCOMPARE(attrs.value("zeta"), std::string("2"));
        QVERIFY(attrs.remove("zeta"));
        QVERIFY(!attrs.has("zeta"));
    }

    void cellSettersUseCanonicalOrder()
    {
        Cell c;
        c.setTarget("t");
        c.setParent("1");
        c.setEdge(true);
        c.setStyleText("edgeStyle=orthogonalEdgeStyle;");
        c.setSource("s");
        c.setValue("");
        c.setId("e1");
        QCOMPARE(names(c.attributes()),
                 (std::vector<std::string>{"id", "value", "style", "edge", "parent", "source", "target"}));
        c.setEdge(false); // default -> omitted
        QVERIFY(!c.attributes().has("edge"));
        c.setConnectable(false);
        QCOMPARE(c.attributes().value("connectable"), std::string("0"));
    }

    void wrapperHoldsIdAndLabel()
    {
        Cell c = Cell::makeVertex("v", "1", "Label", "", {0, 0, 10, 10});
        c.setData("owner", "ops");
        QVERIFY(c.hasWrapper());
        QCOMPARE(c.id(), std::string("v"));
        QCOMPARE(c.value(), std::string("Label"));
        QCOMPARE(names(c.wrapper()->attributes), (std::vector<std::string>{"label", "owner", "id"}));
        QVERIFY(!c.attributes().has("id"));
        c.setValue("New");
        QCOMPARE(c.wrapper()->attributes.value("label"), std::string("New"));
    }

    void geometryEditsKeepUntouchedText()
    {
        Geometry g;
        g.attributes() = {{"x", "10.50"}, {"y", "20"}, {"width", "1e2"}, {"as", "geometry"}, {"foo", "bar"}};
        QCOMPARE(g.width(), 100.0);
        g.setY(25.25);
        QCOMPARE(g.attributes().value("x"), std::string("10.50")); // untouched
        QCOMPARE(g.attributes().value("y"), std::string("25.25"));
        g.setHeight(60);
        QCOMPARE(names(g.attributes()), (std::vector<std::string>{"x", "y", "width", "height", "as", "foo"}));

        g.setSourcePoint(Point{1, 2});
        g.setPoints({{5, 6}, {7.5, 8}});
        QCOMPARE(g.sourcePoint().value(), (Point{1, 2}));
        QCOMPARE(g.points(), (std::vector<Point>{{5, 6}, {7.5, 8}}));
        g.setSourcePoint(std::nullopt);
        g.setPoints({});
        QVERIFY(g.children().empty());
    }

    void numbersFormatLikeDrawio()
    {
        QCOMPARE(formatNumber(120), std::string("120"));
        QCOMPARE(formatNumber(-0.0), std::string("0"));
        QCOMPARE(formatNumber(0.1 + 0.2), std::string("0.30000000000000004"));
        QCOMPARE(formatNumber(12.5), std::string("12.5"));
        QVERIFY(!parseNumber("12px").has_value());
    }

    void numbersFormatExtremeMagnitudes()
    {
        // Fixed notation of these needs far more than any small buffer; the
        // result must still be a valid number that reads back exactly.
        for (const double v : {1e64, -1e70, 1.5e-70, 1e-300, 1.7e308, 5e-324, 1e21, 1e-7}) {
            const std::string text = formatNumber(v);
            QVERIFY2(parseNumber(text).has_value(), text.c_str());
            QCOMPARE(*parseNumber(text), v);
        }
        QCOMPARE(formatNumber(1e15), std::string("1000000000000000"));
        QCOMPARE(formatNumber(1e21), std::string("1e+21"));
        QCOMPARE(formatNumber(0.000001), std::string("0.000001"));
        QCOMPARE(parseNumber(" 7 ", 0), 7.0);
    }

    void pageStructureHelpers()
    {
        Page page = samplePage();
        QCOMPARE(page.cellCount(), std::size_t(7));
        QCOMPARE(page.rootCell()->id(), std::string("0"));
        QCOMPARE(ids(page.layers()), (std::vector<std::string>{"1", "L2"}));
        QCOMPARE(ids(page.children("1")), (std::vector<std::string>{"a", "e", "g"}));
        QCOMPARE(ids(page.descendants("1")), (std::vector<std::string>{"a", "e", "g", "b"}));
        QCOMPARE(page.zIndex("g").value(), std::size_t(4));

        const auto stats = page.stats();
        QCOMPARE(stats.vertices, std::size_t(3));
        QCOMPARE(stats.edges, std::size_t(1));
        QCOMPARE(stats.layers, std::size_t(2));
        QVERIFY(page.validate().empty());
    }

    void addAndTakeCells()
    {
        Page page = samplePage();
        QVERIFY(page.addCell(Cell("a")) == nullptr); // duplicate id rejected

        Cell* fresh = page.addCell(Cell::makeVertex("", "1", "", "", {}), 2);
        QVERIFY(fresh);
        const std::string freshId = fresh->id();
        QVERIFY(!freshId.empty());
        QCOMPARE(page.zIndex(freshId).value(), std::size_t(2));
        QVERIFY(page.generateId() != freshId);

        const auto taken = page.takeCell("a");
        QVERIFY(taken.has_value());
        QVERIFY(!page.findCell("a"));
        QCOMPARE(page.cellCount(), std::size_t(7));
    }

    void idIndexStaysConsistentThroughInsertsAndRemovals()
    {
        // The index is shifted, not rebuilt, on every insert and take: check it
        // against the entries after a mixed sequence.
        Page page = samplePage();
        for (int i = 0; i < 30; ++i) {
            std::optional<std::size_t> z;
            if (i % 3 == 0)
                z = static_cast<std::size_t>(i % 5); // inside
            else if (i % 3 == 1)
                z = 1000; // past the end
            QVERIFY(page.addCell(Cell::makeVertex("n" + std::to_string(i), "1", "", "", {}), z));
            if (i % 4 == 0)
                page.takeCell("n" + std::to_string(i / 2)); // sometimes absent: fine
        }
        QVERIFY(page.insertCellAt(0, Cell("front")));
        QVERIFY(page.takeCell("a").has_value());
        std::size_t cells = 0;
        for (std::size_t entry = 0; entry < page.rootEntries().size(); ++entry) {
            const auto* cell = std::get_if<Cell>(&page.rootEntries()[entry]);
            if (cell == nullptr)
                continue;
            ++cells;
            QCOMPARE(page.entryIndex(cell->id()).value(), entry);
            QCOMPARE(page.findCell(cell->id()), cell);
        }
        QCOMPARE(page.cellCount(), cells);
        QVERIFY(!page.findCell("a"));
    }

    void relationIndexesMatchAScanThroughMutations()
    {
        // children(), childPosition(), connectedEdges() and descendants() come
        // from indexes kept by addCell/takeCell/insertCellAt/replaceCell: compare
        // them with a scan of the entries after a mixed sequence (D-025).
        Page page = samplePage();
        for (int i = 0; i < 24; ++i) {
            const std::string id = "n" + std::to_string(i);
            const std::string parent = i % 3 == 0 ? "g" : "1";
            if (i % 4 == 3)
                page.addCell(Cell::makeEdge(id, parent, "", "", i % 2 ? "a" : "b", i % 3 ? "b" : "a"), static_cast<std::size_t>(i % 7));
            else
                page.addCell(Cell::makeVertex(id, parent, "", "", {}), i % 2 ? std::optional<std::size_t>() : i % 7);
            if (i % 5 == 0) {
                Cell moved = *page.findCell("n" + std::to_string(i / 2));
                moved.setParent(i % 2 ? "L2" : "g");
                moved.setSource(i % 3 ? "a" : "");
                page.replaceCell(std::move(moved));
            }
            if (i % 6 == 5)
                page.takeCell("n" + std::to_string(i - 3));
        }
        page.insertCellAt(2, Cell::makeVertex("late", "g", "", "", {}));
        QVERIFY(page.takeCell("g").has_value()); // its children stay listed under "g"

        for (const std::string parent : {"0", "1", "g", "L2", "gone"}) {
            std::vector<std::string> expected;
            for (const Cell* c : page.cells()) {
                if (c->parent() == parent && c->id() != parent)
                    expected.push_back(c->id());
            }
            QCOMPARE(ids(page.children(parent)), expected);
            QCOMPARE(page.childCount(parent), expected.size());
            for (std::size_t i = 0; i < expected.size(); ++i)
                QCOMPARE(page.childPosition(expected[i]).value(), i);
        }
        for (const std::string terminal : {"a", "b", "n3", "none"}) {
            std::vector<std::string> expected;
            for (const Cell* c : page.cells()) {
                if (c->isEdge() && (c->source() == terminal || c->target() == terminal))
                    expected.push_back(c->id());
            }
            QCOMPARE(ids(page.connectedEdges(terminal)), expected);
        }
        // Depth-first, z-order among siblings.
        std::vector<std::string> expected;
        const auto walk = [&](auto&& self, const std::string& parent) -> void {
            for (const Cell* c : page.cells()) {
                if (c->parent() == parent && c->id() != parent) {
                    expected.push_back(c->id());
                    self(self, c->id());
                }
            }
        };
        walk(walk, "0");
        QCOMPARE(ids(page.descendants("0")), expected);
    }

    void validationReportsButKeeps()
    {
        Page page = samplePage();
        page.appendEntry(Cell::makeEdge("x", "missing", "", "", "a", "nowhere"));
        page.appendEntry(Cell("a")); // duplicate kept on load
        const auto problems = page.validate();
        auto mentions = [&](std::string_view text) {
            return std::any_of(problems.begin(), problems.end(),
                               [&](const Diagnostic& d) { return d.message.find(text) != std::string::npos; });
        };
        QVERIFY(mentions("dangling parent 'missing'"));
        QVERIFY(mentions("dangling target 'nowhere'"));
        QVERIFY(mentions("duplicate cell id 'a'"));
        QCOMPARE(page.cellCount(), std::size_t(9));
    }

    void validationIsLinearOnLongParentChains()
    {
        // 20000 cells in one chain, and the same chain closed into a cycle.
        // The old per-cell walk made this O(n^2) (tens of seconds).
        constexpr int n = 20000;
        Page chain;
        chain.appendEntry(Cell::makeLayer("c0", ""));
        for (int i = 1; i < n; ++i)
            chain.appendEntry(Cell::makeVertex("c" + std::to_string(i), "c" + std::to_string(i - 1), "", "", Rect{}));
        QVERIFY(chain.validate().empty());

        Page loop;
        for (int i = 0; i < n; ++i)
            loop.appendEntry(Cell::makeVertex("c" + std::to_string(i), "c" + std::to_string((i + n - 1) % n), "", "", Rect{}));
        const auto problems = loop.validate();
        const auto cycles = std::count_if(problems.begin(), problems.end(), [](const Diagnostic& d) {
            return d.message.find("contains a cycle") != std::string::npos;
        });
        QCOMPARE(cycles, std::ptrdiff_t(n));
    }

    void documentPagesInterleaveWithForeignEntries()
    {
        Document doc;
        Page p1;
        p1.setId("one");
        doc.addPage(std::move(p1));
        doc.appendEntry(XmlNode::element("unknownThing"));
        Page p0;
        p0.setId("zero");
        doc.addPage(std::move(p0), 0);
        QCOMPARE(doc.pageCount(), std::size_t(2));
        QCOMPARE(doc.page(0)->id(), std::string("zero"));
        QVERIFY(doc.findPage("one"));
        QCOMPARE(doc.entries().size(), std::size_t(3));
        QVERIFY(std::holds_alternative<XmlNode>(doc.entries().back()));
    }
};

QTEST_GUILESS_MAIN(TestModel)
#include "tst_model.moc"
