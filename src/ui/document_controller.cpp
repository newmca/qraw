// File-level document lifecycle (docs/design/ui.md section 7).
#include "qraw/ui/document_controller.h"

#include "qraw/ui/app_settings.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/logging.h"

#include <QDir>
#include <QFileInfo>
#include <QTimer>
#include <QUuid>

namespace qraw::ui {

namespace {

// The timer interval is the autosave setting (0 = off): it runs while the
// document is modified and restarts on every change of that state.
void syncAutosaveTimer(QTimer& timer, bool modified)
{
    if (modified && timer.interval() > 0)
        timer.start();
    else
        timer.stop();
}

QStringList warningMessages(const std::vector<core::Diagnostic>& diagnostics)
{
    QStringList messages;
    for (const core::Diagnostic& d : diagnostics) {
        if (d.severity == core::Diagnostic::Severity::Warning)
            messages.push_back(QString::fromStdString(d.message));
    }
    return messages;
}

} // namespace

DocumentController::DocumentController(DocumentSession& session, DocumentPrompts& prompts, AppSettings& settings,
                                       RecoveryStore* store, QObject* parent)
    : QObject(parent), session_(session), prompts_(prompts), settings_(settings), store_(store),
      sessionId_(QUuid::createUuid().toString(QUuid::WithoutBraces)), autosaveTimer_(new QTimer(this))
{
    autosaveTimer_->setInterval(settings_.autosaveIntervalSeconds() * 1000);
    connect(autosaveTimer_, &QTimer::timeout, this, &DocumentController::autosaveNow);
    connect(&session_, &DocumentSession::modifiedChanged, this, [this](bool modified) {
        syncAutosaveTimer(*autosaveTimer_, modified || restored_);
        emit titleChanged();
    });
}

DocumentController::~DocumentController() = default;

bool DocumentController::isModified() const
{
    return session_.isModified() || restored_;
}

QString DocumentController::displayName() const
{
    return filePath_.isEmpty() ? tr("Untitled Diagram") : QFileInfo(filePath_).fileName();
}

// --- new / open ---------------------------------------------------------------

bool DocumentController::newDocument()
{
    if (!maybeSave())
        return false;
    LoadedFile blank;
    blank.document = FileService::blankDocument();
    return replaceDocument(blank, QString());
}

bool DocumentController::newFromTemplate(const QString& path)
{
    if (!maybeSave())
        return false;
    LoadedFile file = FileService::load(path);
    if (!file.ok()) {
        prompts_.showError(tr("New from Template"), file.error);
        return false;
    }
    file.kind = FileKind::Drawio; // a template never decides how the new document is saved
    return replaceDocument(file, QString());
}

bool DocumentController::open()
{
    const std::optional<QString> path = prompts_.askOpenPath(FileService::openFilter());
    return path && openPath(*path);
}

bool DocumentController::openPath(const QString& path)
{
    const QString absolute = QFileInfo(path).absoluteFilePath();
    // A shape library is not a diagram: hand it to the palette and leave the
    // document alone (D-008).
    if (FileService::isLibraryFile(absolute)) {
        emit libraryFileRequested(absolute);
        return true;
    }
    if (!maybeSave())
        return false;
    const LoadedFile file = FileService::load(absolute);
    if (!file.ok()) {
        // draw.io forgets a recent file that no longer opens.
        if (settings_.recentFiles().contains(absolute)) {
            settings_.removeRecentFile(absolute);
            emit recentFilesChanged();
        }
        prompts_.showError(tr("Open"), file.error);
        return false;
    }
    return replaceDocument(file, absolute);
}

bool DocumentController::revert()
{
    if (filePath_.isEmpty())
        return false;
    const LoadedFile file = FileService::load(filePath_);
    if (!file.ok()) {
        prompts_.showError(tr("Revert"), file.error);
        return false;
    }
    return replaceDocument(file, filePath_);
}

// Installs a loaded (or blank) document. `path` is "" for an untitled one.
bool DocumentController::replaceDocument(const LoadedFile& file, const QString& path)
{
    const QStringList warnings = warningMessages(file.diagnostics);
    session_.resetDocument(*file.document);
    session_.setBaseDirectory(path.isEmpty() ? QString() : QFileInfo(path).absolutePath());
    const bool pathChanged = path != filePath_;
    filePath_ = path;
    kind_ = file.kind;
    restored_ = false;
    syncAutosaveTimer(*autosaveTimer_, session_.isModified());
    if (store_ != nullptr)
        store_->remove(sessionId_);

    if (!path.isEmpty()) {
        settings_.addRecentFile(path);
        emit recentFilesChanged();
    }
    if (pathChanged)
        emit filePathChanged(filePath_);
    emit titleChanged();
    if (!path.isEmpty())
        emit documentOpened(path);
    if (!warnings.isEmpty())
        prompts_.showWarnings(tr("Open"), warnings);
    return true;
}

// --- save ---------------------------------------------------------------------

bool DocumentController::save()
{
    if (filePath_.isEmpty() || !FileService::canSave(kind_))
        return saveAs();
    return saveTo(filePath_, kind_);
}

bool DocumentController::saveAs()
{
    QString suggested;
    if (!filePath_.isEmpty() && FileService::canSave(kind_)) {
        suggested = filePath_;
    } else {
        const QStringList recent = settings_.recentFiles();
        const QString directory = recent.isEmpty() ? QDir::homePath() : QFileInfo(recent.first()).absolutePath();
        const QString base = filePath_.isEmpty() ? displayName() : QFileInfo(filePath_).completeBaseName();
        suggested = directory + QLatin1Char('/') + base + QStringLiteral(".drawio");
    }
    const std::optional<QString> chosen = prompts_.askSavePath(suggested, FileService::saveFilter());
    if (!chosen || chosen->isEmpty())
        return false;
    QString path = *chosen;
    if (QFileInfo(path).suffix().isEmpty())
        path += QStringLiteral(".drawio");
    return saveTo(path, FileService::kindForPath(path));
}

bool DocumentController::saveTo(const QString& path, FileKind kind)
{
    session_.flushPendingEdits();
    QString error;
    if (!FileService::save(session_.document(), path, kind, &error)) {
        prompts_.showError(tr("Save"), error);
        return false;
    }
    session_.markSaved();
    const QString absolute = QFileInfo(path).absoluteFilePath();
    const bool pathChanged = absolute != filePath_;
    filePath_ = absolute;
    kind_ = kind;
    restored_ = false;
    syncAutosaveTimer(*autosaveTimer_, false);
    if (store_ != nullptr)
        store_->remove(sessionId_);
    session_.setBaseDirectory(QFileInfo(absolute).absolutePath());
    settings_.addRecentFile(absolute);
    emit recentFilesChanged();
    if (pathChanged)
        emit filePathChanged(filePath_);
    emit titleChanged();
    return true;
}

bool DocumentController::maybeSave()
{
    session_.flushPendingEdits();
    if (!isModified())
        return true;
    switch (prompts_.askSaveChanges(displayName())) {
    case DocumentPrompts::SaveChoice::Save:
        return save();
    case DocumentPrompts::SaveChoice::Discard:
        return true;
    case DocumentPrompts::SaveChoice::Cancel:
        break;
    }
    return false;
}

// --- autosave / recovery ----------------------------------------------------------

void DocumentController::setAutosaveInterval(int seconds)
{
    autosaveTimer_->setInterval(std::max(0, seconds) * 1000);
    syncAutosaveTimer(*autosaveTimer_, isModified());
}

void DocumentController::autosaveNow()
{
    if (store_ == nullptr || !isModified())
        return;
    QString error;
    if (store_->write(sessionId_, session_.document(), filePath_, &error)) {
        qCDebug(lcUiSession) << "autosaved" << (filePath_.isEmpty() ? QStringLiteral("(untitled)") : filePath_);
    } else {
        qCWarning(lcUiSession) << "autosave failed:" << error;
        emit session_.statusMessage(tr("Autosave failed: %1").arg(error));
    }
}

std::vector<RecoveryEntry> DocumentController::pendingRecoveries() const
{
    return store_ != nullptr ? store_->orphans() : std::vector<RecoveryEntry>{};
}

bool DocumentController::restoreRecovery(const RecoveryEntry& entry)
{
    LoadedFile file = FileService::load(entry.recoveryPath);
    if (!file.ok()) {
        prompts_.showError(tr("Restore"), file.error);
        return false;
    }
    qCInfo(lcUiSession).noquote() << QStringLiteral("recovering %1 (original: %2)")
                                         .arg(entry.recoveryPath,
                                              entry.originalPath.isEmpty() ? QStringLiteral("untitled") : entry.originalPath);
    session_.resetDocument(*file.document);
    session_.setBaseDirectory(entry.originalPath.isEmpty() ? QString() : QFileInfo(entry.originalPath).absolutePath());
    const bool pathChanged = entry.originalPath != filePath_;
    filePath_ = entry.originalPath;
    kind_ = filePath_.isEmpty() ? FileKind::Drawio : FileService::kindForPath(filePath_);
    restored_ = true;
    if (store_ != nullptr)
        store_->discard(entry);
    syncAutosaveTimer(*autosaveTimer_, true);
    autosaveNow(); // the restored work is protected again under this session's id
    if (pathChanged)
        emit filePathChanged(filePath_);
    emit titleChanged();
    emit documentOpened(filePath_);
    return true;
}

void DocumentController::discardRecovery(const RecoveryEntry& entry)
{
    qCInfo(lcUiSession) << "discarding the autosaved copy" << entry.recoveryPath;
    if (store_ != nullptr)
        store_->discard(entry);
}

void DocumentController::closeSession()
{
    autosaveTimer_->stop();
    if (store_ != nullptr)
        store_->remove(sessionId_);
}

} // namespace qraw::ui
