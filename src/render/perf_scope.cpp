#include "qraw/render/perf_scope.h"

#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <mutex>
#include <set>

#if defined(__unix__) || defined(__APPLE__)
#include <time.h>
#define QRAW_HAS_THREAD_CPU_TIME 1
#endif

namespace qraw::render::perf {

namespace {

constexpr std::size_t kMaxTraceEvents = 4'000'000;

bool environmentFlag(const char* variable)
{
    const char* value = std::getenv(variable);
    return value != nullptr && *value != '\0' && std::string_view(value) != "0";
}

std::uint64_t wallNow() noexcept
{
    return static_cast<std::uint64_t>(
        std::chrono::duration_cast<std::chrono::nanoseconds>(std::chrono::steady_clock::now().time_since_epoch())
            .count());
}

std::uint64_t cpuNow() noexcept
{
#ifdef QRAW_HAS_THREAD_CPU_TIME
    timespec ts{};
    if (clock_gettime(CLOCK_THREAD_CPUTIME_ID, &ts) == 0)
        return static_cast<std::uint64_t>(ts.tv_sec) * 1'000'000'000ull + static_cast<std::uint64_t>(ts.tv_nsec);
#endif
    return 0;
}

const std::uint64_t kTraceOrigin = wallNow();

struct TraceEvent {
    const char* name;
    std::uint64_t startNs;
    std::uint64_t durNs;
};

struct Frame {
    Scope scope;
    std::uint64_t startWall;
    std::uint64_t startCpu;
    std::uint64_t childWall = 0;
    std::uint64_t childCpu = 0;
};

// Written by its own thread only (plain load + store on the relaxed atomics),
// read by snapshot() and reset() when the threads are idle.
struct ThreadData {
    struct Totals {
        std::atomic<std::uint64_t> count{0};
        std::atomic<std::uint64_t> wall{0};
        std::atomic<std::uint64_t> cpu{0};
        std::atomic<std::uint64_t> selfWall{0};
        std::atomic<std::uint64_t> selfCpu{0};
    };
    Totals totals[kScopeCount];
    std::vector<Frame> stack;
    std::vector<TraceEvent> events;
    std::size_t index = 0;
};

class Registry {
public:
    static Registry& instance()
    {
        static Registry registry;
        return registry;
    }

    ThreadData& threadData()
    {
        thread_local ThreadData* local = create();
        return *local;
    }

    template <typename Fn>
    void forEachThread(Fn&& fn)
    {
        const std::lock_guard lock(mutex_);
        for (const auto& data : threads_)
            fn(*data);
    }

    // A stable C string for a span name.
    const char* intern(std::string_view text)
    {
        const std::lock_guard lock(mutex_);
        return names_.emplace(text).first->c_str();
    }

    std::atomic<std::size_t> traceEvents{0};
    std::string tracePath;
    std::atomic<bool> tracing{false};

private:
    ThreadData* create()
    {
        const std::lock_guard lock(mutex_);
        threads_.push_back(std::make_unique<ThreadData>());
        threads_.back()->index = threads_.size();
        return threads_.back().get();
    }

    std::mutex mutex_;
    std::vector<std::unique_ptr<ThreadData>> threads_; // kept until exit: late readers stay valid
    std::set<std::string, std::less<>> names_;
};

void addTotal(std::atomic<std::uint64_t>& total, std::uint64_t amount)
{
    total.store(total.load(std::memory_order_relaxed) + amount, std::memory_order_relaxed);
}

void recordEvent(ThreadData& data, const char* name, std::uint64_t startNs, std::uint64_t endNs)
{
    Registry& registry = Registry::instance();
    if (registry.traceEvents.fetch_add(1, std::memory_order_relaxed) >= kMaxTraceEvents)
        return;
    data.events.push_back({name, startNs, endNs - startNs});
}

bool startedFromEnvironment()
{
    if (const char* path = std::getenv("QRAW_TRACE"); path != nullptr && *path != '\0') {
        startTrace(path);
        return true;
    }
    return environmentFlag("QRAW_PAINT_TIMERS");
}

} // namespace

namespace detail {
std::atomic<bool> enabled{startedFromEnvironment()};

void begin(Scope scope) noexcept
{
    ThreadData& data = Registry::instance().threadData();
    data.stack.push_back({scope, wallNow(), cpuNow()});
}

void end() noexcept
{
    ThreadData& data = Registry::instance().threadData();
    if (data.stack.empty())
        return;
    const Frame frame = data.stack.back();
    data.stack.pop_back();
    const std::uint64_t endWall = wallNow();
    const std::uint64_t wall = endWall - frame.startWall;
    const std::uint64_t cpu = cpuNow() - frame.startCpu;
    ThreadData::Totals& totals = data.totals[static_cast<std::size_t>(frame.scope)];
    addTotal(totals.count, 1);
    addTotal(totals.wall, wall);
    addTotal(totals.cpu, cpu);
    addTotal(totals.selfWall, wall > frame.childWall ? wall - frame.childWall : 0);
    addTotal(totals.selfCpu, cpu > frame.childCpu ? cpu - frame.childCpu : 0);
    if (!data.stack.empty()) {
        data.stack.back().childWall += wall;
        data.stack.back().childCpu += cpu;
    }
    if (Registry::instance().tracing.load(std::memory_order_relaxed))
        recordEvent(data, name(frame.scope).data(), frame.startWall, endWall);
}
} // namespace detail

std::string_view name(Scope scope) noexcept
{
    switch (scope) {
    case Scope::ViewPaint: return "ViewPaint";
    case Scope::SceneBackground: return "SceneBackground";
    case Scope::CellItemPaint: return "CellItemPaint";
    case Scope::Foreground: return "Foreground";
    case Scope::ShapePaint: return "ShapePaint";
    case Scope::LabelPaint: return "LabelPaint";
    case Scope::Raster: return "Raster";
    case Scope::TextLayout: return "TextLayout";
    case Scope::TextDraw: return "TextDraw";
    case Scope::ImageResolve: return "ImageResolve";
    case Scope::ImageDecode: return "ImageDecode";
    case Scope::ImageDraw: return "ImageDraw";
    case Scope::PageViewBuild: return "PageViewBuild";
    case Scope::DiffViews: return "DiffViews";
    case Scope::Record: return "Record";
    case Scope::TileRaster: return "TileRaster";
    case Scope::Composite: return "Composite";
    case Scope::PageViewUpdate: return "PageViewUpdate";
    case Scope::Count: break;
    }
    return {};
}

void setEnabled(bool on) noexcept
{
    detail::enabled.store(on, std::memory_order_relaxed);
}

bool cpuTimeAvailable() noexcept
{
#ifdef QRAW_HAS_THREAD_CPU_TIME
    return true;
#else
    return false;
#endif
}

std::vector<ScopeTotals> snapshot()
{
    std::vector<ScopeTotals> result(kScopeCount);
    for (std::size_t i = 0; i < kScopeCount; ++i)
        result[i].scope = static_cast<Scope>(i);
    Registry::instance().forEachThread([&](ThreadData& data) {
        for (std::size_t i = 0; i < kScopeCount; ++i) {
            const ThreadData::Totals& t = data.totals[i];
            result[i].count += t.count.load(std::memory_order_relaxed);
            result[i].wallNs += t.wall.load(std::memory_order_relaxed);
            result[i].cpuNs += t.cpu.load(std::memory_order_relaxed);
            result[i].selfWallNs += t.selfWall.load(std::memory_order_relaxed);
            result[i].selfCpuNs += t.selfCpu.load(std::memory_order_relaxed);
        }
    });
    return result;
}

void reset()
{
    Registry::instance().forEachThread([](ThreadData& data) {
        for (ThreadData::Totals& t : data.totals) {
            t.count.store(0, std::memory_order_relaxed);
            t.wall.store(0, std::memory_order_relaxed);
            t.cpu.store(0, std::memory_order_relaxed);
            t.selfWall.store(0, std::memory_order_relaxed);
            t.selfCpu.store(0, std::memory_order_relaxed);
        }
    });
}

bool tracing() noexcept
{
    return Registry::instance().tracing.load(std::memory_order_relaxed);
}

void startTrace(const std::string& path)
{
    Registry& registry = Registry::instance();
    registry.tracePath = path;
    registry.tracing.store(true, std::memory_order_relaxed);
    detail::enabled.store(true, std::memory_order_relaxed);
    static const bool registered = std::atexit([] { writeTrace(); }) == 0;
    (void)registered;
}

std::uint64_t nowNs() noexcept
{
    return wallNow();
}

void span(std::string_view name, std::uint64_t startNs, std::uint64_t endNs)
{
    Registry& registry = Registry::instance();
    if (!registry.tracing.load(std::memory_order_relaxed))
        return;
    recordEvent(registry.threadData(), registry.intern(name), startNs, endNs);
}

bool writeTrace()
{
    Registry& registry = Registry::instance();
    if (registry.tracePath.empty())
        return false;
    std::FILE* file = std::fopen(registry.tracePath.c_str(), "w");
    if (file == nullptr)
        return false;
    std::fputs("{\"traceEvents\":[", file);
    bool first = true;
    registry.forEachThread([&](ThreadData& data) {
        for (const TraceEvent& event : data.events) {
            // Microseconds since the first clock read of the process.
            const double ts = static_cast<double>(event.startNs - kTraceOrigin) / 1000.0;
            const double dur = static_cast<double>(event.durNs) / 1000.0;
            std::fprintf(file, "%s\n{\"ph\":\"X\",\"name\":\"%s\",\"cat\":\"qraw\",\"pid\":1,\"tid\":%zu,"
                               "\"ts\":%.3f,\"dur\":%.3f}",
                         first ? "" : ",", event.name, data.index, ts, dur);
            first = false;
        }
    });
    std::fputs("\n]}\n", file);
    return std::fclose(file) == 0;
}

} // namespace qraw::render::perf
