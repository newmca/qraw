// Bulk operations on a 5000-cell page: select all, then duplicate, group,
// ungroup, copy and paste, move and delete, each undone back to the
// identical bytes. The time budgets guard against a return to the O(n) per
// cell page queries (D-025), not against slow machines.

#include "support/test_support.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/ui/cells_panel.h"

#include <QElapsedTimer>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;

namespace {

// Budgets in ms: three times the time measured on the dev preset after the
// fix (duplicate 0.27 s, paste 0.27 s, move 0.05 s, delete 0.09 s, group 0.11 s
// and ungroup 0.09 s), never below 1.5 s. Delete removes the cells back to
// front (ops::removeCells); group and ungroup move all their cells in a
// fixed number of primitives (ReplaceCells and ReorderCells, D-036; they took
// 5 s with one page-entry move per cell before).
//
// Sanitizer builds are slower: the budgets are multiplied by the CMake cache
// variable QRAW_TEST_TIMEOUT_SCALE (3 in the asan preset, 1 elsewhere).
#ifndef QRAW_TEST_TIMEOUT_SCALE
#define QRAW_TEST_TIMEOUT_SCALE 1
#endif
constexpr qint64 kBudgetScale = QRAW_TEST_TIMEOUT_SCALE;
constexpr qint64 kBudgetFastMs = 1500 * kBudgetScale;

} // namespace

// A page of `count` vertices on one layer, ten of them nested in each of `count / 10` groups.
QByteArray manyCellsXml(int count)
{
    QByteArray xml = R"(<mxfile><diagram id="p" name="Big"><mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/>)";
    for (int i = 0; i < count; ++i) {
        const bool group = i % 11 == 0;
        const QByteArray parent = group ? QByteArray("1") : "g" + QByteArray::number(i - i % 11);
        const QByteArray id = QByteArray(group ? "g" : "v") + QByteArray::number(i);
        const QByteArray style = group ? "group" : "";
        xml += "<mxCell id=\"" + id + "\" value=\"Cell " + QByteArray::number(i) + "\" style=\"" + style
            + "\" vertex=\"1\" parent=\"" + parent + "\"><mxGeometry x=\"" + QByteArray::number(i % 100 * 20)
            + "\" y=\"" + QByteArray::number(i / 100 * 20) + "\" width=\"16\" height=\"16\" as=\"geometry\"/></mxCell>";
    }
    return xml + "</root></mxGraphModel></diagram></mxfile>";
}

class TestPerfBulk : public QObject {
    Q_OBJECT

private:
    // Runs `step` with the page fully selected, checks the budget and that
    // one undo restores the bytes from before.
    template <class Step>
    void timedStepUndoesToTheSameBytes(DocumentSession& session, const char* name, qint64 budgetMs, Step&& step)
    {
        const QByteArray before = test::savedBytes(session);
        actions::selectAll(session);
        QElapsedTimer timer;
        timer.start();
        QVERIFY2(step(), name);
        const qint64 elapsed = timer.elapsed();
        qInfo().noquote() << name << elapsed << "ms";
        QVERIFY2(elapsed <= budgetMs, qPrintable(QStringLiteral("%1 took %2 ms").arg(name).arg(elapsed)));
        QVERIFY2(session.undo(), name);
        QVERIFY2(test::savedBytes(session) == before, name);
    }

private slots:
    // The Cells panel on 50 000 cells: a hidden panel costs nothing per edit, showing it builds the
    // tree once, and an edit that keeps the tree patches single rows.
    void theCellsModelOnFiftyThousandCellsDoesNoWorkWhileHiddenAndStaysFast()
    {
        const QByteArray xml = manyCellsXml(50000);
        DocumentSession session(test::documentFromXml(std::string_view(xml.constData(), static_cast<std::size_t>(xml.size()))));
        CellTreeModel model(session);
        QVERIFY(!model.isActive());

        QElapsedTimer timer;
        timer.start();
        for (int i = 0; i < 20; ++i) {
            QVERIFY(session.edit(QStringLiteral("Move"), [&](commands::Transaction& tx) {
                        return commands::ops::moveCells(tx, 0, {"v1"}, 1, 0);
                    }).ok);
        }
        QVERIFY2(timer.elapsed() < kBudgetFastMs, qPrintable(QStringLiteral("20 edits took %1 ms").arg(timer.elapsed())));
        QCOMPARE(model.rebuildCount(), std::size_t(0)); // hidden: not one row was looked at

        timer.restart();
        model.setActive(true);
        const qint64 built = timer.elapsed();
        qInfo().noquote() << "cells model, 50000 cells: build" << built << "ms";
        QVERIFY2(built < kBudgetFastMs, qPrintable(QStringLiteral("building the tree took %1 ms").arg(built)));
        QCOMPARE(model.rebuildCount(), std::size_t(1));
        QCOMPARE(model.rowCount(), 1);
        QCOMPARE(model.rowCount(model.index(0, 0)), 4546); // groups on the layer: 50000 / 11, rounded up

        timer.restart();
        for (int i = 0; i < 20; ++i) {
            QVERIFY(session.edit(QStringLiteral("Label"), [&](commands::Transaction& tx) {
                        return commands::ops::setLabel(tx, 0, "v1", "Changed " + std::to_string(i));
                    }).ok);
        }
        // A screenful of rows, as a view asks for them.
        const QModelIndex layer = model.index(0, 0);
        for (int row = 0; row < 60; ++row)
            QVERIFY(!model.index(row, 0, layer).data().toString().isEmpty());
        const qint64 patched = timer.elapsed();
        qInfo().noquote() << "cells model, 50000 cells: 20 label edits and 60 rows" << patched << "ms";
        QVERIFY2(patched < kBudgetFastMs, qPrintable(QStringLiteral("patching took %1 ms").arg(patched)));
        QCOMPARE(model.rebuildCount(), std::size_t(1)); // the tree was kept
    }

    void bulkOperationsOnFiveThousandCellsStayFastAndUndo()
    {
        auto loaded = FileService::load(QStringLiteral(QRAW_SOURCE_DIR "/tests/corpus/generated/stress/5000-cells.drawio"));
        QVERIFY(loaded.ok());
        DocumentSession session(std::move(*loaded.document));
        const QByteArray original = test::savedBytes(session);

        timedStepUndoesToTheSameBytes(session, "duplicate", kBudgetFastMs,
                                      [&] { return actions::duplicateSelection(session); });
        timedStepUndoesToTheSameBytes(session, "copy+paste", kBudgetFastMs, [&] {
            const std::string xml = encodeCells(*session.currentPage(), session.selection().cells());
            const auto bundle = decodeCells(xml);
            return bundle && actions::insertBundle(session, *bundle, QPointF(0, 0), QStringLiteral("Paste"));
        });
        timedStepUndoesToTheSameBytes(session, "move", kBudgetFastMs, [&] {
            const std::vector<std::string> ids = session.selection().cells();
            return session.edit(QStringLiteral("Move"), [&](commands::Transaction& tx) {
                       return commands::ops::moveCells(tx, session.currentPageIndex(), ids, 10, 10);
                   }).ok;
        });
        timedStepUndoesToTheSameBytes(session, "delete", kBudgetFastMs,
                                      [&] { return actions::deleteSelection(session, true); });

        // Group everything, then ungroup the group; undo both.
        actions::selectAll(session);
        QElapsedTimer timer;
        timer.start();
        QVERIFY(actions::group(session));
        qint64 elapsed = timer.elapsed();
        qInfo().noquote() << "group" << elapsed << "ms";
        QVERIFY2(elapsed <= kBudgetFastMs, qPrintable(QStringLiteral("group took %1 ms").arg(elapsed)));
        const QByteArray grouped = test::savedBytes(session);
        timer.restart();
        QVERIFY(actions::ungroup(session)); // the new group is the selection
        elapsed = timer.elapsed();
        qInfo().noquote() << "ungroup" << elapsed << "ms";
        QVERIFY2(elapsed <= kBudgetFastMs, qPrintable(QStringLiteral("ungroup took %1 ms").arg(elapsed)));
        QVERIFY(session.undo());
        QVERIFY(test::savedBytes(session) == grouped);
        QVERIFY(session.undo());
        QVERIFY(test::savedBytes(session) == original);
    }
};

QTEST_MAIN(TestPerfBulk)
#include "tst_perf_bulk.moc"
