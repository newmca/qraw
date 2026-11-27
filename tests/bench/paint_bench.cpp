#include "paint_bench.h"

#include "canvas_input.h"
#include "generator.h"

#include "qraw/commands/ops_properties.h"
#include "qraw/format/codec.h"
#include "qraw/ui/active_layer.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"

#include <QApplication>
#include <QElapsedTimer>
#include <QFile>
#include <QLineF>
#include <QScrollBar>
#include <QThread>
#include <QWindow>

#include <algorithm>
#include <cmath>
#include <functional>
#include <iostream>
#include <limits>
#include <memory>

namespace qraw::bench {

namespace {

namespace stats = render::stats;
using ui::test::move;
using ui::test::press;
using ui::test::release;

constexpr double kPageMargin = 20;       // where the page origin sits at the start, as after opening a file
constexpr double kSettleTimeoutMs = 30000;
constexpr double kZoomFactor = 1.03;     // zoomContinuous: per frame
constexpr int kPanStep = 40;             // px per pan frame

double elapsedMs(const QElapsedTimer& timer)
{
    return static_cast<double>(timer.nsecsElapsed()) / 1e6;
}

double median(std::vector<double> values)
{
    if (values.empty())
        return 0;
    std::sort(values.begin(), values.end());
    const std::size_t n = values.size();
    return n % 2 ? values[n / 2] : (values[n / 2 - 1] + values[n / 2]) / 2;
}

// Counts the paint events of the viewport: a frame is expected to cause one.
class PaintCounter final : public QObject {
public:
    explicit PaintCounter(QObject* watched) { watched->installEventFilter(this); }
    std::size_t count = 0;

protected:
    bool eventFilter(QObject* watched, QEvent* event) override
    {
        if (event->type() == QEvent::Paint)
            ++count;
        return QObject::eventFilter(watched, event);
    }
};

// The result of one pass over a scenario.
struct Run {
    std::vector<double> frameMs;
    double settleMs = 0;
    std::size_t paintEvents = 0;
    stats::Snapshot counters{};
    std::vector<render::perf::ScopeTotals> scopes;
    QString note;
};

QString memoryLine(const QByteArray& status, const char* key)
{
    for (const QByteArray& line : status.split('\n')) {
        if (line.startsWith(key))
            return QString::fromLatin1(line.mid(qsizetype(std::strlen(key))).trimmed());
    }
    return {};
}

// VmHWM / VmRSS in MiB from /proc/self/status; nullopt where it does not exist.
std::optional<std::pair<double, double>> readMemoryMiB()
{
    QFile file(QStringLiteral("/proc/self/status"));
    if (!file.open(QIODevice::ReadOnly))
        return std::nullopt;
    const QByteArray status = file.readAll();
    const auto kib = [&](const char* key) { return memoryLine(status, key).section(QLatin1Char(' '), 0, 0).toDouble(); };
    const double peak = kib("VmHWM:");
    if (peak <= 0)
        return std::nullopt;
    return std::pair{peak / 1024.0, kib("VmRSS:") / 1024.0};
}

// --- the document under test -----------------------------------------------------

class Harness {
public:
    explicit Harness(const PaintOptions& options) : options_(options) {}

    // Reads the document and builds the session and the view (not yet shown).
    // Returns the time of read + session + scene build in ms; empty bytes
    // with a file path read the file (timed).
    bool open(const QByteArray& bytes, double& ms, QString& error)
    {
        view_.reset();
        session_.reset();
        counter_.reset();
        QElapsedTimer timer;
        timer.start();
        QByteArray data = bytes;
        if (data.isEmpty()) {
            QFile file(options_.file);
            if (!file.open(QIODevice::ReadOnly)) {
                error = QStringLiteral("cannot read %1").arg(options_.file);
                return false;
            }
            data = file.readAll();
        }
        format::ReadResult read = format::readDocument(data);
        if (!read.ok() || read.document->pageCount() == 0) {
            error = QStringLiteral("the document does not parse or has no page");
            return false;
        }
        if (static_cast<std::size_t>(options_.page) >= read.document->pageCount()) {
            error = QStringLiteral("page %1 does not exist (the document has %2)")
                        .arg(options_.page).arg(read.document->pageCount());
            return false;
        }
        data.clear();
        session_ = std::make_unique<ui::DocumentSession>(std::move(*read.document));
        session_->setCurrentPage(static_cast<std::size_t>(options_.page));
        view_ = std::make_unique<ui::CanvasView>(*session_);
        view_->resize(options_.viewport);
        counter_ = std::make_unique<PaintCounter>(view_->viewport());
        ms = elapsedMs(timer);
        return true;
    }

    // The first paint, cold. The window is shown first and its own paint is
    // not measured (the viewport has its final size only then); the measured
    // frame sets the start view and paints it.
    Run firstPaint()
    {
        view_->show();
        QElapsedTimer wait;
        wait.start();
        while ((view_->windowHandle() == nullptr || !view_->windowHandle()->isExposed()) && elapsedMs(wait) < 5000)
            QCoreApplication::processEvents(QEventLoop::AllEvents, 10);
        drainEvents();
        beginMeasure();
        frame([&] { applyStart(); });
        return endMeasure();
    }

    // The view of a freshly opened file, or the one the options ask for: the
    // start zoom (or Fit Window) with the page origin (or --at) at the top left.
    void applyStart()
    {
        if (options_.fit) {
            view_->fitWindow();
            return;
        }
        view_->setZoom(options_.zoom, QPointF(0, 0));
        const QPointF origin = options_.at.value_or(view_->diagramScene().pageArea().topLeft());
        const QPointF now = view_->viewportTransform().map(origin);
        scrollBy(int(std::lround(now.x() - kPageMargin)), int(std::lround(now.y() - kPageMargin)));
    }

    [[nodiscard]] ui::CanvasView& view() { return *view_; }
    [[nodiscard]] ui::DocumentSession& session() { return *session_; }
    [[nodiscard]] const render::PageView& pageView() { return view_->diagramScene().pageView(); }
    [[nodiscard]] int frames(int base) const { return std::max(1, int(std::lround(base * options_.frameScale))); }
    [[nodiscard]] bool timers() const { return render::perf::enabled(); }

    // Puts the view in the state of a freshly opened file: the start zoom,
    // the page origin at the top left, nothing selected, no gesture; then one
    // unmeasured paint and the wait for settled quality.
    void prepare()
    {
        view_->interaction().cancelGesture();
        session_->selection().clear();
        applyStart();
        drainEvents();
        view_->viewport()->repaint();
        settle();
        quiesce();
    }

    // Prefetch (the ring around the viewport, the neighbouring zoom buckets)
    // is background work; the set-up waits for it so that the frames of a
    // scenario are charged only with the work they cause.
    void quiesce()
    {
        QElapsedTimer timer;
        timer.start();
        while (!view_->tileScheduler().isQuiescent() && elapsedMs(timer) < kSettleTimeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(1);
        }
    }

    // Processes events until the canvas reports that its picture is final.
    // Returns how long that took, in ms.
    double settle()
    {
        QElapsedTimer timer;
        timer.start();
        QCoreApplication::processEvents();
        while (!view_->isRenderIdle() && elapsedMs(timer) < kSettleTimeoutMs) {
            QCoreApplication::processEvents(QEventLoop::AllEvents, 5);
            QThread::msleep(1);
        }
        return elapsedMs(timer);
    }

    // Two passes: the first one can post what the second one delivers (a
    // scene's deferred update reaches the view as a posted update request).
    static void drainEvents()
    {
        QCoreApplication::processEvents();
        QCoreApplication::processEvents();
    }

    void beginMeasure()
    {
        stats::reset();
        render::perf::reset();
        counter_->count = 0;
        frameMs_.clear();
    }

    // One frame. Events that earlier work posted are processed first, outside
    // the timer. Then `action` runs and its events are processed, which paints
    // what the action asked to update, as the event loop does for a user; if
    // that did not paint, a synchronous viewport()->repaint() follows. The
    // timer covers all of it, so a frame costs one paint wherever it happens.
    void frame(const std::function<void()>& action = {})
    {
        drainEvents();
        const std::size_t paintsBefore = counter_->count;
        QElapsedTimer timer;
        timer.start();
        if (action)
            action();
        drainEvents();
        if (counter_->count == paintsBefore)
            view_->viewport()->repaint();
        frameMs_.push_back(elapsedMs(timer));
    }

    Run endMeasure()
    {
        Run run;
        run.frameMs = std::move(frameMs_);
        run.paintEvents = counter_->count;
        run.counters = stats::snapshot();
        if (timers())
            run.scopes = render::perf::snapshot();
        run.settleMs = settle();
        return run;
    }

    // --- picking cells on the screen ------------------------------------------
    [[nodiscard]] QRectF visibleSceneRect() const
    {
        return view_->mapToScene(view_->viewport()->rect()).boundingRect();
    }

    // States whose paint bounds touch the viewport, in paint order.
    [[nodiscard]] std::vector<const render::CellState*> visibleStates(bool verticesOnly) const
    {
        std::vector<const render::CellState*> result;
        const QRectF visible = visibleSceneRect();
        for (const render::CellState& s : view_->diagramScene().pageView().states()) {
            if (verticesOnly && s.kind != render::CellState::Kind::Vertex)
                continue;
            const QRectF bounds(s.paintBounds.x, s.paintBounds.y, s.paintBounds.width, s.paintBounds.height);
            if (bounds.intersects(visible))
                result.push_back(&s);
        }
        return result;
    }

    static QPointF centreOf(const render::CellState& s)
    {
        return {s.bounds.x + s.bounds.width / 2, s.bounds.y + s.bounds.height / 2};
    }

    // Keeps a scene point inside the viewport (with a margin) so that a synthetic event lands on the canvas.
    [[nodiscard]] QPointF clampToView(QPointF p) const
    {
        const QRectF v = visibleSceneRect().adjusted(4, 4, -4, -4);
        return {std::clamp(p.x(), v.left(), v.right()), std::clamp(p.y(), v.top(), v.bottom())};
    }

    // A visible top-level vertex that is not part of a group, an image first
    // when `preferImage`.
    [[nodiscard]] const render::CellState* pickVertex(bool preferImage, bool imageAllowed = true) const
    {
        const render::CellState* fallback = nullptr;
        for (const render::CellState* s : visibleStates(true)) {
            if (s->bounds.width < 20 || s->bounds.height < 20)
                continue;
            const bool image = !s->style.image().empty();
            if (image && !imageAllowed)
                continue;
            if (image == preferImage)
                return s;
            if (fallback == nullptr)
                fallback = s;
        }
        return fallback;
    }

    void scrollBy(int dx, int dy)
    {
        view_->horizontalScrollBar()->setValue(view_->horizontalScrollBar()->value() + dx);
        view_->verticalScrollBar()->setValue(view_->verticalScrollBar()->value() + dy);
    }

private:
    const PaintOptions& options_;
    std::unique_ptr<ui::DocumentSession> session_;
    std::unique_ptr<ui::CanvasView> view_;
    std::unique_ptr<PaintCounter> counter_;
    std::vector<double> frameMs_;
};

// --- scenarios --------------------------------------------------------------------

Run fullRepaint(Harness& h)
{
    h.prepare();
    h.beginMeasure();
    for (int i = 0, n = h.frames(20); i < n; ++i)
        h.frame();
    return h.endMeasure();
}

// Right, down, left, up in blocks of equal length: back at the start at the end.
Run pan(Harness& h)
{
    h.prepare();
    const int n = h.frames(60);
    h.beginMeasure();
    for (int i = 0; i < n; ++i) {
        const int block = std::min(3, i * 4 / n);
        const int dx = block == 0 ? kPanStep : block == 2 ? -kPanStep : 0;
        const int dy = block == 1 ? kPanStep : block == 3 ? -kPanStep : 0;
        h.frame([&] { h.scrollBy(dx, dy); });
    }
    return h.endMeasure();
}

Run zoomStep(Harness& h)
{
    h.prepare();
    h.beginMeasure();
    for (int i = 0, n = h.frames(20); i < n; ++i)
        h.frame([&] { i % 2 == 0 ? h.view().zoomIn() : h.view().zoomOut(); });
    return h.endMeasure();
}

// Zoom out by a factor of 1.03 per frame (more of the page comes into view),
// anchored at a point of the viewport, then back in to the start.
Run zoomContinuous(Harness& h)
{
    h.prepare();
    const QPointF anchor(h.view().viewport()->width() / 3.0, h.view().viewport()->height() / 2.0);
    const int n = h.frames(60);
    h.beginMeasure();
    for (int i = 0; i < n; ++i)
        h.frame([&] { h.view().setZoom(h.view().zoom() / kZoomFactor, anchor); });
    for (int i = 0; i < n; ++i)
        h.frame([&] { h.view().setZoom(h.view().zoom() * kZoomFactor, anchor); });
    return h.endMeasure();
}

// The mouse moves along a path over the visible cells and around the handles
// of the first one, which is selected.
Run hover(Harness& h)
{
    h.prepare();
    const auto visible = h.visibleStates(true);
    Run run;
    if (visible.empty()) {
        run.note = QStringLiteral("no visible cell to hover");
        return run;
    }
    const render::CellState& selected = *visible.front();
    h.session().selection().set({selected.id});
    QCoreApplication::processEvents();
    h.view().viewport()->repaint();

    const auto& b = selected.bounds;
    std::vector<QPointF> waypoints{{b.x, b.y},
                                   {b.x + b.width, b.y},
                                   {b.x + b.width, b.y + b.height},
                                   {b.x, b.y + b.height},
                                   {b.x + b.width / 2, b.y + b.height / 2}};
    for (std::size_t i = 1; i < visible.size() && waypoints.size() < 12; ++i)
        waypoints.push_back(Harness::centreOf(*visible[i]));
    for (QPointF& p : waypoints)
        p = h.clampToView(p);

    // 100 positions at equal arc length along the polyline.
    double length = 0;
    for (std::size_t i = 1; i < waypoints.size(); ++i)
        length += QLineF(waypoints[i - 1], waypoints[i]).length();
    const int n = h.frames(100);
    h.beginMeasure();
    std::size_t segment = 1;
    double walked = 0;
    for (int i = 0; i < n; ++i) {
        const double target = length * (i + 1) / n;
        while (segment + 1 < waypoints.size()
               && walked + QLineF(waypoints[segment - 1], waypoints[segment]).length() < target) {
            walked += QLineF(waypoints[segment - 1], waypoints[segment]).length();
            ++segment;
        }
        const QLineF line(waypoints[segment - 1], waypoints[segment]);
        const double t = line.length() > 0 ? std::clamp((target - walked) / line.length(), 0.0, 1.0) : 1;
        h.frame([&] { move(h.view(), line.pointAt(t), Qt::NoModifier, Qt::NoButton); });
    }
    return h.endMeasure();
}

// The first (visible cells first) 50 cells, selected one after the other.
Run selectionChange(Harness& h)
{
    h.prepare();
    std::vector<std::string> ids;
    for (const render::CellState* s : h.visibleStates(false))
        ids.push_back(s->id);
    for (const render::CellState& s : h.pageView().states()) {
        if (ids.size() >= 50)
            break;
        if (std::find(ids.begin(), ids.end(), s.id) == ids.end())
            ids.push_back(s.id);
    }
    ids.resize(std::min<std::size_t>(ids.size(), std::size_t(h.frames(50))));
    h.beginMeasure();
    for (const std::string& id : ids)
        h.frame([&] { h.session().selection().set({id}); });
    return h.endMeasure();
}

struct DragStart {
    QPointF from;
    bool ok = false;
};

// Presses on an image (else any) vertex of the page.
DragStart pressOnCell(Harness& h)
{
    const render::CellState* target = h.pickVertex(true);
    if (target == nullptr)
        return {};
    const QPointF from = Harness::centreOf(*target);
    press(h.view(), from);
    return {from, true};
}

constexpr QPointF kDragStepPx{3, 2}; // per mouse move, viewport pixels (a click slop is 3 px)

// The cells under the pointer are lifted out of the tiles when the pointer
// leaves the click tolerance: the lift frame rasterises the tiles under
// them once, every later move only replays them (and the edges that follow).
// dragLift measures the lift, dragMove the moves after it.
Run dragLift(Harness& h)
{
    h.prepare();
    Run run;
    const DragStart start = pressOnCell(h);
    if (!start.ok) {
        run.note = QStringLiteral("no visible cell to drag");
        return run;
    }
    h.frame(); // the press itself
    h.beginMeasure();
    h.frame([&] { move(h.view(), start.from + kDragStepPx / h.view().zoom()); });
    Run result = h.endMeasure(); // includes the wait for the exclusion tiles (settle)
    h.view().interaction().cancelGesture();
    return result;
}

Run dragMove(Harness& h)
{
    h.prepare();
    Run run;
    const DragStart start = pressOnCell(h);
    if (!start.ok) {
        run.note = QStringLiteral("no visible cell to drag");
        return run;
    }
    // The first move lifts the cells; its frame is dragLift's. Wait until the
    // tiles under them are final so that the frames below are the moves alone.
    h.frame([&] { move(h.view(), start.from + kDragStepPx / h.view().zoom()); });
    h.settle();
    h.quiesce();
    h.beginMeasure();
    for (int i = 2, n = h.frames(60) + 1; i <= n; ++i)
        h.frame([&] { move(h.view(), start.from + kDragStepPx / h.view().zoom() * i); });
    Run result = h.endMeasure();
    h.view().interaction().cancelGesture();
    return result;
}

// One vertex resized by its bottom-right handle: live in the active layer.
Run resizeMove(Harness& h)
{
    h.prepare();
    Run run;
    const render::CellState* target = h.pickVertex(true);
    if (target == nullptr) {
        run.note = QStringLiteral("no visible vertex to resize");
        return run;
    }
    h.session().selection().set({target->id});
    QCoreApplication::processEvents();
    h.view().viewport()->repaint();
    const QPointF corner(target->bounds.x + target->bounds.width, target->bounds.y + target->bounds.height);
    press(h.view(), corner);
    const double k = 1 / h.view().zoom();
    // A first move that survives grid snapping lifts the vertex (that frame is dragLift's).
    const QPointF first = corner + QPointF(40, 30) * k;
    h.frame([&] { move(h.view(), first); });
    h.settle();
    h.quiesce();
    h.beginMeasure();
    for (int i = 1, n = h.frames(60); i <= n; ++i)
        h.frame([&] { move(h.view(), first + kDragStepPx * k * i); });
    Run result = h.endMeasure();
    h.view().interaction().cancelGesture();
    return result;
}

// Every visible vertex (at most 300) selected and moved together: the lifted
// set is large, so the active layer chooses between replaying it every frame
// and one bitmap by its cost estimate (ActiveLayer::kReplayBudgetUs).
// `budgetUs` overrides that estimate's limit: infinity replays live always,
// 0 is a bitmap always.
Run dragMany(Harness& h, double budgetUs)
{
    h.prepare();
    Run run;
    std::vector<std::string> ids;
    for (const render::CellState* s : h.visibleStates(true)) {
        if (ids.size() < 300 && s->bounds.width >= 2 && s->bounds.height >= 2)
            ids.push_back(s->id);
    }
    if (ids.size() < 2) {
        run.note = QStringLiteral("fewer than two visible vertices");
        return run;
    }
    h.session().selection().set(ids);
    QCoreApplication::processEvents();
    h.view().viewport()->repaint();
    h.view().activeLayer().setReplayBudgetUs(budgetUs);
    const QPointF from = Harness::centreOf(*h.pageView().find(ids.front()));
    press(h.view(), from);
    const double k = 1 / h.view().zoom();
    h.frame([&] { move(h.view(), from + kDragStepPx * k); });
    h.settle();
    h.quiesce();
    h.beginMeasure();
    for (int i = 2, n = h.frames(60) + 1; i <= n; ++i)
        h.frame([&] { move(h.view(), from + kDragStepPx * k * i); });
    Run result = h.endMeasure();
    result.note = QStringLiteral("%1 cells lifted").arg(ids.size());
    h.view().interaction().cancelGesture();
    h.view().activeLayer().setReplayBudgetUs(ui::ActiveLayer::kReplayBudgetUs);
    return result;
}

Run dragMany(Harness& h)
{
    return dragMany(h, ui::ActiveLayer::kReplayBudgetUs);
}
Run dragManyLive(Harness& h)
{
    return dragMany(h, std::numeric_limits<double>::infinity());
}
Run dragManyBitmap(Harness& h)
{
    return dragMany(h, 0);
}

// The release of a drag (the edit that commits the move) and the frame after it.
Run dragCommit(Harness& h)
{
    h.prepare();
    Run run;
    const DragStart start = pressOnCell(h);
    if (!start.ok) {
        run.note = QStringLiteral("no visible cell to drag");
        return run;
    }
    QPointF end = start.from;
    for (int i = 1; i <= 10; ++i) {
        end = start.from + kDragStepPx / h.view().zoom() * i;
        move(h.view(), end);
    }
    h.frame();
    h.beginMeasure();
    h.frame([&] { release(h.view(), end); });
    Run result = h.endMeasure();
    if (h.session().canUndo())
        h.session().undo();
    else
        result.note = QStringLiteral("the release made no edit");
    return result;
}

// DocumentSession::edit setting fillColor on one vertex, and the frame after it.
Run editCell(Harness& h)
{
    h.prepare();
    Run run;
    const render::CellState* target = h.pickVertex(false, false);
    if (target == nullptr || target->cell == nullptr) {
        run.note = QStringLiteral("no visible vertex to edit");
        return run;
    }
    const std::string id = target->id;
    const std::size_t page = h.session().currentPageIndex();
    static const char* const kColors[] = {"#ff0000", "#00ff00"};
    const int n = h.frames(20);
    h.beginMeasure();
    for (int i = 0; i < n; ++i) {
        h.frame([&] {
            h.session().edit(QStringLiteral("Benchmark edit"), [&](commands::Transaction& tx) {
                return commands::ops::setStyle(tx, page, {id}, "fillColor", std::string(kColors[i % 2]));
            });
        });
    }
    Run result = h.endMeasure();
    for (int i = 0; i < n && h.session().canUndo(); ++i)
        h.session().undo();
    QCoreApplication::processEvents();
    return result;
}

using ScenarioFn = Run (*)(Harness&);

struct Scenario {
    const char* name;
    ScenarioFn fn; // null: handled by the runner (open, firstPaint, memory)
};

constexpr Scenario kScenarios[] = {
    {"open", nullptr},
    {"firstPaint", nullptr},
    {"fullRepaint", fullRepaint},
    {"pan", pan},
    {"zoomStep", zoomStep},
    {"zoomContinuous", zoomContinuous},
    {"hover", hover},
    {"selectionChange", selectionChange},
    {"dragLift", dragLift},
    {"dragMove", dragMove},
    {"resizeMove", resizeMove},
    {"dragMany", dragMany},
    {"dragManyLive", dragManyLive},
    {"dragManyBitmap", dragManyBitmap},
    {"dragCommit", dragCommit},
    {"editCell", editCell},
    {"memory", nullptr},
};

FrameMetrics medianMetrics(const std::vector<FrameMetrics>& runs)
{
    FrameMetrics m;
    const auto pick = [&](double FrameMetrics::*field) {
        std::vector<double> values;
        for (const FrameMetrics& r : runs)
            values.push_back(r.*field);
        return median(std::move(values));
    };
    m.frames = runs.empty() ? 0 : runs.front().frames;
    m.avgMs = pick(&FrameMetrics::avgMs);
    m.p50Ms = pick(&FrameMetrics::p50Ms);
    m.p95Ms = pick(&FrameMetrics::p95Ms);
    m.p99Ms = pick(&FrameMetrics::p99Ms);
    m.maxMs = pick(&FrameMetrics::maxMs);
    m.overBudget60 = pick(&FrameMetrics::overBudget60);
    m.overBudget120 = pick(&FrameMetrics::overBudget120);
    m.worst1s = pick(&FrameMetrics::worst1s);
    return m;
}

// The median over the runs of every counter and of the settle time.
ScenarioResult summarize(const std::string& name, const std::vector<Run>& runs)
{
    ScenarioResult result;
    result.name = name;
    std::vector<FrameMetrics> metrics;
    std::vector<double> settle;
    std::vector<double> paints;
    for (const Run& run : runs) {
        metrics.push_back(computeMetrics(run.frameMs));
        settle.push_back(run.settleMs);
        paints.push_back(double(run.paintEvents));
        if (!run.note.isEmpty())
            result.note = run.note;
    }
    result.metrics = medianMetrics(metrics);
    result.settleMs = median(settle);
    result.paintEvents = std::size_t(median(paints));
    for (std::size_t c = 0; c < stats::kCounterCount; ++c) {
        std::vector<double> values;
        for (const Run& run : runs)
            values.push_back(double(run.counters[c]));
        result.counters[c] = std::uint64_t(median(std::move(values)));
    }
    if (!runs.empty())
        result.scopes = runs.back().scopes;
    return result;
}

} // namespace

const std::vector<std::string>& paintScenarioNames()
{
    static const std::vector<std::string> names = [] {
        std::vector<std::string> list;
        for (const Scenario& s : kScenarios)
            list.emplace_back(s.name);
        return list;
    }();
    return names;
}

const ScenarioResult* PaintReport::find(std::string_view name) const
{
    for (const ScenarioResult& s : scenarios) {
        if (s.name == name)
            return &s;
    }
    return nullptr;
}

PaintReport runPaintBenchmark(const PaintOptions& options)
{
    PaintReport report;
    report.options = options;
    if (options.timers)
        render::perf::setEnabled(true);
    const auto wanted = [&](const char* name) { return options.scenarios.empty() || options.scenarios.contains(name); };
    const auto note = [&](const QString& text) {
        if (options.progress != nullptr)
            *options.progress << text.toStdString() << std::endl;
    };
    const auto fail = [&](const QString& why) {
        ScenarioResult failed;
        failed.name = "open";
        failed.ok = false;
        failed.note = why;
        report.scenarios.push_back(failed);
        report.failed = true;
        return report;
    };

    QByteArray bytes;
    if (options.synthetic) {
        note(QStringLiteral("generating the synthetic image-heavy document ..."));
        bytes = generateImageHeavyDocument();
        report.facts.fileBytes = std::size_t(bytes.size());
        report.facts.description = QStringLiteral("synthetic image-heavy document");
    } else {
        report.facts.fileBytes = std::size_t(std::max<qint64>(0, QFile(options.file).size()));
        report.facts.description = QStringLiteral("page %1 of the given file").arg(options.page);
    }

    Harness harness(options);
    const int openRuns = wanted("open") ? options.repeat : 1;
    std::vector<Run> openTimes;
    for (int i = 0; i < openRuns; ++i) {
        double ms = 0;
        QString error;
        stats::reset();
        render::perf::reset();
        if (!harness.open(bytes, ms, error))
            return fail(error);
        Run run;
        run.frameMs = {ms};
        run.counters = stats::snapshot();
        run.scopes = options.timers ? render::perf::snapshot() : std::vector<render::perf::ScopeTotals>{};
        openTimes.push_back(std::move(run));
    }
    bytes.clear();

    if (wanted("open"))
        report.scenarios.push_back(summarize("open", openTimes));

    // The first paint is cold: it happens once, in this process, whether or not it is reported.
    {
        const Run run = harness.firstPaint();
        if (wanted("firstPaint"))
            report.scenarios.push_back(summarize("firstPaint", {run}));
    }

    // What the (now shown, final size) viewport shows at the start.
    {
        const render::PageView& view = harness.pageView();
        report.facts.cells = view.states().size();
        for (const render::CellState& s : view.states())
            report.facts.imageCells += s.style.image().empty() ? 0 : 1;
        for (const render::CellState* s : harness.visibleStates(false)) {
            ++report.facts.visibleCells;
            report.facts.visibleImages += s->style.image().empty() ? 0 : 1;
        }
        report.facts.startZoom = harness.view().zoom();
    }
    if (options.synthetic
        && (report.facts.imageCells != kImageHeavyImageCount
            || report.facts.cells + 2 != kImageHeavyCellCount))
        note(QStringLiteral("WARNING: the synthetic document has %1 cells and %2 images, expected %3 and %4")
                 .arg(report.facts.cells + 2).arg(report.facts.imageCells).arg(kImageHeavyCellCount)
                 .arg(kImageHeavyImageCount));

    for (const Scenario& scenario : kScenarios) {
        if (scenario.fn == nullptr && std::string_view(scenario.name) != "memory")
            continue;
        if (!wanted(scenario.name))
            continue;
        if (std::string_view(scenario.name) == "memory") {
            ScenarioResult memory;
            memory.name = "memory";
            memory.memoryMiB = readMemoryMiB();
            memory.memoryAvailable = memory.memoryMiB.has_value();
            report.scenarios.push_back(std::move(memory));
            continue;
        }
        note(QStringLiteral("scenario %1 ...").arg(QLatin1String(scenario.name)));
        if (options.warmup && options.repeat > 0)
            (void)scenario.fn(harness);
        std::vector<Run> runs;
        const std::uint64_t started = render::perf::nowNs();
        for (int i = 0; i < options.repeat; ++i)
            runs.push_back(scenario.fn(harness));
        render::perf::span(std::string("scenario ") + scenario.name, started, render::perf::nowNs()); // QRAW_TRACE only
        ScenarioResult result = summarize(scenario.name, runs);
        if (runs.front().frameMs.empty()) {
            result.ok = false;
            report.failed = true;
        } else if (result.paintEvents != result.metrics.frames) {
            // Not an error: two paints in a frame mean that its timing and counters cover both.
            result.note += QStringLiteral(" [%1 paint events for %2 frames]")
                               .arg(result.paintEvents).arg(result.metrics.frames);
        }
        report.scenarios.push_back(std::move(result));
    }
    return report;
}

} // namespace qraw::bench
