// FileService, RecoveryStore and DocumentController.

#include "support/test_support.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/format/embed.h"
#include "qraw/format/semantic.h"
#include "qraw/ui/document_controller.h"

#include <QSignalSpy>
#include <QTemporaryDir>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;

namespace {

// Answers the controller's questions from a script and records what it was told.
class ScriptedPrompts final : public DocumentPrompts {
public:
    SaveChoice saveChoice = SaveChoice::Cancel;
    std::optional<QString> openPath;
    std::optional<QString> savePath;
    int saveChangesAsked = 0;
    int savePathAsked = 0;
    QString lastSuggested;
    QStringList errors;
    QStringList warnings;

    SaveChoice askSaveChanges(const QString&) override
    {
        ++saveChangesAsked;
        return saveChoice;
    }
    std::optional<QString> askOpenPath(const QString&) override { return openPath; }
    std::optional<QString> askSavePath(const QString& suggested, const QString&) override
    {
        ++savePathAsked;
        lastSuggested = suggested;
        return savePath;
    }
    void showError(const QString&, const QString& message) override { errors << message; }
    void showWarnings(const QString&, const QStringList& messages) override { warnings << messages; }
};

void moveA(DocumentSession& session)
{
    session.edit(QStringLiteral("Move"), [](commands::Transaction& tx) { return commands::ops::moveCells(tx, 0, {"a"}, 10, 0); });
}

QByteArray fileBytes(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll() : QByteArray();
}

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

} // namespace

class TestSessionFiles : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qraw::ui::test::isolateSettings(); }
    void init() { AppSettings().clearRecentFiles(); }

    // --- FileService ---------------------------------------------------------

    void blankDocumentHasOneEmptyPage()
    {
        const core::Document blank = FileService::blankDocument();
        QCOMPARE(blank.pageCount(), std::size_t(1));
        const core::Page& page = *blank.page(0);
        QCOMPARE(page.name(), std::string("Page-1"));
        QCOMPARE(page.id().size(), std::size_t(20));
        QCOMPARE(page.cellCount(), std::size_t(2));
        QVERIFY(page.gridEnabled());
        QCOMPARE(page.gridSize(), 10.0);
        QCOMPARE(page.pageWidth(), 827.0);
        // Survives a round trip through the codec.
        QVERIFY(format::readDocument(format::writeDocument(blank)).ok());
    }

    void kindForPathFollowsTheExtension()
    {
        QCOMPARE(FileService::kindForPath("a.drawio"), FileKind::Drawio);
        QCOMPARE(FileService::kindForPath("a.drawio.png"), FileKind::DrawioPng);
        QCOMPARE(FileService::kindForPath("a.DRAWIO.SVG"), FileKind::DrawioSvg);
        QCOMPARE(FileService::kindForPath("a.xml"), FileKind::Xml);
        QVERIFY(!FileService::canSave(FileKind::Pdf));
    }

    void saveAndLoadRoundTripsEveryKind_data()
    {
        QTest::addColumn<QString>("name");
        QTest::addColumn<int>("kind");
        QTest::newRow("drawio") << "d.drawio" << int(FileKind::Drawio);
        QTest::newRow("xml") << "d.xml" << int(FileKind::Xml);
        QTest::newRow("png") << "d.drawio.png" << int(FileKind::DrawioPng);
        QTest::newRow("svg") << "d.drawio.svg" << int(FileKind::DrawioSvg);
    }
    void saveAndLoadRoundTripsEveryKind()
    {
        QFETCH(QString, name);
        QFETCH(int, kind);
        QTemporaryDir dir;
        const QString path = dir.filePath(name);
        const core::Document original = test::simpleDocument();
        QString error;
        QVERIFY2(FileService::save(original, path, FileKind(kind), &error), qPrintable(error));

        const LoadedFile loaded = FileService::load(path);
        QVERIFY2(loaded.ok(), qPrintable(loaded.error));
        QCOMPARE(loaded.kind, FileKind(kind));
        std::string difference;
        QVERIFY2(format::semanticEqual(original, *loaded.document, &difference), difference.c_str());
    }

    void loadReportsSpecificErrors()
    {
        QTemporaryDir dir;
        QVERIFY(!FileService::load(dir.filePath("missing.drawio")).ok());
        QVERIFY(FileService::load(dir.filePath("missing.drawio")).error.contains("could not be read"));

        const QString broken = dir.filePath("broken.drawio");
        QVERIFY(writeFile(broken, "<mxfile><diagram"));
        QVERIFY(FileService::load(broken).error.contains("could not be read"));

        // A PNG without an embedded diagram.
        QImage image(4, 4, QImage::Format_ARGB32);
        image.fill(Qt::white);
        const QString png = dir.filePath("plain.png");
        QVERIFY(image.save(png));
        QVERIFY(FileService::load(png).error.contains("contains no diagram"));
    }

    void savingAPdfKindIsRefused()
    {
        QTemporaryDir dir;
        QString error;
        QVERIFY(!FileService::save(test::simpleDocument(), dir.filePath("x.pdf"), FileKind::Pdf, &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(!QFileInfo::exists(dir.filePath("x.pdf")));
    }

    // --- DocumentController --------------------------------------------------

    void savingAnUntitledDocumentAsksForAPath()
    {
        QTemporaryDir dir;
        DocumentSession session(test::simpleDocument());
        ScriptedPrompts prompts;
        AppSettings settings;
        DocumentController controller(session, prompts, settings);
        QSignalSpy path(&controller, &DocumentController::filePathChanged);
        QSignalSpy title(&controller, &DocumentController::titleChanged);

        moveA(session);
        QVERIFY(controller.isModified());
        prompts.savePath = dir.filePath("out"); // no extension: .drawio is appended
        QVERIFY(controller.save());
        QCOMPARE(prompts.savePathAsked, 1);
        QCOMPARE(controller.filePath(), dir.filePath("out.drawio"));
        QCOMPARE(controller.displayName(), QStringLiteral("out.drawio"));
        QVERIFY(!controller.isModified());
        QVERIFY(QFileInfo::exists(dir.filePath("out.drawio")));
        QCOMPARE(path.count(), 1);
        QVERIFY(title.count() >= 1);
        QCOMPARE(settings.recentFiles().value(0), dir.filePath("out.drawio"));

        // A second save writes in place without asking.
        moveA(session);
        QVERIFY(controller.save());
        QCOMPARE(prompts.savePathAsked, 1);
        QCOMPARE(fileBytes(dir.filePath("out.drawio")), format::writeDocument(session.document()));
    }

    void maybeSaveHonoursTheChoice()
    {
        QTemporaryDir dir;
        DocumentSession session(test::simpleDocument());
        ScriptedPrompts prompts;
        AppSettings settings;
        DocumentController controller(session, prompts, settings);

        QVERIFY(controller.maybeSave()); // unmodified: no question
        QCOMPARE(prompts.saveChangesAsked, 0);

        moveA(session);
        prompts.saveChoice = DocumentPrompts::SaveChoice::Cancel;
        QVERIFY(!controller.maybeSave());
        prompts.saveChoice = DocumentPrompts::SaveChoice::Discard;
        QVERIFY(controller.maybeSave());
        QVERIFY(controller.isModified());
        QCOMPARE(prompts.saveChangesAsked, 2);

        prompts.saveChoice = DocumentPrompts::SaveChoice::Save;
        prompts.savePath = std::nullopt; // the user cancels the Save As dialog
        QVERIFY(!controller.maybeSave());
        prompts.savePath = dir.filePath("kept.drawio");
        QVERIFY(controller.maybeSave());
        QVERIFY(!controller.isModified());
    }

    void newDocumentAsksBeforeDiscarding()
    {
        DocumentSession session(test::simpleDocument());
        ScriptedPrompts prompts;
        AppSettings settings;
        DocumentController controller(session, prompts, settings);
        moveA(session);
        prompts.saveChoice = DocumentPrompts::SaveChoice::Cancel;
        QVERIFY(!controller.newDocument());
        QCOMPARE(session.document().page(0)->id(), std::string("p1"));

        prompts.saveChoice = DocumentPrompts::SaveChoice::Discard;
        QVERIFY(controller.newDocument());
        QCOMPARE(session.document().page(0)->cellCount(), std::size_t(2));
        QVERIFY(controller.filePath().isEmpty());
        QVERIFY(!controller.isModified());
        QVERIFY(!session.canUndo());
    }

    void openPathLoadsAndTracksRecentFiles()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("in.drawio");
        QVERIFY(FileService::save(test::simpleDocument(), path, FileKind::Drawio));
        DocumentSession session;
        ScriptedPrompts prompts;
        AppSettings settings;
        DocumentController controller(session, prompts, settings);
        QSignalSpy opened(&controller, &DocumentController::documentOpened);

        QVERIFY(controller.openPath(path));
        QCOMPARE(opened.count(), 1);
        QCOMPARE(controller.filePath(), path);
        QCOMPARE(session.baseDirectory(), dir.path());
        QVERIFY(session.currentPage()->findCell("c1") != nullptr);
        QCOMPARE(settings.recentFiles().first(), path);
        QVERIFY(prompts.errors.isEmpty());
    }

    void failedOpenRemovesTheRecentEntry()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("gone.drawio");
        DocumentSession session(test::simpleDocument());
        ScriptedPrompts prompts;
        AppSettings settings;
        settings.addRecentFile(path);
        DocumentController controller(session, prompts, settings);
        QSignalSpy recent(&controller, &DocumentController::recentFilesChanged);

        QVERIFY(!controller.openPath(path));
        QCOMPARE(prompts.errors.size(), 1);
        QVERIFY(!settings.recentFiles().contains(path));
        QCOMPARE(recent.count(), 1);
        QCOMPARE(session.document().page(0)->id(), std::string("p1")); // untouched
    }

    void openFromPromptAndCancel()
    {
        DocumentSession session;
        ScriptedPrompts prompts;
        AppSettings settings;
        DocumentController controller(session, prompts, settings);
        QVERIFY(!controller.open()); // askOpenPath returned nothing
        QVERIFY(prompts.errors.isEmpty());
    }

    void revertReloadsTheFileWithoutAsking()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("r.drawio");
        QVERIFY(FileService::save(test::simpleDocument(), path, FileKind::Drawio));
        DocumentSession session;
        ScriptedPrompts prompts;
        AppSettings settings;
        DocumentController controller(session, prompts, settings);

        QVERIFY(!controller.revert()); // untitled
        QVERIFY(controller.openPath(path));
        const QByteArray before = test::savedBytes(session);
        moveA(session);
        QVERIFY(controller.isModified());
        QVERIFY(controller.revert());
        QCOMPARE(prompts.saveChangesAsked, 0);
        QCOMPARE(test::savedBytes(session), before);
        QVERIFY(!controller.isModified());
    }

    void savedImageKeepsItsKind()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath("img.drawio.svg");
        QVERIFY(FileService::save(test::simpleDocument(), path, FileKind::DrawioSvg));
        DocumentSession session;
        ScriptedPrompts prompts;
        AppSettings settings;
        DocumentController controller(session, prompts, settings);
        QVERIFY(controller.openPath(path));
        QCOMPARE(controller.fileKind(), FileKind::DrawioSvg);
        moveA(session);
        QVERIFY(controller.save());
        QCOMPARE(prompts.savePathAsked, 0);
        QCOMPARE(format::sniffContainer(fileBytes(path)), format::ContainerKind::Svg);
        const LoadedFile again = FileService::load(path);
        QVERIFY(again.ok());
        QVERIFY(format::semanticEqual(session.document(), *again.document));
    }

    // --- RecoveryStore -------------------------------------------------------

    void recoveryStoreLocksItsOwnSnapshots()
    {
        QTemporaryDir dir;
        const core::Document document = test::simpleDocument();
        {
            RecoveryStore first(dir.path());
            QVERIFY(first.write("s1", document, "/some/file.drawio"));
            QVERIFY(QFileInfo::exists(dir.filePath("s1.drawio")));
            QVERIFY(QFileInfo::exists(dir.filePath("s1.json")));
            QVERIFY(first.orphans().empty()); // ours

            RecoveryStore second(dir.path());
            QVERIFY(second.orphans().empty()); // locked by a live holder
        }
        // The first instance went away without remove(): a crash as far as the files tell.
        RecoveryStore later(dir.path());
        const std::vector<RecoveryEntry> orphans = later.orphans();
        QCOMPARE(orphans.size(), std::size_t(1));
        QCOMPARE(orphans[0].id, QStringLiteral("s1"));
        QCOMPARE(orphans[0].originalPath, QStringLiteral("/some/file.drawio"));
        QVERIFY(orphans[0].savedAt.isValid());

        later.discard(orphans[0]);
        QVERIFY(later.orphans().empty());
        QVERIFY(!QFileInfo::exists(dir.filePath("s1.drawio")));
    }

    void recoveryRemoveDeletesAllFiles()
    {
        QTemporaryDir dir;
        RecoveryStore store(dir.path());
        QVERIFY(store.write("s1", test::simpleDocument(), {}));
        store.remove("s1");
        QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    }

    void autosaveThenRestoreMarksTheDocumentModified()
    {
        QTemporaryDir dir;
        const QString original = dir.filePath("work.drawio");
        QVERIFY(FileService::save(test::simpleDocument(), original, FileKind::Drawio));
        QByteArray modifiedBytes;
        {
            RecoveryStore store(dir.filePath("recovery"));
            DocumentSession session(test::simpleDocument());
            ScriptedPrompts prompts;
            AppSettings settings;
            DocumentController controller(session, prompts, settings, &store);
            QVERIFY(controller.openPath(original));
            controller.autosaveNow();
            QVERIFY(store.orphans().empty() && QDir(dir.filePath("recovery")).entryList(QDir::Files).isEmpty()); // clean: nothing written
            moveA(session);
            modifiedBytes = test::savedBytes(session);
            controller.autosaveNow();
            QVERIFY(QFileInfo::exists(dir.filePath("recovery/%1.drawio").arg(controller.sessionId())));
            // The store and controller are dropped without closeSession(): the app "crashed".
        }

        RecoveryStore store(dir.filePath("recovery"));
        DocumentSession session;
        ScriptedPrompts prompts;
        AppSettings settings;
        DocumentController controller(session, prompts, settings, &store);
        const std::vector<RecoveryEntry> pending = controller.pendingRecoveries();
        QCOMPARE(pending.size(), std::size_t(1));
        QCOMPARE(pending[0].originalPath, original);

        QVERIFY(controller.restoreRecovery(pending[0]));
        QVERIFY(controller.isModified());
        QVERIFY(!session.isModified()); // the history is clean, the controller still knows
        QCOMPARE(controller.filePath(), original);
        QCOMPARE(controller.displayName(), QStringLiteral("work.drawio"));
        QCOMPARE(test::savedBytes(session), modifiedBytes);
        QVERIFY(controller.pendingRecoveries().empty());

        // Saving clears the restored flag and the snapshot.
        QVERIFY(controller.save());
        QVERIFY(!controller.isModified());
        QCOMPARE(fileBytes(original), modifiedBytes);
        QVERIFY(QDir(dir.filePath("recovery")).entryList({"*.drawio"}, QDir::Files).isEmpty());
    }

    void discardRecoveryRemovesTheOrphan()
    {
        QTemporaryDir dir;
        {
            RecoveryStore crashed(dir.path());
            QVERIFY(crashed.write("old", test::simpleDocument(), {}));
        }
        RecoveryStore store(dir.path());
        DocumentSession session;
        ScriptedPrompts prompts;
        AppSettings settings;
        DocumentController controller(session, prompts, settings, &store);
        const auto pending = controller.pendingRecoveries();
        QCOMPARE(pending.size(), std::size_t(1));
        controller.discardRecovery(pending[0]);
        QVERIFY(controller.pendingRecoveries().empty());
    }

    void closeSessionRemovesTheSnapshot()
    {
        QTemporaryDir dir;
        RecoveryStore store(dir.path());
        DocumentSession session(test::simpleDocument());
        ScriptedPrompts prompts;
        AppSettings settings;
        DocumentController controller(session, prompts, settings, &store);
        moveA(session);
        controller.autosaveNow();
        QVERIFY(!QDir(dir.path()).entryList({"*.drawio"}, QDir::Files).isEmpty());
        controller.closeSession();
        QVERIFY(QDir(dir.path()).entryList(QDir::Files).isEmpty());
    }

    void autosaveTimerFiresWhileModified()
    {
        QTemporaryDir dir;
        RecoveryStore store(dir.path());
        DocumentSession session(test::simpleDocument());
        ScriptedPrompts prompts;
        AppSettings settings;
        DocumentController controller(session, prompts, settings, &store);
        controller.setAutosaveInterval(1);
        moveA(session);
        QTRY_VERIFY_WITH_TIMEOUT(QFileInfo::exists(dir.filePath("%1.drawio").arg(controller.sessionId())), 3000);
    }
};

QTEST_MAIN(TestSessionFiles)
#include "tst_session_files.moc"
