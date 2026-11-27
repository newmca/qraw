#pragma once
// A user shape library: one .xml <mxlibrary> file shown as a palette section
// (EditorUi.loadLibrary / closeLibrary, js/diagramly/EditorUi.js). Library
// changes are file operations, not document edits: every change is written
// to the file at once (atomically) and never touches the undo history.
// Private to the palette package. QtCore only.

#include "qraw/format/library.h"
#include "qraw/ui/palette_library.h"

#include <QString>

#include <memory>
#include <optional>
#include <string>

namespace qraw::ui {

class UserLibrary {
public:
    // Reads `path`. nullopt with a translated `error` when the file cannot be
    // read or is not a library.
    [[nodiscard]] static std::optional<UserLibrary> open(const QString& path, QString* error = nullptr);
    // Writes an empty library to `path` and opens it.
    [[nodiscard]] static std::optional<UserLibrary> create(const QString& path, QString* error = nullptr);

    [[nodiscard]] const QString& path() const { return path_; }
    // The file name, shown as the section title.
    [[nodiscard]] QString title() const;
    // "user:<path>": the palette id (and the key of the section's expanded state).
    [[nodiscard]] QString paletteId() const { return paletteIdFor(path_); }
    [[nodiscard]] static QString paletteIdFor(const QString& path) { return QStringLiteral("user:") + path; }
    [[nodiscard]] const Palette& palette() const { return *palette_; }
    // Shared so that a button built from an older state stays valid while the
    // library changes underneath it.
    [[nodiscard]] const std::shared_ptr<const Palette>& palettePtr() const { return palette_; }
    [[nodiscard]] const format::Library& library() const { return library_; }

    // Each change is saved first; on failure the library is left as it was
    // and false is returned with a translated `error`.
    bool addCells(const std::string& cellsXml, double w, double h, const QString& title, QString* error = nullptr);
    bool renameEntry(std::size_t index, const QString& title, QString* error = nullptr);
    bool removeEntry(std::size_t index, QString* error = nullptr);

private:
    UserLibrary(QString path, format::Library library);
    bool commit(format::Library next, QString* error);

    QString path_;
    format::Library library_;
    std::shared_ptr<const Palette> palette_;
};

} // namespace qraw::ui
