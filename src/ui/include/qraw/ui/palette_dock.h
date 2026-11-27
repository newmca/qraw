#pragma once
// The shape palette (left dock), draw.io's Sidebar.
// (src/ui/palette_dock.cpp, src/ui/palette_*.cpp).

#include "qraw/ui/canvas_view.h"

#include <QWidget>

#include <memory>

class QLineEdit;

namespace qraw::ui {

class AppSettings;
class CanvasView;
class DocumentSession;
class PaletteLibrary;

// Layout, top to bottom: a search field; "Favorites" and "Recently Used"
// sections (hidden when empty); one collapsible section per palette
// (header button with the title; expanded state persisted in
// AppSettings::expandedPalettes) holding a flow grid of 32x32 px
// thumbnails (draw.io thumbWidth/thumbHeight 32, padding 3). Typing in the
// search field replaces the sections with one result grid
// (PaletteLibrary::search).
//
// Thumbnails: rendered with the M3 renderer (render::PageView +
// SceneRenderer on a PainterCanvas into a QImage at devicePixelRatio,
// scaled to fit with 1 px border, label painted when showLabel), lazily
// when a section becomes visible, and cached in memory by entry key.
//
// Interaction (Sidebar.itemClicked :6723, createDragSource :5871):
//  * click: actions::insertBundle at CanvasView::centerInsertPoint();
//  * drag: QDrag with kCellsMimeType (+ text/plain) carrying
//    PaletteEntry::cellsXml() and a thumbnail pixmap; the dock also acts as
//    the CanvasDropHandler that inserts at the drop point;
//  * each insertion adds the entry to AppSettings recent shapes;
//  * context menu: Add to / Remove from Favorites.
// Tooltip: the title (and size).
class PaletteDock : public QWidget, public CanvasDropHandler {
    Q_OBJECT

public:
    PaletteDock(const PaletteLibrary& library, DocumentSession& session, CanvasView& canvas, AppSettings& settings,
                QWidget* parent = nullptr);
    ~PaletteDock() override;

    [[nodiscard]] QLineEdit* searchField() const;
    void setSearchText(const QString& text);
    // Inserts the entry at the canvas centre (what a click does).
    bool insertEntry(const QString& key);

    // --- user libraries (.xml mxlibrary files) --------------------------------
    // Each library is a section between "Recently Used" and the built-in
    // palettes, titled with the file name, with buttons to add the selection,
    // export and close it, and a Rename / Remove menu on its entries.
    // Library changes are file operations: they are written to the file at
    // once and never create undo entries. The open files are remembered in
    // AppSettings::openLibraries() and reopened on start (files that are gone
    // are dropped with a statusMessage()).
    // Return false with a translated `error` on failure; opening an open
    // library only expands it. newLibrary writes an empty file.
    bool openLibrary(const QString& path, QString* error = nullptr);
    bool newLibrary(const QString& path, QString* error = nullptr);
    void closeLibrary(const QString& path);
    [[nodiscard]] QStringList libraryPaths() const;
    // Adds the selected cells (encodeCells, size = their bounds) as one entry.
    bool addSelectionToLibrary(const QString& path, const QString& title, QString* error = nullptr);
    bool renameLibraryEntry(const QString& path, int index, const QString& title);
    bool removeLibraryEntry(const QString& path, int index);
    // Writes a copy of the library to `target`.
    bool exportLibrary(const QString& path, const QString& target, QString* error = nullptr);

signals:
    // A message for the status bar (a library that could not be reopened, ...).
    void statusMessage(const QString& message);

public:
    // CanvasDropHandler
    [[nodiscard]] bool canDrop(const QMimeData& mime) const override;
    bool drop(const QMimeData& mime, QPointF scenePos) override;

protected:
    // A move to a screen with another device pixel ratio renders the thumbnails again.
    bool event(QEvent* event) override;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
