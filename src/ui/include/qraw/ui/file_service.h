#pragma once
// File I/O for the editor: loading, saving, crash-recovery files
// (src/ui/file_service.cpp, src/ui/recovery_store.cpp). No widgets: everything here is callable from tests and never shows a dialog.

#include "qraw/core/diagnostic.h"
#include "qraw/core/document.h"

#include <QDateTime>
#include <QString>

#include <memory>
#include <optional>
#include <vector>

namespace qraw::ui {

// How a file stores the diagram, which decides how Save writes it back.
enum class FileKind {
    Drawio,    // .drawio: <mxfile>, pages re-encoded as read (Preserve)
    Xml,       // .xml: the same bytes as Drawio (a bare <mxGraphModel> file stays bare)
    DrawioPng, // .png with an embedded diagram (Save re-exports page 1 with embed)
    DrawioSvg, // .svg with an embedded diagram (Save re-exports page 1 with embed)
    Pdf,       // .pdf with an embedded diagram: opened read-only, Save asks for a new .drawio path
};

struct LoadedFile {
    std::optional<core::Document> document; // empty on failure
    FileKind kind = FileKind::Drawio;
    std::vector<core::Diagnostic> diagnostics;
    QString error; // translated, user-presentable, set when !document
    bool isLibrary = false; // the file is an <mxlibrary> shape library, not a diagram (D-008)
    [[nodiscard]] bool ok() const { return document.has_value(); }
};

class FileService {
public:
    // format::readAnyFile: .drawio/.xml, or PNG/SVG/PDF with an embedded
    // diagram (format::sniffContainer decides, not the extension). A PNG/SVG
    // without a diagram fails with a clear error ("... contains no diagram").
    // The kind comes from the container.
    [[nodiscard]] static LoadedFile load(const QString& path);

    // Whether `path` is an <mxlibrary> file (looks at the first bytes only).
    [[nodiscard]] static bool isLibraryFile(const QString& path);

    // Writes `document` to `path` atomically (QSaveFile):
    //  * Drawio / Xml: format::writeFile, PageEncodingPolicy::Preserve.
    //  * DrawioPng / DrawioSvg: render::exportPage(page 0, embed = true,
    //    border 10, scale 1) like draw.io's .drawio.png/.drawio.svg save.
    //  * Pdf: refused (false, error set): callers save as .drawio instead.
    // Returns false and fills `error` (translated) on failure.
    static bool save(const core::Document& document, const QString& path, FileKind kind, QString* error = nullptr);

    // The kind Save As uses for a chosen path: *.png -> DrawioPng, *.svg ->
    // DrawioSvg, *.xml -> Xml, anything else -> Drawio (".drawio" appended
    // when the name has no extension).
    [[nodiscard]] static FileKind kindForPath(const QString& path);
    [[nodiscard]] static bool canSave(FileKind kind) { return kind != FileKind::Pdf; }

    // Name filters for QFileDialog (translated).
    [[nodiscard]] static QString openFilter();
    [[nodiscard]] static QString saveFilter();

    // A new diagram as draw.io creates it: one page "Page-1" (random page
    // id, commands::ops::randomPageId) with the default mxGraphModel
    // attributes (dx 1000, dy 800, grid 1, gridSize 10, guides 1, tooltips 1,
    // connect 1, arrows 1, fold 1, page 1, pageScale 1, pageWidth 827,
    // pageHeight 1169, math 0, shadow 0), root cell "0" and layer "1".
    // Written uncompressed (draw.io's current default).
    [[nodiscard]] static core::Document blankDocument();
};

// One crash-recovery snapshot found on disk.
struct RecoveryEntry {
    QString id;           // file stem, unique per editing session
    QString recoveryPath; // the .drawio snapshot
    QString originalPath; // the document's file, "" for an unsaved document
    QDateTime savedAt;
};

// Recovery snapshots under a directory (default diag::recoveryDirectory(), which is
// QStandardPaths::AppLocalDataLocation + "/recovery" in the GUI). Each running session
// holds a QLockFile "<id>.lock" next to its snapshot "<id>.drawio" and the
// sidecar "<id>.json" ({"originalPath": ..., "savedAt": ISO 8601}). A
// snapshot whose lock is not held by a live process is an orphan: the app
// crashed and offers it on the next start.
class RecoveryStore {
public:
    explicit RecoveryStore(QString directory = defaultDirectory());
    ~RecoveryStore();

    RecoveryStore(const RecoveryStore&) = delete;
    RecoveryStore& operator=(const RecoveryStore&) = delete;

    [[nodiscard]] static QString defaultDirectory();
    [[nodiscard]] const QString& directory() const { return directory_; }

    // Writes (atomically, uncompressed) the snapshot of the session `id`,
    // taking its lock on first use. Returns false and fills `error`.
    bool write(const QString& id, const core::Document& document, const QString& originalPath,
               QString* error = nullptr);
    // Deletes the session's snapshot, sidecar and lock (clean save, close).
    void remove(const QString& id);
    // Orphaned snapshots (no live lock), newest first.
    [[nodiscard]] std::vector<RecoveryEntry> orphans() const;
    // Deletes an orphan after it was restored or discarded.
    void discard(const RecoveryEntry& entry);

private:
    struct Impl;
    QString directory_;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
