// Page and layer operations (ops_pages.h, ops_layers.h). Every case checks
// the emitted model and that rollback restores the saved bytes.
#include "qraw/commands/ops_layers.h"
#include "qraw/commands/ops_pages.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/cell.h"
#include "qraw/core/document.h"
#include "qraw/core/page.h"
#include "qraw/core/xml_node.h"
#include "qraw/format/codec.h"

#include <QTest>

#include <optional>
#include <string>
#include <vector>

using namespace qraw;
using namespace qraw::commands;

namespace {

// Deterministic page ids: pid-1, pid-2, ...
ops::PageIdGenerator sequentialPageIds()
{
    return [n = 0]() mutable { return "pid-" + std::to_string(++n); };
}

core::Page namedPage(const std::string& id, const std::string& name)
{
    return ops::makeEmptyPage(id, name);
}

// Entries: foreign "a", page p1, foreign "b".
core::Document pagesDocument()
{
    core::Document doc;
    doc.appendEntry(core::XmlNode::element("a"));
    doc.appendEntry(namedPage("p1", "Page-1"));
    doc.appendEntry(core::XmlNode::element("b"));
    return doc;
}

// pagesDocument() with a second page "p2" named "Second" after p1.
core::Document twoPagesDocument()
{
    core::Document doc = pagesDocument();
    doc.addPage(namedPage("p2", "Second"));
    return doc;
}

std::vector<std::string> pageIds(const core::Document& doc)
{
    std::vector<std::string> ids;
    for (const core::Page* page : doc.pages())
        ids.push_back(page->id());
    return ids;
}

// Root "0"; layer "1" (hidden, unnamed) with vertex 2 and edge 3 (2 -> 5);
// layer "4" ("Top") with vertex 5.
core::Document layerDocument()
{
    core::Page page;
    page.setId("p");
    page.appendEntry(core::Cell("0"));
    core::Cell first = core::Cell::makeLayer("1", "0");
    first.setVisible(false);
    page.appendEntry(std::move(first));
    page.appendEntry(core::Cell::makeVertex("2", "1", "Hello", "", {0, 0, 40, 40}));
    page.appendEntry(core::Cell::makeEdge("3", "1", "", "", "2", "5"));
    core::Cell top = core::Cell::makeLayer("4", "0");
    top.setValue("Top");
    page.appendEntry(std::move(top));
    page.appendEntry(core::Cell::makeVertex("5", "4", "Out", "", {100, 0, 40, 40}));
    core::Document doc;
    doc.appendEntry(std::move(page));
    return doc;
}

const core::Page& firstPage(const core::Document& doc) { return *doc.page(0); }

} // namespace

class TestOpsPagesLayers : public QObject {
    Q_OBJECT

private slots:
    // --- pages -----------------------------------------------------------

    void makeEmptyPageHasDrawioDefaults()
    {
        const core::Page page = ops::makeEmptyPage("x", "Name");
        const std::vector<std::string> names = [&page] {
            std::vector<std::string> out;
            for (const core::Attribute& a : page.modelAttributes())
                out.push_back(a.name);
            return out;
        }();
        QVERIFY(names == (std::vector<std::string>{"grid", "gridSize", "guides", "tooltips", "connect", "arrows",
                                                   "fold", "page", "pageScale", "pageWidth", "pageHeight", "math",
                                                   "shadow"}));
        QCOMPARE(page.rootEntries().size(), std::size_t{2});
        QCOMPARE(page.findCell("1")->parent(), std::string("0"));
        QCOMPARE(page.name(), std::string("Name"));
    }

    void nextPageNameSkipsTakenNames()
    {
        core::Document doc;
        doc.appendEntry(namedPage("a", "Page-1"));
        doc.appendEntry(namedPage("b", "Page-2"));
        QCOMPARE(ops::nextPageName(doc), std::string("Page-3"));

        core::Document taken;
        taken.appendEntry(namedPage("a", "Page-1"));
        taken.appendEntry(namedPage("b", "Page-3"));
        QCOMPARE(ops::nextPageName(taken), std::string("Page-4"));
    }

    void addPageAtEndKeepsForeignNodes()
    {
        core::Document doc = pagesDocument();
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);
        const Result r = ops::addPage(tx, std::nullopt, std::nullopt, sequentialPageIds());
        QVERIFY(r.ok);
        QCOMPARE(r.page, std::optional<std::size_t>{1});
        QCOMPARE(doc.entries().size(), std::size_t{4});
        QVERIFY(std::holds_alternative<core::Page>(doc.entries()[2]));
        QVERIFY(std::holds_alternative<core::XmlNode>(doc.entries()[3]));
        QCOMPARE(std::get<core::XmlNode>(doc.entries()[3]).name, std::string("b"));
        QCOMPARE(doc.page(1)->id(), std::string("pid-1"));
        QCOMPARE(doc.page(1)->name(), std::string("Page-2"));
        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void addPageAtIndexZero()
    {
        core::Document doc = pagesDocument();
        Transaction tx(doc);
        const Result r = ops::addPage(tx, 0, std::string("First"), sequentialPageIds());
        QVERIFY(r.ok);
        QCOMPARE(r.page, std::optional<std::size_t>{0});
        QCOMPARE(pageIds(doc), (std::vector<std::string>{"pid-1", "p1"}));
        QCOMPARE(doc.page(0)->name(), std::string("First"));
    }

    void addPageUpdatesPagesAttributeOnlyWhenPresent()
    {
        core::Document withAttribute = pagesDocument();
        withAttribute.fileAttributes().set("pages", "1");
        Transaction tx(withAttribute);
        QVERIFY(ops::addPage(tx, std::nullopt, std::nullopt, sequentialPageIds()).ok);
        QCOMPARE(withAttribute.fileAttributes().value("pages"), std::string("2"));

        core::Document without = pagesDocument();
        Transaction tx2(without);
        QVERIFY(ops::addPage(tx2, std::nullopt, std::nullopt, sequentialPageIds()).ok);
        QVERIFY(!without.fileAttributes().has("pages"));
    }

    void addPageConvertsBareModelToMxFile()
    {
        core::Document doc;
        doc.setKind(core::DocumentKind::BareModel);
        doc.appendEntry(namedPage("a", "Page-1"));
        Transaction tx(doc);
        QVERIFY(ops::addPage(tx, std::nullopt, std::nullopt, sequentialPageIds()).ok);
        QCOMPARE(doc.kind(), core::DocumentKind::MxFile);
        QCOMPARE(doc.pageCount(), std::size_t{2});
    }

    void removeOnlyPageFails()
    {
        core::Document doc = pagesDocument();
        Transaction tx(doc);
        QVERIFY(!ops::removePage(tx, 0).ok);
        QVERIFY(tx.empty());
    }

    void removePageReturnsPreviousPage()
    {
        core::Document doc = twoPagesDocument();
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);
        const Result r = ops::removePage(tx, 1);
        QVERIFY(r.ok);
        QCOMPARE(r.page, std::optional<std::size_t>{0});
        QCOMPARE(pageIds(doc), (std::vector<std::string>{"p1"}));
        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void duplicatePageCopiesAndInserts()
    {
        core::Document doc = pagesDocument();
        Transaction tx(doc);
        const std::size_t cellsBefore = firstPage(doc).rootEntries().size();
        const Result r = ops::duplicatePage(tx, 0, sequentialPageIds());
        QVERIFY(r.ok);
        QCOMPARE(r.page, std::optional<std::size_t>{1});
        QCOMPARE(pageIds(doc), (std::vector<std::string>{"p1", "pid-1"}));
        QCOMPARE(doc.page(1)->name(), std::string("Copy of Page-1"));
        QCOMPARE(doc.page(1)->rootEntries().size(), cellsBefore);
        QVERIFY(doc.page(1)->findCell("1") != nullptr);
    }

    void renamePageWithSameNameEmitsNothing()
    {
        core::Document doc = pagesDocument();
        Transaction tx(doc);
        QVERIFY(ops::renamePage(tx, 0, "Page-1").ok);
        QVERIFY(tx.empty());
        QVERIFY(ops::renamePage(tx, 0, "Intro").ok);
        QCOMPARE(doc.page(0)->name(), std::string("Intro"));
        QVERIFY(!ops::renamePage(tx, 5, "x").ok);
    }

    void movePageForthAndBack()
    {
        core::Document doc = twoPagesDocument();
        const QByteArray before = format::writeDocument(doc);

        Transaction tx(doc);
        Result r = ops::movePage(tx, 0, 1);
        QVERIFY(r.ok);
        QCOMPARE(r.page, std::optional<std::size_t>{1});
        QCOMPARE(pageIds(doc), (std::vector<std::string>{"p2", "p1"}));

        r = ops::movePage(tx, 1, 0);
        QVERIFY(r.ok);
        QCOMPARE(r.page, std::optional<std::size_t>{0});
        QCOMPARE(pageIds(doc), (std::vector<std::string>{"p1", "p2"}));
        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void setPagePropertiesWritesAndSkips()
    {
        core::Document doc = pagesDocument();
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);

        ops::PageProperties grid;
        grid.grid = false;
        QVERIFY(ops::setPageProperties(tx, 0, grid).ok);
        QCOMPARE(firstPage(doc).modelAttributes().value("grid"), std::string("0"));

        ops::PageProperties sameGrid;
        sameGrid.grid = false;
        QVERIFY(ops::setPageProperties(tx, 0, sameGrid).ok);
        const std::size_t saved = tx.savepoint();

        ops::PageProperties size;
        size.pageWidth = 100;
        QVERIFY(!ops::setPageProperties(tx, 0, size).ok); // height missing
        QCOMPARE(tx.savepoint(), saved);

        ops::PageProperties background;
        background.background = std::string("#ffffff");
        QVERIFY(ops::setPageProperties(tx, 0, background).ok);
        QCOMPARE(firstPage(doc).modelAttributes().value("background"), std::string("#ffffff"));
        ops::PageProperties none;
        none.background = std::string("none");
        QVERIFY(ops::setPageProperties(tx, 0, none).ok);
        QVERIFY(!firstPage(doc).modelAttributes().has("background"));

        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void adaptiveColorsIsWrittenAfterShadowAndRemovedByDefault()
    {
        core::Document doc = pagesDocument();
        doc.page(0)->setMath(false);
        doc.page(0)->setShadow(false);
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);

        ops::PageProperties none;
        none.adaptiveColors = std::string("none");
        QVERIFY(ops::setPageProperties(tx, 0, none).ok);
        QCOMPARE(firstPage(doc).adaptiveColors(), std::string("none"));
        QVERIFY(format::writeDocument(doc).contains("math=\"0\" shadow=\"0\" adaptiveColors=\"none\""));

        ops::PageProperties same;
        same.adaptiveColors = std::string("none");
        const std::size_t saved = tx.savepoint();
        QVERIFY(ops::setPageProperties(tx, 0, same).ok);
        QCOMPARE(tx.savepoint(), saved); // nothing changed, nothing recorded

        ops::PageProperties reset;
        reset.adaptiveColors = std::string("default");
        QVERIFY(ops::setPageProperties(tx, 0, reset).ok);
        QVERIFY(!firstPage(doc).modelAttributes().has("adaptiveColors"));

        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    // --- layers ----------------------------------------------------------

    void addLayerAtEndAndFront()
    {
        core::Document doc = layerDocument();
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);

        const Result end = ops::addLayer(tx, 0);
        QVERIFY(end.ok);
        QCOMPARE(end.cells.size(), std::size_t{1});
        const std::string endId = end.cells[0];
        QCOMPARE(ops::layerIds(firstPage(doc)), (std::vector<std::string>{"1", "4", endId}));
        const core::Cell* layer = firstPage(doc).findCell(endId);
        QCOMPARE(layer->value(), std::string("Untitled Layer"));
        QCOMPARE(layer->parent(), std::string("0"));
        QCOMPARE(firstPage(doc).entryIndex(endId), std::optional<std::size_t>{6});

        const Result front = ops::addLayer(tx, 0, std::size_t{0}, "Front");
        QVERIFY(front.ok);
        QCOMPARE(ops::layerIds(firstPage(doc)).front(), front.cells[0]);
        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void duplicateLayerClonesSubtree()
    {
        core::Document doc = layerDocument();
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);
        const Result r = ops::duplicateLayer(tx, 0, "1");
        QVERIFY(r.ok);
        const std::string clone = r.cells.at(0);
        const core::Page& page = firstPage(doc);

        QCOMPARE(ops::layerIds(page), (std::vector<std::string>{"1", clone, "4"}));
        QCOMPARE(page.entryIndex(clone), std::optional<std::size_t>{4});
        const core::Cell* layer = page.findCell(clone);
        QCOMPARE(layer->value(), std::string("Copy of Background"));
        QVERIFY(layer->isVisible());
        QVERIFY(!layer->attributes().has("visible"));

        // Vertex 2 and edge 3 are cloned; the edge keeps its inside source
        // (remapped) and loses the outside target (removed).
        const core::Cell* vertexCopy = nullptr;
        const core::Cell* edgeCopy = nullptr;
        for (const core::Cell* child : page.children(clone)) {
            if (child->isEdge())
                edgeCopy = child;
            else
                vertexCopy = child;
        }
        QVERIFY(vertexCopy && edgeCopy);
        QCOMPARE(vertexCopy->value(), std::string("Hello"));
        QCOMPARE(edgeCopy->source(), vertexCopy->id());
        QVERIFY(edgeCopy->target().empty());
        QVERIFY(!edgeCopy->attributes().has("target"));
        QVERIFY(page.findCell("2")->value() == "Hello"); // original untouched

        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void duplicateLayerRejectsNonLayer()
    {
        core::Document doc = layerDocument();
        Transaction tx(doc);
        QVERIFY(!ops::duplicateLayer(tx, 0, "2").ok);
        QVERIFY(!ops::duplicateLayer(tx, 0, "0").ok);
        QVERIFY(tx.empty());
    }

    void renameLayerSetsLabel()
    {
        core::Document doc = layerDocument();
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);
        QVERIFY(ops::renameLayer(tx, 0, "4", "Overlay").ok);
        QCOMPARE(firstPage(doc).findCell("4")->value(), std::string("Overlay"));
        QVERIFY(!ops::renameLayer(tx, 0, "5", "Nope").ok);
        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void moveLayerMovesSubtree()
    {
        core::Document doc = layerDocument();
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);
        QVERIFY(ops::moveLayer(tx, 0, "1", 1).ok);
        QCOMPARE(ops::layerIds(firstPage(doc)), (std::vector<std::string>{"4", "1"}));
        QCOMPARE(firstPage(doc).children("1").size(), std::size_t{2});
        QVERIFY(ops::moveLayer(tx, 0, "1", 0).ok);
        QCOMPARE(ops::layerIds(firstPage(doc)), (std::vector<std::string>{"1", "4"}));
        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void visibilityWritesAndRemovesAttribute()
    {
        core::Document doc = layerDocument();
        Transaction tx(doc);
        QVERIFY(ops::setLayerVisible(tx, 0, "1", true).ok);
        QVERIFY(!firstPage(doc).findCell("1")->attributes().has("visible"));
        QVERIFY(ops::setLayerVisible(tx, 0, "1", false).ok);
        QCOMPARE(firstPage(doc).findCell("1")->attributes().value("visible"), std::string("0"));
        tx.rollback();
    }

    void lockedWritesOneAndZero()
    {
        core::Document doc = layerDocument();
        const QByteArray before = format::writeDocument(doc);
        Transaction tx(doc);
        QVERIFY(ops::setLayerLocked(tx, 0, "4", true).ok);
        QCOMPARE(firstPage(doc).findCell("4")->style().value("locked"), std::optional<std::string>{"1"});
        QVERIFY(ops::setLayerLocked(tx, 0, "4", false).ok);
        QCOMPARE(firstPage(doc).findCell("4")->style().value("locked"), std::optional<std::string>{"0"});
        tx.rollback();
        QCOMPARE(format::writeDocument(doc), before);
    }

    void nonLayerIdsFail()
    {
        core::Document doc = layerDocument();
        Transaction tx(doc);
        QVERIFY(!ops::setLayerVisible(tx, 0, "2", false).ok);
        QVERIFY(!ops::setLayerLocked(tx, 0, "2", true).ok);
        QVERIFY(!ops::moveLayer(tx, 0, "2", 0).ok);
        QVERIFY(!ops::setLayerVisible(tx, 0, "missing", false).ok);
        QVERIFY(tx.empty());
    }
};

QTEST_GUILESS_MAIN(TestOpsPagesLayers)
#include "tst_ops_pages_layers.moc"
