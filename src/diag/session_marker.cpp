#include "qraw/diag/session_marker.h"

#include "qraw/diag/crash_handler.h"
#include "qraw/diag/logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <algorithm>

namespace qraw::diag {

SessionMarker::SessionMarker(QString directory) : directory_(std::move(directory)) {}

SessionMarker::~SessionMarker()
{
    end();
}

QString SessionMarker::path() const
{
    return directory_ + QStringLiteral("/%1.lock").arg(QCoreApplication::applicationPid());
}

bool SessionMarker::begin()
{
    if (lock_ != nullptr)
        return true;
    if (!QDir().mkpath(directory_))
        return false;
    auto lock = std::make_unique<QLockFile>(path());
    lock->setStaleLockTime(0); // staleness is decided by the owner process being gone, never by age
    if (!lock->tryLock(0)) {
        qCWarning(lcAppSession) << "cannot create the session marker" << path() << "error" << lock->error();
        return false;
    }
    lock_ = std::move(lock);
    return true;
}

void SessionMarker::end()
{
    lock_.reset(); // unlocks and deletes the file
}

namespace {

// Whether `path` is a session marker whose process is gone. Takes the lock and releases it, which
// removes the file; a live owner makes tryLock() fail and the marker stays.
bool consumeIfStale(const QString& path, qint64 ownProcessId)
{
    QLockFile lock(path);
    lock.setStaleLockTime(0);
    qint64 owner = 0;
    QString host;
    QString application;
    // This process has not created its marker yet, so a marker with its id is a leftover whose id the
    // system handed out again (QLockFile would see a live process and keep it).
    if (lock.getLockInfo(&owner, &host, &application) && owner == ownProcessId) {
        return QFile::remove(path);
    }
    if (!lock.tryLock(0))
        return false; // alive (another instance) or not ours to remove
    lock.unlock();
    return true;
}

// "Process: gui" in the first lines of the report.
bool isGuiReport(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QList<QByteArray> lines = file.read(512).split('\n');
    return std::any_of(lines.begin(), lines.end(),
                       [](const QByteArray& line) { return line.trimmed() == kCrashReportGuiLine; });
}

QFileInfoList crashReportFiles(const QString& directory)
{
    return QDir(directory).entryInfoList({QStringLiteral("crash-*.txt")}, QDir::Files, QDir::Time);
}

} // namespace

PreviousRun collectPreviousRun(const QString& sessionDirectory, const QString& crashDirectory,
                               const QDateTime& reportsSeenUntil, qint64 ownProcessId)
{
    PreviousRun previous;
    const QFileInfoList markers = QDir(sessionDirectory).entryInfoList({QStringLiteral("*.lock")}, QDir::Files);
    for (const QFileInfo& marker : markers) {
        if (consumeIfStale(marker.absoluteFilePath(), ownProcessId))
            ++previous.staleSessions;
    }
    for (const QFileInfo& report : crashReportFiles(crashDirectory)) { // newest first
        if ((!reportsSeenUntil.isValid() || report.lastModified() > reportsSeenUntil) && isGuiReport(report.absoluteFilePath()))
            previous.crashReports.push_back(report.absoluteFilePath());
    }
    if (previous.endedUnexpectedly()) {
        qCWarning(lcAppSession) << "the previous session ended unexpectedly:" << previous.staleSessions
                                << "stale session marker(s)," << previous.crashReports.size() << "new crash report(s)";
    }
    return previous;
}

QDateTime newestCrashReportTime(const QString& crashDirectory)
{
    const QFileInfoList reports = crashReportFiles(crashDirectory);
    return reports.isEmpty() ? QDateTime() : reports.first().lastModified();
}

} // namespace qraw::diag
