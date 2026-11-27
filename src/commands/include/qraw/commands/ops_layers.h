#pragma once

// Layer operations (the Layers dialog, js/grapheditor/Dialogs.js:5298ff.).
// A layer is a child of the page's root cell. Layer order = order of the
// root's children = entry order of their subtrees (the last layer is drawn
// on top). Same conventions as ops_cells.h.

#include "qraw/commands/result.h"
#include "qraw/core/page.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::commands {
class Transaction;
}

namespace qraw::commands::ops {

// Ids of the layers in order (children of the root cell, entry order).
// Empty when the page has no root cell.
[[nodiscard]] std::vector<std::string> layerIds(const core::Page& page);

// Adds a layer `<mxCell id=".." value="<name>" parent="<root>"/>`
// (Dialogs.js:5372 `new mxCell(mxResources.get('untitledLayer'))`; default
// name "Untitled Layer") as layer number `index` (end when nullopt or out of
// range): inserted before the first entry of the current index-th layer's
// subtree, or after the last entry of the last layer's subtree (end of the
// entries when the page has no layer). Failure when the page has no root
// cell. Result.cells = {new layer id}.
Result addLayer(Transaction& tx, std::size_t page, std::optional<std::size_t> index = std::nullopt,
                std::string_view name = "Untitled Layer");

// Dialogs.js:5806-5836: graph.removeCells(layers, false) -- i.e.
// ops::removeCells with includeEdges = false and respectDeletable = false,
// so edges in other layers connected to removed cells are disconnected,
// not deleted -- and, when no layer is left, a new default layer
// `<mxCell id="<newCellId>" parent="<root>"/>` (no value) is appended.
// Every id must be a layer (failure otherwise).
// Result.cells = {the layer to make current}: the layer before the first
// removed one when there is one, else the first remaining layer.
// (Implemented by the cell-ops package, in src/commands/ops_remove.cpp,
// because it is built on removeCells.)
Result removeLayers(Transaction& tx, std::size_t page, const std::vector<std::string>& ids);

// Dialogs.js:5622-5643: clones the layer with all descendants
// (graph.cloneCell), names the clone "Copy of <label>" where <label> is the
// layer's label or "Background" when it is empty, makes it visible
// (`visible` attribute removed), and inserts it as the layer right after
// the original (its subtree directly after the original's subtree).
// Cloning (mxGraphModel.cloneCells / restoreClone): every cell gets a fresh
// id from Transaction::newCellId(); parent/source/target that point inside
// the cloned set are remapped to the clones; source/target pointing outside
// it are removed (mxCell.clone drops them; restoreClone only reconnects
// cloned terminals). Wrapper, style, geometry and foreign children are
// copied as they are. Result.cells = {clone id}.
Result duplicateLayer(Transaction& tx, std::size_t page, const std::string& layer);

// Sets the layer's label (graph.cellLabelChanged; wrapper-aware as
// ops::setLabel).
Result renameLayer(Transaction& tx, std::size_t page, const std::string& layer, std::string_view name);

// Moves the layer (with its subtree) so that it becomes layer number `to`
// (clamped to the last index), as model.add(root, layer, to) does.
Result moveLayer(Transaction& tx, std::size_t page, const std::string& layer, std::size_t to);

// model.setVisible: visible -> the `visible` attribute is removed, hidden ->
// visible="0" (core::Cell::setVisible).
Result setLayerVisible(Transaction& tx, std::size_t page, const std::string& layer, bool visible);

// Dialogs.js:5543 setLayersLocked: style key `locked` = "1" or "0" (draw.io
// writes "0" when unlocking; it does not remove the key).
Result setLayerLocked(Transaction& tx, std::size_t page, const std::string& layer, bool locked);

} // namespace qraw::commands::ops
