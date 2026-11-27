#pragma once
// "Did the last session end the way it should?" (D-049)
//
// Every GUI process holds a QLockFile <sessionDirectory>/<pid>.lock from its start until it exits
// normally. A lock file whose process is gone is stale: that session crashed, was killed or lost
// power. A second instance that runs at the same time holds a live lock and is never mistaken for a
// dead one. The crash handler's reports are the second witness (a report newer than the last one the
// user has seen).

#include <QCoreApplication>
#include <QDateTime>
#include <QLockFile>
#include <QString>
#include <QStringList>

#include <memory>

namespace qraw::diag {

// The running session's marker. Destroying it (or end()) is the clean exit: the file goes away.
class SessionMarker {
public:
    explicit SessionMarker(QString directory);
    ~SessionMarker();

    SessionMarker(const SessionMarker&) = delete;
    SessionMarker& operator=(const SessionMarker&) = delete;

    // Creates <directory>/<pid>.lock. False when the folder or the lock cannot be created.
    bool begin();
    void end();
    [[nodiscard]] bool isActive() const { return lock_ != nullptr; }
    [[nodiscard]] QString path() const;

private:
    QString directory_;
    std::unique_ptr<QLockFile> lock_;
};

// What the previous run(s) left behind.
struct PreviousRun {
    int staleSessions = 0;       // session markers of processes that no longer exist
    QStringList crashReports;    // GUI crash reports newer than `seenUntil`, newest first

    [[nodiscard]] bool endedUnexpectedly() const { return staleSessions > 0 || !crashReports.isEmpty(); }
};

// Looks for stale session markers (and removes them: each is reported once) and for GUI crash
// reports whose modification time is after `reportsSeenUntil` (an invalid time: all of them). Call
// it before the own marker is created. A marker that names `ownProcessId` is stale too, because the
// caller has no marker yet: the id was handed out again after a crash.
[[nodiscard]] PreviousRun collectPreviousRun(const QString& sessionDirectory, const QString& crashDirectory,
                                             const QDateTime& reportsSeenUntil,
                                             qint64 ownProcessId = QCoreApplication::applicationPid());

// The modification time of the newest crash report (GUI or not) in the folder, invalid if none.
[[nodiscard]] QDateTime newestCrashReportTime(const QString& crashDirectory);

// The recovery question is asked when the last session ended unexpectedly or autosaved documents
// are waiting (an autosave copy that nobody restored stays an offer for the next start).
[[nodiscard]] inline bool shouldOfferRecovery(const PreviousRun& previous, int autosavedDocuments)
{
    return previous.endedUnexpectedly() || autosavedDocuments > 0;
}

} // namespace qraw::diag
