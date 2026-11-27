// ActiveLayer (qraw/ui/active_layer.h).

#include "qraw/ui/active_layer.h"

#include "qraw/render/painter_canvas.h"
#include "qraw/render/stats.h"
#include "qraw/render/tile_rasterizer.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/diagram_scene.h"

#include <QMetaObject>
#include <QPainter>

#include <algorithm>
#include <cmath>

namespace qraw::ui {

namespace {

// Pixels around a bitmap's content: antialiasing of the outermost strokes.
constexpr int kBitmapMarginPx = 1;

} // namespace

ActiveLayer::ActiveLayer(CanvasView& view, DiagramScene& scene, TileScheduler& tiles)
    : QObject(&view), view_(view), scene_(scene), tiles_(tiles)
{
    retireTimer_.setSingleShot(true);
    connect(&retireTimer_, &QTimer::timeout, this, [this] { drop(); });
    retirePoll_.setSingleShot(true);
    connect(&retirePoll_, &QTimer::timeout, this, [this] { retireIfReady(); });
    // Whenever new tiles arrive the layer may have become unnecessary.
    connect(&tiles_, &TileScheduler::areaChanged, this, [this] { scheduleRetireCheck(); });
}

// --- lifting -----------------------------------------------------------------

void ActiveLayer::collect(const std::vector<std::size_t>& rigid, const std::string& reshaped)
{
    const render::PageView& page = scene_.pageView();
    const std::shared_ptr<const render::RenderSnapshot> snapshot = scene_.snapshot();
    const auto& states = page.states();

    std::unordered_set<std::string> members;
    for (const std::size_t i : rigid)
        members.insert(states[i].id);
    // The ends an edge must have to follow: any cell that moves along, or the
    // one vertex that is reshaped.
    std::unordered_set<std::string> terminals = reshaped.empty() ? members : std::unordered_set<std::string>{reshaped};

    rigid_.clear();
    rigidBounds_ = QRectF();
    lifted_ = members;
    liftedRects_.clear();
    for (const std::size_t i : rigid) {
        const std::string& id = states[i].id;
        const auto record = snapshot->indexOf(id) == render::RenderSnapshot::npos
            ? std::shared_ptr<const render::CellRecord>()
            : snapshot->records()[snapshot->indexOf(id)];
        if (!record)
            continue;
        liftedRects_.push_back(record->visualBounds);
        if (id == reshaped) {
            reshapedRecord_ = record;
            continue;
        }
        rigid_.push_back(record);
        rigidBounds_ = rigidBounds_.isNull() ? record->visualBounds : rigidBounds_.united(record->visualBounds);
    }

    reroutes_.clear();
    for (const render::CellState& state : states) {
        if (state.kind != render::CellState::Kind::Edge || state.cell == nullptr || members.contains(state.id))
            continue;
        const std::string& source = state.cell->source();
        const std::string& target = state.cell->target();
        Reroute reroute;
        reroute.sourceMoves = !source.empty() && terminals.contains(source);
        reroute.targetMoves = !target.empty() && terminals.contains(target);
        if (!reroute.sourceMoves && !reroute.targetMoves)
            continue;
        const std::size_t index = snapshot->indexOf(state.id);
        if (index == render::RenderSnapshot::npos)
            continue;
        reroute.id = state.id;
        reroute.record = snapshot->records()[index];
        liftedRects_.push_back(reroute.record->visualBounds);
        lifted_.insert(state.id);
        reroutes_.push_back(std::move(reroute));
    }
}

bool ActiveLayer::liftMove(const std::vector<std::string>& moving, bool duplicate)
{
    if (state_ == State::Retiring)
        drop();
    if (state_ != State::Idle || moving.empty())
        return false;
    mode_ = Mode::Move;
    duplicate_ = duplicate;
    delta_ = QPointF();
    reshaped_.clear();
    reshapedRecord_.reset();
    collect(scene_.pageView().selection(moving), {});
    if (rigid_.empty()) {
        drop();
        return false;
    }
    state_ = State::Lifted;
    revision_ = scene_.contentRevision();
    setExcluded(!duplicate_);
    return true;
}

bool ActiveLayer::liftVertex(const std::string& id)
{
    if (state_ == State::Retiring)
        drop();
    if (state_ != State::Idle || scene_.state(id) == nullptr)
        return false;
    mode_ = Mode::Reshape;
    duplicate_ = false;
    delta_ = QPointF();
    reshaped_ = id;
    reshapedRecord_.reset();
    collect(scene_.pageView().selection(std::vector<std::string>{id}), id);
    if (!reshapedRecord_) {
        drop();
        return false;
    }
    state_ = State::Lifted;
    revision_ = scene_.contentRevision();
    setExcluded(true);
    return true;
}

void ActiveLayer::setExcluded(bool excluded)
{
    if (!excluded) {
        if (!scene_.excluded().empty()) {
            scene_.setExcluded({});
            tiles_.reinstate(kept_); // nothing changed: the tiles from before the lift are right
        }
        return;
    }
    if (!keptTaken_) {
        kept_ = tiles_.keepTiles(liftedRects_);
        keptTaken_ = true;
    }
    scene_.setExcluded(lifted_);
}

// --- every move ----------------------------------------------------------------

void ActiveLayer::rerouteEdges()
{
    if (duplicate_ || reroutes_.empty())
        return;
    const render::PageView& page = scene_.pageView();
    for (Reroute& reroute : reroutes_) {
        const render::CellState* edge = scene_.state(reroute.id);
        if (edge == nullptr)
            continue;
        render::RouteOverride change;
        for (const bool source : {true, false}) {
            if (!(source ? reroute.sourceMoves : reroute.targetMoves))
                continue;
            render::RouteEnd& end = source ? change.source : change.target;
            if (mode_ == Mode::Move) {
                end.shift = {delta_.x(), delta_.y()};
            } else {
                end.bounds = reshapedBounds_;
                end.rotation = reshapedRotation_;
            }
        }
        const std::optional<render::CellState> state = page.previewEdgeState(*edge, change);
        if (state)
            reroute.record = render::recordState(scene_.renderer(), *state, &page);
    }
}

void ActiveLayer::applyChange(const QRectF& before)
{
    const QRectF now = bounds();
    view_.requestContentUpdate(before.isNull() ? now : before.united(now));
}

void ActiveLayer::setMoveDelta(QPointF delta, bool duplicate)
{
    if (state_ != State::Lifted || mode_ != Mode::Move)
        return;
    const QRectF before = bounds();
    if (duplicate != duplicate_) {
        duplicate_ = duplicate;
        // Ctrl pressed or released during the drag: the originals leave the
        // tiles or come back to them.
        setExcluded(!duplicate_);
        delta_ = delta;
        rerouteEdges();
        applyChange(before);
        return;
    }
    if (delta == delta_)
        return;
    delta_ = delta;
    rerouteEdges();
    applyChange(before);
}

void ActiveLayer::setVertexGeometry(const QRectF& bounds, std::optional<double> rotation)
{
    if (state_ != State::Lifted || mode_ != Mode::Reshape)
        return;
    const render::CellState* state = scene_.state(reshaped_);
    if (state == nullptr)
        return;
    const QRectF before = this->bounds();
    reshapedBounds_ = {bounds.x(), bounds.y(), bounds.width(), bounds.height()};
    reshapedRotation_ = rotation;
    const render::CellState preview = render::PageView::previewVertexState(*state, reshapedBounds_, rotation);
    reshapedRecord_ = render::recordState(scene_.renderer(), preview, &scene_.pageView());
    rerouteEdges();
    applyChange(before);
}

// --- the end of the gesture ----------------------------------------------------

void ActiveLayer::commit()
{
    if (state_ != State::Lifted)
        return;
    state_ = State::Retiring;
    kept_.clear();
    keptTaken_ = false;
    // The edit gave the cells their new records; the tiles show them again,
    // at their new place, as soon as they are rasterised.
    scene_.setExcluded({});
    retireTimer_.start(kRetireTimeoutMs);
    scheduleRetireCheck();
}

void ActiveLayer::cancel()
{
    if (state_ == State::Idle)
        return;
    if (state_ == State::Lifted) {
        const bool unchanged = scene_.contentRevision() == revision_;
        std::vector<TileScheduler::KeptTile> kept = std::move(kept_);
        kept_.clear();
        scene_.setExcluded({});
        if (unchanged)
            tiles_.reinstate(kept);
    }
    drop();
}

void ActiveLayer::discard()
{
    retireTimer_.stop();
    retirePoll_.stop();
    rigid_.clear();
    reroutes_.clear();
    reshapedRecord_.reset();
    lifted_.clear();
    kept_.clear();
    bitmap_ = QImage();
    costScale_ = -1;
    state_ = State::Idle;
}

void ActiveLayer::drop()
{
    const QRectF area = bounds();
    discard();
    bitmapInUse_ = false;
    keptTaken_ = false;
    liftedRects_.clear();
    delta_ = QPointF();
    if (!area.isNull())
        view_.requestContentUpdate(area);
}

void ActiveLayer::scheduleRetireCheck()
{
    if (state_ != State::Retiring || retireCheckPending_)
        return;
    retireCheckPending_ = true;
    // After the signal that announced new tiles has finished: the scheduler
    // is only idle once it has put the whole batch.
    QMetaObject::invokeMethod(
        this,
        [this] {
            retireCheckPending_ = false;
            retireIfReady();
        },
        Qt::QueuedConnection);
}

void ActiveLayer::retireIfReady()
{
    if (state_ != State::Retiring)
        return;
    // The scheduler is idle a moment after its last tile arrived (its next plan
    // is queued behind the signal that announced the tile): ask again shortly.
    if (tiles_.isIdle())
        drop();
    else
        retirePoll_.start(kRetirePollMs);
}

// --- what the compositor draws ----------------------------------------------------

QRectF ActiveLayer::bounds() const
{
    QRectF united;
    const auto add = [&united](const QRectF& r) {
        if (!r.isNull())
            united = united.isNull() ? r : united.united(r);
    };
    if (!rigid_.empty())
        add(mode_ == Mode::Move ? rigidBounds_.translated(delta_) : rigidBounds_);
    if (reshapedRecord_)
        add(reshapedRecord_->visualBounds);
    if (!duplicate_) {
        for (const Reroute& reroute : reroutes_) {
            if (reroute.record)
                add(reroute.record->visualBounds);
        }
    }
    return united;
}

bool ActiveLayer::wantsBitmap(double scale)
{
    if (costScale_ != scale) {
        costScale_ = scale;
        costUs_ = 0;
        for (const auto& record : rigid_)
            costUs_ += render::estimateRecordCostUs(record->display, scale);
    }
    return costUs_ > budgetUs_;
}

bool ActiveLayer::ensureBitmap(double scale)
{
    if (!bitmap_.isNull() && bitmapScale_ == scale)
        return true;
    const QRect device = QRectF(rigidBounds_.left() * scale, rigidBounds_.top() * scale, rigidBounds_.width() * scale,
                                rigidBounds_.height() * scale)
                             .toAlignedRect()
                             .adjusted(-kBitmapMarginPx, -kBitmapMarginPx, kBitmapMarginPx, kBitmapMarginPx);
    if (device.isEmpty() || static_cast<long long>(device.width()) * device.height() > kMaxBitmapPixels) {
        bitmap_ = QImage();
        return false;
    }
    QImage image(device.size(), QImage::Format_ARGB32_Premultiplied);
    image.fill(Qt::transparent);
    {
        QPainter painter(&image);
        painter.translate(-device.topLeft());
        painter.scale(scale, scale);
        painter.setRenderHint(QPainter::TextAntialiasing, true);
        for (const auto& record : rigid_)
            render::replayRecord(painter, *record, render::ImageQuality::Interactive, scale, true);
    }
    render::stats::add(render::stats::Counter::activeBitmaps);
    bitmap_ = std::move(image);
    bitmapScale_ = scale;
    bitmapOrigin_ = device.topLeft();
    return true;
}

std::vector<render::ActiveItem> ActiveLayer::items(const QTransform& sceneToDevice)
{
    std::vector<render::ActiveItem> result;
    if (state_ == State::Idle)
        return result;
    const double scale = std::hypot(sceneToDevice.m11(), sceneToDevice.m12());
    const QTransform shifted = QTransform::fromTranslate(delta_.x(), delta_.y()) * sceneToDevice;

    bitmapInUse_ = false;
    if (!rigid_.empty()) {
        if (wantsBitmap(scale) && ensureBitmap(scale)) {
            bitmapInUse_ = true;
            // The bitmap's top-left sits at a whole device pixel: a copy, no resampling.
            const double x = std::round(sceneToDevice.dx() + delta_.x() * scale) + bitmapOrigin_.x();
            const double y = std::round(sceneToDevice.dy() + delta_.y() * scale) + bitmapOrigin_.y();
            render::ActiveItem item;
            item.bitmap = bitmap_;
            item.transform = QTransform::fromTranslate(x, y);
            result.push_back(std::move(item));
        } else {
            bitmap_ = QImage();
            for (const auto& record : rigid_)
                result.push_back({record, QImage(), shifted, 1});
        }
    }
    if (reshapedRecord_)
        result.push_back({reshapedRecord_, QImage(), sceneToDevice, 1});
    if (!duplicate_) {
        for (const Reroute& reroute : reroutes_) {
            if (reroute.record)
                result.push_back({reroute.record, QImage(), sceneToDevice, 1});
        }
    }
    return result;
}

} // namespace qraw::ui
