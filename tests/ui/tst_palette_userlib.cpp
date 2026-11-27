// User shape libraries in the palette: open, insert, add selection, close,
// reopen on start, and the routing of <mxlibrary> files (M5 libraries).

#include "support/test_support.h"

#include "qraw/format/library.h"
#include "qraw/ui/document_controller.h"
#include "qraw/ui/palette_dock.h"

#include <QFile>
#include <QSettings>
#include <QSignalSpy>
#include <QTemporaryDir>
#include <QToolButton>

using namespace qraw;
using namespace qraw::ui;

namespace {

const std::string kModel =
    "<mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/><mxCell id=\"2\" value=\"Lib\" "
    "style=\"rounded=1;\" vertex=\"1\" parent=\"1\"><mxGeometry width=\"80\" height=\"40\" as=\"geometry\"/></mxCell>"
    "</root></mxGraphModel>";

QString writeLibraryFile(const QTemporaryDir& dir, const QString& name, const format::Library& library)
{
    const QString path = dir.filePath(name);
    QFile file(path);
    if (!file.open(QIODevice::WriteOnly))
        return {};
    const std::string bytes = format::serializeLibrary(library);
    file.write(bytes.data(), static_cast<qint64>(bytes.size()));
    return path;
}

format::Library oneEntryLibrary()
{
    format::Library library;
    library.entries.push_back({"MyCard", kModel, {}, 80, 40, ""});
    return library;
}

format::Library readLibrary(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    const QByteArray bytes = file.readAll();
    return format::parseLibrary(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()))).value_or(format::Library{});
}

QList<QToolButton*> itemButtons(PaletteDock& dock, const QString& title)
{
    QList<QToolButton*> found;
    for (QToolButton* button : dock.findChildren<QToolButton*>()) {
        if (button->toolTip().startsWith(title + QLatin1String(" (")) && button->isVisibleTo(&dock))
            found.push_back(button);
    }
    return found;
}

QToolButton* sectionHeader(PaletteDock& dock, const QString& title)
{
    for (QToolButton* button : dock.findChildren<QToolButton*>()) {
        if (button->text() == title)
            return button;
    }
    return nullptr;
}

int cellCount(const DocumentSession& session)
{
    return static_cast<int>(session.currentPage()->cellCount());
}

class ScriptedPrompts final : public DocumentPrompts {
public:
    SaveChoice askSaveChanges(const QString&) override { return SaveChoice::Cancel; }
    std::optional<QString> askOpenPath(const QString&) override { return std::nullopt; }
    std::optional<QString> askSavePath(const QString&, const QString&) override { return std::nullopt; }
    void showError(const QString&, const QString& message) override { errors << message; }
    QStringList errors;
};

} // namespace

class TstPaletteUserLib : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qraw::ui::test::isolateSettings();
        QCoreApplication::setOrganizationName(QStringLiteral("QrawUserLibTest"));
        QCoreApplication::setApplicationName(QStringLiteral("QrawUserLibTest"));
    }

    void init() { QSettings().clear(); }

    void openLibraryShowsSectionAndInsertsEntry()
    {
        QTemporaryDir dir;
        const QString path = writeLibraryFile(dir, QStringLiteral("mine.xml"), oneEntryLibrary());
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        dock.resize(280, 600);
        dock.show();

        QString error;
        QVERIFY2(dock.openLibrary(path, &error), qPrintable(error));
        QCOMPARE(dock.libraryPaths(), QStringList{path});
        QVERIFY(sectionHeader(dock, QStringLiteral("mine.xml")) != nullptr);
        QCOMPARE(settings.openLibraries(), QStringList{path});

        const auto card = itemButtons(dock, QStringLiteral("MyCard"));
        QCOMPARE(card.size(), 1);
        QTRY_VERIFY(!card.first()->icon().isNull());

        const int before = cellCount(session);
        const QByteArray saved = test::savedBytes(session);
        card.first()->click();
        QCOMPARE(cellCount(session), before + 1);
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), saved);
        // Opening it again does not add a second section.
        QVERIFY(dock.openLibrary(path));
        QCOMPARE(dock.libraryPaths().size(), 1);
    }

    void addSelectionIsSavedAndReloads()
    {
        QTemporaryDir dir;
        const QString path = dir.filePath(QStringLiteral("new.xml"));
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        dock.show();

        QString error;
        QVERIFY2(dock.newLibrary(path, &error), qPrintable(error));
        QVERIFY(readLibrary(path).entries.empty());

        // Nothing selected: nothing added.
        QVERIFY(!dock.addSelectionToLibrary(path, QStringLiteral("None"), &error));
        QVERIFY(!error.isEmpty());

        const QByteArray saved = test::savedBytes(session);
        session.selection().set({"a"});
        QVERIFY2(dock.addSelectionToLibrary(path, QStringLiteral("Box A"), &error), qPrintable(error));
        QCOMPARE(test::savedBytes(session), saved); // a library change is not a document edit
        QVERIFY(!session.canUndo());

        const format::Library onDisk = readLibrary(path);
        QCOMPARE(onDisk.entries.size(), std::size_t(1));
        QCOMPARE(onDisk.entries[0].title, std::string("Box A"));
        QCOMPARE(onDisk.entries[0].w, 120.0);
        QCOMPARE(onDisk.entries[0].h, 60.0);
        QVERIFY(onDisk.entries[0].xml.find("rounded=0") != std::string::npos);
        QTRY_VERIFY(!itemButtons(dock, QStringLiteral("Box A")).isEmpty()); // shown on the next event-loop pass

        // Rename and remove are saved too.
        QVERIFY(dock.renameLibraryEntry(path, 0, QStringLiteral("Renamed")));
        QCOMPARE(readLibrary(path).entries[0].title, std::string("Renamed"));
        QVERIFY(dock.removeLibraryEntry(path, 0));
        QVERIFY(readLibrary(path).entries.empty());
        QTRY_VERIFY(itemButtons(dock, QStringLiteral("Renamed")).isEmpty());

        // Export writes a copy.
        session.selection().set({"b"});
        QVERIFY(dock.addSelectionToLibrary(path, QStringLiteral("Ellipse B")));
        const QString copy = dir.filePath(QStringLiteral("copy.xml"));
        QVERIFY(dock.exportLibrary(path, copy, &error));
        QCOMPARE(readLibrary(copy).entries.size(), std::size_t(1));
    }

    void closeRemovesSectionAndSetting()
    {
        QTemporaryDir dir;
        const QString path = writeLibraryFile(dir, QStringLiteral("mine.xml"), oneEntryLibrary());
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        {
            PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
            dock.show();
            QVERIFY(dock.openLibrary(path));
        }
        // A new dock (the next start) reopens it.
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        dock.show();
        QCOMPARE(dock.libraryPaths(), QStringList{path});
        QTRY_VERIFY(!itemButtons(dock, QStringLiteral("MyCard")).isEmpty());

        dock.closeLibrary(path);
        QVERIFY(dock.libraryPaths().isEmpty());
        QVERIFY(settings.openLibraries().isEmpty());
        QTRY_VERIFY(sectionHeader(dock, QStringLiteral("mine.xml")) == nullptr);
        QVERIFY(QFile::exists(path)); // closing never deletes the file
    }

    void missingFileIsDroppedOnStart()
    {
        QTemporaryDir dir;
        AppSettings settings;
        settings.setOpenLibraries({dir.filePath(QStringLiteral("gone.xml"))});
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        QSignalSpy status(&dock, &PaletteDock::statusMessage);
        QTRY_COMPARE(status.size(), 1);
        QVERIFY(dock.libraryPaths().isEmpty());
        QVERIFY(settings.openLibraries().isEmpty());
    }

    void notALibraryIsRefused()
    {
        QTemporaryDir dir;
        QFile bad(dir.filePath(QStringLiteral("bad.xml")));
        QVERIFY(bad.open(QIODevice::WriteOnly));
        bad.write("<mxfile/>");
        bad.close();
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        QString error;
        QVERIFY(!dock.openLibrary(bad.fileName(), &error));
        QVERIFY(!error.isEmpty());
        QVERIFY(dock.libraryPaths().isEmpty());
    }

    void openingALibraryFileKeepsTheDocument()
    {
        QTemporaryDir dir;
        const QString path = writeLibraryFile(dir, QStringLiteral("mine.xml"), oneEntryLibrary());
        DocumentSession session(test::simpleDocument());
        AppSettings settings;
        ScriptedPrompts prompts;
        DocumentController controller(session, prompts, settings);
        QSignalSpy requested(&controller, &DocumentController::libraryFileRequested);
        const QByteArray saved = test::savedBytes(session);
        QVERIFY(controller.openPath(path));
        QCOMPARE(requested.size(), 1);
        QCOMPARE(requested.first().first().toString(), path);
        QCOMPARE(test::savedBytes(session), saved);
        QVERIFY(prompts.errors.isEmpty());
        QVERIFY(FileService::load(path).isLibrary);
    }
};

QTEST_MAIN(TstPaletteUserLib)
#include "tst_palette_userlib.moc"
