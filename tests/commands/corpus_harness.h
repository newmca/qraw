#pragma once

// Corpus-driven undo/redo identity check shared by the commands tests.
//
// For one corpus file: load it, save it (the "before" bytes), run a script
// of steps where each step is one Transaction (like one undo entry), save
// again (the "after" bytes), then
//   1. revert all steps newest first  -> save must equal "before" bytes,
//   2. re-apply all steps in order     -> save must equal "after" bytes,
//   3. revert all again                -> save must equal "before" bytes.
// Byte equality of the saved file is the M2 acceptance criterion (ARCHITECTURE.md).
//
// This deliberately uses only Transaction/CommandGroup, not the Editor or
// UndoStack, so that every operations package can test against it
// independently. tst_editor_corpus.cpp runs the same idea through the Editor.

#include "qraw/commands/command_group.h"
#include "qraw/commands/result.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/document.h"
#include "qraw/format/codec.h"

#include <QDirIterator>
#include <QFile>
#include <QString>
#include <QStringList>

#include <algorithm>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace qraw::testing {

// Every *.drawio / *.xml file under QRAW_CORPUS_DIRS, sorted.
inline QStringList corpusFiles()
{
    QStringList files;
    const QStringList dirs = QString::fromUtf8(QRAW_CORPUS_DIRS).split(u'|', Qt::SkipEmptyParts);
    for (const QString& dir : dirs) {
        QDirIterator it(dir, {"*.drawio", "*.xml"}, QDir::Files, QDirIterator::Subdirectories);
        while (it.hasNext())
            files << it.next();
    }
    std::sort(files.begin(), files.end());
    return files;
}

inline std::optional<core::Document> loadFile(const QString& path)
{
    auto result = format::readFile(path);
    if (result.hasErrors() || !result.document)
        return std::nullopt;
    return std::move(result.document);
}

// Runs script steps on a document and keeps each committed step.
class StepRunner {
public:
    using StepFunction = std::function<commands::Result(commands::Transaction&)>;

    explicit StepRunner(core::Document& document) : document_(document) {}

    [[nodiscard]] const core::Document& document() const { return document_; }

    // Runs `fn` in its own transaction. A failed result rolls the step back
    // and is not recorded; an empty successful step is not recorded either.
    commands::Result step(const StepFunction& fn)
    {
        commands::Transaction tx(document_);
        commands::Result r = fn(tx);
        if (!r.ok) {
            tx.rollback();
            ++failed_;
            return r;
        }
        auto committed = tx.commit();
        if (!committed.group->empty())
            steps_.push_back(std::move(committed.group));
        return r;
    }

    [[nodiscard]] std::size_t recorded() const { return steps_.size(); }
    [[nodiscard]] std::size_t failed() const { return failed_; }

    void undoAll()
    {
        commands::ChangeSet ignored;
        for (auto it = steps_.rbegin(); it != steps_.rend(); ++it)
            (*it)->revert(document_, ignored);
    }
    bool redoAll()
    {
        commands::ChangeSet ignored;
        for (auto& s : steps_) {
            if (!s->apply(document_, ignored))
                return false;
        }
        return true;
    }

private:
    core::Document& document_;
    std::vector<std::unique_ptr<commands::CommandGroup>> steps_;
    std::size_t failed_ = 0;
};

struct IdentityOutcome {
    QString failure;          // empty: pass
    std::size_t recorded = 0; // steps that changed something
};

inline QString firstDifference(const QByteArray& a, const QByteArray& b)
{
    const auto mismatch = std::mismatch(a.begin(), a.end(), b.begin(), b.end());
    const auto at = mismatch.first - a.begin();
    return QStringLiteral("first difference at byte %1: expected \"%2\", got \"%3\"")
        .arg(at)
        .arg(QString::fromUtf8(a.mid(std::max<qsizetype>(0, at - 40), 120)))
        .arg(QString::fromUtf8(b.mid(std::max<qsizetype>(0, at - 40), 120)));
}

// The check described at the top of this file. `script` issues steps
// through the runner; it may inspect runner.document() between steps.
inline IdentityOutcome checkUndoRedoIdentity(const QString& path, const std::function<void(StepRunner&)>& script)
{
    IdentityOutcome out;
    auto loaded = loadFile(path);
    if (!loaded) {
        out.failure = QStringLiteral("load failed");
        return out;
    }
    core::Document document = std::move(*loaded);
    const QByteArray before = format::writeDocument(document);

    StepRunner runner(document);
    script(runner);
    out.recorded = runner.recorded();
    const QByteArray after = format::writeDocument(document);

    runner.undoAll();
    if (const QByteArray undone = format::writeDocument(document); undone != before) {
        out.failure = QStringLiteral("undo all != original: ") + firstDifference(before, undone);
        return out;
    }
    if (!runner.redoAll()) {
        out.failure = QStringLiteral("redo failed to apply");
        return out;
    }
    if (const QByteArray redone = format::writeDocument(document); redone != after) {
        out.failure = QStringLiteral("redo all != post-mutation save: ") + firstDifference(after, redone);
        return out;
    }
    runner.undoAll();
    if (const QByteArray undone = format::writeDocument(document); undone != before)
        out.failure = QStringLiteral("second undo all != original: ") + firstDifference(before, undone);
    return out;
}

// Ids of the cells of `page` matching `pred`, in z-order.
template <class Pred>
std::vector<std::string> cellIds(const core::Page& page, Pred&& pred)
{
    std::vector<std::string> out;
    for (const core::Cell* c : page.cells()) {
        if (!c->id().empty() && pred(*c))
            out.push_back(c->id());
    }
    return out;
}

} // namespace qraw::testing
