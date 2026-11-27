// Cell operations over the whole corpus: a fixed script of operations per
// file, then the undo/redo byte-identity check (corpus_harness.h).
#include "corpus_harness.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_layers.h"

#include <QDir>
#include <QTest>

using namespace qraw;
using namespace qraw::commands;

class TestOpsCellsCorpus : public QObject {
    Q_OBJECT

private slots:
    void corpus_data()
    {
        QTest::addColumn<QString>("path");
        for (const QString& f : testing::corpusFiles())
            QTest::newRow(qPrintable(QDir(QStringLiteral(QRAW_SOURCE_DIR)).relativeFilePath(f))) << f;
    }

    void corpus()
    {
        QFETCH(QString, path);
        const auto outcome = testing::checkUndoRedoIdentity(path, [this](testing::StepRunner& run) {
            const core::Page* page = run.document().page(0);
            if (!page)
                return;
            // Targets are chosen up front; a step on a cell that an earlier
            // step removed fails and is rolled back by the runner.
            const auto vertices = testing::cellIds(*page, [](const core::Cell& c) { return c.isVertex(); });
            const auto edges = testing::cellIds(*page, [](const core::Cell& c) { return c.isEdge(); });
            std::vector<std::string> layers;
            for (const core::Cell* l : page->layers())
                layers.push_back(l->id());
            const std::string layer = layers.empty() ? "1" : layers.front();
            const std::string edgeSource = edges.empty() ? "" : page->findCell(edges.front())->source();

            if (!vertices.empty()) {
                run.step([&](Transaction& tx) { return ops::moveCells(tx, 0, {vertices[0]}, 5, 5); });
                run.step([&](Transaction& tx) {
                    const core::Geometry* g = tx.page(0)->findCell(vertices[0])->geometry();
                    if (!g)
                        return Result::failure("vertex has no geometry");
                    core::Rect b = g->bounds();
                    b.width += 10;
                    b.height += 10;
                    return ops::resizeCells(tx, 0, {vertices[0]}, {b});
                });
            }
            if (!edges.empty()) {
                run.step([&](Transaction& tx) { return ops::setWaypoints(tx, 0, edges[0], {{1, 2}, {3, 4}}); });
                run.step([&](Transaction& tx) { return ops::disconnectEdge(tx, 0, edges[0], true, {0, 0}); });
                if (!edgeSource.empty())
                    run.step([&](Transaction& tx) { return ops::connectEdge(tx, 0, edges[0], edgeSource, true); });
            }
            run.step([&](Transaction& tx) {
                return ops::addCells(tx, 0, {core::Cell::makeVertex("", layer, "new", "rounded=1;", {0, 0, 40, 40})});
            });
            if (vertices.size() > 1)
                run.step([&](Transaction& tx) { return ops::removeCells(tx, 0, {vertices[1]}, {.includeEdges = false}); });
            if (vertices.size() > 2)
                run.step([&](Transaction& tx) { return ops::removeCells(tx, 0, {vertices[2]}, {.includeEdges = true}); });
            if (layers.size() >= 2)
                run.step([&](Transaction& tx) { return ops::removeLayers(tx, 0, {layers.back()}); });
            total_ += run.recorded();
        });
        QVERIFY2(outcome.failure.isEmpty(), qPrintable(outcome.failure));
    }

    void cleanupTestCase() { QVERIFY(total_ > 0); }

private:
    std::size_t total_ = 0;
};

QTEST_GUILESS_MAIN(TestOpsCellsCorpus)
#include "tst_ops_cells_corpus.moc"
