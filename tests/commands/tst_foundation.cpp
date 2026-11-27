// Primitives, CommandGroup, Transaction, ChangeSet and queries: the
// foundation the operation packages build on. Includes a corpus run that
// mutates every file with primitives only and checks undo/redo identity.
#include "corpus_harness.h"

#include "qraw/commands/change_set.h"
#include "qraw/commands/command_group.h"
#include "qraw/commands/primitives.h"
#include "qraw/commands/queries.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/document.h"

#include <QTest>

using namespace qraw;
using namespace qraw::commands;

namespace {

core::XmlNode foreignNode(const char* name) { return core::XmlNode::element(name, {{"keep", "me"}}); }

// <root>: 0, 1, v2, <foreign/>, v3(child of v2), e4(v2->v3)
core::Document sampleDocument()
{
    core::Page page;
    page.setId("p1");
    page.setName("Page-1");
    page.appendEntry(core::Cell("0"));
    page.appendEntry(core::Cell::makeLayer("1", "0"));
    page.appendEntry(core::Cell::makeVertex("2", "1", "A", "rounded=1;", {10.5, 20, 100, 50}));
    page.appendEntry(foreignNode("custom"));
    page.appendEntry(core::Cell::makeVertex("3", "2", "B", "", {5, 5, 20, 20}));
    page.appendEntry(core::Cell::makeEdge("4", "1", "", "", "2", "3"));
    core::Document doc;
    doc.appendEntry(foreignNode("before"));
    doc.appendEntry(std::move(page));
    doc.appendEntry(foreignNode("between"));
    core::Page second;
    second.setId("p2");
    doc.appendEntry(std::move(second));
    return doc;
}

std::vector<core::Page::RootEntry> entriesOf(const core::Document& doc, std::size_t page = 0)
{
    return doc.page(page)->rootEntries();
}

std::vector<std::string> idsInEntryOrder(const core::Page& page)
{
    std::vector<std::string> out;
    for (const auto& e : page.rootEntries())
        out.push_back(std::holds_alternative<core::Cell>(e) ? std::get<core::Cell>(e).id() : "#");
    return out;
}

} // namespace

class TestFoundation : public QObject {
    Q_OBJECT

private slots:
    void changeSetFolding()
    {
        ChangeSet a;
        a.cellAdded("p", "x");
        a.cellChanged("p", "x");
        a.cellRemoved("p", "x");
        QVERIFY(a.empty());

        ChangeSet b;
        b.cellRemoved("p", "y");
        ChangeSet later;
        later.cellAdded("p", "y");
        b.merge(later);
        QVERIFY(b.pages().at("p").changed.contains("y"));
        QVERIFY(b.pages().at("p").removed.empty());

        ChangeSet c;
        c.pageAdded("q");
        c.cellChanged("q", "1");
        c.pageRemoved("q");
        QVERIFY(c.empty());

        ChangeSet d;
        d.pageRemoved("r");
        d.pageAdded("r");
        QVERIFY(d.pages().at("r").reset);
        QVERIFY(d.pagesAdded().empty() && d.pagesRemoved().empty());
    }

    void removeAndInsertRestoreExactPositions()
    {
        core::Document doc = sampleDocument();
        const auto original = entriesOf(doc);
        Transaction tx(doc);
        QVERIFY(tx.emplace<RemoveCell>(0, "3"));
        QVERIFY(tx.emplace<RemoveCell>(0, "2"));
        QVERIFY(!tx.emplace<RemoveCell>(0, "missing"));
        QCOMPARE(idsInEntryOrder(*doc.page(0)), (std::vector<std::string>{"0", "1", "#", "4"}));
        tx.rollback();
        QVERIFY(entriesOf(doc) == original);

        Transaction tx2(doc);
        QVERIFY(!tx2.emplace<InsertCell>(0, 0, core::Cell("2"))); // id in use
        QVERIFY(!tx2.emplace<InsertCell>(0, 99, core::Cell("9"))); // out of range
        QVERIFY(tx2.emplace<InsertCell>(0, 3, core::Cell::makeVertex("9", "1", "", "", {})));
        QCOMPARE(idsInEntryOrder(*doc.page(0)), (std::vector<std::string>{"0", "1", "2", "9", "#", "3", "4"}));
        const auto changes = tx2.changes();
        QVERIFY(changes.pages().at("p1").added.contains("9"));
    }

    void destructorRollsBack()
    {
        core::Document doc = sampleDocument();
        const auto original = entriesOf(doc);
        {
            Transaction tx(doc);
            core::Cell changed = *doc.page(0)->findCell("2");
            changed.setValue("changed");
            QVERIFY(tx.emplace<ReplaceCell>(0, changed));
        }
        QVERIFY(entriesOf(doc) == original);
    }

    void savepointRollsBackOnlyNestedPart()
    {
        core::Document doc = sampleDocument();
        Transaction tx(doc);
        core::Cell a = *doc.page(0)->findCell("2");
        a.setValue("outer");
        QVERIFY(tx.emplace<ReplaceCell>(0, a));
        const auto sp = tx.savepoint();
        QVERIFY(tx.emplace<RemoveCell>(0, "4"));
        tx.rollbackTo(sp);
        QVERIFY(doc.page(0)->findCell("4"));
        QCOMPARE(doc.page(0)->findCell("2")->value(), std::string("outer"));
        const auto changes = tx.changes();
        QVERIFY(changes.pages().at("p1").removed.empty());
        auto committed = tx.commit();
        QCOMPARE(committed.group->size(), std::size_t(1));
    }

    void reorderCellsLandsAndReverts()
    {
        core::Document doc = sampleDocument();
        const auto original = entriesOf(doc);
        Transaction tx(doc);
        // Block {2, 3} before entry 1 (the layer): 0, 2, 3, 1, #, 4
        QVERIFY(tx.emplace<ReorderCells>(0, std::vector<std::string>{"3", "2"}, 1));
        QCOMPARE(idsInEntryOrder(*doc.page(0)), (std::vector<std::string>{"0", "2", "3", "1", "#", "4"}));
        // To the end.
        QVERIFY(tx.emplace<ReorderCells>(0, std::vector<std::string>{"2"}, doc.page(0)->rootEntries().size()));
        QCOMPARE(idsInEntryOrder(*doc.page(0)), (std::vector<std::string>{"0", "3", "1", "#", "4", "2"}));
        // Anchor on a moved entry skips to the next unmoved one.
        QVERIFY(tx.emplace<ReorderCells>(0, std::vector<std::string>{"1"}, 2));
        QCOMPARE(idsInEntryOrder(*doc.page(0)), (std::vector<std::string>{"0", "3", "1", "#", "4", "2"}));
        QVERIFY(!tx.emplace<ReorderCells>(0, std::vector<std::string>{"1", "1"}, 0));
        tx.rollback();
        QVERIFY(entriesOf(doc) == original);
    }

    void reorderCellsKeepsTheGivenOrderWhenAsked()
    {
        core::Document doc = sampleDocument();
        const auto original = entriesOf(doc);
        Transaction tx(doc);
        // 0, 1, 2, #, 3, 4: the block takes the order of the vector, not the entry order.
        QVERIFY(tx.emplace<ReorderCells>(0, std::vector<std::string>{"4", "2"}, 1, ReorderCells::Order::KeepGivenOrder));
        QCOMPARE(idsInEntryOrder(*doc.page(0)), (std::vector<std::string>{"0", "4", "2", "1", "#", "3"}));
        tx.rollback();
        QVERIFY(entriesOf(doc) == original);
        // An empty block is a no-op.
        QVERIFY(tx.emplace<ReorderCells>(0, std::vector<std::string>{}, 1));
        QVERIFY(entriesOf(doc) == original);
    }

    void reorderOfThousandsOfCellsIsExactAndReverts()
    {
        core::Page page;
        page.setId("big");
        page.appendEntry(core::Cell("0"));
        page.appendEntry(core::Cell::makeLayer("1", "0"));
        for (int i = 0; i < 2000; ++i) {
            page.appendEntry(core::Cell::makeVertex("v" + std::to_string(i), "1", "", "", {double(i), 0, 5, 5}));
            if (i % 50 == 0)
                page.appendEntry(foreignNode("custom"));
        }
        core::Document doc;
        doc.appendEntry(std::move(page));
        const auto original = entriesOf(doc);

        // Every third vertex, reversed, in front of the layer.
        std::vector<std::string> ids;
        for (int i = 1998; i >= 0; i -= 3)
            ids.push_back("v" + std::to_string(i));
        Transaction tx(doc);
        QVERIFY(tx.emplace<ReorderCells>(0, ids, 1, ReorderCells::Order::KeepGivenOrder));
        const core::Page& moved = *doc.page(0);
        QCOMPARE(moved.entryIndex(ids.front()).value(), std::size_t(1));
        QCOMPARE(moved.entryIndex(ids.back()).value(), ids.size());
        QCOMPARE(moved.childIds("1").size(), std::size_t(2000));
        QCOMPARE(moved.childIds("1").front(), ids.front()); // the parent index follows the entries
        tx.rollback();
        QVERIFY(entriesOf(doc) == original);
        QCOMPARE(doc.page(0)->childIds("1").front(), std::string("v0"));
    }

    void replaceCellsReplacesManyAndReverts()
    {
        core::Document doc = sampleDocument();
        const auto original = entriesOf(doc);
        core::Cell a = *doc.page(0)->findCell("2");
        a.setValue("changed");
        core::Cell b = *doc.page(0)->findCell("3");
        b.setParent("1"); // out of its parent
        Transaction tx(doc);
        QVERIFY(tx.emplace<ReplaceCells>(0, std::vector<core::Cell>{a, b}));
        QCOMPARE(doc.page(0)->findCell("2")->value(), std::string("changed"));
        QVERIFY(doc.page(0)->childIds("2").empty());
        const auto changes = tx.changes();
        const auto& page = changes.pages().at("p1");
        QVERIFY(page.changed.contains("2") && page.changed.contains("3"));
        QVERIFY(page.childrenChanged.contains("2") && page.childrenChanged.contains("1")); // the parents of 3
        // An unknown cell, or one twice, fails and leaves everything as it was.
        QVERIFY(!tx.emplace<ReplaceCells>(0, std::vector<core::Cell>{a, core::Cell("nope")}));
        QVERIFY(!tx.emplace<ReplaceCells>(0, std::vector<core::Cell>{a, a}));
        QCOMPARE(doc.page(0)->findCell("2")->value(), std::string("changed"));
        tx.rollback();
        QVERIFY(entriesOf(doc) == original);
        QCOMPARE(doc.page(0)->childIds("2"), (std::vector<std::string>{"3"}));
    }

    void replaceCellMerges()
    {
        core::Document doc = sampleDocument();
        const core::Cell original = *doc.page(0)->findCell("2");
        core::Cell step1 = original;
        step1.geometry()->setX(20);
        core::Cell step2 = original;
        step2.geometry()->setX(30);

        CommandGroup first;
        ChangeSet ignored;
        auto c1 = std::make_unique<ReplaceCell>(0, step1);
        QVERIFY(c1->apply(doc, ignored));
        first.append(std::move(c1));
        CommandGroup second;
        auto c2 = std::make_unique<ReplaceCell>(0, step2);
        QVERIFY(c2->apply(doc, ignored));
        second.append(std::move(c2));

        CommandGroup other;
        other.append(std::make_unique<RemoveCell>(0, "2"));
        QVERIFY(!first.canMergeWith(other));
        QVERIFY(first.mergeWith(second));
        first.revert(doc, ignored);
        QVERIFY(*doc.page(0)->findCell("2") == original);
        QVERIFY(first.apply(doc, ignored));
        QCOMPARE(doc.page(0)->findCell("2")->geometry()->x(), 30.0);
    }

    void pagePrimitivesKeepForeignEntries()
    {
        core::Document doc = sampleDocument();
        const auto originalCount = doc.entries().size();
        Transaction tx(doc);
        QVERIFY(tx.emplace<MovePage>(1, 0)); // p2 before <before>
        QCOMPARE(doc.page(0)->id(), std::string("p2"));
        QVERIFY(std::holds_alternative<core::Page>(doc.entries()[0]));
        QVERIFY(tx.emplace<RemovePage>(1));
        QCOMPARE(doc.pageCount(), std::size_t(1));
        core::Page fresh;
        fresh.setId("p3");
        QVERIFY(tx.emplace<InsertPage>(doc.entries().size(), fresh));
        PageAttributes attrs = PageAttributes::of(*doc.page(0));
        attrs.diagram.set("name", "renamed");
        QVERIFY(tx.emplace<ReplacePageAttributes>(0, attrs));
        DocumentHeader header = DocumentHeader::of(doc);
        header.fileAttributes.set("pages", "2");
        QVERIFY(tx.emplace<ReplaceDocumentHeader>(header));
        tx.rollback();
        QCOMPARE(doc.entries().size(), originalCount);
        QVERIFY(std::holds_alternative<core::XmlNode>(doc.entries()[0]));
        QCOMPARE(doc.page(0)->id(), std::string("p1"));
        QCOMPARE(doc.page(1)->id(), std::string("p2"));
        QVERIFY(doc.fileAttributes().empty());
    }

    void queries()
    {
        const core::Document doc = sampleDocument();
        const core::Page& page = *doc.page(0);
        QVERIFY(isAncestor(page, "2", "3"));
        QVERIFY(isAncestor(page, "3", "3"));
        QVERIFY(!isAncestor(page, "3", "2"));
        QCOMPARE(origin(page, "3"), (core::Point{15.5, 25}));
        QCOMPARE(geometryOrigin(page, "3"), (core::Point{10.5, 20}));
        QCOMPARE(topmostCells(page, {"3", "2", "2"}), (std::vector<std::string>{"2"}));
        QCOMPARE(sortByZOrder(page, {"4", "2", "x"}), (std::vector<std::string>{"2", "4"}));
        QCOMPARE(allEdges(page, {"2"}), (std::vector<std::string>{"4"}));
        QCOMPARE(nearestCommonAncestor(page, "3", "4"), std::string("1"));
        QCOMPARE(layerOf(page, "3"), std::string("1"));
        QCOMPARE(layerOf(page, "0"), std::string());
        const auto range = subtreeEntryRange(page, "2");
        QVERIFY(range && range->first == 2 && range->last == 4);

        QVERIFY(isTransparentCell(core::Cell::makeVertex("g", "1", "", "group", {})));
        QVERIFY(!isTransparentCell(core::Cell::makeVertex("g", "1", "", "group;fillColor=#fff;", {})));
        QVERIFY(isTransparentCell(core::Cell::makeVertex("g", "1", "", "fillColor=none;strokeColor=none;", {})));
        QVERIFY(!isTransparentCell(core::Cell::makeVertex("g", "1", "", "", {})));
        QVERIFY(!isTransparentCell(core::Cell::makeVertex("g", "1", "", "text;image=x.png;", {})));
    }

    void translateKeepsUntouchedText()
    {
        core::Geometry g = core::Geometry::create();
        g.attributes().set("x", "10.50");
        g.attributes().set("y", "7.0");
        g.setSourcePoint(core::Point{1, 2});
        g.setPoints({{3, 4}});
        translateGeometry(g, 0, 5, false);
        QCOMPARE(g.attributes().value("x"), std::string("10.50")); // dx == 0: untouched
        QCOMPARE(g.attributes().value("y"), std::string("12"));
        QCOMPARE(g.sourcePoint()->y, 7.0);
        QCOMPARE(g.points().front().y, 9.0);

        core::Geometry edge = core::Geometry::create();
        edge.setRelative(true);
        edge.setX(-0.5);
        translateGeometry(edge, 10, 10, true);
        QCOMPARE(edge.x(), -0.5);
    }

    void translatedCellRelativeOffsetIsRounded()
    {
        core::Document doc = sampleDocument();
        core::Page& page = *doc.page(0);
        core::Cell label = core::Cell::makeVertex("5", "2", "L", "", {0.5, 1, 10, 10});
        label.geometry()->setRelative(true);
        page.appendEntry(label);
        const core::Cell moved = translatedCell(page, *page.findCell("5"), 2.5, -2.5);
        QCOMPARE(*moved.geometry()->offset(), (core::Point{3, -2})); // JS Math.round
        QCOMPARE(moved.geometry()->x(), 0.5);
    }

    void corpusPrimitiveScript_data()
    {
        QTest::addColumn<QString>("path");
        for (const QString& f : testing::corpusFiles())
            QTest::newRow(qPrintable(QDir(QStringLiteral(QRAW_SOURCE_DIR)).relativeFilePath(f))) << f;
    }

    // A fixed primitive script per file: restyle and move the first vertex,
    // reorder it to the back, remove the last cell's subtree, change page
    // attributes, duplicate the first page at the end, remove the first one.
    void corpusPrimitiveScript()
    {
        QFETCH(QString, path);
        const auto outcome = testing::checkUndoRedoIdentity(path, [](testing::StepRunner& run) {
            const core::Page* page = run.document().page(0);
            if (!page)
                return;
            const auto vertices = testing::cellIds(*page, [](const core::Cell& c) { return c.isVertex(); });
            if (!vertices.empty()) {
                const std::string v = vertices.front();
                run.step([&](Transaction& tx) {
                    core::Cell c = *tx.page(0)->findCell(v);
                    core::Style s = c.style();
                    s.set("fillColor", "#ff0000");
                    c.setStyle(s);
                    if (core::Geometry* g = c.geometry())
                        translateGeometry(*g, 13, -7, false);
                    return tx.emplace<ReplaceCell>(0, c) ? Result::success() : Result::failure("replace");
                });
                run.step([&](Transaction& tx) {
                    return moveSubtree(tx, 0, v, 0) ? Result::success() : Result::failure("reorder");
                });
            }
            const auto all = testing::cellIds(*page, [](const core::Cell&) { return true; });
            if (all.size() > 2) {
                const std::string last = all.back();
                run.step([&](Transaction& tx) {
                    return removeSubtree(tx, 0, last) ? Result::success() : Result::failure("remove");
                });
            }
            run.step([](Transaction& tx) {
                PageAttributes attrs = PageAttributes::of(*tx.page(0));
                attrs.model.set("background", "#eeeeee");
                attrs.diagram.set("name", "mutated");
                return tx.emplace<ReplacePageAttributes>(0, attrs) ? Result::success() : Result::failure("attrs");
            });
            run.step([](Transaction& tx) {
                core::Page copy = *tx.page(0);
                copy.setId("copy-of-first");
                return tx.emplace<InsertPage>(tx.document().entries().size(), copy) ? Result::success()
                                                                                     : Result::failure("insert");
            });
            run.step([](Transaction& tx) {
                return tx.emplace<RemovePage>(0) ? Result::success() : Result::failure("remove page");
            });
        });
        QVERIFY2(outcome.failure.isEmpty(), qPrintable(outcome.failure));
        QVERIFY(outcome.recorded > 0);
    }
};

QTEST_GUILESS_MAIN(TestFoundation)
#include "tst_foundation.moc"
