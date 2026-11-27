// Runs a structure-operation script over every corpus file and checks that
// undo, redo and undo again reproduce the saved bytes exactly.
#include "corpus_harness.h"

#include "qraw/commands/ops_structure.h"
#include "qraw/commands/queries.h"

#include <QTest>

using namespace qraw;
using namespace qraw::commands;

namespace {

bool isVertex(const core::Cell& c) { return c.isVertex(); }

// The first two vertices that share a parent.
std::vector<std::string> firstSiblingPair(const core::Page& page, const std::vector<std::string>& vertices)
{
    for (std::size_t i = 0; i < vertices.size(); ++i) {
        for (std::size_t j = i + 1; j < vertices.size(); ++j) {
            if (page.findCell(vertices[i])->parent() == page.findCell(vertices[j])->parent())
                return {vertices[i], vertices[j]};
        }
    }
    return {};
}

void script(testing::StepRunner& run)
{
    const core::Page* page = run.document().page(0);
    if (!page)
        return;
    const std::vector<std::string> vertices = testing::cellIds(*page, isVertex);
    if (vertices.empty())
        return;
    using ops::ZOrder;

    run.step([&](Transaction& tx) { return ops::orderCells(tx, 0, {vertices.front()}, ZOrder::ToFront); });
    run.step([&](Transaction& tx) { return ops::orderCells(tx, 0, {vertices.back()}, ZOrder::ToBack); });
    if (vertices.size() > 1)
        run.step([&](Transaction& tx) { return ops::orderCells(tx, 0, {vertices[1]}, ZOrder::BringForward); });

    const std::vector<std::string> pair = firstSiblingPair(*page, vertices);
    if (!pair.empty()) {
        const Result grouped = run.step([&](Transaction& tx) { return ops::groupCells(tx, 0, pair); });
        if (grouped.ok)
            run.step([&](Transaction& tx) { return ops::ungroupCells(tx, 0, grouped.cells); });
    }

    // A vertex and another one that is neither its ancestor nor descendant.
    for (const std::string& a : vertices) {
        for (const std::string& b : vertices) {
            const core::Page& now = *run.document().page(0);
            if (a == b || !isAddressable(now, a) || !isAddressable(now, b) || isAncestor(now, a, b)
                || isAncestor(now, b, a))
                continue;
            run.step([&](Transaction& tx) { return ops::reparentCells(tx, 0, {a}, b); });
            run.step([&](Transaction& tx) { return ops::removeFromGroup(tx, 0, {a}); });
            return;
        }
    }
}

} // namespace

class TestOpsStructureCorpus : public QObject {
    Q_OBJECT

    std::size_t recorded_ = 0;

private slots:
    void identity_data()
    {
        QTest::addColumn<QString>("path");
        for (const QString& f : testing::corpusFiles())
            QTest::newRow(qPrintable(f.section('/', -2))) << f;
    }

    void identity()
    {
        QFETCH(QString, path);
        const testing::IdentityOutcome out = testing::checkUndoRedoIdentity(path, script);
        QVERIFY2(out.failure.isEmpty(), qPrintable(out.failure));
        recorded_ += out.recorded;
    }

    void cleanupTestCase() { QVERIFY(recorded_ > 0); }
};

QTEST_GUILESS_MAIN(TestOpsStructureCorpus)
#include "tst_ops_structure_corpus.moc"
