#pragma once

// Property operations: style keys, label, custom data. Same conventions as
// ops_cells.h. Each operation emits at most one ReplaceCell per cell and
// none for a cell whose state would not change (so repeated identical edits
// leave no undo entry). Upstream paths are relative to
// $DRAWIO_SRC/src/main/webapp/.

#include "qraw/commands/result.h"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace qraw::commands {
class Transaction;
}

namespace qraw::commands::ops {

// One style edit: a value sets the key, nullopt or "" removes it (mxUtils.setStyle
// treats an empty value as removal, mxgraph/src/util/mxUtils.js:4867-4869).
struct StyleEdit {
    std::string key;
    std::optional<std::string> value;
};

// mxGraph.setCellStyles(key, value, cells) for several keys, applied in
// order to every cell, through core::Style (lossless: an existing key is
// edited in place -- the last occurrence --, a new key is appended before a
// trailing ';', removal drops every occurrence, untouched tokens keep their
// exact text). An empty style string afterwards removes the `style`
// attribute (core::Cell::setStyleText). Keys must be non-empty and contain
// none of ";=" (failure).
// Result.cells = ids.
Result setStyles(Transaction& tx, std::size_t page, const std::vector<std::string>& ids,
                 const std::vector<StyleEdit>& edits);
// Convenience for one key.
Result setStyle(Transaction& tx, std::size_t page, const std::vector<std::string>& ids, const std::string& key,
                std::optional<std::string> value);

// Replaces the whole style string (Edit Style dialog, model.setStyle).
Result setStyleText(Transaction& tx, std::size_t page, const std::vector<std::string>& ids, std::string_view style);

// Adds / removes a bare style name such as "ellipse" (core::Style::addName /
// removeName). Adding a name that is present, or removing one that is
// absent, changes nothing.
Result setStyleName(Transaction& tx, std::size_t page, const std::vector<std::string>& ids, const std::string& name,
                    bool present);

// mxGraph.cellLabelChanged / Graph.cellLabelChanged: the label of a wrapped
// cell is its wrapper's `label` attribute, otherwise the mxCell `value`
// (core::Cell::setValue handles both, keeping `id` last on wrappers).
Result setLabel(Transaction& tx, std::size_t page, const std::string& id, std::string_view label);

// Edit Data / Graph.setAttributeForCell (js/grapheditor/Graph.js:38748):
// sets (value) or removes (nullopt) a custom attribute on the cell's wrapper.
//  * A plain cell is wrapped first (core::Cell::ensureWrapper(wrapperTag)):
//    id and value move onto the wrapper, value becomes `label`. draw.io's
//    Edit Data dialog creates <object>; setAttributeForCell creates
//    <UserObject>; callers pick. Removing from a plain cell changes nothing.
//  * New attributes go before `id` (core::Cell::setData); an existing one is
//    edited in place.
//  * Removing the last custom attribute keeps the wrapper (draw.io does not
//    unwrap either).
//  * `name` must be a non-empty XML name and not "id" or "label" (use
//    setLabel); failure otherwise. `placeholders`, `tooltip`, `link` are
//    ordinary attributes here.
Result setCellData(Transaction& tx, std::size_t page, const std::string& id, const std::string& name,
                   std::optional<std::string> value, std::string_view wrapperTag = "object");

// One custom-data edit: a value sets the attribute (an empty value is a value),
// nullopt removes it.
struct DataEdit {
    std::string name;
    std::optional<std::string> value;
};

// Edit Data's Apply (EditDataDialog, js/grapheditor/Dialogs.js:4264; one
// mxGraphModel.setValue per cell): applies `edits` in order to every cell in
// `ids` with the rules of setCellData, emitting ONE ReplaceCell per cell, so
// several properties are one command (and one undo step with the others of the
// transaction). All names are validated first: nothing changes on failure.
// Result.cells = ids.
Result editCellData(Transaction& tx, std::size_t page, const std::vector<std::string>& ids,
                    const std::vector<DataEdit>& edits, std::string_view wrapperTag = "object");

// Graph.reservedDataNames (js/grapheditor/Graph.js:1805): `label`, `id` and
// `placeholders` have their own meaning, and names with a colon are namespace
// prefixes. Edit Data refuses them for new properties.
[[nodiscard]] bool isReservedDataName(std::string_view name);

// EditDataDialog.isValidAttributeName: an XML name (XML 1.0 5th edition
// characters, no colon) that is not reserved. `tooltip`, `link`, `tags` and the
// like are ordinary names.
[[nodiscard]] bool isValidDataName(std::string_view name);

} // namespace qraw::commands::ops
