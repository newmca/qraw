#include "qraw/ui/selection_model.h"

#include <algorithm>
#include <unordered_set>

namespace qraw::ui {

namespace {

std::vector<std::string> withoutDuplicates(std::vector<std::string> ids)
{
    std::unordered_set<std::string> seen;
    std::vector<std::string> unique;
    unique.reserve(ids.size());
    for (auto& id : ids) {
        if (seen.insert(id).second)
            unique.push_back(std::move(id));
    }
    return unique;
}

} // namespace

SelectionModel::SelectionModel(QObject* parent) : QObject(parent) {}

bool SelectionModel::contains(std::string_view id) const
{
    return std::find(cells_.begin(), cells_.end(), id) != cells_.end();
}

void SelectionModel::set(std::vector<std::string> ids)
{
    assign(withoutDuplicates(std::move(ids)));
}

void SelectionModel::add(const std::vector<std::string>& ids)
{
    std::vector<std::string> next = cells_;
    next.insert(next.end(), ids.begin(), ids.end());
    assign(withoutDuplicates(std::move(next)));
}

void SelectionModel::remove(const std::vector<std::string>& ids)
{
    // A hash set: pruning a selection of n cells after a bulk change removes
    // up to n ids, and a linear search per cell made that quadratic.
    const std::unordered_set<std::string> removed(ids.begin(), ids.end());
    std::vector<std::string> next = cells_;
    std::erase_if(next, [&](const std::string& id) { return removed.contains(id); });
    assign(std::move(next));
}

void SelectionModel::toggle(const std::string& id)
{
    if (contains(id))
        remove({id});
    else
        add({id});
}

void SelectionModel::clear()
{
    assign({});
}

void SelectionModel::assign(std::vector<std::string> ids)
{
    if (ids == cells_)
        return;
    cells_ = std::move(ids);
    emit selectionChanged();
}

} // namespace qraw::ui
