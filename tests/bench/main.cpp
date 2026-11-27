// qraw-bench: timings of the main operations on generated 1k / 10k / 50k
// cell documents. See docs/BENCHMARKS.md. Exit status: 0 when every operation
// succeeded, 1 when one failed, 2 on a usage error.

#include "generator.h"
#include "operations.h"
#include "paint_bench.h"
#include "qraw/render/level_of_detail.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QSysInfo>
#include <QThread>

#include <algorithm>
#include <cstdio>
#include <iostream>
#include <sstream>

#ifndef QRAW_BENCH_BUILD_TYPE
#define QRAW_BENCH_BUILD_TYPE "unknown"
#endif
#ifndef QRAW_BENCH_GIT_COMMIT
#define QRAW_BENCH_GIT_COMMIT "unknown"
#endif

using namespace qraw::bench;

namespace {

constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;

struct Arguments {
    std::vector<std::size_t> sizes{1000, 10000, 50000};
    Options options;
    bool markdown = false;
    QString writeDir;
    // --paint / --paint-synthetic: the canvas paint benchmark instead of the operations.
    bool paintMode = false;
    PaintOptions paint;
    QString jsonPath;
    bool checkBudgets = false;
    // Wall-clock budgets are a statement about one machine: QRAW_BENCH_TIME_BUDGETS=off (or --no-time-budgets)
    // keeps the exact work-counter ceilings and skips them (hosted CI runners, ci.yml).
    bool checkTimeBudgets = true;
};

void printUsage(std::ostream& out)
{
    out << "usage: qraw-bench [--sizes N,N,...] [--repeat N] [--ops all|GROUP,...] [--markdown]\n"
           "                    [--write DIR] [--with-scene]\n"
           "       qraw-bench (--paint FILE [--page N] | --paint-synthetic) [--viewport WxH] [--zoom Z | --fit] [--at X,Y]\n"
           "                    [--repeat N] [--scenarios a,b,...] [--frame-scale F] [--no-warmup] [--timers] [--no-lod]\n"
           "                    [--markdown] [--json OUT] [--check-budgets] [--no-time-budgets] [--write DIR]\n"
           "  --sizes       cell counts of the generated documents (default 1000,10000,50000; minimum 100)\n"
           "  --repeat      runs per operation, min and median are reported (default 3)\n"
           "  --ops         groups to run, default all:";
    for (const std::string& group : operationGroups())
        out << ' ' << group;
    out << "\n  --markdown    print the results as a Markdown table\n"
           "  --write DIR   also write the generated documents to DIR/bench-<N>.drawio\n"
           "  --with-scene  keep a canvas scene alive during move, duplicate, delete, group and ungroup\n"
           "canvas paint benchmark (docs/BENCHMARKS.md, \"Canvas paint benchmark\"):\n"
           "  --paint FILE       frame times and work counters on page --page (0-based, default 0) of FILE\n"
           "  --paint-synthetic  the same on the generated image-heavy document (with --write DIR: also written to\n"
           "                     DIR/bench-images.drawio)\n"
           "  --viewport WxH     viewport size (default 1600x1000)\n"
           "  --zoom Z           zoom at the start of every scenario, 1 = 100 % (default)\n"
           "  --fit              start with Fit Window (all content in view, at most 100 %) instead of --zoom\n"
           "  --at X,Y           start with this scene point at the top left (default: the page origin)\n"
           "  --scenarios a,b    subset of:";
    for (const std::string& name : paintScenarioNames())
        out << ' ' << name;
    out << "\n  --frame-scale F    multiply the frame counts of the scenarios (default 1, at least one frame)\n"
           "  --no-warmup        skip the untimed warm-up pass of every scenario\n"
           "  --no-lod           switch the level of detail off (to measure what it saves)\n"
           "  --timers           enable the scope timers (as QRAW_PAINT_TIMERS=1) and print the attribution\n"
           "  --json OUT         also write the results as JSON, for comparing runs\n"
           "  --check-budgets    check the report against tests/bench/paint_budgets.h; exit 1 on a violation\n"
           "  --no-time-budgets  with --check-budgets: check only the work-counter ceilings, not the wall-clock p95\n"
           "                     budgets (also QRAW_BENCH_TIME_BUDGETS=off|0|false|no; set by the hosted CI runners)\n";
}

std::vector<std::string> split(const std::string& text)
{
    std::vector<std::string> parts;
    std::istringstream stream(text);
    for (std::string part; std::getline(stream, part, ',');)
        parts.push_back(part);
    return parts;
}

// Returns false (after printing the reason) on a usage error.
bool parseArguments(const QStringList& args, Arguments& out)
{
    const QString timeBudgets = qEnvironmentVariable("QRAW_BENCH_TIME_BUDGETS").trimmed().toLower();
    if (timeBudgets == QLatin1String("off") || timeBudgets == QLatin1String("0") || timeBudgets == QLatin1String("false")
        || timeBudgets == QLatin1String("no"))
        out.checkTimeBudgets = false;
    for (int i = 1; i < args.size(); ++i) {
        const QString& arg = args[i];
        const auto value = [&]() -> QString { return i + 1 < args.size() ? args[++i] : QString(); };
        if (arg == QLatin1String("--markdown")) {
            out.markdown = true;
        } else if (arg == QLatin1String("--with-scene")) {
            out.options.withScene = true;
        } else if (arg == QLatin1String("--sizes")) {
            out.sizes.clear();
            for (const std::string& part : split(value().toStdString())) {
                bool ok = false;
                const qulonglong n = QString::fromStdString(part).toULongLong(&ok);
                if (!ok || n < 100) {
                    std::cerr << "qraw-bench: bad size '" << part << "' (a cell count of at least 100)\n";
                    return false;
                }
                out.sizes.push_back(static_cast<std::size_t>(n));
            }
        } else if (arg == QLatin1String("--repeat")) {
            bool ok = false;
            out.options.repeat = value().toInt(&ok);
            if (!ok || out.options.repeat < 1) {
                std::cerr << "qraw-bench: --repeat needs a positive number\n";
                return false;
            }
        } else if (arg == QLatin1String("--ops")) {
            for (const std::string& group : split(value().toStdString())) {
                if (group == "all") {
                    out.options.groups.clear();
                    break;
                }
                const auto& known = operationGroups();
                if (std::find(known.begin(), known.end(), group) == known.end()) {
                    std::cerr << "qraw-bench: unknown operation group '" << group << "'\n";
                    return false;
                }
                out.options.groups.insert(group);
            }
        } else if (arg == QLatin1String("--write")) {
            out.writeDir = value();
            if (out.writeDir.isEmpty()) {
                std::cerr << "qraw-bench: --write needs a directory\n";
                return false;
            }
        } else if (arg == QLatin1String("--paint")) {
            out.paintMode = true;
            out.paint.file = value();
            if (out.paint.file.isEmpty()) {
                std::cerr << "qraw-bench: --paint needs a file\n";
                return false;
            }
        } else if (arg == QLatin1String("--paint-synthetic")) {
            out.paintMode = true;
            out.paint.synthetic = true;
        } else if (arg == QLatin1String("--page")) {
            bool ok = false;
            out.paint.page = value().toInt(&ok);
            if (!ok || out.paint.page < 0) {
                std::cerr << "qraw-bench: --page needs a page index from 0\n";
                return false;
            }
        } else if (arg == QLatin1String("--viewport")) {
            const QStringList parts = value().split(QLatin1Char('x'));
            const int w = parts.value(0).toInt();
            const int h = parts.value(1).toInt();
            if (parts.size() != 2 || w < 100 || h < 100) {
                std::cerr << "qraw-bench: --viewport needs WxH, each at least 100\n";
                return false;
            }
            out.paint.viewport = QSize(w, h);
        } else if (arg == QLatin1String("--zoom")) {
            bool ok = false;
            out.paint.zoom = value().toDouble(&ok);
            if (!ok || out.paint.zoom <= 0) {
                std::cerr << "qraw-bench: --zoom needs a positive factor (1 = 100 %)\n";
                return false;
            }
        } else if (arg == QLatin1String("--fit")) {
            out.paint.fit = true;
        } else if (arg == QLatin1String("--at")) {
            const QStringList parts = value().split(QLatin1Char(','));
            bool okX = false;
            bool okY = false;
            const double x = parts.value(0).toDouble(&okX);
            const double y = parts.value(1).toDouble(&okY);
            if (parts.size() != 2 || !okX || !okY) {
                std::cerr << "qraw-bench: --at needs X,Y in scene units\n";
                return false;
            }
            out.paint.at = QPointF(x, y);
        } else if (arg == QLatin1String("--scenarios")) {
            for (const std::string& name : split(value().toStdString())) {
                const auto& known = paintScenarioNames();
                if (std::find(known.begin(), known.end(), name) == known.end()) {
                    std::cerr << "qraw-bench: unknown scenario '" << name << "'\n";
                    return false;
                }
                out.paint.scenarios.insert(name);
            }
        } else if (arg == QLatin1String("--frame-scale")) {
            bool ok = false;
            out.paint.frameScale = value().toDouble(&ok);
            if (!ok || out.paint.frameScale <= 0) {
                std::cerr << "qraw-bench: --frame-scale needs a positive number\n";
                return false;
            }
        } else if (arg == QLatin1String("--no-warmup")) {
            out.paint.warmup = false;
        } else if (arg == QLatin1String("--no-lod")) {
            qraw::render::lod::setEnabled(false);
        } else if (arg == QLatin1String("--timers")) {
            out.paint.timers = true;
        } else if (arg == QLatin1String("--json")) {
            out.jsonPath = value();
            if (out.jsonPath.isEmpty()) {
                std::cerr << "qraw-bench: --json needs a file\n";
                return false;
            }
        } else if (arg == QLatin1String("--check-budgets")) {
            out.checkBudgets = true;
        } else if (arg == QLatin1String("--no-time-budgets")) {
            out.checkTimeBudgets = false;
        } else if (arg == QLatin1String("--help") || arg == QLatin1String("-h")) {
            printUsage(std::cout);
            std::exit(0);
        } else {
            std::cerr << "qraw-bench: unknown argument '" << arg.toStdString() << "'\n";
            return false;
        }
    }
    return !out.sizes.empty();
}

// --- machine block -----------------------------------------------------------

// The lines of a /proc file. Not QFile::atEnd(): procfs files report size 0.
QList<QByteArray> procLines(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly) ? file.readAll().split('\n') : QList<QByteArray>();
}

QString cpuModel()
{
    for (const QByteArray& line : procLines(QStringLiteral("/proc/cpuinfo"))) {
        if (line.startsWith("model name"))
            return QString::fromUtf8(line.mid(line.indexOf(':') + 1)).trimmed();
    }
    return QSysInfo::currentCpuArchitecture();
}

QString totalRam()
{
    for (const QByteArray& line : procLines(QStringLiteral("/proc/meminfo"))) {
        if (line.startsWith("MemTotal:")) {
            const qulonglong kib = line.mid(9).trimmed().split(' ').value(0).toULongLong();
            return QStringLiteral("%1 GiB").arg(static_cast<double>(kib) / (1024.0 * 1024.0), 0, 'f', 1);
        }
    }
    return QStringLiteral("unknown");
}

QString compilerVersion()
{
#if defined(__clang__)
    return QStringLiteral("Clang ") + QStringLiteral(__clang_version__);
#elif defined(__GNUC__)
    return QStringLiteral("GCC ") + QStringLiteral(__VERSION__);
#elif defined(_MSC_VER)
    return QStringLiteral("MSVC %1").arg(_MSC_VER);
#else
    return QStringLiteral("unknown");
#endif
}

bool isReleaseBuild()
{
    return QStringLiteral(QRAW_BENCH_BUILD_TYPE) == QLatin1String("Release");
}

MachineRows machineRows()
{
    return {
        {QStringLiteral("CPU"),
         QStringLiteral("%1 (%2 threads)").arg(cpuModel()).arg(QThread::idealThreadCount())},
        {QStringLiteral("RAM"), totalRam()},
        {QStringLiteral("OS"), QSysInfo::prettyProductName()},
        {QStringLiteral("Compiler"), compilerVersion()},
        {QStringLiteral("Qt"), QString::fromLatin1(qVersion())},
        {QStringLiteral("Build type"), QStringLiteral(QRAW_BENCH_BUILD_TYPE)},
        {QStringLiteral("Git commit"), QStringLiteral(QRAW_BENCH_GIT_COMMIT)},
    };
}

void printMachineBlock(std::ostream& out, bool markdown)
{
    for (const auto& [key, value] : machineRows()) {
        out << (markdown ? "- **" : "") << key.toStdString() << (markdown ? ":** " : ": ") << value.toStdString()
            << '\n';
    }
}

// --- canvas paint benchmark ---------------------------------------------------

int runPaint(Arguments& args)
{
    args.paint.repeat = args.options.repeat;
    args.paint.progress = &std::cerr;
    if (!args.writeDir.isEmpty()) {
        if (!args.paint.synthetic) {
            std::cerr << "qraw-bench: --write with --paint needs --paint-synthetic\n";
            return kExitUsage;
        }
        const QByteArray bytes = generateImageHeavyDocument();
        QDir().mkpath(args.writeDir);
        QFile file(QDir(args.writeDir).filePath(QStringLiteral("bench-images.drawio")));
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
            std::cerr << "qraw-bench: cannot write " << file.fileName().toStdString() << '\n';
            return kExitFailure;
        }
        std::cerr << "wrote " << file.fileName().toStdString() << " (" << bytes.size() / (1024 * 1024) << " MiB)\n";
    }
    const PaintReport report = runPaintBenchmark(args.paint);
    std::cout << (args.markdown ? "### Machine\n\n" : "Machine\n");
    printMachineBlock(std::cout, args.markdown);
    std::cout << '\n';
    printPaintReport(std::cout, report, args.markdown);
    if (!args.jsonPath.isEmpty() && !writePaintJson(args.jsonPath, report, machineRows())) {
        std::cerr << "qraw-bench: cannot write " << args.jsonPath.toStdString() << '\n';
        return kExitFailure;
    }
    int violations = 0;
    if (args.checkBudgets) {
        if (!args.paint.synthetic) {
            std::cerr << "qraw-bench: --check-budgets applies to --paint-synthetic only\n";
            return kExitUsage;
        }
        violations = checkBudgets(std::cout, report, isReleaseBuild(), args.checkTimeBudgets);
        std::cout << (violations == 0 ? "budgets: ok\n" : "budgets: violated\n");
    }
    return report.failed || violations > 0 ? kExitFailure : 0;
}

// --- results -----------------------------------------------------------------

struct SizeResults {
    std::size_t cells = 0;
    std::vector<Measurement> measurements;
};

std::string formatMs(double ms)
{
    char buffer[32];
    std::snprintf(buffer, sizeof buffer, ms < 100 ? "%.1f" : "%.0f", ms);
    return buffer;
}

// Rows = operation, columns = sizes; each value is "median / min" in ms.
void printTable(std::ostream& out, const std::vector<SizeResults>& results, bool markdown)
{
    const char* sep = markdown ? " | " : "  ";
    const char* edge = markdown ? "| " : "";
    const char* edgeEnd = markdown ? " |" : "";
    out << edge << "Operation (ms, median / min)";
    for (const SizeResults& r : results)
        out << sep << r.cells << " cells";
    out << edgeEnd << '\n';
    if (markdown) {
        out << "|---|";
        for (std::size_t i = 0; i < results.size(); ++i)
            out << "---:|";
        out << '\n';
    }
    for (std::size_t row = 0; row < results.front().measurements.size(); ++row) {
        out << edge << results.front().measurements[row].name;
        for (const SizeResults& r : results) {
            out << sep;
            if (row >= r.measurements.size())
                out << "-";
            else if (!r.measurements[row].ok)
                out << "FAILED";
            else if (r.measurements[row].runsMs.empty())
                out << "-";
            else
                out << formatMs(r.measurements[row].medianMs()) << " / " << formatMs(r.measurements[row].minMs());
        }
        out << edgeEnd << '\n';
    }
}

} // namespace

int main(int argc, char** argv)
{
    // The paint benchmark is a 1600x1000 viewport at 100 % and a device pixel ratio of 1 (BENCHMARKS.md),
    // and its work-counter ceilings are for that: QT_SCALE_FACTOR=2 in the environment would make the
    // viewport four times as many device pixels and tiles. (QT_FONT_DPI changes the ratio of the offscreen
    // platform only, by at most 1.5 in practice; that stays within the ceilings.)
    qputenv("QT_SCALE_FACTOR", "1");
    QApplication app(argc, argv);
    Arguments args;
    if (!parseArguments(app.arguments(), args)) {
        printUsage(std::cerr);
        return kExitUsage;
    }

    if (!isReleaseBuild()) {
        std::cout << "WARNING: build type is '" << QRAW_BENCH_BUILD_TYPE
                  << "', not Release. These timings are not representative; use `cmake --preset release`.\n";
        std::cerr << "WARNING: not a Release build, the timings are not representative.\n";
    }

    if (args.paintMode)
        return runPaint(args);

    std::vector<SizeResults> results;
    bool failed = false;
    for (const std::size_t cells : args.sizes) {
        const QByteArray bytes = generateDocument(cells);
        if (bytes.isEmpty()) {
            std::cerr << "qraw-bench: could not generate a document of " << cells << " cells\n";
            return kExitFailure;
        }
        if (!args.writeDir.isEmpty()) {
            QDir().mkpath(args.writeDir);
            QFile file(QDir(args.writeDir).filePath(QStringLiteral("bench-%1.drawio").arg(cells)));
            if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size()) {
                std::cerr << "qraw-bench: cannot write " << file.fileName().toStdString() << '\n';
                return kExitFailure;
            }
        }
        std::cerr << cells << " cells (" << bytes.size() / 1024 << " KiB)\n";
        Options options = args.options;
        options.progress = &std::cerr;
        SizeResults size{cells, runOperations(bytes, options)};
        for (const Measurement& m : size.measurements)
            failed = failed || !m.ok;
        results.push_back(std::move(size));
    }

    std::cout << (args.markdown ? "### Machine\n\n" : "Machine\n");
    printMachineBlock(std::cout, args.markdown);
    std::cout << "\nRuns per operation: " << args.options.repeat
              << (args.options.withScene ? ", canvas scene alive during the edit operations" : "") << "\n\n";
    if (!results.front().measurements.empty()) {
        printTable(std::cout, results, args.markdown);
        std::cout << '\n';
        for (const SizeResults& r : results) {
            for (const Measurement& m : r.measurements) {
                if (!m.detail.isEmpty())
                    std::cout << r.cells << " cells, " << m.name << ": " << m.detail.toStdString() << '\n';
            }
        }
    }
    return failed ? kExitFailure : 0;
}
