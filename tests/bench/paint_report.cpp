// Metrics, tables, JSON and budget checks of qraw-bench --paint.

#include "paint_bench.h"
#include "paint_budgets.h"

#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <ostream>

#ifndef QRAW_TEST_TIMEOUT_SCALE
#define QRAW_TEST_TIMEOUT_SCALE 1
#endif

namespace qraw::bench {

namespace {

namespace stats = render::stats;
namespace perf = render::perf;

// Nearest-rank percentile of an ascending list.
double percentile(const std::vector<double>& sorted, double p)
{
    if (sorted.empty())
        return 0;
    const std::size_t rank = static_cast<std::size_t>(std::ceil(p * double(sorted.size())));
    return sorted[std::clamp<std::size_t>(rank, 1, sorted.size()) - 1];
}

// The worst share of over-budget frames in any window of consecutive frames
// that adds up to one second of frame time (the cumulative frame time is the
// clock, so a slow frame spans several wall-clock milliseconds). A run
// shorter than one second is one window.
double worstWindowShare(const std::vector<double>& frameMs, double budgetMs)
{
    const double total = [&] {
        double sum = 0;
        for (const double ms : frameMs)
            sum += ms;
        return sum;
    }();
    if (frameMs.empty())
        return 0;
    constexpr double kWindowMs = 1000;
    const auto share = [&](std::size_t from, std::size_t to) {
        std::size_t over = 0;
        for (std::size_t i = from; i < to; ++i)
            over += frameMs[i] > budgetMs ? 1 : 0;
        return double(over) / double(to - from);
    };
    if (total < kWindowMs)
        return share(0, frameMs.size());
    double worst = 0;
    std::size_t end = 0;
    double span = 0;
    for (std::size_t start = 0; start < frameMs.size(); ++start) {
        while (end < frameMs.size() && span < kWindowMs)
            span += frameMs[end++];
        if (span < kWindowMs)
            break; // the tail is shorter than a window
        worst = std::max(worst, share(start, end));
        span -= frameMs[start];
    }
    return worst;
}

std::string format(double value, int decimals = 1)
{
    char buffer[48];
    std::snprintf(buffer, sizeof buffer, "%.*f", decimals, value);
    return buffer;
}

std::string percent(double share)
{
    return format(share * 100, share > 0 && share < 0.1 ? 1 : 0) + "%";
}

// A count per frame: whole numbers stay whole.
std::string perFrame(std::uint64_t total, std::size_t frames)
{
    if (frames == 0)
        return "-";
    const double value = double(total) / double(frames);
    return std::abs(value - std::round(value)) < 0.005 ? format(value, 0) : format(value, 2);
}

class Table {
public:
    explicit Table(std::vector<std::string> header) : header_(std::move(header)) {}
    void row(std::vector<std::string> cells) { rows_.push_back(std::move(cells)); }

    void print(std::ostream& out, bool markdown) const
    {
        std::vector<std::size_t> widths(header_.size());
        for (std::size_t c = 0; c < header_.size(); ++c) {
            widths[c] = header_[c].size();
            for (const auto& r : rows_)
                widths[c] = std::max(widths[c], c < r.size() ? r[c].size() : 0);
        }
        const auto line = [&](const std::vector<std::string>& cells) {
            out << (markdown ? "| " : "");
            for (std::size_t c = 0; c < header_.size(); ++c) {
                const std::string& text = c < cells.size() ? cells[c] : std::string();
                const std::string pad(widths[c] - text.size(), ' ');
                // The first column is left aligned, numbers right aligned.
                out << (c == 0 ? text + pad : pad + text) << (c + 1 < header_.size() ? (markdown ? " | " : "  ") : "");
            }
            out << (markdown ? " |" : "") << '\n';
        };
        line(header_);
        if (markdown) {
            out << "|---|";
            for (std::size_t c = 1; c < header_.size(); ++c)
                out << "---:|";
            out << '\n';
        }
        for (const auto& r : rows_)
            line(r);
    }

private:
    std::vector<std::string> header_;
    std::vector<std::vector<std::string>> rows_;
};

bool hasFrames(const ScenarioResult& s)
{
    return s.ok && s.metrics.frames > 0;
}

const perf::ScopeTotals& scope(const ScenarioResult& s, perf::Scope which)
{
    return s.scopes[static_cast<std::size_t>(which)];
}

double ms(std::uint64_t ns, std::size_t frames)
{
    return frames == 0 ? 0 : double(ns) / 1e6 / double(frames);
}

void printScopeTable(std::ostream& out, const ScenarioResult& s, bool markdown)
{
    Table table({"Scope", "calls/frame", "wall ms/frame", "self wall", "self cpu"});
    for (const perf::ScopeTotals& t : s.scopes) {
        if (t.count == 0)
            continue;
        table.row({std::string(perf::name(t.scope)), perFrame(t.count, s.metrics.frames),
                   format(ms(t.wallNs, s.metrics.frames), 3), format(ms(t.selfWallNs, s.metrics.frames), 3),
                   perf::cpuTimeAvailable() ? format(ms(t.selfCpuNs, s.metrics.frames), 3) : "n/a"});
    }
    out << (markdown ? "#### " : "") << s.name << " (per frame)\n\n";
    table.print(out, markdown);
    out << '\n';
}

// The attribution of one frame (fullRepaint) by cause, and of one edit (editCell).
void printAttribution(std::ostream& out, const PaintReport& report, bool markdown)
{
    const ScenarioResult* full = report.find("fullRepaint");
    if (full == nullptr || !hasFrames(*full) || full->scopes.empty())
        return;
    const std::size_t n = full->metrics.frames;
    const auto self = [&](perf::Scope which) { return ms(scope(*full, which).selfWallNs, n); };
    const auto total = [&](perf::Scope which) { return ms(scope(*full, which).wallNs, n); };
    const auto cpuSelf = [&](perf::Scope which) { return ms(scope(*full, which).selfCpuNs, n); };
    const auto cpuTotal = [&](perf::Scope which) { return ms(scope(*full, which).cpuNs, n); };
    using perf::Scope;

    struct Line {
        std::string label;
        double wall;
        double cpu;
    };
    const double view = total(Scope::ViewPaint);
    const double viewCpu = cpuTotal(Scope::ViewPaint);
    const std::vector<Line> lines{
        // The cells are painted by tile rasterisation (TileRaster),
        // which contains (a) to (d) and the decoration (e); recording (shape
        // logic) happens when a cell changes, not per frame.
        {"(a) shape logic: recording a cell (paintShape without the canvas operations)",
         self(Scope::ShapePaint) + self(Scope::Record), cpuSelf(Scope::ShapePaint) + cpuSelf(Scope::Record)},
        {"(b) QPainter rasterisation: fill / stroke / fillAndStroke", total(Scope::Raster), cpuTotal(Scope::Raster)},
        {"(c) text: layout", total(Scope::TextLayout), cpuTotal(Scope::TextLayout)},
        {"(c) text: draw", total(Scope::TextDraw), cpuTotal(Scope::TextDraw)},
        {"(c) text: label set-up", self(Scope::LabelPaint), cpuSelf(Scope::LabelPaint)},
        {"(d) images: resolve (base64 / file)", total(Scope::ImageResolve), cpuTotal(Scope::ImageResolve)},
        {"(d) images: decode", total(Scope::ImageDecode), cpuTotal(Scope::ImageDecode)},
        {"(d) images: scale and draw", total(Scope::ImageDraw), cpuTotal(Scope::ImageDraw)},
        {"(e) page, borders and grid (in the tiles)", total(Scope::SceneBackground), cpuTotal(Scope::SceneBackground)},
        {"(e) tile rasterisation, all of the above in tiles", total(Scope::TileRaster), cpuTotal(Scope::TileRaster)},
        {"(e) compositor (tile blit)", total(Scope::Composite), cpuTotal(Scope::Composite)},
        {"(e) overlay (drawForeground)", total(Scope::Foreground), cpuTotal(Scope::Foreground)},
        {"(e) scene and view overhead (the rest of the paint event)",
         view - total(Scope::TileRaster) - total(Scope::Composite) - total(Scope::Foreground),
         viewCpu - cpuTotal(Scope::TileRaster) - cpuTotal(Scope::Composite) - cpuTotal(Scope::Foreground)},
    };
    Table table({"Cause", "wall ms/frame", "share", "cpu ms/frame"});
    for (const Line& l : lines)
        table.row({l.label, format(l.wall, 2), percent(view > 0 ? l.wall / view : 0), format(l.cpu, 2)});
    table.row({"paint event total (profiled)", format(view, 2), "100%", format(viewCpu, 2)});
    out << (markdown ? "### Attribution of one full repaint\n\n" : "Attribution of one full repaint\n");
    table.print(out, markdown);
    out << '\n';

    if (const ScenarioResult* edit = report.find("editCell"); edit != nullptr && hasFrames(*edit) && !edit->scopes.empty()) {
        const std::size_t m = edit->metrics.frames;
        out << "(f) per edit: PageView::build " << format(ms(scope(*edit, Scope::PageViewBuild).wallNs, m), 2)
            << " ms, PageView::update " << format(ms(scope(*edit, Scope::PageViewUpdate).wallNs, m), 2)
            << " ms, projection diff and item updates " << format(ms(scope(*edit, Scope::DiffViews).selfWallNs, m), 2)
            << " ms (wall, profiled)\n\n";
    }
}

} // namespace

FrameMetrics computeMetrics(const std::vector<double>& frameMs)
{
    FrameMetrics m;
    m.frames = frameMs.size();
    if (frameMs.empty())
        return m;
    std::vector<double> sorted = frameMs;
    std::sort(sorted.begin(), sorted.end());
    double sum = 0;
    std::size_t over60 = 0;
    std::size_t over120 = 0;
    for (const double v : frameMs) {
        sum += v;
        over60 += v > kBudget60Ms ? 1 : 0;
        over120 += v > kBudget120Ms ? 1 : 0;
    }
    m.avgMs = sum / double(frameMs.size());
    m.p50Ms = percentile(sorted, 0.50);
    m.p95Ms = percentile(sorted, 0.95);
    m.p99Ms = percentile(sorted, 0.99);
    m.maxMs = sorted.back();
    m.overBudget60 = double(over60) / double(frameMs.size());
    m.overBudget120 = double(over120) / double(frameMs.size());
    m.worst1s = worstWindowShare(frameMs, kBudget60Ms);
    return m;
}

void printPaintReport(std::ostream& out, const PaintReport& report, bool markdown)
{
    const PaintOptions& o = report.options;
    const DocumentFacts& f = report.facts;
    out << (markdown ? "### Document\n\n" : "Document\n");
    out << f.description.toStdString() << ": " << f.cells + 2 << " cells (with root and layer), " << f.imageCells
        << " image cells, " << format(double(f.fileBytes) / (1024.0 * 1024.0), 1) << " MiB file; at the start "
        << f.visibleCells << " cells and " << f.visibleImages << " images touch the " << o.viewport.width() << "x"
        << o.viewport.height() << " viewport at " << format(f.startZoom * 100, 0) << " % zoom"
        << (o.fit ? " (Fit Window)" : "") << ".\n";
    out << "Runs per scenario: " << o.repeat << (o.warmup ? " after one warm-up pass" : ", no warm-up")
        << (o.frameScale != 1 ? ", frame counts x" + format(o.frameScale, 2) : std::string()) << ".\n\n";

    for (const ScenarioResult& s : report.scenarios) {
        if (!s.ok)
            out << "FAILED " << s.name << ": " << s.note.toStdString() << '\n';
    }

    out << (markdown ? "### Frame times (ms)\n\n" : "Frame times (ms)\n");
    Table times({"Scenario", "frames", "avg", "p50", "p95", "p99", "max", ">16.7ms", ">8.3ms", "worst1s", "settle"});
    for (const ScenarioResult& s : report.scenarios) {
        if (!hasFrames(s))
            continue;
        const FrameMetrics& m = s.metrics;
        times.row({s.name, std::to_string(m.frames), format(m.avgMs), format(m.p50Ms), format(m.p95Ms),
                   format(m.p99Ms), format(m.maxMs), percent(m.overBudget60), percent(m.overBudget120),
                   percent(m.worst1s), format(s.settleMs, 0)});
    }
    times.print(out, markdown);
    out << '\n';

    out << (markdown ? "### Work counters per frame\n\n" : "Work counters per frame\n");
    std::vector<std::string> header{"Scenario"};
    for (std::size_t c = 0; c < stats::kCounterCount; ++c) {
        header.emplace_back(stats::name(static_cast<stats::Counter>(c)));
        if (static_cast<stats::Counter>(c) == stats::Counter::bytesDecoded)
            header.back() += " (MB)";
    }
    Table counters(header);
    for (const ScenarioResult& s : report.scenarios) {
        if (!hasFrames(s))
            continue;
        std::vector<std::string> row{s.name};
        for (std::size_t c = 0; c < stats::kCounterCount; ++c) {
            if (static_cast<stats::Counter>(c) == stats::Counter::bytesDecoded)
                row.push_back(format(double(s.counters[c]) / 1e6 / double(s.metrics.frames), 1));
            else
                row.push_back(perFrame(s.counters[c], s.metrics.frames));
        }
        counters.row(std::move(row));
    }
    counters.print(out, markdown);
    out << "\ntilesRasterized counts the tiles of every thread (workers rasterise the ring around the viewport and the neighbouring zoom buckets in the "
           "background), tilesRasterizedSync only those the GUI thread rasterised while a frame waited for them; settle is the time from the last frame until "
           "the canvas reports its picture final (isRenderIdle). open and firstPaint show their totals.\n\n";

    for (const ScenarioResult& s : report.scenarios) {
        if (s.name == "memory") {
            out << (markdown ? "### Memory\n\n" : "Memory\n");
            if (s.memoryMiB)
                out << "peak RSS (VmHWM) " << format(s.memoryMiB->first, 0) << " MiB, RSS at the end "
                    << format(s.memoryMiB->second, 0) << " MiB (the whole process"
                    << (report.options.synthetic ? ", including the generator of the synthetic document" : "")
                    << ")\n\n";
            else
                out << "n/a (no /proc/self/status on this platform)\n\n";
        }
    }
    for (const ScenarioResult& s : report.scenarios) {
        if (!s.note.isEmpty() && s.ok)
            out << s.name << ": " << s.note.toStdString() << '\n';
    }

    if (perf::enabled()) {
        out << (markdown ? "### Scope timers (profiled; the timers themselves cost time)\n\n"
                         : "Scope timers (profiled; the timers themselves cost time)\n");
        for (const char* name : {"fullRepaint", "pan", "editCell"}) {
            if (const ScenarioResult* s = report.find(name); s != nullptr && hasFrames(*s) && !s->scopes.empty())
                printScopeTable(out, *s, markdown);
        }
        printAttribution(out, report, markdown);
    }
}

bool writePaintJson(const QString& path, const PaintReport& report, const MachineRows& machine)
{
    QJsonObject root;
    QJsonObject machineObject;
    for (const auto& [key, value] : machine)
        machineObject.insert(key, value);
    root.insert(QStringLiteral("machine"), machineObject);

    const PaintOptions& o = report.options;
    QJsonObject options;
    options.insert(QStringLiteral("document"), report.facts.description);
    options.insert(QStringLiteral("page"), o.page);
    options.insert(QStringLiteral("viewport"), QStringLiteral("%1x%2").arg(o.viewport.width()).arg(o.viewport.height()));
    options.insert(QStringLiteral("zoom"), report.facts.startZoom);
    options.insert(QStringLiteral("fit"), o.fit);
    options.insert(QStringLiteral("repeat"), o.repeat);
    options.insert(QStringLiteral("warmup"), o.warmup);
    options.insert(QStringLiteral("frameScale"), o.frameScale);
    options.insert(QStringLiteral("cells"), double(report.facts.cells + 2));
    options.insert(QStringLiteral("imageCells"), double(report.facts.imageCells));
    options.insert(QStringLiteral("visibleCells"), double(report.facts.visibleCells));
    options.insert(QStringLiteral("visibleImages"), double(report.facts.visibleImages));
    root.insert(QStringLiteral("options"), options);

    QJsonArray scenarios;
    for (const ScenarioResult& s : report.scenarios) {
        QJsonObject object;
        object.insert(QStringLiteral("name"), QString::fromStdString(s.name));
        object.insert(QStringLiteral("ok"), s.ok);
        if (!s.note.isEmpty())
            object.insert(QStringLiteral("note"), s.note);
        if (hasFrames(s)) {
            const FrameMetrics& m = s.metrics;
            object.insert(QStringLiteral("frames"), double(m.frames));
            object.insert(QStringLiteral("avgMs"), m.avgMs);
            object.insert(QStringLiteral("p50Ms"), m.p50Ms);
            object.insert(QStringLiteral("p95Ms"), m.p95Ms);
            object.insert(QStringLiteral("p99Ms"), m.p99Ms);
            object.insert(QStringLiteral("maxMs"), m.maxMs);
            object.insert(QStringLiteral("overBudget60"), m.overBudget60);
            object.insert(QStringLiteral("overBudget120"), m.overBudget120);
            object.insert(QStringLiteral("worst1s"), m.worst1s);
            object.insert(QStringLiteral("settleMs"), s.settleMs);
            QJsonObject counters;
            for (std::size_t c = 0; c < stats::kCounterCount; ++c) {
                QJsonObject entry;
                entry.insert(QStringLiteral("total"), double(s.counters[c]));
                entry.insert(QStringLiteral("perFrame"), double(s.counters[c]) / double(m.frames));
                counters.insert(QString::fromUtf8(stats::name(static_cast<stats::Counter>(c)).data()), entry);
            }
            object.insert(QStringLiteral("counters"), counters);
        }
        if (!s.scopes.empty()) {
            QJsonObject scopes;
            for (const perf::ScopeTotals& t : s.scopes) {
                QJsonObject entry;
                entry.insert(QStringLiteral("count"), double(t.count));
                entry.insert(QStringLiteral("wallNs"), double(t.wallNs));
                entry.insert(QStringLiteral("cpuNs"), double(t.cpuNs));
                entry.insert(QStringLiteral("selfWallNs"), double(t.selfWallNs));
                entry.insert(QStringLiteral("selfCpuNs"), double(t.selfCpuNs));
                scopes.insert(QString::fromUtf8(perf::name(t.scope).data()), entry);
            }
            object.insert(QStringLiteral("scopes"), scopes);
        }
        if (s.name == "memory") {
            if (s.memoryMiB) {
                object.insert(QStringLiteral("peakRssMiB"), s.memoryMiB->first);
                object.insert(QStringLiteral("rssMiB"), s.memoryMiB->second);
            } else {
                object.insert(QStringLiteral("peakRssMiB"), QStringLiteral("n/a"));
            }
        }
        scenarios.append(object);
    }
    root.insert(QStringLiteral("scenarios"), scenarios);

    QFile file(path);
    if (!file.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return file.write(QJsonDocument(root).toJson(QJsonDocument::Indented)) >= 0;
}

int checkBudgets(std::ostream& out, const PaintReport& report, bool release, bool timeBudgets)
{
    int violations = 0;
    const auto unit = [&](budgets::Per per) {
        switch (per) {
        case budgets::Per::Frame: return 1.0;
        case budgets::Per::VisibleImage: return double(report.facts.visibleImages);
        case budgets::Per::VisibleCell: return double(report.facts.visibleCells);
        case budgets::Per::PageImage: return double(report.facts.imageCells);
        case budgets::Per::PageCell: return double(report.facts.cells);
        case budgets::Per::TotalPerPageImage: return double(report.facts.imageCells);
        case budgets::Per::TotalPerPageCell: return double(report.facts.cells);
        }
        return 1.0;
    };
    for (const budgets::CounterCeiling& gate : budgets::kCounterCeilings) {
        const ScenarioResult* s = report.find(gate.scenario);
        if (s == nullptr || !hasFrames(*s))
            continue; // not run, or it reports its own failure
        const double ceiling = gate.perUnit * unit(gate.per);
        const bool total = gate.per == budgets::Per::TotalPerPageImage || gate.per == budgets::Per::TotalPerPageCell;
        const double count = double(s->counters[static_cast<std::size_t>(gate.counter)]);
        const double actual = total ? count : count / double(s->metrics.frames);
        if (actual > ceiling + 1e-9) {
            out << "BUDGET counter " << gate.scenario << " " << stats::name(gate.counter) << ": " << format(actual, 2)
                << (total ? " in total, ceiling " : " per frame, ceiling ") << format(ceiling, 2) << '\n';
            ++violations;
        }
    }
    // Every image is decoded once over the whole run, not once per scenario.
    double decoded = 0;
    for (const ScenarioResult& s : report.scenarios) {
        if (hasFrames(s))
            decoded += double(s.counters[static_cast<std::size_t>(stats::Counter::imagesDecoded)]);
    }
    const double distinct = budgets::kWholeRunImagesDecodedPerPageImage * double(report.facts.imageCells);
    if (decoded > distinct + 1e-9) {
        out << "BUDGET counter whole run imagesDecoded: " << format(decoded, 2) << " in total, ceiling "
            << format(distinct, 2) << '\n';
        ++violations;
    }
    if (!timeBudgets) {
        out << "time budgets: skipped (counter ceilings only)\n";
        return violations;
    }
    const double factor =
        budgets::kSlackFactor * (release ? 1.0 : budgets::kDebugFactor) * QRAW_TEST_TIMEOUT_SCALE;
    for (const budgets::TimeBudget& gate : budgets::kTimeBudgets) {
        const ScenarioResult* s = report.find(gate.scenario);
        if (s == nullptr || !hasFrames(*s) || gate.releaseP95Ms <= 0)
            continue;
        const double limit = gate.releaseP95Ms * factor;
        if (s->metrics.p95Ms > limit) {
            out << "BUDGET time " << gate.scenario << ": p95 " << format(s->metrics.p95Ms) << " ms, limit "
                << format(limit) << " ms\n";
            ++violations;
        }
    }
    return violations;
}

} // namespace qraw::bench
