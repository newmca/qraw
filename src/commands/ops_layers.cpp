#include "qraw/commands/ops_layers.h"

#include "qraw/commands/ops_properties.h"
#include "qraw/commands/primitives.h"
#include "qraw/commands/queries.h"
#include "qraw/commands/transaction.h"
#include "qraw/core/cell.h"
#include "qraw/core/page.h"
#include "qraw/core/style.h"

#include <algorithm>
#include <functional>
#include <unordered_map>

namespace qraw::commands::ops {

namespace {

// Empty when `id` names a layer of the page, otherwise the reason it does not.
std::string notALayerReason(const core::Page& page, const std::string& id)
{
    if (!isAddressable(page, id))
        return "unknown cell '" + id + "'";
    const core::Cell* root = page.rootCell();
    if (!root || page.findCell(id)->parent() != root->id())
        return "cell '" + id + "' is not a layer";
    return {};
}

// Applies `edit` to the layer and replaces it when the edit changed it.
Result editLayer(Transaction& tx, std::size_t page, const std::string& layer,
                 const std::function<void(core::Cell&)>& edit)
{
    const core::Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");
    if (std::string reason = notALayerReason(*p, layer); !reason.empty())
        return Result::failure(reason);
    core::Cell cell = *p->findCell(layer);
    const core::Cell original = cell;
    edit(cell);
    if (cell != original && !tx.emplace<ReplaceCell>(page, cell))
        return Result::failure("cannot replace layer '" + layer + "'");
    return Result::success({layer});
}

} // namespace

std::vector<std::string> layerIds(const core::Page& page)
{
    const core::Cell* root = page.rootCell();
    if (!root)
        return {};
    std::vector<std::string> ids;
    for (const core::Cell* layer : page.children(root->id()))
        ids.push_back(layer->id());
    return ids;
}

Result addLayer(Transaction& tx, std::size_t page, std::optional<std::size_t> index, std::string_view name)
{
    const core::Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");
    const core::Cell* root = p->rootCell();
    if (!root)
        return Result::failure("page has no root cell");

    const std::vector<std::string> layers = layerIds(*p);
    std::size_t position = p->rootEntries().size();
    if (index && *index < layers.size()) {
        position = subtreeEntryRange(*p, layers[*index])->first;
    } else if (!layers.empty()) {
        position = subtreeEntryRange(*p, layers.back())->last + 1;
    }

    core::Cell layer = core::Cell::makeLayer(tx.newCellId(page), root->id());
    layer.setValue(name);
    const std::string id = layer.id();
    if (!tx.emplace<InsertCell>(page, position, std::move(layer)))
        return Result::failure("cannot insert layer");
    return Result::success({id});
}

Result duplicateLayer(Transaction& tx, std::size_t page, const std::string& layer)
{
    const core::Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");
    if (std::string reason = notALayerReason(*p, layer); !reason.empty())
        return Result::failure(reason);

    // Copy the originals first: the page changes as soon as copies are inserted.
    std::vector<core::Cell> originals{*p->findCell(layer)};
    for (const core::Cell* descendant : p->descendants(layer))
        originals.push_back(*descendant);

    // One fresh id per cell, all drawn before anything is inserted.
    std::unordered_map<std::string, std::string> newIds;
    for (const core::Cell& original : originals)
        newIds[original.id()] = tx.newCellId(page);

    const std::size_t insertAt = subtreeEntryRange(*p, layer)->last + 1;

    // Remaps a terminal (source/target) into the clone, or drops it when it
    // points outside the cloned subtree.
    const auto remapTerminal = [&newIds](const std::string& terminal) -> std::string {
        if (terminal.empty())
            return {};
        const auto it = newIds.find(terminal);
        return it == newIds.end() ? std::string{} : it->second;
    };

    std::vector<core::Cell> copies;
    copies.reserve(originals.size());
    for (std::size_t i = 0; i < originals.size(); ++i) {
        core::Cell copy = originals[i];
        copy.setId(newIds.at(originals[i].id()));
        if (const auto parent = newIds.find(copy.parent()); parent != newIds.end())
            copy.setParent(parent->second);
        copy.setSource(remapTerminal(copy.source()));
        copy.setTarget(remapTerminal(copy.target()));
        if (i == 0) {
            const std::string& value = originals[i].value();
            copy.setValue("Copy of " + (value.empty() ? std::string("Background") : value));
            copy.setVisible(true);
        }
        copies.push_back(std::move(copy));
    }

    for (std::size_t i = 0; i < copies.size(); ++i) {
        if (!tx.emplace<InsertCell>(page, insertAt + i, copies[i]))
            return Result::failure("cannot insert cloned layer cell");
    }
    return Result::success({newIds.at(layer)});
}

Result renameLayer(Transaction& tx, std::size_t page, const std::string& layer, std::string_view name)
{
    const core::Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");
    if (std::string reason = notALayerReason(*p, layer); !reason.empty())
        return Result::failure(reason);
    return setLabel(tx, page, layer, name);
}

Result moveLayer(Transaction& tx, std::size_t page, const std::string& layer, std::size_t to)
{
    const core::Page* p = tx.page(page);
    if (!p)
        return Result::failure("unknown page");
    if (std::string reason = notALayerReason(*p, layer); !reason.empty())
        return Result::failure(reason);

    const std::vector<std::string> layers = layerIds(*p);
    const auto current = std::find(layers.begin(), layers.end(), layer);
    const std::size_t from = static_cast<std::size_t>(current - layers.begin());
    const std::size_t target = std::min(to, layers.size() - 1);
    if (target == from)
        return Result::success({layer});

    const EntryRange targetRange = *subtreeEntryRange(*p, layers[target]);
    const std::size_t before = target > from ? targetRange.last + 1 : targetRange.first;
    if (!moveSubtree(tx, page, layer, before))
        return Result::failure("cannot move layer '" + layer + "'");
    return Result::success({layer});
}

Result setLayerVisible(Transaction& tx, std::size_t page, const std::string& layer, bool visible)
{
    return editLayer(tx, page, layer, [visible](core::Cell& cell) { cell.setVisible(visible); });
}

Result setLayerLocked(Transaction& tx, std::size_t page, const std::string& layer, bool locked)
{
    return editLayer(tx, page, layer, [locked](core::Cell& cell) {
        core::Style style = cell.style();
        style.set("locked", locked ? "1" : "0");
        cell.setStyle(style);
    });
}

} // namespace qraw::commands::ops
