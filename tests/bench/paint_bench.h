#pragma once
// qraw-bench --paint: frame times and work counters of the editor canvas on
// a real DocumentSession + CanvasView (docs/BENCHMARKS.md, "Canvas paint benchmark").
//
// A frame is the input or edit that a scenario performs (a scroll step, a
// mouse move, a document edit) together with the paint it causes: the events
// it posts are processed (the paint that update() queued happens there) and,
// if nothing painted, a synchronous `viewport()->repaint()` follows. All of
// it is timed. Events that earlier frames posted are processed before the
// timer starts, so they are not charged to this frame.

#include "qraw/render/perf_scope.h"
#include "qraw/render/stats.h"

#include <QPointF>
#include <QSize>
#include <QString>

#include <cstddef>
#include <iosfwd>
#include <optional>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace qraw::bench {

struct PaintOptions {
    QString file;            // the diagram to open; empty with `synthetic`
    bool synthetic = false;  // the generated image-heavy document (generator.h)
    int page = 0;            // 0-based page index
    QSize viewport{1600, 1000};
    int repeat = 3;          // runs per scenario, the median of each metric is reported
    bool warmup = true;      // one untimed pass of every scenario before the timed runs
    std::set<std::string> scenarios; // empty = all
    double zoom = 1;         // zoom at the start of every scenario
    bool fit = false;        // start with Fit Window (all content in view, at most 100 %) instead of `zoom`
    std::optional<QPointF> at; // start with this scene point at the top left corner (default: the page origin)
    double frameScale = 1;   // multiplies the frame counts (at least one frame)
    bool timers = false;     // enable the scope timers (also QRAW_PAINT_TIMERS=1)
    std::ostream* progress = nullptr;
};

// The 1 s window rule, the budget shares and the percentiles of one run.
struct FrameMetrics {
    std::size_t frames = 0;
    double avgMs = 0;
    double p50Ms = 0;
    double p95Ms = 0;
    double p99Ms = 0;
    double maxMs = 0;
    double overBudget60 = 0;  // share of frames above 16.7 ms
    double overBudget120 = 0; // share of frames above 8.3 ms
    double worst1s = 0;       // worst sliding 1 s window by over-budget (60 Hz) share
};

inline constexpr double kBudget60Ms = 16.7;
inline constexpr double kBudget120Ms = 8.3;

[[nodiscard]] FrameMetrics computeMetrics(const std::vector<double>& frameMs);

struct ScenarioResult {
    std::string name;
    bool ok = true;
    QString note;            // the failure reason, or a remark on the scenario
    FrameMetrics metrics;    // medians over the runs
    double settleMs = 0;     // time to settled quality after the last frame
    std::size_t paintEvents = 0; // paint events of the viewport during the frames (one per frame expected)
    render::stats::Snapshot counters{};              // totals of one run (median over runs)
    std::vector<render::perf::ScopeTotals> scopes;   // last run; only with timers
    // memory scenario only
    std::optional<std::pair<double, double>> memoryMiB; // peak RSS, current RSS; empty elsewhere
    bool memoryAvailable = true;
};

struct DocumentFacts {
    std::size_t cells = 0;        // states of the page
    std::size_t imageCells = 0;   // states with an image style
    std::size_t visibleCells = 0; // states whose paint bounds touch the viewport at the start
    std::size_t visibleImages = 0;
    double startZoom = 1;         // the zoom of the start view
    std::size_t fileBytes = 0;
    QString description;
};

struct PaintReport {
    PaintOptions options;
    DocumentFacts facts;
    std::vector<ScenarioResult> scenarios;
    bool failed = false;

    [[nodiscard]] const ScenarioResult* find(std::string_view name) const;
};

// The fixed scenario names, in run order.
[[nodiscard]] const std::vector<std::string>& paintScenarioNames();

// Runs the selected scenarios. A fatal setup problem (the file cannot be
// read) sets `failed` and puts the reason in the first scenario's note.
[[nodiscard]] PaintReport runPaintBenchmark(const PaintOptions& options);

using MachineRows = std::vector<std::pair<QString, QString>>;

void printPaintReport(std::ostream& out, const PaintReport& report, bool markdown);
// Checks the report against paint_budgets.h (synthetic document only), prints
// every violation and returns their number. `release`: the time budgets use
// the Release figures as they are (otherwise times kDebugFactor).
[[nodiscard]] int checkBudgets(std::ostream& out, const PaintReport& report, bool release, bool timeBudgets = true);
// Machine-readable results for comparing two runs.
bool writePaintJson(const QString& path, const PaintReport& report, const MachineRows& machine);

} // namespace qraw::bench
