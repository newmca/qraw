// Runs a restyle / merged move / delete script through the Editor for every
// corpus file and checks that undo-all and redo-all reproduce the exact bytes.
#include "corpus_harness.h"

#include "qraw/commands/editor.h"
#include "qraw/commands/primitives.h"
#include "qraw/commands/queries.h"

#include <QDir>
#include <QTest>

using namespace qraw;
using namespace qraw::commands;

class TestEditorCorpus : public QObject {
    Q_OBJECT

private slots:
    void editUndoRedoIdentity_data()
    {
        QTest::addColumn<QString>("path");
        const QDir root(QStringLiteral(QRAW_SOURCE_DIR));
        for (const QString& path : testing::corpusFiles())
            QTest::newRow(qPrintable(root.relativeFilePath(path))) << path;
    }

    void editUndoRedoIdentity()
    {
        QFETCH(QString, path);
        auto loaded = testing::loadFile(path);
        QVERIFY2(loaded, "load failed");
        const QByteArray before = format::writeDocument(*loaded);
        Editor editor(std::move(*loaded));

        const core::Page* page = editor.document().page(0);
        if (!page)
            return;
        const auto vertices = testing::cellIds(*page, [](const core::Cell& c) { return c.isVertex(); });
        if (vertices.empty())
            return;
        const std::string v = vertices.front();
        const auto allIds = testing::cellIds(*page, [](const core::Cell&) { return true; });
        const bool canDelete = allIds.size() > 2;
        const std::string last = allIds.back();

        auto replaceVertex = [&](Transaction& tx, auto&& modify) {
            core::Cell cell = *tx.document().page(0)->findCell(v);
            modify(cell);
            return tx.emplace<ReplaceCell>(0, std::move(cell)) ? Result::success() : Result::failure("ReplaceCell failed");
        };
        auto move = [&](Transaction& tx) {
            return replaceVertex(tx, [](core::Cell& cell) {
                if (core::Geometry* g = cell.geometry())
                    translateGeometry(*g, 7, 3, cell.isEdge());
            });
        };

        editor.markSaved();
        QVERIFY(!editor.isModified());

        std::size_t expected = 0;
        QVERIFY(editor
                    .edit("Restyle", [&](Transaction& tx) {
                        return replaceVertex(tx, [](core::Cell& cell) {
                            core::Style style = cell.style();
                            style.set("fillColor", "#00ff00");
                            cell.setStyle(style);
                        });
                    })
                    .ok);
        ++expected;
        QVERIFY(editor.isModified());
        QVERIFY(editor.edit("Move", move, "move:" + v).ok);
        ++expected;
        QVERIFY(editor.edit("Move", move, "move:" + v).ok); // merges into the previous move
        if (canDelete) {
            QVERIFY2(editor.edit("Delete", [&](Transaction& tx) {
                         return removeSubtree(tx, 0, last) ? Result::success() : Result::failure("removeSubtree failed");
                     }).ok,
                     "delete failed");
            ++expected;
        }
        QCOMPARE(editor.undoStack().count(), expected);

        const QByteArray after = format::writeDocument(editor.document());
        QVERIFY(editor.isModified());

        while (editor.undo()) {
        }
        QCOMPARE(editor.undoStack().index(), 0u);
        QVERIFY(!editor.isModified());
        QVERIFY2(format::writeDocument(editor.document()) == before,
                 qPrintable(testing::firstDifference(before, format::writeDocument(editor.document()))));

        while (editor.redo()) {
        }
        QCOMPARE(editor.undoStack().index(), expected);
        QVERIFY(editor.isModified());
        QVERIFY2(format::writeDocument(editor.document()) == after,
                 qPrintable(testing::firstDifference(after, format::writeDocument(editor.document()))));

        editor.markSaved();
        QVERIFY(!editor.isModified());
        QVERIFY(editor.undo());
        QVERIFY(editor.isModified());
    }
};

QTEST_GUILESS_MAIN(TestEditorCorpus)
#include "tst_editor_corpus.moc"
