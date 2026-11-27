#pragma once
// Shape palettes generated from draw.io's sidebar definitions
// (resources/palettes/*.json, written by tools/palette/extract_palettes.py).
// Implemented in src/ui/palette_library.cpp. QtCore only: no widgets,
// no painting.

#include "qraw/format/library.h"

#include <QString>
#include <QStringList>

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

namespace qraw::ui {

// One sidebar item. Exactly one of (style) or (xml) describes the cells:
//  * Vertex: one vertex of size w x h with `style` and `value`
//    (Sidebar.createVertexTemplateEntry, js/grapheditor/Sidebar.js:6950).
//  * Edge: one edge with style/value whose source point is (0, h) and
//    target point (w, 0) (Sidebar.createEdgeTemplateEntry, :7025).
//  * Cells: `xml` is an <mxGraphModel> (addDataEntry decoded, or a
//    multi-cell template) -- decode with cell_transfer.h decodeCells.
struct PaletteEntry {
    enum class Kind { Vertex, Edge, Cells };

    QString id;    // stable within the palette (slug of the title, "-2", "-3" for duplicates)
    QString title; // tooltip / search title
    Kind kind = Kind::Vertex;
    QString style;
    QString value;
    QString xml;
    double width = 0;
    double height = 0;
    QStringList tags; // lower-case search words (getTagsForStencil + explicit tags)
    bool showLabel = true; // the thumbnail paints the value (createVertexTemplateEntry showLabel)

    // "<paletteId>/<id>": the key for favorites and recent shapes.
    [[nodiscard]] QString key(const QString& paletteId) const { return paletteId + QLatin1Char('/') + id; }
    // The entry as cells: for Vertex/Edge a one-cell <mxGraphModel> built from
    // style/value/size; for Cells `xml` itself.
    [[nodiscard]] std::string cellsXml() const;
};

struct Palette {
    QString id;       // e.g. "general", "flowchart", "aws4Compute"
    QString title;    // English title as in draw.io (translated with tr() at display time)
    QString library;  // grouping id, e.g. "aws4"; "" for the core palettes
    int order = 0;    // display order (draw.io Sidebar.init order)
    std::vector<PaletteEntry> entries;
};

// Loading result for one file; the loader never throws.
struct PaletteLoadReport {
    int palettes = 0;
    int entries = 0;
    QStringList errors; // "<file>: <reason>"
};

class PaletteLibrary {
public:
    // Every resources/palettes/*.json compiled in under :/qraw/palettes/
    // (sorted by Palette::order, then id). Cached; GUI thread.
    [[nodiscard]] static const PaletteLibrary& builtin();

    // Parses one JSON document in the schema of docs/design/ui.md §8.
    // Malformed entries are skipped and reported; the rest loads.
    void loadJson(const QByteArray& json, const QString& origin, PaletteLoadReport* report = nullptr);
    // Loads every *.json in a directory (Qt resource or disk), sorted by name.
    PaletteLoadReport loadDirectory(const QString& directory);

    [[nodiscard]] const std::vector<Palette>& palettes() const { return palettes_; }
    [[nodiscard]] const Palette* palette(const QString& id) const;
    // Resolves "<paletteId>/<entryId>".
    [[nodiscard]] const PaletteEntry* entry(const QString& key) const;
    [[nodiscard]] std::size_t entryCount() const;

    struct SearchHit {
        const Palette* palette;
        const PaletteEntry* entry;
    };
    // Case-insensitive search over title and tags of all palettes. Every
    // whitespace-separated term must match a prefix of a tag or a substring
    // of the title (draw.io Sidebar.searchEntries matches tags). Results in
    // palette order, at most `limit`.
    [[nodiscard]] std::vector<SearchHit> search(const QString& query, std::size_t limit = 200) const;

private:
    std::vector<Palette> palettes_;
};

// A user library (an .xml mxlibrary file) as a palette: xml entries become
// Cells entries, image entries the image vertex that draw.io's
// addLibraryEntries builds (shape=image;...;image=<data>). Entry ids are
// "e<position>" (1-based), so they change when an entry is removed: user
// entries are never stored in favorites or recent shapes.
[[nodiscard]] Palette paletteFromLibrary(const format::Library& library, const QString& id, const QString& title);

} // namespace qraw::ui
