// FileOpenHandler: files the OS asks the application to open (QFileOpenEvent, macOS).
#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/ui/document_controller.h"
#include "qraw/ui/file_open_handler.h"
#include "qraw/ui/main_window.h"

#include <QFileOpenEvent>
#include <QTemporaryDir>
#include <QUrl>

using namespace qraw::ui;

class TestFileOpen : public QObject {
    Q_OBJECT

private:
    static QString writeDiagram(const QTemporaryDir& dir, const QString& name)
    {
        const QString path = dir.filePath(name);
        QFile file(path);
        if (!file.open(QIODevice::WriteOnly) || file.write(test::savedBytes(DocumentSession(test::simpleDocument()))) < 0)
            return {};
        return path;
    }

    static void send(const QString& path)
    {
        QFileOpenEvent event{QUrl::fromLocalFile(path)};
        QCoreApplication::sendEvent(QCoreApplication::instance(), &event);
    }

private slots:
    void opensTheFileInTheWindow()
    {
        QTemporaryDir dir;
        const QString path = writeDiagram(dir, "a.drawio");
        QVERIFY(!path.isEmpty());
        MainWindow window;
        FileOpenHandler handler;
        qApp->installEventFilter(&handler);
        handler.setWindow(&window);

        send(path);
        QCOMPARE(window.controller().filePath(), path);
        qApp->removeEventFilter(&handler);
    }

    void queuesFilesUntilTheWindowExists()
    {
        QTemporaryDir dir;
        const QString first = writeDiagram(dir, "first.drawio");
        const QString second = writeDiagram(dir, "second.drawio");
        FileOpenHandler handler;
        qApp->installEventFilter(&handler);

        send(first);
        send(second);

        MainWindow window;
        QVERIFY(window.controller().filePath().isEmpty());
        handler.setWindow(&window); // flushes: one document per window, the first path wins
        QCOMPARE(window.controller().filePath(), first);
        QVERIFY(handler.takePendingFiles().isEmpty());
        qApp->removeEventFilter(&handler);
    }

    void startupTakesTheQueuedFiles()
    {
        QTemporaryDir dir;
        const QString path = writeDiagram(dir, "a.drawio");
        FileOpenHandler handler;
        qApp->installEventFilter(&handler);
        send(path);
        QCOMPARE(handler.takePendingFiles(), QStringList{path});
        QVERIFY(handler.takePendingFiles().isEmpty());
        qApp->removeEventFilter(&handler);
    }

    void ignoresUrlsThatAreNotLocalFiles()
    {
        FileOpenHandler handler;
        qApp->installEventFilter(&handler);
        QFileOpenEvent event{QUrl(QStringLiteral("https://example.org/a.drawio"))};
        QCoreApplication::sendEvent(QCoreApplication::instance(), &event);
        QVERIFY(handler.takePendingFiles().isEmpty());
        qApp->removeEventFilter(&handler);
    }
};

SHELL_TEST_MAIN(TestFileOpen)
#include "tst_file_open.moc"
