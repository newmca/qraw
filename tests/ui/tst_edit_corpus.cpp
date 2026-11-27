// Editing sessions over the whole corpus: every file survives select all,
// duplicate, group, align, delete and paste in the UI model, renders after each
// step, and undoes back to the identical bytes (stress files excluded: slow).

#include "support/test_support.h"

#include "qraw/commands/ops_pages.h"

#include <QClipboard>
#include <QDirIterator>
#include <QMimeData>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;

class TestEditCorpus : public QObject {
    Q_OBJECT
private slots:
    void editingEveryCorpusFileUndoesToTheSameBytes()
    {
        QDirIterator it(QStringLiteral(QRAW_SOURCE_DIR "/tests/corpus"), {"*.drawio", "*.xml"}, QDir::Files,
                        QDirIterator::Subdirectories);
        int files = 0;
        QStringList failures;
        while (it.hasNext()) {
            const QString path = it.next();
            if (path.contains(QLatin1String("pathological")) || path.contains(QLatin1String("stress")))
                continue;
            QString error;
            auto loaded = FileService::load(path);
            if (!loaded.ok())
                continue;
            ++files;
            QElapsedTimer timer;
            timer.start();
            DocumentSession session(std::move(*loaded.document));
            auto canvas = test::makeCanvas(session, {600, 400});
            const QByteArray original = test::savedBytes(session);
            for (std::size_t page = 0; page < session.document().pageCount(); ++page) {
                session.setCurrentPage(page);
                canvas->fitWindow();
                canvas->grab();
                const QByteArray pageStart = test::savedBytes(session);
                int undos = 0;
                auto step = [&](const char* name, bool ok) {
                    if (ok)
                        ++undos;
                    canvas->grab();
                    (void)name;
                };
                actions::selectAll(session);
                step("dup", actions::duplicateSelection(session));
                actions::selectAll(session);
                step("group", actions::group(session));
                step("align", actions::align(session, actions::Align::Left));
                step("front", actions::order(session, commands::ops::ZOrder::ToFront));
                step("clearWaypoints", actions::clearWaypoints(session));
                step("lock", actions::toggleLock(session));
                actions::selectAll(session);
                QGuiApplication::clipboard()->clear();
                if (actions::copySelection(session, *QGuiApplication::clipboard()))
                    step("paste", actions::paste(session, *QGuiApplication::clipboard()->mimeData()));
                actions::selectAll(session);
                step("delete", actions::deleteSelection(session, true));
                while (undos-- > 0)
                    if (!session.undo())
                        failures << path + ": undo failed";
                if (test::savedBytes(session) != pageStart)
                    failures << path + QStringLiteral(" page %1: bytes differ after undo").arg(page);
            }
            if (timer.elapsed() > 2000)
                qInfo() << path << timer.elapsed() << "ms";
            if (test::savedBytes(session) != original)
                failures << path + ": document differs after undo";
        }
        QVERIFY2(files > 300, "corpus not found");
        QVERIFY2(failures.isEmpty(), qPrintable(failures.join('\n')));
    }
};

QTEST_MAIN(TestEditCorpus)
#include "tst_edit_corpus.moc"
