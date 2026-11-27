#pragma once

// Scoped timers for profiling the paint path (docs/BENCHMARKS.md, "Canvas
// paint"; BUILDING.md). Off unless the environment variable
// QRAW_PAINT_TIMERS=1 is set (or perf::setEnabled(true) is called); when
// off a scope costs one relaxed atomic load. When on, each scope records the
// wall time and, where the platform has it (Linux, macOS: clock_gettime
// CLOCK_THREAD_CPUTIME_ID), the thread CPU time, in per-thread accumulators:
// the scope's call count, its inclusive time and its self time (inclusive
// minus the scopes nested inside it on the same thread). Reading one thread's
// CPU clock is a system call, so a profiled run is slower than a normal one;
// compare scopes with each other, not with unprofiled frame times.
//
// QRAW_TRACE=out.json also records every scope as a Chrome Trace Event
// ("ph":"X") and writes the file at exit, for the Perfetto UI or
// chrome://tracing. It implies QRAW_PAINT_TIMERS=1.
//
// Usage: QRAW_PERF_SCOPE(ShapePaint); at the top of the block to time.

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::render::perf {

// The measured regions. A scope nests inside the ones that call it; the
// benchmark's attribution table (BENCHMARKS.md) is built from their self
// times, so keep the nesting stable: ViewPaint > {TileRaster > {SceneBackground,
// Raster, TextLayout/TextDraw, Image*}, Composite, Foreground}; on the edit
// path PageViewBuild (or PageViewUpdate), DiffViews and Record > {ShapePaint > Raster, LabelPaint}.
enum class Scope : std::size_t {
    ViewPaint,       // CanvasView paint event: everything the view paints
    SceneBackground, // the decoration of a tile or of DiagramScene::render (backdrop, page borders, grid)
    CellItemPaint,   // CellItem::paint (shape and label of one cell); unused (items have no contents)
    Foreground,      // CanvasView::drawForeground (overlay: selection, handles, guides)
    ShapePaint,      // SceneRenderer::paintShape (style lookups, path building, stencils)
    LabelPaint,      // SceneRenderer::paintLabel (text block set-up)
    Raster,          // PainterCanvas fill / stroke / fillAndStroke (QPainter::drawPath)
    TextLayout,      // PainterCanvas::text, layout of the QTextDocument
    TextDraw,        // PainterCanvas::text, painting the laid-out document
    ImageResolve,    // resolveImage: base64 / percent decode or file read
    ImageDecode,     // QImage::fromData or QSvgRenderer::load of the resolved bytes
    ImageDraw,       // PainterCanvas::image, scaling and drawing the decoded image
    PageViewBuild,   // PageView::build
    DiffViews,       // DiagramScene projection diff after an edit (view diff and item updates)
    Record,          // recording one cell into a display list
    TileRaster,      // rasterising one tile: decoration and replay of the records
    Composite,       // presenting the tiles of a frame
    PageViewUpdate,  // PageView::update: patching the view after an edit that kept the tree
    Count
};

inline constexpr std::size_t kScopeCount = static_cast<std::size_t>(Scope::Count);

[[nodiscard]] std::string_view name(Scope scope) noexcept;

namespace detail {
extern std::atomic<bool> enabled;
void begin(Scope scope) noexcept;
void end() noexcept;
} // namespace detail

[[nodiscard]] inline bool enabled() noexcept
{
    return detail::enabled.load(std::memory_order_relaxed);
}

// Overrides the environment (tests, and the benchmark's --timers option).
void setEnabled(bool on) noexcept;

// True where thread CPU time is recorded; false: the cpu columns are 0.
[[nodiscard]] bool cpuTimeAvailable() noexcept;

class ScopeTimer {
public:
    explicit ScopeTimer(Scope scope) noexcept : active_(enabled())
    {
        if (active_)
            detail::begin(scope);
    }
    ~ScopeTimer()
    {
        if (active_)
            detail::end();
    }
    ScopeTimer(const ScopeTimer&) = delete;
    ScopeTimer& operator=(const ScopeTimer&) = delete;

private:
    bool active_;
};

#define QRAW_PERF_CONCAT_INNER(a, b) a##b
#define QRAW_PERF_CONCAT(a, b) QRAW_PERF_CONCAT_INNER(a, b)
#define QRAW_PERF_SCOPE(scope)                                                                                      \
    const ::qraw::render::perf::ScopeTimer QRAW_PERF_CONCAT(qrawPerfScope, __LINE__)(                          \
        ::qraw::render::perf::Scope::scope)

// The totals of one scope, summed over all threads since the last reset().
struct ScopeTotals {
    Scope scope = Scope::Count;
    std::uint64_t count = 0;
    std::uint64_t wallNs = 0;     // inclusive
    std::uint64_t cpuNs = 0;      // inclusive
    std::uint64_t selfWallNs = 0; // without the scopes nested inside
    std::uint64_t selfCpuNs = 0;
};

// One entry per Scope, in enum order (also the ones that never ran). Call
// when the measured threads are idle.
[[nodiscard]] std::vector<ScopeTotals> snapshot();
void reset();

// Trace events. Recording needs QRAW_TRACE (or startTrace); `span` adds a
// named interval (the benchmark marks its scenarios); writeTrace writes
// every event so far as Chrome Trace Event JSON and returns false when the
// file cannot be written. Call when the measured threads are idle.
[[nodiscard]] bool tracing() noexcept;
void startTrace(const std::string& path);
[[nodiscard]] std::uint64_t nowNs() noexcept; // the trace clock
void span(std::string_view name, std::uint64_t startNs, std::uint64_t endNs);
bool writeTrace();

} // namespace qraw::render::perf
