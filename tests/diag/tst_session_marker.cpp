// Session markers and crash reports of the last run (diag/session_marker.h).
#include "qraw/diag/crash_handler.h"
#include "qraw/diag/session_marker.h"

#include <QProcess>
#include <QtTest>

using namespace qraw::diag;

namespace {

void touch(const QString& path, const QByteArray& content, const QDateTime& modified)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QFile file(path);
    QVERIFY(file.open(QIODevice::WriteOnly));
    file.write(content);
    file.close();
    QVERIFY(file.open(QIODevice::ReadWrite));
    QVERIFY(file.setFileTime(modified, QFileDevice::FileModificationTime));
}

QByteArray report(const char* process)
{
    return QByteArray(kCrashReportTitle) + "\n" + process + "\nVersion: 0.1.0\n";
}

// The id of a process that has ended.
qint64 deadProcessId()
{
    QProcess child;
    child.start(QCoreApplication::applicationFilePath(), {QStringLiteral("-help")});
    if (!child.waitForStarted(20000))
        return 0;
    const qint64 pid = child.processId();
    child.waitForFinished(20000);
    return pid;
}

// A lock file as QLockFile writes it, naming `pid` as the owner.
void writeLockOf(const QString& path, qint64 pid)
{
    QDir().mkpath(QFileInfo(path).absolutePath());
    QLockFile model(path + QStringLiteral(".model"));
    QVERIFY(model.tryLock(0));
    QFile modelFile(model.fileName());
    QVERIFY(modelFile.open(QIODevice::ReadOnly));
    QList<QByteArray> parts = modelFile.readAll().split('\n');
    modelFile.close();
    parts[0] = QByteArray::number(pid);
    QFile out(path);
    QVERIFY(out.open(QIODevice::WriteOnly));
    out.write(parts.join('\n'));
}

} // namespace

class TestSessionMarker : public QObject {
    Q_OBJECT

private slots:
    void beginCreatesTheMarkerAndEndRemovesIt()
    {
        QTemporaryDir dir;
        const QString folder = dir.filePath("sessions");
        {
            SessionMarker marker(folder);
            QVERIFY(marker.begin());
            QVERIFY(marker.isActive());
            QCOMPARE(marker.path(), QStringLiteral("%1/%2.lock").arg(folder).arg(QCoreApplication::applicationPid()));
            QVERIFY(QFileInfo::exists(marker.path()));
            marker.end();
            QVERIFY(!QFileInfo::exists(marker.path()));
            QVERIFY(!marker.isActive());
        }
        {
            SessionMarker marker(folder);
            QVERIFY(marker.begin());
        } // the destructor is the clean exit too
        QVERIFY(QDir(folder).entryList(QDir::Files).isEmpty());
    }

    void aCleanExitLeavesNothingBehind()
    {
        QTemporaryDir dir;
        {
            SessionMarker marker(dir.filePath("sessions"));
            QVERIFY(marker.begin());
        }
        const PreviousRun previous = collectPreviousRun(dir.filePath("sessions"), dir.filePath("crashes"), QDateTime());
        QVERIFY(!previous.endedUnexpectedly());
        QCOMPARE(previous.staleSessions, 0);
    }

    void aDeadSessionIsStaleAndReportedOnce()
    {
        QTemporaryDir dir;
        const QString folder = dir.filePath("sessions");
        const qint64 dead = deadProcessId();
        QVERIFY(dead > 0);
        writeLockOf(folder + QStringLiteral("/%1.lock").arg(dead), dead);

        PreviousRun previous = collectPreviousRun(folder, dir.filePath("crashes"), QDateTime());
        QCOMPARE(previous.staleSessions, 1);
        QVERIFY(previous.endedUnexpectedly());
        QVERIFY(QDir(folder).entryList({"*.lock"}, QDir::Files).isEmpty());

        previous = collectPreviousRun(folder, dir.filePath("crashes"), QDateTime());
        QVERIFY(!previous.endedUnexpectedly()); // consumed
    }

    void aSecondRunningInstanceIsNotMistakenForACrash()
    {
        QTemporaryDir dir;
        const QString folder = dir.filePath("sessions");
        SessionMarker other(folder); // alive: this very process stands in for "another instance"
        QVERIFY(other.begin());
        const qint64 someoneElse = QCoreApplication::applicationPid() + 1;
        const PreviousRun previous = collectPreviousRun(folder, dir.filePath("crashes"), QDateTime(), someoneElse);
        QVERIFY(!previous.endedUnexpectedly());
        QVERIFY(QFileInfo::exists(other.path())); // and its marker stays
    }

    void aReusedProcessIdIsAStaleMarker()
    {
        QTemporaryDir dir;
        const QString folder = dir.filePath("sessions");
        const qint64 mine = QCoreApplication::applicationPid();
        writeLockOf(folder + QStringLiteral("/%1.lock").arg(mine), mine); // a crashed run had this id
        const PreviousRun previous = collectPreviousRun(folder, dir.filePath("crashes"), QDateTime());
        QCOMPARE(previous.staleSessions, 1);
        SessionMarker marker(folder);
        QVERIFY(marker.begin()); // and the id is free again
    }

    void findsNewGuiCrashReportsOnly()
    {
        QTemporaryDir dir;
        const QString crashes = dir.filePath("crashes");
        const QDateTime now = QDateTime::currentDateTime();
        touch(crashes + "/crash-20261126T100000Z-1.txt", report("Process: gui"), now.addSecs(-300));
        touch(crashes + "/crash-20261126T110000Z-2.txt", report("Process: gui"), now.addSecs(-100));
        touch(crashes + "/crash-20261126T120000Z-3.txt", report("Process: cli"), now.addSecs(-50));
        touch(crashes + "/notes.txt", report("Process: gui"), now);
        touch(crashes + "/crash-20261126T130000Z-4.dmp", "binary", now);

        PreviousRun previous = collectPreviousRun(dir.filePath("sessions"), crashes, QDateTime());
        QCOMPARE(previous.crashReports.size(), 2); // the command-line crash does not count
        QVERIFY(previous.crashReports[0].endsWith("-2.txt")); // newest first
        QVERIFY(previous.endedUnexpectedly());

        previous = collectPreviousRun(dir.filePath("sessions"), crashes, now.addSecs(-200));
        QCOMPARE(previous.crashReports.size(), 1);
        previous = collectPreviousRun(dir.filePath("sessions"), crashes, now.addSecs(-100)); // seen up to and including this one
        QVERIFY(!previous.endedUnexpectedly());

        QCOMPARE(newestCrashReportTime(crashes).toSecsSinceEpoch(), now.addSecs(-50).toSecsSinceEpoch()); // the command-line report is the newest file
        QVERIFY(!newestCrashReportTime(dir.filePath("none")).isValid());
    }

    void decidesWhetherToOfferRecovery()
    {
        PreviousRun clean;
        QVERIFY(!shouldOfferRecovery(clean, 0));
        QVERIFY(shouldOfferRecovery(clean, 1)); // an autosaved copy nobody restored
        PreviousRun stale;
        stale.staleSessions = 1;
        QVERIFY(shouldOfferRecovery(stale, 0)); // killed or powered off: say so, even with nothing to restore
        PreviousRun reported;
        reported.crashReports = {QStringLiteral("a")};
        QVERIFY(shouldOfferRecovery(reported, 3));
    }
};

QTEST_MAIN(TestSessionMarker)
#include "tst_session_marker.moc"
