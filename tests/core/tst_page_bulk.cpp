// Page::replaceCells, moveEntries and restoreEntries: one pass over the
// entries, with the same result as the single changes, and the indexes (id,
// parent, source, target) exact afterwards, foreign entries interleaved.

#include "qraw/core/document.h"
#include "qraw/core/page.h"

#include <QTest>

#include <algorithm>
#include <numeric>

using namespace qraw::core;

namespace {

// A reproducible pseudo-random sequence.
class Random {
public:
    explicit Random(std::uint64_t seed) : state_(seed) {}
    std::size_t below(std::size_t n)
    {
        state_ ^= state_ << 13;
        state_ ^= state_ >> 7;
        state_ ^= state_ << 17;
        return static_cast<std::size_t>(state_ % n);
    }

private:
    std::uint64_t state_;
};

// 0, layer 1, then `count` cells (a third of them edges between earlier
// vertices, some children of earlier vertices) with a foreign node after
// every seventh entry.
Page makePage(std::size_t count)
{
    Page page;
    page.setId("p");
    page.appendEntry(Cell("0"));
    page.appendEntry(Cell::makeLayer("1", "0"));
    for (std::size_t i = 0; i < count; ++i) {
        const std::string id = "c" + std::to_string(i);
        if (i % 3 == 2) {
            page.appendEntry(Cell::makeEdge(id, "1", "", "", "c" + std::to_string(i - 1), "c" + std::to_string(i - 2)));
        } else {
            const std::string parent = i > 6 && i % 5 == 0 ? "c" + std::to_string(i - 6) : "1";
            page.appendEntry(Cell::makeVertex(id, parent, "", "", {double(i), 0, 10, 10}));
        }
        if (i % 7 == 6)
            page.appendEntry(XmlNode::element("custom", {{"i", std::to_string(i)}}));
    }
    return page;
}

// A page with the same entries built from scratch: the indexes that a mutation
// has to keep must give the same answers as these.
Page rebuilt(const Page& page)
{
    Page copy;
    copy.setId(page.id());
    for (const auto& entry : page.rootEntries())
        copy.appendEntry(entry);
    return copy;
}

QString order(const Page& page)
{
    QStringList ids;
    for (const auto& entry : page.rootEntries())
        ids << (std::holds_alternative<Cell>(entry) ? QString::fromStdString(std::get<Cell>(entry).id()) : "#");
    return ids.join(',');
}

// Every public answer of the indexes equals the fresh build's.
void expectIndexesExact(const Page& page)
{
    const Page fresh = rebuilt(page);
    QCOMPARE(order(page), order(fresh));
    QCOMPARE(page.cellCount(), fresh.cellCount());
    for (const Cell* cell : fresh.cells()) {
        const std::string& id = cell->id();
        QCOMPARE(page.entryIndex(id), fresh.entryIndex(id));
        QCOMPARE(page.childIds(id), fresh.childIds(id));
        QCOMPARE(page.childPosition(id), fresh.childPosition(id));
        std::vector<std::string> a;
        std::vector<std::string> b;
        for (const Cell* e : page.connectedEdges(id))
            a.push_back(e->id());
        for (const Cell* e : fresh.connectedEdges(id))
            b.push_back(e->id());
        QCOMPARE(a, b);
    }
}

} // namespace

class TestPageBulk : public QObject {
    Q_OBJECT

private slots:
    void moveEntriesMatchesSingleMoves_data()
    {
        QTest::addColumn<int>("count");
        QTest::addColumn<int>("moves");
        QTest::newRow("a few cells (per-cell index update)") << 120 << 5;
        QTest::newRow("many cells (indexes rebuilt)") << 400 << 150;
    }

    void moveEntriesMatchesSingleMoves()
    {
        QFETCH(int, count);
        QFETCH(int, moves);
        Random random(static_cast<std::uint64_t>(count) * 31 + 7);
        for (int round = 0; round < 25; ++round) {
            Page page = makePage(static_cast<std::size_t>(count));
            // Choose cells (not foreign nodes) at random positions, in a random order.
            std::vector<std::size_t> cellPositions;
            for (std::size_t i = 0; i < page.rootEntries().size(); ++i) {
                if (std::holds_alternative<Cell>(page.rootEntries()[i]))
                    cellPositions.push_back(i);
            }
            std::vector<std::size_t> sources;
            for (int i = 0; i < moves; ++i) {
                const std::size_t pick = random.below(cellPositions.size());
                sources.push_back(cellPositions[pick]);
                cellPositions.erase(cellPositions.begin() + static_cast<std::ptrdiff_t>(pick));
            }
            const std::size_t landing = random.below(page.rootEntries().size() - sources.size() + 1);

            // The reference: take the cells out and insert them as a block.
            Page expected = page;
            std::vector<std::string> ids;
            for (const std::size_t source : sources)
                ids.push_back(std::get<Cell>(page.rootEntries()[source]).id());
            std::vector<Cell> block;
            for (const std::string& id : ids)
                block.push_back(*expected.takeCell(id));
            for (std::size_t i = 0; i < block.size(); ++i)
                expected.insertCellAt(landing + i, std::move(block[i]));

            const Page before = page;
            QVERIFY(page.moveEntries(sources, landing));
            QCOMPARE(order(page), order(expected));
            expectIndexesExact(page);

            QVERIFY(page.restoreEntries(sources, landing));
            QCOMPARE(order(page), order(before));
            expectIndexesExact(page);
        }
    }

    void moveEntriesRejectsBadArguments()
    {
        Page page = makePage(30);
        const QString before = order(page);
        QVERIFY(!page.moveEntries({}, 0));
        QVERIFY(!page.moveEntries({3, 3}, 0));                               // twice
        QVERIFY(!page.moveEntries({1000}, 0));                               // out of range
        QVERIFY(!page.moveEntries({3}, page.rootEntries().size()));          // no room for the block
        std::size_t foreign = 0;
        while (std::holds_alternative<Cell>(page.rootEntries()[foreign]))
            ++foreign;
        QVERIFY(!page.moveEntries({foreign}, 0)); // a foreign node never moves
        QCOMPARE(order(page), before);
    }

    void replaceCellsReparentsAndKeepsIndexesExact_data()
    {
        QTest::addColumn<int>("count");
        QTest::newRow("per-cell index update") << 60;
        QTest::newRow("rebuilt") << 300;
    }

    void replaceCellsReparentsAndKeepsIndexesExact()
    {
        QFETCH(int, count);
        Page page = makePage(static_cast<std::size_t>(count));
        std::vector<Cell> cells;
        for (int i = 0; i < count; i += 4) {
            Cell cell = *page.findCell("c" + std::to_string(i));
            if (cell.isEdge()) {
                cell.setSource("c0");
            } else {
                cell.setParent(i % 8 == 0 ? "1" : "c1");
                cell.setValue("changed");
            }
            cells.push_back(std::move(cell));
        }
        const Page original = page;
        std::vector<Cell> undo;
        for (const Cell& cell : cells)
            undo.push_back(*page.findCell(cell.id()));
        QVERIFY(page.replaceCells(cells));
        expectIndexesExact(page);
        QCOMPARE(page.findCell("c4")->value(), std::string("changed"));
        QVERIFY(page.replaceCells(undo));
        expectIndexesExact(page);
        QCOMPARE(order(page), order(original));
        for (const Cell* cell : original.cells())
            QVERIFY(*page.findCell(cell->id()) == *cell);
    }

    void replaceCellsRefusesAnUnknownOrRepeatedId()
    {
        Page page = makePage(20);
        Cell a = *page.findCell("c0");
        a.setValue("x");
        Cell ghost("ghost");
        QVERIFY(!page.replaceCells({a, ghost}));
        QVERIFY(!page.replaceCells({a, a}));
        QCOMPARE(page.findCell("c0")->value(), std::string());
    }
};

QTEST_GUILESS_MAIN(TestPageBulk)
#include "tst_page_bulk.moc"
