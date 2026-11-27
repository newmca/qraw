// Page, layer and property operations on every corpus file: each file is
// loaded, a script of steps is applied (one undo entry per step), and the
// saved bytes must return to the original after undo and match again after
// redo (corpus_harness.h).
#include "corpus_harness.h"

#include "qraw/commands/ops_layers.h"
#include "qraw/commands/ops_pages.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/cell.h"
#include "qraw/core/document.h"
#include "qraw/core/page.h"

#include <QDir>
#include <QTest>

#include <optional>
#include <string>

using namespace qraw;
using namespace qraw::commands;

namespace {

// Id of the first vertex on page 0, if any.
std::optional<std::string> firstVertex(const core::Document& doc)
{
    const core::Page* page = doc.page(0);
    if (!page)
        return std::nullopt;
    for (const core::Cell* cell : page->cells()) {
        if (!cell->id().empty() && cell->isVertex())
            return cell->id();
    }
    return std::nullopt;
}

// Id of the bottom layer of page 0, if any.
std::optional<std::string> firstLayer(const core::Document& doc)
{
    const core::Page* page = doc.page(0);
    if (!page)
        return std::nullopt;
    const std::vector<std::string> layers = ops::layerIds(*page);
    if (layers.empty())
        return std::nullopt;
    return layers.front();
}

// Every step is issued through the runner; steps whose targets are missing
// in this file are skipped. Targets are looked up again before each step,
// because earlier steps may have moved or duplicated pages.
void runScript(testing::StepRunner& run)
{
    if (const auto vertex = firstVertex(run.document())) {
        const std::string id = *vertex;
        run.step([&](Transaction& tx) { return ops::setStyle(tx, 0, {id}, "fillColor", std::string("#123456")); });
        run.step([&](Transaction& tx) { return ops::setLabel(tx, 0, id, "renamed"); });
        run.step([&](Transaction& tx) { return ops::setCellData(tx, 0, id, "qrawTest", std::string("1")); });
    }

    if (run.document().pageCount() > 0) {
        ops::PageProperties grid;
        grid.grid = false;
        run.step([&](Transaction& tx) { return ops::setPageProperties(tx, 0, grid); });
        run.step([&](Transaction& tx) { return ops::renamePage(tx, 0, "renamed page"); });
    }

    int pageCounter = 0;
    const ops::PageIdGenerator newId = [&pageCounter] { return "pid-" + std::to_string(++pageCounter); };
    run.step([&](Transaction& tx) { return ops::addPage(tx, std::nullopt, std::nullopt, newId); });
    if (run.document().pageCount() > 0)
        run.step([&](Transaction& tx) { return ops::duplicatePage(tx, 0, newId); });
    if (run.document().pageCount() >= 2)
        run.step([&](Transaction& tx) { return ops::movePage(tx, 0, 1); });
    if (run.document().pageCount() >= 2)
        run.step([&](Transaction& tx) { return ops::removePage(tx, 1); });

    run.step([&](Transaction& tx) { return ops::addLayer(tx, 0); });
    if (const auto layer = firstLayer(run.document())) {
        const std::string id = *layer;
        run.step([&](Transaction& tx) { return ops::setLayerLocked(tx, 0, id, true); });
    }
    if (const auto layer = firstLayer(run.document())) {
        const std::string id = *layer;
        run.step([&](Transaction& tx) { return ops::setLayerVisible(tx, 0, id, false); });
    }
    if (const auto layer = firstLayer(run.document())) {
        const std::string id = *layer;
        run.step([&](Transaction& tx) { return ops::duplicateLayer(tx, 0, id); });
    }
    if (const auto layer = firstLayer(run.document())) {
        const std::string id = *layer;
        run.step([&](Transaction& tx) { return ops::moveLayer(tx, 0, id, 1); });
    }
}

} // namespace

class TestOpsPagesLayersCorpus : public QObject {
    Q_OBJECT

private:
    std::size_t recorded_ = 0;

private slots:
    void undoRedoIdentity_data()
    {
        QTest::addColumn<QString>("path");
        const QDir root(QStringLiteral(QRAW_SOURCE_DIR));
        for (const QString& path : testing::corpusFiles())
            QTest::newRow(qPrintable(root.relativeFilePath(path))) << path;
    }

    void undoRedoIdentity()
    {
        QFETCH(QString, path);
        const testing::IdentityOutcome outcome = testing::checkUndoRedoIdentity(path, runScript);
        QVERIFY2(outcome.failure.isEmpty(), qPrintable(outcome.failure));
        recorded_ += outcome.recorded;
    }

    void cleanupTestCase()
    {
        QVERIFY2(recorded_ > 0, "no corpus step changed anything");
    }
};

QTEST_GUILESS_MAIN(TestOpsPagesLayersCorpus)
#include "tst_ops_pages_layers_corpus.moc"
