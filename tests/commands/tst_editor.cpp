// Editor facade: edit/undo/redo, rollback on failure, nesting, observer
// notification and its re-entrancy rules.
#include "qraw/commands/editor.h"
#include "qraw/commands/primitives.h"
#include "qraw/format/codec.h"

#include <QTest>

#include <stdexcept>

using namespace qraw;
using namespace qraw::commands;

namespace {

core::Document sampleDocument(const char* pageId = "p1")
{
    core::Page page;
    page.setId(pageId);
    page.appendEntry(core::Cell("0"));
    page.appendEntry(core::Cell::makeLayer("1", "0"));
    page.appendEntry(core::Cell::makeVertex("2", "1", "A", "rounded=1;", {10, 20, 100, 50}));
    core::Document doc;
    doc.appendEntry(std::move(page));
    return doc;
}

QByteArray bytesOf(const Editor& editor) { return format::writeDocument(editor.document()); }

const std::string& styleOf(const Editor& editor) { return editor.document().page(0)->findCell("2")->styleText(); }

// Sets the style of cell "2" through a ReplaceCell.
Result setStyle(Transaction& tx, std::string style)
{
    core::Cell cell = *tx.document().page(0)->findCell("2");
    cell.setStyleText(style);
    return tx.emplace<ReplaceCell>(0, std::move(cell)) ? Result::success() : Result::failure("replace failed");
}

struct Recorder : DocumentObserver {
    std::vector<std::pair<ChangeSet, ChangeOrigin>> events;
    int undoStateCalls = 0;

    void documentChanged(const ChangeSet& changes, ChangeOrigin origin) override { events.emplace_back(changes, origin); }
    void undoStateChanged() override { ++undoStateCalls; }
};

} // namespace

class TestEditor : public QObject {
    Q_OBJECT

private slots:
    void successfulEdit()
    {
        Editor editor(sampleDocument());
        Recorder rec;
        editor.addObserver(&rec);

        const Result r = editor.edit("Restyle", [](Transaction& tx) { return setStyle(tx, "fillColor=#ff0000;"); });
        QVERIFY(r.ok);
        QCOMPARE(styleOf(editor), std::string("fillColor=#ff0000;"));
        QCOMPARE(editor.undoStack().count(), 1u);
        QCOMPARE(editor.undoStack().undoText(), std::string_view("Restyle"));
        QCOMPARE(rec.events.size(), 1u);
        QCOMPARE(rec.events[0].second, ChangeOrigin::Edit);
        QVERIFY(rec.events[0].first.pages().at("p1").changed.contains("2"));
        QCOMPARE(rec.undoStateCalls, 1);
        QVERIFY(editor.isModified());
    }

    void failedEditRollsBack()
    {
        Editor editor(sampleDocument());
        Recorder rec;
        editor.addObserver(&rec);
        const QByteArray before = bytesOf(editor);

        const Result r = editor.edit("Fail", [](Transaction& tx) {
            [[maybe_unused]] const Result ok = setStyle(tx, "x=1;");
            return Result::failure("nope");
        });
        QVERIFY(!r.ok);
        QCOMPARE(r.error, std::string("nope"));
        QCOMPARE(bytesOf(editor), before);
        QCOMPARE(editor.undoStack().count(), 0u);
        QVERIFY(rec.events.empty());
        QCOMPARE(rec.undoStateCalls, 0);
    }

    void throwingEditRollsBack()
    {
        Editor editor(sampleDocument());
        const QByteArray before = bytesOf(editor);

        QVERIFY_THROWS_EXCEPTION(std::runtime_error, editor.edit("Throw", [](Transaction& tx) -> Result {
            [[maybe_unused]] const Result ok = setStyle(tx, "x=1;");
            throw std::runtime_error("boom");
        }));
        QCOMPARE(bytesOf(editor), before);
        QVERIFY(!editor.isEditing());
        QCOMPARE(editor.undoStack().count(), 0u);
        QVERIFY(editor.edit("After", [](Transaction& tx) { return setStyle(tx, "y=1;"); }).ok);
    }

    void nestedFailureRollsBackOnlyInnerPart()
    {
        Editor editor(sampleDocument());
        bool editingInside = false;
        const Result r = editor.edit("Outer", [&](Transaction& tx) {
            if (const Result a = setStyle(tx, "a=1;"); !a.ok)
                return a;
            editingInside = editor.isEditing();
            const Result inner = editor.edit("Inner", [](Transaction& t) {
                [[maybe_unused]] const Result b = setStyle(t, "b=1;");
                return Result::failure("inner failed");
            });
            if (inner.ok)
                return Result::failure("inner should have failed");
            return Result::success();
        });
        QVERIFY(r.ok);
        QVERIFY(editingInside);
        QCOMPARE(editor.undoStack().count(), 1u);
        QCOMPARE(editor.undoStack().undoText(), std::string_view("Outer"));
        QCOMPARE(styleOf(editor), std::string("a=1;"));
        QVERIFY(editor.undo());
        QCOMPARE(styleOf(editor), std::string("rounded=1;"));
    }

    void emptyEditIsNotRecorded()
    {
        Editor editor(sampleDocument());
        Recorder rec;
        editor.addObserver(&rec);
        QVERIFY(editor.edit("Nothing", [](Transaction&) { return Result::success(); }).ok);
        QCOMPARE(editor.undoStack().count(), 0u);
        QVERIFY(rec.events.empty());
        QVERIFY(!editor.isModified());
    }

    void undoRedoNotify()
    {
        Editor editor(sampleDocument());
        QVERIFY(!editor.undo());
        QVERIFY(!editor.redo());
        QVERIFY(editor.edit("Restyle", [](Transaction& tx) { return setStyle(tx, "a=1;"); }).ok);

        Recorder rec;
        editor.addObserver(&rec);
        QVERIFY(editor.undo());
        QCOMPARE(styleOf(editor), std::string("rounded=1;"));
        QVERIFY(editor.redo());
        QCOMPARE(styleOf(editor), std::string("a=1;"));
        QCOMPARE(rec.events.size(), 2u);
        QCOMPARE(rec.events[0].second, ChangeOrigin::Undo);
        QCOMPARE(rec.events[1].second, ChangeOrigin::Redo);
        QCOMPARE(rec.undoStateCalls, 2);
        QVERIFY(rec.events[0].first.pages().at("p1").changed.contains("2"));
    }

    void observerCannotStartEditOrUndo()
    {
        struct Reentrant : DocumentObserver {
            Editor* editor = nullptr;
            bool editRan = false;
            Result editResult;
            bool undoResult = true;
            void documentChanged(const ChangeSet&, ChangeOrigin) override
            {
                editResult = editor->edit("From observer", [this](Transaction&) {
                    editRan = true;
                    return Result::success();
                });
                undoResult = editor->undo();
            }
        };

        Editor editor(sampleDocument());
        Reentrant observer;
        observer.editor = &editor;
        editor.addObserver(&observer);
        QVERIFY(editor.edit("Restyle", [](Transaction& tx) { return setStyle(tx, "a=1;"); }).ok);
        QVERIFY(!observer.editResult.ok);
        QVERIFY(!observer.editRan);
        QVERIFY(!observer.undoResult);
        QCOMPARE(editor.undoStack().count(), 1u);
        // The guard is released again afterwards.
        QVERIFY(editor.undo());
    }

    void observerRemovingItselfOrAnother()
    {
        struct Remover : DocumentObserver {
            Editor* editor = nullptr;
            DocumentObserver* other = nullptr; // removed in addition to itself
            int calls = 0;
            void documentChanged(const ChangeSet&, ChangeOrigin) override
            {
                ++calls;
                editor->removeObserver(other);
                editor->removeObserver(this);
            }
        };

        Editor editor(sampleDocument());
        Recorder victim;
        Remover remover;
        remover.editor = &editor;
        remover.other = &victim;
        editor.addObserver(&remover);
        editor.addObserver(&victim);
        editor.addObserver(&victim); // duplicate: ignored
        editor.addObserver(nullptr);

        QVERIFY(editor.edit("One", [](Transaction& tx) { return setStyle(tx, "a=1;"); }).ok);
        QCOMPARE(remover.calls, 1);
        QVERIFY(victim.events.empty());
        QCOMPARE(victim.undoStateCalls, 0);
        QVERIFY(editor.edit("Two", [](Transaction& tx) { return setStyle(tx, "a=2;"); }).ok);
        QCOMPARE(remover.calls, 1);
        QVERIFY(victim.events.empty());
    }

    void modifiedAndMarkSaved()
    {
        Editor editor(sampleDocument());
        Recorder rec;
        editor.addObserver(&rec);
        QVERIFY(!editor.isModified());
        QVERIFY(editor.edit("a", [](Transaction& tx) { return setStyle(tx, "a=1;"); }).ok);
        QVERIFY(editor.isModified());
        editor.markSaved();
        QVERIFY(!editor.isModified());
        QCOMPARE(rec.undoStateCalls, 2);
        editor.undo();
        QVERIFY(editor.isModified());
        editor.redo();
        QVERIFY(!editor.isModified());
    }

    void resetDocumentFoldsPagesAndClearsHistory()
    {
        Editor editor(sampleDocument("p1"));
        QVERIFY(editor.edit("a", [](Transaction& tx) { return setStyle(tx, "a=1;"); }).ok);
        Recorder rec;
        editor.addObserver(&rec);

        core::Document next = sampleDocument("p1");
        next.appendEntry([] {
            core::Page page;
            page.setId("p2");
            return page;
        }());
        QVERIFY(editor.resetDocument(std::move(next)));

        QCOMPARE(rec.events.size(), 1u);
        QCOMPARE(rec.events[0].second, ChangeOrigin::Reset);
        const ChangeSet& changes = rec.events[0].first;
        QVERIFY(changes.pages().at("p1").reset);
        QVERIFY(changes.pagesRemoved().empty());
        QCOMPARE(changes.pagesAdded(), (std::set<std::string>{"p2"}));
        QVERIFY(changes.documentChangedFlag());
        QCOMPARE(rec.undoStateCalls, 1);
        QCOMPARE(editor.undoStack().count(), 0u);
        QVERIFY(!editor.isModified());
        QVERIFY(!editor.undo());
    }

    void mergeThroughEditor()
    {
        Editor editor(sampleDocument());
        auto restyle = [&](const char* style) {
            return editor.edit("Restyle", [&](Transaction& tx) { return setStyle(tx, style); }, "style:2");
        };
        QVERIFY(restyle("a=1;").ok);
        QVERIFY(restyle("a=2;").ok);
        QCOMPARE(editor.undoStack().count(), 1u);
        editor.undoStack().mergeBreak();
        QVERIFY(restyle("a=3;").ok);
        QCOMPARE(editor.undoStack().count(), 2u);

        QVERIFY(editor.undo());
        QCOMPARE(styleOf(editor), std::string("a=2;"));
        QVERIFY(editor.undo());
        QCOMPARE(styleOf(editor), std::string("rounded=1;"));
    }
};

QTEST_GUILESS_MAIN(TestEditor)
#include "tst_editor.moc"
