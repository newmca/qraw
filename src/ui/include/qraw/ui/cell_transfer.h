#pragma once
// Cells as draw.io-compatible XML: the clipboard format, the palette drag
// format and template snippets.
// (src/ui/cell_transfer.cpp). QtCore only (QMimeData for convenience).

#include "qraw/commands/result.h"
#include "qraw/core/cell.h"
#include "qraw/core/page.h"

#include <QByteArray>
#include <QPointF>

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

class QMimeData;

namespace qraw::commands {
class Transaction;
}

namespace qraw::ui {

// MIME type for cells dragged or copied inside Qraw. The payload is the
// same UTF-8 XML as the text/plain flavour (encodeCells()).
inline constexpr char kCellsMimeType[] = "application/x-qraw-cells";

// A set of cells detached from any page: the cells of a clipboard payload, a
// palette entry or a template, in document order (parents before children).
// Top-level cells have parent "" (they get the insertion parent when
// inserted). Geometry of top-level cells is absolute.
struct CellBundle {
    std::vector<core::Cell> cells;
    [[nodiscard]] bool empty() const { return cells.empty(); }
    // Bounding box of the top-level vertices' geometries and the edges'
    // points (mxGraph.getBoundingBoxFromGeometry); null when none.
    [[nodiscard]] std::optional<core::Rect> bounds() const;
};

// Graph.encodeCells (js/grapheditor/Graph.js:34308) as used by
// EditorUi.copyXml (js/diagramly/EditorUi.js): the topmost cells of `ids`
// (queries.h topmostCells, sorted by z-order), each with its whole subtree,
// plus nothing else (edges between copied cells come along only when they
// are selected or inside a copied subtree, as upstream). Top-level cells are
// re-parented to "1" with absolute geometry (parent origin added, as
// mxGraph.cloneCells(cells, allowInvalidEdges, mapping, keepPosition)
// does); edge terminals outside the copied set are dropped and replaced by
// the terminal point (removeCells' view-less fallback). Output (D-004
// style, no XML declaration):
//   <mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/>...
//   </root></mxGraphModel>
// Ids are kept. Wrapped cells (<object>/<UserObject>) stay wrapped.
[[nodiscard]] std::string encodeCells(const core::Page& page, const std::vector<std::string>& ids);

// Parses a clipboard/drag payload: an <mxGraphModel> as above, an <mxfile>
// (its first page), or either of them encodeURIComponent-encoded (draw.io's
// legacy copyCells format, starts with "%3C"). The root cell and the layers
// are dropped; their children become top-level (parent ""). nullopt when the
// text is not such a payload.
[[nodiscard]] std::optional<CellBundle> decodeCells(std::string_view text);

// The cells of `bundle` with fresh ids for `page` (Transaction::newCellId),
// parents/source/target remapped inside the bundle, top-level cells
// re-parented to `parent` and translated by (dx, dy), added with
// commands::ops::addCells. Coordinates are converted from absolute to
// `parent`'s coordinates (queries.h origin). Result.cells = the new
// top-level ids (draw.io selects the pasted top-level cells).
commands::Result insertBundle(commands::Transaction& tx, std::size_t page, const CellBundle& bundle,
                              const std::string& parent, double dx, double dy);

// Fills `mime` with kCellsMimeType and text/plain (both the encodeCells XML).
void setCellsMimeData(QMimeData& mime, const std::string& xml);
// The payload of kCellsMimeType, else of text/plain when it decodes.
[[nodiscard]] std::optional<CellBundle> cellsFromMimeData(const QMimeData& mime);

} // namespace qraw::ui
