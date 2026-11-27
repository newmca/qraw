// The crash handler in the real programs: qraw (offscreen) and qraw-cli crash on purpose through
// QRAW_DEBUG_CRASH and must leave a report with the signal and a backtrace, die from the signal,
// and leave the session marker that makes the next start offer recovery.
#include "qraw/diag/crash_handler.h"
#include "qraw/diag/data_locations.h"
#include "qraw/diag/session_marker.h"

#include <QProcess>
#include <QtTest>

#if defined(__SANITIZE_ADDRESS__)
#define QRAW_UNDER_ASAN 1
#elif defined(__has_feature)
#if __has_feature(address_sanitizer)
#define QRAW_UNDER_ASAN 1
#endif
#endif

#ifdef Q_OS_UNIX
#include <sys/resource.h>
#endif

using namespace qraw::diag;

namespace {

// The environment under which Qt puts the data files below `root`, and the crash folder it then uses.
// Linux: XDG_DATA_HOME and XDG_CONFIG_HOME; macOS: HOME. Windows ignores both: the folder stays in
// the real profile, which the test must not touch.
struct Redirect {
    QProcessEnvironment environment;
    QString crashDirectory;
    QString sessionDirectory;
    bool effective = false;
};

Redirect redirectInto(const QString& root)
{
    Redirect redirect;
    const QByteArray home = (root + QStringLiteral("/home")).toLocal8Bit();
    const QByteArray data = (root + QStringLiteral("/data")).toLocal8Bit();
    const QByteArray config = (root + QStringLiteral("/config")).toLocal8Bit();
    redirect.environment = QProcessEnvironment::systemEnvironment();
    redirect.environment.insert(QStringLiteral("HOME"), QString::fromLocal8Bit(home));
    redirect.environment.insert(QStringLiteral("XDG_DATA_HOME"), QString::fromLocal8Bit(data));
    redirect.environment.insert(QStringLiteral("XDG_CONFIG_HOME"), QString::fromLocal8Bit(config));
    redirect.environment.insert(QStringLiteral("QT_QPA_PLATFORM"), QStringLiteral("offscreen"));
    redirect.environment.remove(QStringLiteral("QT_LOGGING_RULES"));

    // Ask this process where its own library would write under the same variables.
    const QByteArray oldHome = qgetenv("HOME");
    const QByteArray oldData = qgetenv("XDG_DATA_HOME");
    qputenv("HOME", home);
    qputenv("XDG_DATA_HOME", data);
    redirect.crashDirectory = crashDirectory();
    redirect.sessionDirectory = sessionDirectory();
    qputenv("HOME", oldHome);
    if (oldData.isNull())
        qunsetenv("XDG_DATA_HOME");
    else
        qputenv("XDG_DATA_HOME", oldData);
    redirect.effective = QDir::cleanPath(redirect.crashDirectory).startsWith(QDir::cleanPath(root));
    return redirect;
}

QString readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? QString::fromUtf8(file.readAll()) : QString();
}

} // namespace

class TestCrashProcess : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
#ifdef QRAW_UNDER_ASAN
        QSKIP("AddressSanitizer installs its own SIGSEGV and SIGABRT handlers and ends the process itself: "
              "the crash handler is not exercised in an asan build.");
#endif
#ifdef Q_OS_WIN
        QSKIP("The data folder of Windows (%LOCALAPPDATA%) cannot be redirected by an environment variable, and "
              "a test must not write crash reports into the profile of whoever runs it. The Windows handler is "
              "only built, not run (KNOWN_ISSUES.md).");
#endif
    }

    void crashesLeaveAReport_data()
    {
        QTest::addColumn<QString>("program");
        QTest::addColumn<QStringList>("arguments");
        QTest::addColumn<QString>("kind");
        QTest::addColumn<QString>("process");
        QTest::addColumn<QString>("signalName");
        QTest::addColumn<QString>("reasonText");
        const QString gui = QStringLiteral(QRAW_APP_EXECUTABLE);
        const QString cli = QStringLiteral(QRAW_CLI_EXECUTABLE);
        const QStringList cliArguments{QStringLiteral("--version")};
        QTest::newRow("gui segv") << gui << QStringList() << "segv" << "Process: gui" << "SIGSEGV" << "fatal signal";
        QTest::newRow("gui abort") << gui << QStringList() << "abort" << "Process: gui" << "SIGABRT" << "fatal signal";
        QTest::newRow("gui exception") << gui << QStringList() << "exception" << "Process: gui" << "SIGABRT"
                                       << "uncaught exception";
        QTest::newRow("cli segv") << cli << cliArguments << "segv" << "Process: cli" << "SIGSEGV" << "fatal signal";
    }

    void crashesLeaveAReport()
    {
        QFETCH(QString, program);
        QFETCH(QStringList, arguments);
        QFETCH(QString, kind);
        QFETCH(QString, process);
        QFETCH(QString, signalName);
        QFETCH(QString, reasonText);

        QTemporaryDir root;
        QVERIFY(root.isValid());
        const Redirect redirect = redirectInto(root.path());
        if (!redirect.effective)
            QSKIP("the data folder cannot be redirected on this platform (the report would go to the real profile)");

        QProcess child;
        child.setProcessEnvironment(redirect.environment);
        QProcessEnvironment environment = redirect.environment;
        environment.insert(QStringLiteral("QRAW_DEBUG_CRASH"), kind);
        child.setProcessEnvironment(environment);
#ifdef Q_OS_UNIX
        // No core file for a test: the handler re-raises, and the system's handler (systemd-coredump,
        // ABRT) would otherwise store one for every run.
        child.setChildProcessModifier([] {
            const rlimit none{0, 0};
            setrlimit(RLIMIT_CORE, &none);
        });
#endif
        child.start(program, arguments);
        QVERIFY2(child.waitForStarted(20000), qPrintable(child.errorString()));
        QVERIFY2(child.waitForFinished(60000), "the child did not crash within a minute");
        const QString stderrText = QString::fromLocal8Bit(child.readAllStandardError());
        QCOMPARE(child.exitStatus(), QProcess::CrashExit); // it ended with the signal, not with exit()

        const QStringList reports = QDir(redirect.crashDirectory).entryList({"crash-*.txt"}, QDir::Files);
        QCOMPARE(reports.size(), 1); // one report, also for std::terminate followed by SIGABRT
        const QString path = redirect.crashDirectory + QLatin1Char('/') + reports.first();
        QVERIFY2(QRegularExpression(QStringLiteral("^crash-\\d{8}T\\d{6}Z-\\d+\\.txt$")).match(reports.first()).hasMatch(),
                 qPrintable(reports.first()));
        const QString text = readFile(path);

        QVERIFY2(text.startsWith(QString::fromLatin1(kCrashReportTitle) + QLatin1Char('\n')), qPrintable(text));
        QVERIFY2(text.contains(process), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("Version: ")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("Git commit: ")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("Qt: ")), qPrintable(text));
        QVERIFY2(text.contains(QStringLiteral("System: ")), qPrintable(text));
        QVERIFY2(text.contains(reasonText), qPrintable(text));
        if (reasonText == QLatin1String("fatal signal"))
            QVERIFY2(text.contains(signalName), qPrintable(text));
        else
            QVERIFY2(text.contains(QStringLiteral("QRAW_DEBUG_CRASH=exception")), qPrintable(text)); // what()
        QVERIFY2(stderrText.contains(QStringLiteral("Qraw crashed")), qPrintable(stderrText));

#if __has_include(<execinfo.h>)
        const int marker = text.indexOf(QStringLiteral("Backtrace (innermost frame first):\n"));
        QVERIFY2(marker >= 0, qPrintable(text));
        const QStringList frames = text.mid(marker).split(u'\n', Qt::SkipEmptyParts).mid(1);
        QVERIFY2(frames.size() >= 3, qPrintable(text));
        for (const QString& frame : frames)
            QVERIFY2(frame.contains(QStringLiteral("0x")), qPrintable(frame)); // glibc "(+0x..) [0x..]", macOS "0x.. main + 4"
#endif

        // The session marker of the dead process is still there: the next start sees a stale session
        // and the new report, but only the GUI's own crash counts as "Qraw closed unexpectedly".
        const PreviousRun previous = collectPreviousRun(redirect.sessionDirectory, redirect.crashDirectory, QDateTime());
        if (process == QLatin1String("Process: gui")) {
            QCOMPARE(previous.staleSessions, 1);
            QCOMPARE(previous.crashReports.size(), 1);
        } else {
            QCOMPARE(previous.staleSessions, 0); // qraw-cli keeps no session marker
            QVERIFY(previous.crashReports.isEmpty());
        }
    }

    void aNormalRunLeavesNoReportAndNoMarker()
    {
        QTemporaryDir root;
        const Redirect redirect = redirectInto(root.path());
        if (!redirect.effective)
            QSKIP("the data folder cannot be redirected on this platform");
        QProcess child;
        child.setProcessEnvironment(redirect.environment);
        child.start(QStringLiteral(QRAW_CLI_EXECUTABLE), {QStringLiteral("--version")});
        QVERIFY(child.waitForFinished(60000));
        QCOMPARE(child.exitStatus(), QProcess::NormalExit);
        QVERIFY(!QDir(redirect.crashDirectory).exists() || QDir(redirect.crashDirectory).isEmpty(QDir::Files));
    }
};

QTEST_MAIN(TestCrashProcess)
#include "tst_crash_process.moc"
