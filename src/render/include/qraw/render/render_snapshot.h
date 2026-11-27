#pragma once

// The immutable render snapshot of one page (docs/research/fast-canvas-
// architectures.md 3.2; ARCHITECTURE.md, "Canvas rendering").
//
// A snapshot is what the tile rasteriser reads: one CellRecord per cell in
// paint order (the cell recorded once into a DisplayList, with its visual
// bounds), the page decoration (backdrop, page tiles, grid) and a spatial
// grid over the visual bounds. It holds no pointer into the live model (no
// CellState::cell, no core::Cell), so any thread can keep and read it.
//
// A new snapshot is built for every projection change; cells that did not
// change share the previous snapshot's record (copy on write), so an edit
// re-records exactly the cells that changed (stats recordsBuilt) and
// computeDamage() reports what has to be rasterised again.

#include "qraw/render/display_list.h"
#include "qraw/render/page_view.h"
#include "qraw/render/scene_renderer.h"
#include "qraw/render/spatial_grid.h"

#include <QColor>
#include <QMetaType>
#include <QRectF>
#include <QSizeF>

#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace qraw::render {

// What is painted under the cells: draw.io's backdrop, the page tiles and the
// grid (js/grapheditor/Graph.js geDiagramBackdrop; mxGraph page and grid
// colours). Compared as a whole: any difference damages everything.
struct PageDecor {
    bool hasPage = false;        // false: no current page, only the backdrop
    QColor background = Qt::white;
    bool dark = false;           // the dark scheme: the backdrop, page border and grid are dark too
    bool pageVisible = true;     // page tiles and the backdrop around them
    QRectF pageArea;             // the union of whole page tiles covering the content
    QSizeF pageTile;             // one page tile: pageWidth x pageHeight times pageScale
    bool gridEnabled = false;
    double gridSize = 10;

    friend bool operator==(const PageDecor&, const PageDecor&) = default;
};

// One cell, recorded.
struct CellRecord {
    std::string id;
    DisplayList display;
    // Every pixel the cell can paint, in model units: the display list's
    // bounds and CellState::paintBounds, whichever is larger (never smaller
    // than paintBounds). The rasteriser adds its antialiasing slack.
    QRectF visualBounds;
    // The cell is recorded with its label editor open: no label, and shapes
    // that paint the value themselves (curvedText) skip it.
    bool labelHidden = false;
};

struct SnapshotInput;
class RenderSnapshot;
[[nodiscard]] std::shared_ptr<const RenderSnapshot> buildSnapshot(const SnapshotInput& input);

class RenderSnapshot {
public:
    [[nodiscard]] std::uint64_t generation() const { return generation_; }
    [[nodiscard]] const std::vector<std::shared_ptr<const CellRecord>>& records() const { return records_; }
    [[nodiscard]] const PageDecor& decor() const { return decor_; }
    // The cell whose label the editor shows (recorded without its label); "" = none.
    [[nodiscard]] const std::string& hiddenLabel() const { return hiddenLabel_; }
    // The paint index of `id`, or npos.
    [[nodiscard]] std::size_t indexOf(std::string_view id) const;
    [[nodiscard]] const CellRecord* find(std::string_view id) const;
    static constexpr std::size_t npos = static_cast<std::size_t>(-1);
    // Union of all visual bounds (null when there are no cells).
    [[nodiscard]] const QRectF& contentBounds() const { return contentBounds_; }

    // Paint indices of the records whose visual bounds meet `rect`, ascending.
    [[nodiscard]] std::vector<std::uint32_t> query(const QRectF& rect) const;

    // The same content without the records of `ids` (the cells the active
    // layer shows live while the user moves them): a new snapshot (new
    // generation) that shares every record, nothing is recorded again.
    [[nodiscard]] std::shared_ptr<const RenderSnapshot> withoutRecords(const std::unordered_set<std::string>& ids) const;

private:
    friend std::shared_ptr<const RenderSnapshot> buildSnapshot(const SnapshotInput&);

    std::uint64_t generation_ = 0;
    std::vector<std::shared_ptr<const CellRecord>> records_;
    std::unordered_map<std::string_view, std::uint32_t> index_; // views into records_' ids
    PageDecor decor_;
    std::string hiddenLabel_;
    SpatialGrid grid_;
    QRectF contentBounds_;
};

struct SnapshotInput {
    const PageView* view = nullptr;
    const SceneRenderer* renderer = nullptr;
    PageDecor decor;
    std::string hiddenLabel;
    // The snapshot to take unchanged records from. Null: record everything.
    const RenderSnapshot* previous = nullptr;
    // Cells to record again although `previous` has them (the projection diff's
    // added and refreshed cells). Ignored without `previous`. The hidden label
    // of `previous` and of this input are re-recorded automatically.
    const std::unordered_set<std::string>* changed = nullptr;
};

// Builds the snapshot of `input.view`: one record per state in paint order,
// recording (stats recordsBuilt) the cells that are new, in `changed` or whose
// label visibility changed, and sharing the others with `previous`.
// (Declared above, before RenderSnapshot.)

// Records one state on its own: the shape, then the label unless `hiddenLabel`
// (stats recordsBuilt). For cells that exist only for a moment, such as the
// preview of an edge re-routed while one of its ends is dragged; `view` is
// what shapes that look at their children (tables) read, may be null.
[[nodiscard]] std::shared_ptr<const CellRecord> recordState(const SceneRenderer& renderer, const CellState& state,
                                                            const PageView* view, bool hiddenLabel = false);

// Scene-coordinate areas that have to be rasterised again.
struct Damage {
    bool everything = false;
    std::vector<QRectF> rects;

    void add(const QRectF& rect)
    {
        if (!everything && !rect.isNull() && rect.isValid())
            rects.push_back(rect);
    }
    [[nodiscard]] bool empty() const { return !everything && rects.empty(); }
};

// What differs between two snapshots of one page, as areas to rasterise
// again: everything when the decoration differs (page properties, background,
// page area) or `before` is null; otherwise the old and new visual bounds of
// every record that is not shared between the two (added, vanished,
// re-recorded), plus the bounds of the cells that were restacked, that is the
// cells that have to leave the longest run of survivors that kept their
// relative order (a restack changes pixels only where a moved cell overlaps
// another, i.e. inside its own bounds). Nothing is re-recorded.
[[nodiscard]] Damage computeDamage(const RenderSnapshot* before, const RenderSnapshot& after);

} // namespace qraw::render

Q_DECLARE_METATYPE(qraw::render::Damage)
