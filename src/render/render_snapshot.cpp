// RenderSnapshot, SpatialGrid and computeDamage (render_snapshot.h).

#include "qraw/render/render_snapshot.h"

#include "qraw/render/perf_scope.h"
#include "qraw/render/stats.h"

#include <algorithm>
#include <atomic>
#include <cmath>

namespace qraw::render {

namespace {

std::atomic<std::uint64_t> nextGeneration{1};

QRectF toQRectF(const Rect& r)
{
    return {r.x, r.y, r.width, r.height};
}

std::shared_ptr<const CellRecord> recordCell(const SceneRenderer& renderer, const PageView* view,
                                             const CellState& state, bool hidden)
{
    QRAW_PERF_SCOPE(Record);
    stats::add(stats::Counter::recordsBuilt);
    RecordingCanvas canvas;
    try {
        renderer.paintShape(canvas, state, view, hidden);
        canvas.markLabelStart();
        if (!hidden)
            renderer.paintLabel(canvas, state);
    } catch (...) {
        // Painting never fails: a broken cell keeps what it recorded so far
        // (as paintPage does for export).
    }
    auto record = std::make_shared<CellRecord>();
    record->id = state.id;
    record->display = canvas.finish();
    record->labelHidden = hidden;
    // Never smaller than the estimate CellState promises.
    QRectF bounds = record->display.bounds();
    const QRectF estimate = toQRectF(state.paintBounds);
    if (bounds.isNull())
        bounds = estimate;
    else if (!estimate.isNull())
        bounds = bounds.united(estimate);
    record->visualBounds = sanitizedBounds(bounds);
    return record;
}

} // namespace

// ---------------------------------------------------------- RenderSnapshot

std::size_t RenderSnapshot::indexOf(std::string_view id) const
{
    const auto it = index_.find(id);
    return it == index_.end() ? npos : it->second;
}

const CellRecord* RenderSnapshot::find(std::string_view id) const
{
    const std::size_t index = indexOf(id);
    return index == npos ? nullptr : records_[index].get();
}

std::vector<std::uint32_t> RenderSnapshot::query(const QRectF& rect) const
{
    std::vector<std::uint32_t> result;
    grid_.query(rect, result);
    return result;
}

std::shared_ptr<const CellRecord> recordState(const SceneRenderer& renderer, const CellState& state,
                                              const PageView* view, bool hiddenLabel)
{
    return recordCell(renderer, view, state, hiddenLabel);
}

namespace {

std::vector<QRectF> visualBounds(const std::vector<std::shared_ptr<const CellRecord>>& records)
{
    std::vector<QRectF> bounds;
    bounds.reserve(records.size());
    for (const auto& record : records)
        bounds.push_back(record->visualBounds);
    return bounds;
}

// Everything a snapshot derives from its records.
void indexRecords(std::unordered_map<std::string_view, std::uint32_t>& index, QRectF& content,
                  const std::vector<std::shared_ptr<const CellRecord>>& records)
{
    index.reserve(records.size());
    for (std::size_t i = 0; i < records.size(); ++i) {
        const CellRecord& record = *records[i];
        index.emplace(std::string_view(record.id), static_cast<std::uint32_t>(i));
        if (!record.visualBounds.isNull())
            content = content.isNull() ? record.visualBounds : content.united(record.visualBounds);
    }
}

} // namespace

std::shared_ptr<const RenderSnapshot> RenderSnapshot::withoutRecords(const std::unordered_set<std::string>& ids) const
{
    if (ids.empty())
        return nullptr; // callers keep using this snapshot
    auto snapshot = std::make_shared<RenderSnapshot>();
    snapshot->generation_ = nextGeneration.fetch_add(1, std::memory_order_relaxed);
    snapshot->decor_ = decor_;
    snapshot->hiddenLabel_ = hiddenLabel_;
    snapshot->records_.reserve(records_.size());
    for (const auto& record : records_) {
        if (!ids.contains(record->id))
            snapshot->records_.push_back(record);
    }
    indexRecords(snapshot->index_, snapshot->contentBounds_, snapshot->records_);
    snapshot->grid_.build(visualBounds(snapshot->records_));
    return snapshot;
}

std::shared_ptr<const RenderSnapshot> buildSnapshot(const SnapshotInput& input)
{
    auto snapshot = std::make_shared<RenderSnapshot>();
    snapshot->generation_ = nextGeneration.fetch_add(1, std::memory_order_relaxed);
    snapshot->decor_ = input.decor;
    snapshot->hiddenLabel_ = input.hiddenLabel;
    const RenderSnapshot* previous = input.previous;

    const auto& states = input.view->states();
    snapshot->records_.reserve(states.size());
    for (const CellState& state : states) {
        const bool hidden = !input.hiddenLabel.empty() && state.id == input.hiddenLabel;
        std::shared_ptr<const CellRecord> record;
        if (previous != nullptr && !(input.changed != nullptr && input.changed->contains(state.id))) {
            const std::size_t old = previous->indexOf(state.id);
            if (old != RenderSnapshot::npos && previous->records_[old]->labelHidden == hidden)
                record = previous->records_[old];
        }
        if (!record)
            record = recordCell(*input.renderer, input.view, state, hidden);
        snapshot->records_.push_back(std::move(record));
    }

    indexRecords(snapshot->index_, snapshot->contentBounds_, snapshot->records_);
    snapshot->grid_.build(visualBounds(snapshot->records_));
    return snapshot;
}

// ------------------------------------------------------------------ damage

namespace {

// Indices (into `values`) that are not on a longest strictly increasing
// subsequence: the elements that moved relative to the rest.
std::vector<std::size_t> outsideLongestIncreasing(const std::vector<std::size_t>& values)
{
    std::vector<std::size_t> moved;
    if (values.empty() || std::is_sorted(values.begin(), values.end(), std::less_equal<>{}))
        return moved;
    // Patience sorting with predecessor links.
    std::vector<std::size_t> tails;             // index into values of the smallest tail of each length
    std::vector<std::size_t> previous(values.size(), static_cast<std::size_t>(-1));
    for (std::size_t i = 0; i < values.size(); ++i) {
        const auto it = std::lower_bound(tails.begin(), tails.end(), values[i],
                                         [&](std::size_t tail, std::size_t v) { return values[tail] < v; });
        if (it != tails.begin())
            previous[i] = *(it - 1);
        if (it == tails.end())
            tails.push_back(i);
        else
            *it = i;
    }
    std::vector<char> keep(values.size(), 0);
    for (std::size_t i = tails.back(); i != static_cast<std::size_t>(-1); i = previous[i])
        keep[i] = 1;
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (!keep[i])
            moved.push_back(i);
    }
    return moved;
}

} // namespace

Damage computeDamage(const RenderSnapshot* before, const RenderSnapshot& after)
{
    Damage damage;
    if (before == nullptr || !(before->decor() == after.decor())) {
        damage.everything = true;
        return damage;
    }
    // Vanished and re-recorded cells.
    for (const auto& record : before->records()) {
        const std::size_t now = after.indexOf(record->id);
        if (now == RenderSnapshot::npos || after.records()[now] != record)
            damage.add(record->visualBounds);
    }
    // Added and re-recorded cells (new bounds); survivors in paint order with
    // their old position.
    std::vector<std::size_t> oldPositions;
    std::vector<std::size_t> survivors; // index in `after` of each survivor
    for (std::size_t i = 0; i < after.records().size(); ++i) {
        const auto& record = after.records()[i];
        const std::size_t old = before->indexOf(record->id);
        if (old == RenderSnapshot::npos || before->records()[old] != record)
            damage.add(record->visualBounds);
        if (old != RenderSnapshot::npos) {
            oldPositions.push_back(old);
            survivors.push_back(i);
        }
    }
    // Restack: survivors whose relative order changed.
    for (const std::size_t k : outsideLongestIncreasing(oldPositions))
        damage.add(after.records()[survivors[k]]->visualBounds);
    return damage;
}

} // namespace qraw::render
