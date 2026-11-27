// Table operations and tableLayout (ops_tables.h), with values derived by
// hand from Graph.js insertTableRow/Column, deleteTableRow/Column,
// setTableRowHeight/ColumnWidth and TableLayout.execute. The operations run
// on a bare Transaction, so each case applies the layout itself, as
// DocumentSession::edit does. Rolling back restores the bytes.
#include "corpus_harness.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_layout.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/commands/ops_tables.h"
#include "qraw/commands/primitives.h"

#include <QTest>

using namespace qraw;
using namespace qraw::commands;
using core::Cell;
using core::Page;
using core::Rect;

namespace {

constexpr auto kTableStyle = "shape=table;startSize=30;childLayout=tableLayout;fixedRows=1;";
constexpr auto kRowStyle = "shape=tableRow;horizontal=0;startSize=0;fillColor=none;";
constexpr auto kCellStyle = "shape=partialRectangle;fillColor=none;rowspan=1;";

Cell vertex(const std::string& id, const std::string& parent, Rect bounds, const char* style, const char* value = "")
{
    return Cell::makeVertex(id, parent, value, style, bounds);
}

// A 3x3 table "t" (rows r1..r3 with cells r<i>c<j>), 100x30 cells below a 30 title.
core::Document tableDocument()
{
    Page page;
    page.setId("p1");
    page.appendEntry(Cell("0"));
    page.appendEntry(Cell::makeLayer("1", "0"));
    page.appendEntry(vertex("t", "1", {10, 10, 300, 120}, kTableStyle));
    for (int i = 1; i <= 3; ++i) {
        const std::string row = "r" + std::to_string(i);
        page.appendEntry(vertex(row, "t", {0, 30.0 * i, 300, 30}, kRowStyle));
        for (int j = 1; j <= 3; ++j) {
            Cell cell = vertex(row + "c" + std::to_string(j), row, {100.0 * (j - 1), 0, 100, 30}, kCellStyle,
                               "text");
            cell.geometry()->setAlternateBounds(Rect{0, 0, 100, 30});
            page.appendEntry(std::move(cell));
        }
    }
    core::Document doc;
    doc.appendEntry(std::move(page));
    return doc;
}

class TableEdit {
public:
    TableEdit() : doc_(tableDocument()), before_(format::writeDocument(doc_)), tx_(doc_) {}

    Transaction& tx() { return tx_; }
    [[nodiscard]] const Page& page() const { return *doc_.page(0); }
    [[nodiscard]] Rect bounds(const std::string& id) const { return page().findCell(id)->geometry()->bounds(); }
    [[nodiscard]] std::vector<std::string> children(const std::string& parent) const
    {
        std::vector<std::string> ids;
        for (const Cell* c : page().children(parent))
            ids.push_back(c->id());
        return ids;
    }

    // Runs the layout over what the edit changed (DocumentSession::edit).
    void layout()
    {
        QVERIFY2(ops::applyLayouts(tx_, 0, tx_.changes()).ok, "layout failed");
    }
    [[nodiscard]] bool visible(const std::string& id) const { return page().findCell(id)->isVisible(); }
    [[nodiscard]] QByteArray bytes() const { return format::writeDocument(doc_); }
    [[nodiscard]] bool rollbackRestores()
    {
        tx_.rollback();
        return format::writeDocument(doc_) == before_;
    }

private:
    core::Document doc_;
    QByteArray before_;
    Transaction tx_;
};

} // namespace

class OpsTablesTest : public QObject {
    Q_OBJECT

private slots:
    void queries()
    {
        TableEdit e;
        QVERIFY(ops::isTable(*e.page().findCell("t")));
        QVERIFY(!ops::isTable(*e.page().findCell("r1")));
        QVERIFY(ops::isTableRow(e.page(), *e.page().findCell("r2")));
        QVERIFY(!ops::isTableRow(e.page(), *e.page().findCell("r2c1")));
        QVERIFY(ops::isTableCell(e.page(), *e.page().findCell("r2c1")));
        QCOMPARE(ops::tableOf(e.page(), "r2c3"), std::string("t"));
        QCOMPARE(ops::rowOf(e.page(), "r2c3"), std::string("r2"));
        QCOMPARE(ops::rowOf(e.page(), "t"), std::string());
        QCOMPARE(ops::tableOf(e.page(), "1"), std::string());
    }

    void layoutOfAnExactTableChangesNothing()
    {
        TableEdit e;
        const Transaction::Committed none = [&] {
            e.layout();
            return e.tx().commit();
        }();
        QVERIFY(none.group->empty());
    }

    void insertRowBelowCopiesTheRowAndGrowsTheTable()
    {
        TableEdit e;
        QVERIFY(ops::insertTableRow(e.tx(), 0, "r2c2", false).ok);
        e.layout();
        const auto rows = e.children("t");
        QCOMPARE(rows.size(), std::size_t(4));
        QCOMPARE(rows[0], std::string("r1"));
        QCOMPARE(rows[1], std::string("r2"));
        QCOMPARE(rows[3], std::string("r3"));
        const std::string added = rows[2];
        QCOMPARE(e.page().findCell(added)->styleText(), std::string(kRowStyle));
        QCOMPARE(e.bounds(added), (Rect{0, 90, 300, 30}));
        QCOMPARE(e.bounds("r3"), (Rect{0, 120, 300, 30}));
        QCOMPARE(e.bounds("t"), (Rect{10, 10, 300, 150}));
        const auto cells = e.children(added);
        QCOMPARE(cells.size(), std::size_t(3));
        const Cell& copy = *e.page().findCell(cells[1]);
        QCOMPARE(copy.value(), std::string());                           // value dropped
        QCOMPARE(copy.styleText(), std::string("shape=partialRectangle;fillColor=none;")); // rowspan dropped
        QCOMPARE(e.bounds(cells[1]), (Rect{100, 0, 100, 30}));
        QVERIFY(e.rollbackRestores());
    }

    void insertRowAboveATableGoesFirstAndBelowGoesLast()
    {
        TableEdit e;
        QVERIFY(ops::insertTableRow(e.tx(), 0, "t", true).ok);
        QVERIFY(ops::insertTableRow(e.tx(), 0, "r3", false).ok);
        e.layout();
        const auto rows = e.children("t");
        QCOMPARE(rows.size(), std::size_t(5));
        QCOMPARE(rows[1], std::string("r1"));
        QCOMPARE(rows[3], std::string("r3"));
        QCOMPARE(e.bounds("t").height, 180.0);
        QVERIFY(e.rollbackRestores());
    }

    void insertColumnRightOfTheSelectedCell()
    {
        TableEdit e;
        QVERIFY(ops::insertTableColumn(e.tx(), 0, "r2c2", false).ok);
        e.layout();
        QCOMPARE(e.bounds("t"), (Rect{10, 10, 400, 120}));
        for (const char* row : {"r1", "r2", "r3"}) {
            const auto cells = e.children(row);
            QCOMPARE(cells.size(), std::size_t(4));
            QCOMPARE(cells[1].back(), '2');
            QCOMPARE(e.bounds(cells[0]), (Rect{0, 0, 100, 30}));
            QCOMPARE(e.bounds(cells[1]), (Rect{100, 0, 100, 30}));
            QCOMPARE(e.bounds(cells[2]), (Rect{200, 0, 100, 30})); // the new one
            QCOMPARE(e.bounds(cells[3]), (Rect{300, 0, 100, 30}));
            QCOMPARE(e.page().findCell(cells[2])->value(), std::string());
        }
        QVERIFY(e.rollbackRestores());
    }

    void insertColumnLeftOfARowGoesFirst()
    {
        TableEdit e;
        QVERIFY(ops::insertTableColumn(e.tx(), 0, "r1", true).ok);
        e.layout();
        const auto cells = e.children("r1");
        QCOMPARE(cells.size(), std::size_t(4));
        QCOMPARE(cells[1], std::string("r1c1"));
        QCOMPARE(e.bounds("t").width, 400.0);
        QVERIFY(e.rollbackRestores());
    }

    void deleteRowShrinksTheTable()
    {
        TableEdit e;
        QVERIFY(ops::deleteTableRow(e.tx(), 0, "r2c3").ok);
        e.layout();
        QCOMPARE(e.children("t"), (std::vector<std::string>{"r1", "r3"}));
        QVERIFY(!e.page().findCell("r2c1"));
        QCOMPARE(e.bounds("t"), (Rect{10, 10, 300, 90}));
        QCOMPARE(e.bounds("r3"), (Rect{0, 60, 300, 30}));
        QVERIFY(e.rollbackRestores());
    }

    void deleteTheOnlyRowRemovesTheTable()
    {
        TableEdit e;
        QVERIFY(ops::deleteTableRow(e.tx(), 0, "r1").ok);
        QVERIFY(ops::deleteTableRow(e.tx(), 0, "r2").ok);
        QVERIFY(ops::deleteTableRow(e.tx(), 0, "r3").ok);
        QVERIFY(!e.page().findCell("t"));
        QVERIFY(e.rollbackRestores());
    }

    void deleteColumnShrinksTheTable()
    {
        TableEdit e;
        QVERIFY(ops::deleteTableColumn(e.tx(), 0, "r1c1").ok);
        e.layout();
        QVERIFY(!e.page().findCell("r2c1"));
        QCOMPARE(e.children("r3"), (std::vector<std::string>{"r3c2", "r3c3"}));
        QCOMPARE(e.bounds("t"), (Rect{10, 10, 200, 120}));
        QCOMPARE(e.bounds("r3c2"), (Rect{0, 0, 100, 30}));
        QCOMPARE(e.bounds("r3c3"), (Rect{100, 0, 100, 30}));
        QVERIFY(e.rollbackRestores());
    }

    void deletingARowWithRemoveCellsShrinksTheTable()
    {
        TableEdit e;
        QVERIFY(ops::removeCells(e.tx(), 0, {"r1", "r3"}).ok);
        QCOMPARE(e.bounds("t").height, 60.0);
        // A single table cell leaves the table alone.
        QVERIFY(ops::removeCells(e.tx(), 0, {"r2c1"}).ok);
        QCOMPARE(e.bounds("t").height, 60.0);
        QVERIFY(e.rollbackRestores());
    }

    void rowHeightExtendsTheTable()
    {
        TableEdit e;
        QVERIFY(ops::setTableRowHeight(e.tx(), 0, "r1", 10, true).ok);
        e.layout();
        QCOMPARE(e.bounds("t").height, 130.0);
        QCOMPARE(e.bounds("r1"), (Rect{0, 30, 300, 40}));
        QCOMPARE(e.bounds("r2"), (Rect{0, 70, 300, 30}));
        QCOMPARE(e.bounds("r3"), (Rect{0, 100, 300, 30}));
        QCOMPARE(e.bounds("r1c2"), (Rect{100, 0, 100, 40}));
        QVERIFY(e.rollbackRestores());
    }

    void rowHeightWithoutExtendTakesFromTheNextRow()
    {
        TableEdit e;
        QVERIFY(ops::setTableRowHeight(e.tx(), 0, "r1", 10, false).ok);
        e.layout();
        QCOMPARE(e.bounds("t").height, 120.0);
        QCOMPARE(e.bounds("r1").height, 40.0);
        QCOMPARE(e.bounds("r2"), (Rect{0, 70, 300, 20}));
        QCOMPARE(e.bounds("r3"), (Rect{0, 90, 300, 30}));
        // The last row always extends the table.
        QVERIFY(ops::setTableRowHeight(e.tx(), 0, "r3", 5, false).ok);
        QCOMPARE(e.bounds("t").height, 125.0);
        QVERIFY(e.rollbackRestores());
    }

    void columnWidthTradesWithTheNextColumn()
    {
        TableEdit e;
        QVERIFY(ops::setTableColumnWidth(e.tx(), 0, "r2c1", 20).ok);
        e.layout();
        QCOMPARE(e.bounds("t").width, 300.0);
        for (const char* row : {"r1", "r2", "r3"}) {
            const auto cells = e.children(row);
            QCOMPARE(e.bounds(cells[0]), (Rect{0, 0, 120, 30}));
            QCOMPARE(e.bounds(cells[1]), (Rect{120, 0, 80, 30}));
            QCOMPARE(e.bounds(cells[2]), (Rect{200, 0, 100, 30}));
        }
        QVERIFY(e.rollbackRestores());
    }

    void lastColumnWidthGrowsTheTable()
    {
        TableEdit e;
        QVERIFY(ops::setTableColumnWidth(e.tx(), 0, "r1c3", 40).ok);
        e.layout();
        QCOMPARE(e.bounds("t").width, 340.0);
        QCOMPARE(e.bounds("r2c3"), (Rect{200, 0, 140, 30}));
        QVERIFY(e.rollbackRestores());
    }

    void resizeLastGivesTheLastColumnTheRest()
    {
        // resizeLast: cells keep their widths, the last one fills the row.
        TableEdit e;
        Cell t = *e.page().findCell("t");
        QVERIFY(e.tx().emplace<ReplaceCell>(0, [&] {
            t.setStyleText(std::string(kTableStyle) + "resizeLast=1;");
            t.geometry()->setWidth(350);
            return t;
        }()));
        e.layout();
        QCOMPARE(e.bounds("r1c1"), (Rect{0, 0, 100, 30}));
        QCOMPARE(e.bounds("r1c3"), (Rect{200, 0, 150, 30}));
        QVERIFY(e.rollbackRestores());
    }

    // --- merged cells ----------------------------------------------------------

    void tableCellsWalkTheGridAndResolveSpans()
    {
        TableEdit e;
        QVERIFY(ops::mergeTableCells(e.tx(), 0, {"r1c1", "r1c2", "r2c1", "r2c2"}).ok);
        const auto cells = ops::tableCells(e.page(), "t");
        QCOMPARE(cells.size(), std::size_t(9));
        const auto at = [&](const char* id) -> const ops::TableCellInfo& {
            for (const auto& info : cells) {
                if (info.id == id)
                    return info;
            }
            QTest::qFail("no such cell", __FILE__, __LINE__);
            return cells.front();
        };
        // 2x2 at the cell, then what is left of it to the right, below and diagonally.
        QCOMPARE(std::make_pair(at("r1c1").rowspan, at("r1c1").colspan), std::make_pair(2, 2));
        QCOMPARE(std::make_pair(at("r1c2").rowspan, at("r1c2").colspan), std::make_pair(2, 1));
        QCOMPARE(std::make_pair(at("r2c1").rowspan, at("r2c1").colspan), std::make_pair(1, 2));
        QCOMPARE(std::make_pair(at("r2c2").rowspan, at("r2c2").colspan), std::make_pair(1, 1));
        for (const char* covered : {"r1c2", "r2c1", "r2c2"})
            QCOMPARE(cells[at(covered).actual].id, std::string("r1c1"));
        QCOMPARE(cells[at("r1c3").actual].id, std::string("r1c3"));
        QCOMPARE(at("r3c2").row, 2);
        QCOMPARE(at("r3c2").col, 1);
    }

    void mergingASquareGrowsTheCellAndHidesTheCoveredOnes()
    {
        TableEdit e;
        QVERIFY(ops::mergeTableCells(e.tx(), 0, {"r2c2", "r1c1", "r2c1", "r1c2"}).ok);
        QCOMPARE(e.page().findCell("r1c1")->style().value("rowspan").value_or(""), std::string("2"));
        QCOMPARE(e.page().findCell("r1c1")->style().value("colspan").value_or(""), std::string("2"));
        e.layout();
        QCOMPARE(e.bounds("r1c1"), (Rect{0, 0, 200, 60}));
        // The alternate bounds keep the size of the single column and row.
        QCOMPARE(*e.page().findCell("r1c1")->geometry()->alternateBounds(), (Rect{0, 0, 100, 30}));
        for (const char* covered : {"r1c2", "r2c1", "r2c2"})
            QVERIFY(!e.visible(covered));
        for (const char* shown : {"r1c1", "r1c3", "r2c3", "r3c1", "r3c2", "r3c3"})
            QVERIFY(e.visible(shown));
        QCOMPARE(e.bounds("r1c3"), (Rect{200, 0, 100, 30}));
        QCOMPARE(e.bounds("t"), (Rect{10, 10, 300, 120}));
        QVERIFY(e.rollbackRestores());
    }

    void aRowSpanAndAColumnSpanGrowOneDirectionEach()
    {
        TableEdit e;
        QVERIFY(ops::setStyles(e.tx(), 0, {"r1c1"}, {{"colspan", "3"}}).ok);
        QVERIFY(ops::setStyles(e.tx(), 0, {"r2c3"}, {{"rowspan", "2"}}).ok);
        e.layout();
        QCOMPARE(e.bounds("r1c1"), (Rect{0, 0, 300, 30}));
        QCOMPARE(e.bounds("r2c3"), (Rect{200, 0, 100, 60}));
        QVERIFY(!e.visible("r1c2"));
        QVERIFY(!e.visible("r1c3"));
        QVERIFY(!e.visible("r3c3"));
        QVERIFY(e.visible("r2c1") && e.visible("r2c2") && e.visible("r3c1"));
        QVERIFY(e.rollbackRestores());
    }

    void layoutOfAMergedTableIsStable()
    {
        TableEdit e;
        QVERIFY(ops::mergeTableCells(e.tx(), 0, {"r1c1", "r1c2"}).ok);
        e.layout();
        const QByteArray once = e.bytes();
        e.layout();
        QCOMPARE(e.bytes(), once);
        // Resizing the table spreads the columns, and the merged cell follows.
        QVERIFY(ops::setTableColumnWidth(e.tx(), 0, "r3c3", 60).ok);
        e.layout();
        QCOMPARE(e.bounds("t").width, 360.0);
        QCOMPARE(e.bounds("r1c1"), (Rect{0, 0, 200, 30}));
        QCOMPARE(e.bounds("r1c3"), (Rect{200, 0, 160, 30}));
        QVERIFY(e.rollbackRestores());
    }

    void unmergingRestoresTheCellsAndUndoIsIdentity()
    {
        TableEdit e;
        const QByteArray original = e.bytes();
        QVERIFY(ops::mergeTableCells(e.tx(), 0, {"r1c1", "r1c2", "r2c1", "r2c2"}).ok);
        e.layout();
        QVERIFY(ops::unmergeTableCells(e.tx(), 0, {"r1c1"}).ok);
        e.layout();
        QVERIFY(!e.page().findCell("r1c1")->style().value("colspan"));
        QVERIFY(!e.page().findCell("r1c1")->style().value("rowspan"));
        for (const char* id : {"r1c1", "r1c2", "r2c1", "r2c2"}) {
            QVERIFY(e.visible(id));
            QCOMPARE(e.bounds(id).width, 100.0);
            QCOMPARE(e.bounds(id).height, 30.0);
        }
        // The cells are what they were, apart from the style attribute the merge left.
        QCOMPARE(e.page().findCell("r1c1")->styleText(), std::string("shape=partialRectangle;fillColor=none;"));
        QVERIFY(e.bytes() != original); // the original has rowspan=1
        QVERIFY(e.rollbackRestores());
    }

    void mergeRangeNeedsAFilledRectangleOfPlainCells()
    {
        TableEdit e;
        const auto range = ops::mergeRange(e.page(), {"r2c1", "r3c1", "r2c2", "r3c2"});
        QVERIFY(range.has_value());
        QCOMPARE(range->cell, std::string("r2c1"));
        QCOMPARE(range->rowspan, 2);
        QCOMPARE(range->colspan, 2);
        QVERIFY(!ops::mergeRange(e.page(), {"r1c1"}));                        // one cell
        QVERIFY(!ops::mergeRange(e.page(), {"r1c1", "r1c2", "r2c1"}));        // an L
        QVERIFY(!ops::mergeRange(e.page(), {"r1c1", "r1c3"}));                // a gap
        QVERIFY(!ops::mergeRange(e.page(), {"r1c1", "r1c1"}));                // twice the same
        QVERIFY(!ops::mergeRange(e.page(), {"r1c1", "r2"}));                  // a row
        QVERIFY(!ops::mergeRange(e.page(), {"t", "r1"}));
        QVERIFY(!ops::mergeTableCells(e.tx(), 0, {"r1c1", "r2c2"}).ok);
        // A merged cell is not merged again.
        QVERIFY(ops::mergeTableCells(e.tx(), 0, {"r1c1", "r1c2"}).ok);
        QVERIFY(!ops::mergeRange(e.page(), {"r1c1", "r1c3"}));
        QVERIFY(!ops::mergeRange(e.page(), {"r1c3", "r2c3", "r2c2"}));
        QVERIFY(ops::isMergedCell(e.page(), *e.page().findCell("r1c1")));
        QVERIFY(!ops::isMergedCell(e.page(), *e.page().findCell("r1c3")));
        QVERIFY(!ops::isMergedCell(e.page(), *e.page().findCell("r1")));
    }

    void unmergeFailsWithoutTableCells()
    {
        TableEdit e;
        QVERIFY(!ops::unmergeTableCells(e.tx(), 0, {"t", "r1"}).ok);
        QVERIFY(ops::unmergeTableCells(e.tx(), 0, {"r1c1"}).ok); // nothing to remove is fine
    }

    void insertingARowInsideARowSpanDropsTheSpanOnlyFromTheNewCells()
    {
        // As upstream: the span stays where it was and now covers the new row.
        TableEdit e;
        QVERIFY(ops::setStyles(e.tx(), 0, {"r1c1"}, {{"rowspan", "2"}}).ok);
        e.layout();
        QVERIFY(!e.visible("r2c1"));
        QVERIFY(ops::insertTableRow(e.tx(), 0, "r1c2", false).ok);
        e.layout();
        const auto rows = e.children("t");
        const auto added = e.children(rows[1]);
        QCOMPARE(e.page().findCell(added[0])->style().value("rowspan").value_or(""), std::string());
        QCOMPARE(e.bounds("r1c1"), (Rect{0, 0, 100, 60}));
        QVERIFY(!e.visible(added[0]));
        QVERIFY(e.visible("r2c1")); // no longer covered
        QCOMPARE(e.bounds("r2c1"), (Rect{0, 0, 100, 30}));
        QCOMPARE(e.bounds("t").height, 150.0);
        QVERIFY(e.rollbackRestores());
    }

    void insertingAColumnInsideAColumnSpanDropsTheSpanOnlyFromTheNewCells()
    {
        TableEdit e;
        QVERIFY(ops::setStyles(e.tx(), 0, {"r1c1"}, {{"colspan", "2"}}).ok);
        e.layout();
        QVERIFY(ops::insertTableColumn(e.tx(), 0, "r2c1", false).ok);
        e.layout();
        const auto cells = e.children("r1");
        QCOMPARE(cells.size(), std::size_t(4));
        QCOMPARE(e.bounds("t").width, 400.0);
        QCOMPARE(e.bounds("r1c1"), (Rect{0, 0, 200, 30})); // the new column is under it
        QVERIFY(!e.visible(cells[1]));
        QVERIFY(e.visible("r1c2"));
        QCOMPARE(e.bounds("r1c2"), (Rect{200, 0, 100, 30}));
        QCOMPARE(e.page().findCell(cells[1])->style().value("colspan").value_or(""), std::string());
        QVERIFY(e.rollbackRestores());
    }

    void deletingTheCoveredColumnLetsTheSpanCoverTheNextOne()
    {
        TableEdit e;
        QVERIFY(ops::mergeTableCells(e.tx(), 0, {"r1c1", "r1c2"}).ok);
        e.layout();
        QVERIFY(ops::deleteTableColumn(e.tx(), 0, "r3c2").ok);
        e.layout();
        QCOMPARE(e.children("r1"), (std::vector<std::string>{"r1c1", "r1c3"}));
        QCOMPARE(e.bounds("t").width, 200.0);
        QCOMPARE(e.bounds("r1c1"), (Rect{0, 0, 200, 30}));
        QVERIFY(!e.visible("r1c3"));
        QVERIFY(e.rollbackRestores());
    }

    void mergeAndUnmergeKeepTheDocumentLossless()
    {
        // A merged table written and read again is the same table.
        TableEdit e;
        QVERIFY(ops::mergeTableCells(e.tx(), 0, {"r1c1", "r1c2", "r2c1", "r2c2"}).ok);
        e.layout();
        const QByteArray merged = e.bytes();
        const auto reloaded = format::readDocument(merged);
        QVERIFY(reloaded.ok());
        QCOMPARE(format::writeDocument(*reloaded.document), merged);
    }

    void notATableFails()
    {
        TableEdit e;
        QVERIFY(!ops::insertTableRow(e.tx(), 0, "1", true).ok);
        QVERIFY(!ops::setTableRowHeight(e.tx(), 0, "r1c1", 5).ok);
        QVERIFY(!ops::setTableColumnWidth(e.tx(), 0, "r1", 5).ok);
    }

    void undoIdentityOnACorpusTable()
    {
        const QString path = QString::fromUtf8(QRAW_CORPUS_DIRS).section(u'|', 0, 0) +
                             QStringLiteral("/generated/er-tables/shop-schema-tables.drawio");
        const auto outcome = testing::checkUndoRedoIdentity(path, [](testing::StepRunner& run) {
            const auto step = [&](auto fn) {
                run.step([&](Transaction& tx) {
                    Result r = fn(tx);
                    if (!r)
                        return r;
                    Result laid = ops::applyLayouts(tx, 0, tx.changes());
                    return laid ? r : laid;
                });
            };
            step([](Transaction& tx) { return ops::insertTableRow(tx, 0, "5", false); });
            step([](Transaction& tx) { return ops::insertTableColumn(tx, 0, "5", true); });
            step([](Transaction& tx) { return ops::setTableRowHeight(tx, 0, "3", 12, true); });
            step([](Transaction& tx) { return ops::setTableColumnWidth(tx, 0, "5", 10, false); });
            step([](Transaction& tx) { return ops::deleteTableRow(tx, 0, "6"); });
            step([](Transaction& tx) { return ops::deleteTableColumn(tx, 0, "4"); });
        });
        QVERIFY2(outcome.failure.isEmpty(), qPrintable(outcome.failure));
        QCOMPARE(outcome.recorded, std::size_t(6));
    }
};

QTEST_APPLESS_MAIN(OpsTablesTest)
#include "tst_ops_tables.moc"
