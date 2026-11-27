#pragma once
// File-level document lifecycle: new, open, save, save as, revert, recent
// files, autosave and crash recovery.
// (src/ui/document_controller.cpp). All user interaction goes through
// DocumentPrompts, so tests drive it without dialogs; all file I/O goes
// through FileService / RecoveryStore.

#include "qraw/ui/file_service.h"

#include <QObject>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>
#include <vector>

class QTimer;

namespace qraw::ui {

class AppSettings;
class DocumentSession;

// The questions the controller asks. MainWindow implements them with
// QMessageBox / QFileDialog; tests use a scripted fake.
class DocumentPrompts {
public:
    enum class SaveChoice { Save, Discard, Cancel };

    virtual ~DocumentPrompts() = default;
    // "Save changes to <name>?" before closing/replacing a modified document.
    virtual SaveChoice askSaveChanges(const QString& documentName) = 0;
    // An existing file to open; nullopt = cancelled.
    virtual std::optional<QString> askOpenPath(const QString& filter) = 0;
    // A path to save to; nullopt = cancelled. `suggested` is the current
    // path or "<displayName>.drawio" in the last used directory.
    virtual std::optional<QString> askSavePath(const QString& suggested, const QString& filter) = 0;
    // Error presentation (already translated).
    virtual void showError(const QString& title, const QString& message) = 0;
    // Warnings from loading (diagnostics); default ignores them.
    virtual void showWarnings(const QString& title, const QStringList& messages)
    {
        (void)title;
        (void)messages;
    }
};

// Drives one DocumentSession. Window title, recent-file menus and the
// welcome dialog observe its signals.
class DocumentController : public QObject {
    Q_OBJECT

public:
    // `store` may be nullptr (no autosave/recovery, as in most tests).
    DocumentController(DocumentSession& session, DocumentPrompts& prompts, AppSettings& settings,
                       RecoveryStore* store = nullptr, QObject* parent = nullptr);
    ~DocumentController() override;

    [[nodiscard]] DocumentSession& session() const { return session_; }
    [[nodiscard]] const QString& filePath() const { return filePath_; } // "" when untitled
    [[nodiscard]] FileKind fileKind() const { return kind_; }
    // File name without directory, or tr("Untitled Diagram") (draw.io's
    // default name).
    [[nodiscard]] QString displayName() const;
    // A unique id of this editing session (recovery file stem).
    [[nodiscard]] const QString& sessionId() const { return sessionId_; }
    // The document has unsaved changes: DocumentSession::isModified(), or
    // it was restored from a recovery snapshot and not saved since (the
    // restore itself leaves the undo history clean). The window title and
    // maybeSave() use this, not the session's flag.
    [[nodiscard]] bool isModified() const;

    // Each returns false when cancelled or failed (the error was shown).
    // Every operation that replaces the document first calls maybeSave().
    bool newDocument();                         // FileService::blankDocument()
    bool newFromTemplate(const QString& path);  // a template file (Qt resource or disk); stays untitled
    bool open();                                // asks for a path
    bool openPath(const QString& path);         // also from the command line and Open Recent; an <mxlibrary> file
                                                // emits libraryFileRequested() instead and leaves the document
    bool save();                                // Save As when untitled or kind cannot be saved
    bool saveAs();
    bool revert();                              // reload filePath() without asking (Actions 'revert')
    // Before closing: asks to save a modified document. True = may close.
    bool maybeSave();

    // --- autosave / recovery ---------------------------------------------
    // Writes a recovery snapshot every `seconds` while the document is
    // modified (0 = off). The snapshot is removed on a clean save, on revert,
    // on new/open and on close.
    void setAutosaveInterval(int seconds);
    void autosaveNow(); // writes a snapshot if modified (and a store exists)
    [[nodiscard]] std::vector<RecoveryEntry> pendingRecoveries() const;
    // Loads the snapshot as a modified document (isModified() true until
    // saved; title shows the original file name; filePath() = originalPath
    // or "") and discards the orphan.
    bool restoreRecovery(const RecoveryEntry& entry);
    void discardRecovery(const RecoveryEntry& entry);
    // Removes this session's snapshot (call on normal close).
    void closeSession();

signals:
    void filePathChanged(const QString& path);
    // displayName() or isModified() changed (MainWindow sets
    // "<name>[*] - Qraw" and setWindowModified(isModified())).
    void titleChanged();
    // A document was loaded from `path` (also restore); recent files updated.
    void documentOpened(const QString& path);
    void recentFilesChanged();
    // openPath() was given a shape library file (the main window opens it in the palette).
    void libraryFileRequested(const QString& path);

private:
    bool replaceDocument(const LoadedFile& file, const QString& path);
    bool saveTo(const QString& path, FileKind kind);

    DocumentSession& session_;
    DocumentPrompts& prompts_;
    AppSettings& settings_;
    RecoveryStore* store_;
    QString filePath_;
    FileKind kind_ = FileKind::Drawio;
    QString sessionId_;
    QTimer* autosaveTimer_ = nullptr;
    bool restored_ = false; // restored from recovery, not saved since
};

} // namespace qraw::ui
