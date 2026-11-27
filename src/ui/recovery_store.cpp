// Crash-recovery snapshots (docs/design/ui.md section 7).
#include "qraw/ui/file_service.h"

#include "qraw/diag/data_locations.h"
#include "qraw/format/codec.h"
#include "qraw/ui/logging.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLockFile>
#include <QSaveFile>

#include <algorithm>
#include <map>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::RecoveryStore)
};

constexpr auto kSnapshotSuffix = ".drawio";

QString pathFor(const QString& directory, const QString& id, const char* suffix)
{
    return directory + QLatin1Char('/') + id + QLatin1String(suffix);
}

bool writeSidecar(const QString& path, const QString& originalPath, const QDateTime& savedAt)
{
    const QJsonObject object{{QStringLiteral("originalPath"), originalPath},
                             {QStringLiteral("savedAt"), savedAt.toString(Qt::ISODate)}};
    QSaveFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(QJsonDocument(object).toJson()) >= 0 && file.commit();
}

// The sidecar is advisory: a missing or damaged one only loses the original
// path, the snapshot itself is still restorable.
RecoveryEntry readEntry(const QString& directory, const QString& id)
{
    RecoveryEntry entry;
    entry.id = id;
    entry.recoveryPath = pathFor(directory, id, kSnapshotSuffix);
    entry.savedAt = QFileInfo(entry.recoveryPath).lastModified();
    QFile sidecar(pathFor(directory, id, ".json"));
    if (sidecar.open(QIODevice::ReadOnly)) {
        const QJsonObject object = QJsonDocument::fromJson(sidecar.readAll()).object();
        entry.originalPath = object.value(QStringLiteral("originalPath")).toString();
        const QDateTime saved = QDateTime::fromString(object.value(QStringLiteral("savedAt")).toString(), Qt::ISODate);
        if (saved.isValid())
            entry.savedAt = saved;
    }
    return entry;
}

} // namespace

struct RecoveryStore::Impl {
    // Locks of the sessions this instance wrote; released on remove() or
    // destruction.
    std::map<QString, std::unique_ptr<QLockFile>> locks;
};

RecoveryStore::RecoveryStore(QString directory) : directory_(std::move(directory)), d(std::make_unique<Impl>()) {}

RecoveryStore::~RecoveryStore() = default;

QString RecoveryStore::defaultDirectory()
{
    return diag::recoveryDirectory();
}

bool RecoveryStore::write(const QString& id, const core::Document& document, const QString& originalPath,
                          QString* error)
{
    auto fail = [&](const QString& message) {
        if (error != nullptr)
            *error = message;
        return false;
    };
    if (!QDir().mkpath(directory_)) {
        qCWarning(lcUiSession) << "cannot create the recovery folder" << directory_;
        return fail(Text::tr("The recovery folder %1 could not be created.").arg(directory_));
    }

    auto& lock = d->locks[id];
    if (!lock) {
        auto created = std::make_unique<QLockFile>(pathFor(directory_, id, ".lock"));
        created->setStaleLockTime(0); // staleness is decided by the owner process being gone
        if (!created->tryLock(0)) {
            d->locks.erase(id);
            return fail(Text::tr("The recovery snapshot %1 is in use by another process.").arg(id));
        }
        lock = std::move(created);
    }

    format::WriteOptions options;
    options.pageEncoding = format::PageEncodingPolicy::Uncompressed;
    QString writeError;
    if (!format::writeFile(document, pathFor(directory_, id, kSnapshotSuffix), options, &writeError))
        return fail(writeError);
    if (!writeSidecar(pathFor(directory_, id, ".json"), originalPath, QDateTime::currentDateTime()))
        return fail(Text::tr("The recovery information could not be written."));
    return true;
}

void RecoveryStore::remove(const QString& id)
{
    QFile::remove(pathFor(directory_, id, kSnapshotSuffix));
    QFile::remove(pathFor(directory_, id, ".json"));
    d->locks.erase(id); // unlocks and deletes the lock file
}

std::vector<RecoveryEntry> RecoveryStore::orphans() const
{
    std::vector<RecoveryEntry> found;
    const QFileInfoList snapshots =
        QDir(directory_).entryInfoList({QStringLiteral("*.drawio")}, QDir::Files, QDir::Name);
    for (const QFileInfo& info : snapshots) {
        const QString id = info.completeBaseName();
        if (d->locks.count(id) != 0)
            continue;
        // A live lock means another running session owns the snapshot; a
        // lock whose process is gone is detected as stale and taken over.
        QLockFile lock(pathFor(directory_, id, ".lock"));
        lock.setStaleLockTime(0);
        if (!lock.tryLock(0))
            continue;
        found.push_back(readEntry(directory_, id));
    }
    std::sort(found.begin(), found.end(),
              [](const RecoveryEntry& a, const RecoveryEntry& b) { return a.savedAt > b.savedAt; });
    return found;
}

void RecoveryStore::discard(const RecoveryEntry& entry)
{
    QFile::remove(pathFor(directory_, entry.id, kSnapshotSuffix));
    QFile::remove(pathFor(directory_, entry.id, ".json"));
    QFile::remove(pathFor(directory_, entry.id, ".lock"));
}

} // namespace qraw::ui
