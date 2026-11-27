// The parts of crash reports that need no crash: the header text and the debug switch.
#include "qraw/diag/build_info.h"
#include "qraw/diag/crash_handler.h"
#include "qraw/diag/data_locations.h"
#include "qraw/diag/debug_crash.h"

#include <QtTest>

using namespace qraw::diag;

class TestCrashReport : public QObject {
    Q_OBJECT

private slots:
    void headerNamesTheBuildAndTheMachine()
    {
        const QStringList lines = crashReportHeader({QStringLiteral("/tmp"), ProcessKind::Gui, QStringLiteral("xcb")}).split(u'\n');
        QCOMPARE(lines[0], QString::fromLatin1(kCrashReportTitle));
        QCOMPARE(lines[1], QString::fromLatin1(kCrashReportGuiLine));
        QVERIFY(lines.contains(QStringLiteral("Version: %1").arg(versionString())));
        QVERIFY(lines.contains(QStringLiteral("Git commit: %1").arg(gitCommit())));
        QVERIFY(lines.contains(QStringLiteral("Qt: %1").arg(qtVersionString())));
        QVERIFY(lines.contains(QStringLiteral("System: %1").arg(systemDescription())));
        QVERIFY(lines.contains(QStringLiteral("Platform plugin: xcb")));
        QVERIFY(!gitCommit().isEmpty());
        QVERIFY(!versionString().isEmpty());
    }

    void commandLineReportsAreMarked()
    {
        const QString header = crashReportHeader({QStringLiteral("/tmp"), ProcessKind::Cli});
        QVERIFY(header.contains(QStringLiteral("\nProcess: cli\n")));
        QVERIFY(!header.contains(QStringLiteral("Platform plugin")));
    }

    void parsesTheDebugSwitch()
    {
        QCOMPARE(parseDebugCrash("segv"), std::optional<DebugCrash>(DebugCrash::Segv));
        QCOMPARE(parseDebugCrash("abort"), std::optional<DebugCrash>(DebugCrash::Abort));
        QCOMPARE(parseDebugCrash("exception"), std::optional<DebugCrash>(DebugCrash::Exception));
        QVERIFY(!parseDebugCrash("").has_value());
        QVERIFY(!parseDebugCrash("SEGV").has_value());
        QVERIFY(!parseDebugCrash("crash").has_value());
    }

    void readsTheDebugSwitchFromTheEnvironment()
    {
        qunsetenv("QRAW_DEBUG_CRASH");
        QVERIFY(!debugCrashFromEnvironment().has_value());
        qputenv("QRAW_DEBUG_CRASH", "abort");
        QCOMPARE(debugCrashFromEnvironment(), std::optional<DebugCrash>(DebugCrash::Abort));
        qputenv("QRAW_DEBUG_CRASH", "nonsense");
        QTest::ignoreMessage(QtWarningMsg, QRegularExpression("QRAW_DEBUG_CRASH"));
        QVERIFY(!debugCrashFromEnvironment().has_value());
        qunsetenv("QRAW_DEBUG_CRASH");
    }

    void dataLocationsShareOneRoot()
    {
        QStandardPaths::setTestModeEnabled(true);
        const QString root = dataRoot();
        QVERIFY(root.endsWith(QStringLiteral("/Qraw/Qraw")));
        QCOMPARE(logFilePath(), root + QStringLiteral("/logs/qraw.log"));
        QCOMPARE(crashDirectory(), root + QStringLiteral("/crashes"));
        QCOMPARE(sessionDirectory(), root + QStringLiteral("/sessions"));
        QCOMPARE(recoveryDirectory(), root + QStringLiteral("/recovery"));
        // The GUI sets organisation and application to "Qraw": the same folder as AppLocalDataLocation.
        QCoreApplication::setOrganizationName(QStringLiteral("Qraw"));
        QCoreApplication::setApplicationName(QStringLiteral("Qraw"));
        QCOMPARE(QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation), root);
        QStandardPaths::setTestModeEnabled(false);
    }
};

QTEST_MAIN(TestCrashReport)
#include "tst_crash_report.moc"
