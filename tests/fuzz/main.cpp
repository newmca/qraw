// qraw-fuzz: a deterministic mutation fuzzer for the five parsers (D-028).
// See BUILDING.md "Fuzzing". Same --seed, same inputs, on every platform.

#include "mutator.h"
#include "seeds.h"
#include "targets.h"

#include <QCoreApplication>
#include <QDir>
#include <QGuiApplication>
#include <QProcess>
#include <QString>
#include <QTemporaryFile>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <thread>

#ifdef _WIN32
#include <io.h>
#define QRAW_FUZZ_WRITE(fd, data, length) _write(fd, data, static_cast<unsigned>(length))
#else
#include <unistd.h>
#define QRAW_FUZZ_WRITE(fd, data, length) ::write(fd, data, length)
#endif

using namespace qraw::fuzz;
namespace fs = std::filesystem;

namespace {

constexpr int kExitFailure = 1;
constexpr int kExitUsage = 2;
constexpr int kExitHang = 3;
constexpr int kMaxFailuresPerTarget = 5;

// ----------------------------------------------- crash and hang capture
//
// The current iteration lives in plain globals; the handlers only write them
// to stderr (and the input to the crash directory), so a run does no I/O per
// iteration. `--dump TARGET:ITERATION` regenerates the input of a crash.

const char* volatile g_targetName = "";
volatile std::uint64_t g_iteration = 0;
std::uint64_t g_seed = 1;
char g_label[512] = ""; // replay: the file being run
const char* volatile g_inputData = nullptr;
volatile std::size_t g_inputSize = 0;
char g_crashDir[512] = "";
std::atomic<std::int64_t> g_iterationStartMs{0}; // 0: no iteration running
std::atomic<std::int64_t> g_hangMs{5000};

std::int64_t nowMs()
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count() + 1;
}

void writeErr(const char* text)
{
    (void)QRAW_FUZZ_WRITE(2, text, std::strlen(text));
}

// Decimal digits of `value` in `out` (at least 21 bytes). Signal-safe: no allocation.
void formatNumber(std::uint64_t value, char* out)
{
    char digits[24];
    int n = 0;
    do {
        digits[n++] = static_cast<char>('0' + value % 10);
        value /= 10;
    } while (value && n < 23);
    for (int i = 0; i < n; ++i)
        out[i] = digits[n - 1 - i];
    out[n] = '\0';
}

void writeErrNumber(std::uint64_t value)
{
    char text[24];
    formatNumber(value, text);
    writeErr(text);
}

void appendText(char* buffer, std::size_t capacity, const char* text)
{
    const std::size_t used = std::strlen(buffer);
    std::strncat(buffer, text, capacity - used - 1);
}

void appendNumber(char* buffer, std::size_t capacity, std::uint64_t value)
{
    char text[24];
    formatNumber(value, text);
    appendText(buffer, capacity, text);
}

// Saves the input being run to <crash-dir>/<target>-s<seed>-i<iteration>.bin.
void saveCrashInput(const char* kind)
{
    if (!g_crashDir[0] || !g_inputData)
        return;
    char path[1024] = "";
    appendText(path, sizeof path, g_crashDir);
    appendText(path, sizeof path, "/");
    appendText(path, sizeof path, kind);
    appendText(path, sizeof path, "-");
    appendText(path, sizeof path, g_targetName);
    appendText(path, sizeof path, "-s");
    appendNumber(path, sizeof path, g_seed);
    appendText(path, sizeof path, "-i");
    appendNumber(path, sizeof path, g_iteration);
    appendText(path, sizeof path, ".bin");
    if (std::FILE* file = std::fopen(path, "wb")) {
        std::fwrite(const_cast<const char*>(g_inputData), 1, g_inputSize, file);
        std::fclose(file);
        writeErr("  input saved to ");
        writeErr(path);
        writeErr("\n");
    }
}

void reportAbnormalEnd(const char* what, int signalNumber)
{
    writeErr("FUZZ-");
    writeErr(what);
    writeErr(" target=");
    writeErr(g_targetName);
    writeErr(" seed=");
    writeErrNumber(g_seed);
    writeErr(" iteration=");
    writeErrNumber(g_iteration);
    if (signalNumber) {
        writeErr(" signal=");
        writeErrNumber(static_cast<std::uint64_t>(signalNumber));
    }
    if (g_label[0]) {
        writeErr(" file=");
        writeErr(g_label);
    }
    writeErr("\n  reproduce: qraw-fuzz --target ");
    writeErr(g_targetName);
    writeErr(" --seed ");
    writeErrNumber(g_seed);
    writeErr(" --dump ");
    writeErr(g_targetName);
    writeErr(":");
    writeErrNumber(g_iteration);
    writeErr(" --out crash.bin\n");
    saveCrashInput(what);
}

extern "C" void onFatalSignal(int signalNumber)
{
    reportAbnormalEnd("CRASH", signalNumber);
    std::signal(signalNumber, SIG_DFL);
    std::raise(signalNumber);
}

void installSignalHandlers()
{
#ifdef _WIN32
    for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL})
        std::signal(sig, onFatalSignal);
#else
    // A stack overflow (deep recursion) needs an alternate stack to be reported.
    static char alternateStack[1 << 16];
    stack_t stack{};
    stack.ss_sp = alternateStack;
    stack.ss_size = sizeof alternateStack;
    sigaltstack(&stack, nullptr);
    struct sigaction action {};
    action.sa_handler = onFatalSignal;
    action.sa_flags = SA_ONSTACK;
    sigemptyset(&action.sa_mask);
    for (const int sig : {SIGSEGV, SIGABRT, SIGFPE, SIGILL, SIGBUS})
        sigaction(sig, &action, nullptr);
#endif
}

void startWatchdog()
{
    std::thread([] {
        while (true) {
            std::this_thread::sleep_for(std::chrono::milliseconds(50));
            const std::int64_t started = g_iterationStartMs.load();
            if (started != 0 && nowMs() - started > g_hangMs.load()) {
                reportAbnormalEnd("HANG", 0);
                std::_Exit(kExitHang);
            }
        }
    }).detach();
}

// --------------------------------------------------------------- options

struct Options {
    std::string target = "all";
    std::uint64_t seed = 1;
    std::uint64_t iterations = 0; // 0: not given
    double seconds = 0;
    std::size_t maxLength = 64 * 1024;
    std::int64_t hangMs = 5000;
    std::string replayDir, dump, out, minimizeFile, checkFile, crashDir;
};

[[noreturn]] void usage(const std::string& problem)
{
    if (!problem.empty())
        std::cerr << "qraw-fuzz: " << problem << "\n\n";
    std::cerr << "usage: qraw-fuzz [--target codec|compression|style|stencil|html|all] [--seed N]\n"
                 "                   [--iterations N | --seconds S] [--max-len BYTES] [--hang-ms MS] [--crash-dir DIR]\n"
                 "       qraw-fuzz --replay DIR [--target T]          run every file of DIR/<target>/ once\n"
                 "       qraw-fuzz --dump TARGET:ITERATION --out FILE  regenerate the input of an iteration (use the same --seed)\n"
                 "       qraw-fuzz --minimize FILE --target T [--out FILE]  shrink a failing input\n"
                 "       qraw-fuzz --check FILE --target T            run one file (exit 0 pass, 1 invariant failure)\n";
    std::exit(kExitUsage);
}

Options parseOptions(int argc, char** argv)
{
    Options o;
    const auto value = [&](int& i) -> std::string {
        if (i + 1 >= argc)
            usage(std::string("missing value for ") + argv[i]);
        return argv[++i];
    };
    const auto number = [&](const std::string& text) -> std::uint64_t {
        try {
            return std::stoull(text);
        } catch (const std::exception&) {
            usage("not a number: " + text);
        }
    };
    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];
        if (arg == "--target") o.target = value(i);
        else if (arg == "--seed") o.seed = number(value(i));
        else if (arg == "--iterations") o.iterations = number(value(i));
        else if (arg == "--seconds") o.seconds = static_cast<double>(number(value(i)));
        else if (arg == "--max-len") o.maxLength = static_cast<std::size_t>(number(value(i)));
        else if (arg == "--hang-ms") o.hangMs = static_cast<std::int64_t>(number(value(i)));
        else if (arg == "--replay") o.replayDir = value(i);
        else if (arg == "--dump") o.dump = value(i);
        else if (arg == "--out") o.out = value(i);
        else if (arg == "--minimize") o.minimizeFile = value(i);
        else if (arg == "--check") o.checkFile = value(i);
        else if (arg == "--crash-dir") o.crashDir = value(i);
        else usage("unknown option " + arg);
    }
    return o;
}

std::string readFile(const fs::path& path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream out;
    out << in.rdbuf();
    return out.str();
}

void writeFile(const fs::path& path, std::string_view bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

// ----------------------------------------------------------------- running

// One run of a target, with the globals the handlers report.
std::string runGuarded(const Target& target, std::string_view input, std::uint64_t iteration)
{
    g_targetName = target.name.c_str();
    g_iteration = iteration;
    g_inputData = input.data();
    g_inputSize = input.size();
    g_iterationStartMs = nowMs();
    std::string failure;
    try {
        failure = target.run(input);
    } catch (const std::exception& e) {
        failure = std::string("uncaught exception: ") + e.what();
    } catch (...) {
        failure = "uncaught exception";
    }
    g_iterationStartMs = 0;
    g_inputData = nullptr;
    return failure;
}

const Target* findTarget(const std::vector<Target>& targets, const std::string& name)
{
    for (const Target& target : targets)
        if (target.name == name)
            return &target;
    return nullptr;
}

std::vector<const Target*> selectTargets(const std::vector<Target>& targets, const std::string& name)
{
    std::vector<const Target*> selected;
    if (name == "all") {
        for (const Target& target : targets)
            selected.push_back(&target);
    } else if (const Target* target = findTarget(targets, name)) {
        selected.push_back(target);
    } else {
        usage("unknown target " + name);
    }
    return selected;
}

void reportFailure(const Options& o, const Target& target, std::uint64_t iteration, const std::string& failure, std::string_view input)
{
    std::cerr << "FUZZ-FAIL target=" << target.name << " seed=" << o.seed << " iteration=" << iteration << ": " << failure << '\n';
    if (!o.crashDir.empty()) {
        std::error_code error;
        fs::create_directories(o.crashDir, error);
        const fs::path path = fs::path(o.crashDir) / ("FAIL-" + target.name + "-s" + std::to_string(o.seed) + "-i" + std::to_string(iteration) + ".bin");
        writeFile(path, input);
        std::cerr << "  input saved to " << path.string() << '\n';
    }
}

int fuzz(const Options& o, const std::vector<Target>& targets, const SeedSets& seeds)
{
    std::printf("seeds: documents=%zu compressed=%zu inflated=%zu styles=%zu html=%zu stencils=%zu inline=%zu packed=%zu\n",
                seeds.documents.size(), seeds.compressedPayloads.size(), seeds.inflatedModels.size(), seeds.styles.size(),
                seeds.htmlLabels.size(), seeds.stencilShapes.size(), seeds.inlineStencils.size(), seeds.packedStencilSets.size());
    int failures = 0;
    const std::uint64_t iterationLimit = o.iterations ? o.iterations : (o.seconds > 0 ? ~0ULL : 1000);
    for (const Target* target : selectTargets(targets, o.target)) {
        const auto start = std::chrono::steady_clock::now();
        std::uint64_t done = 0;
        int targetFailures = 0;
        for (std::uint64_t i = 0; i < iterationLimit; ++i) {
            if (o.seconds > 0 && std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count() >= o.seconds)
                break;
            Rng rng = iterationRng(o.seed, target->name, i);
            const std::string input = target->generate(rng);
            ++done;
            if (const std::string failure = runGuarded(*target, input, i); !failure.empty()) {
                reportFailure(o, *target, i, failure, input);
                ++failures;
                if (++targetFailures >= kMaxFailuresPerTarget)
                    break;
            }
        }
        const double elapsed = std::chrono::duration<double>(std::chrono::steady_clock::now() - start).count();
        std::printf("%-12s seed=%llu iterations=%llu failures=%d time=%.1fs\n", target->name.c_str(),
                    static_cast<unsigned long long>(o.seed), static_cast<unsigned long long>(done), targetFailures, elapsed);
        std::fflush(stdout);
    }
    return failures ? kExitFailure : 0;
}

int replay(const Options& o, const std::vector<Target>& targets)
{
    int failures = 0;
    int files = 0;
    for (const Target* target : selectTargets(targets, o.target)) {
        std::vector<fs::path> inputs;
        std::error_code error;
        for (fs::directory_iterator it(fs::path(o.replayDir) / target->name, error), end; !error && it != end; it.increment(error))
            if (it->is_regular_file() && it->path().filename() != "README.md")
                inputs.push_back(it->path());
        std::sort(inputs.begin(), inputs.end());
        for (const fs::path& path : inputs) {
            const std::string input = readFile(path);
            std::snprintf(g_label, sizeof g_label, "%s", path.string().c_str());
            ++files;
            if (const std::string failure = runGuarded(*target, input, 0); !failure.empty()) {
                std::cerr << "FUZZ-FAIL target=" << target->name << " file=" << path.string() << ": " << failure << '\n';
                ++failures;
            }
        }
        g_label[0] = '\0';
    }
    std::printf("replayed %d files, %d failures\n", files, failures);
    return failures ? kExitFailure : 0;
}

int dump(const Options& o, const std::vector<Target>& targets)
{
    const auto colon = o.dump.find(':');
    if (colon == std::string::npos || o.out.empty())
        usage("--dump needs TARGET:ITERATION and --out FILE");
    const Target* target = findTarget(targets, o.dump.substr(0, colon));
    if (!target)
        usage("unknown target in --dump");
    Rng rng = iterationRng(o.seed, target->name, std::stoull(o.dump.substr(colon + 1)));
    writeFile(o.out, target->generate(rng));
    return 0;
}

int check(const Options& o, const std::vector<Target>& targets)
{
    const Target* target = findTarget(targets, o.target);
    if (!target)
        usage("--check needs --target");
    const std::string failure = runGuarded(*target, readFile(o.checkFile), 0);
    if (!failure.empty())
        std::cerr << failure << '\n';
    return failure.empty() ? 0 : kExitFailure;
}

// Delta minimization. A candidate counts when it fails the way the original
// did. Invariant failures are checked in this process (fast); a crash or a
// hang takes the process down, so those candidates run in a child process
// (--check) that is allowed to die.
int minimize(const Options& o, const std::vector<Target>& targets, const std::string& self)
{
    const Target* target = findTarget(targets, o.target);
    if (!target)
        usage("--minimize needs a single --target");
    std::string data = readFile(o.minimizeFile);
    QTemporaryFile candidateFile(QDir::temp().filePath("qraw-fuzz-minimize-XXXXXX"));
    if (!candidateFile.open())
        usage("cannot create a temporary file");
    const auto childStatus = [&](const std::string& candidate) {
        candidateFile.resize(0);
        candidateFile.write(candidate.data(), static_cast<qint64>(candidate.size()));
        candidateFile.flush();
        QProcess process;
        process.setStandardOutputFile(QProcess::nullDevice());
        process.setStandardErrorFile(QProcess::nullDevice());
        process.start(QString::fromStdString(self), {"--check", candidateFile.fileName(), "--target", QString::fromStdString(o.target),
                                                     "--hang-ms", QString::number(o.hangMs)});
        if (!process.waitForFinished(static_cast<int>(o.hangMs) * 2 + 20000)) {
            process.kill();
            process.waitForFinished();
            return -3;
        }
        return process.exitStatus() == QProcess::CrashExit ? -2 : process.exitCode();
    };
    const int original = childStatus(data);
    if (original == 0) {
        std::cerr << "the input passes; nothing to minimize\n";
        return kExitFailure;
    }
    const bool inProcess = original == kExitFailure;
    std::cerr << "original ends with status " << original << ", " << data.size() << " bytes"
              << (inProcess ? " (invariant failure: minimizing in process)" : " (crash or hang: one child process per candidate)") << '\n';
    const auto stillFails = [&](const std::string& candidate) {
        return inProcess ? !runGuarded(*target, candidate, 0).empty() : childStatus(candidate) == original;
    };
    for (int pass = 0; pass < 3; ++pass) {
        bool changed = false;
        for (std::size_t chunk = std::max<std::size_t>(data.size() / 2, 1); chunk >= 1; chunk /= 2) {
            for (std::size_t pos = 0; pos < data.size();) {
                std::string candidate = data;
                candidate.erase(pos, chunk);
                if (stillFails(candidate)) {
                    data = std::move(candidate);
                    changed = true;
                } else {
                    pos += chunk;
                }
            }
        }
        std::cerr << "pass " << pass + 1 << ": " << data.size() << " bytes\n";
        if (!changed)
            break;
    }
    const std::string out = o.out.empty() ? o.minimizeFile + ".min" : o.out;
    writeFile(out, data);
    std::cout << "minimized to " << data.size() << " bytes: " << out << '\n';
    return 0;
}

} // namespace

int main(int argc, char** argv)
{
    const Options options = parseOptions(argc, argv);
    // Painting needs a platform plugin; nobody wants a window here.
    if (!qEnvironmentVariableIsSet("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);

    g_seed = options.seed;
    g_hangMs = options.hangMs;
    std::snprintf(g_crashDir, sizeof g_crashDir, "%s", options.crashDir.c_str());
    if (!options.crashDir.empty()) {
        std::error_code error;
        fs::create_directories(options.crashDir, error);
    }
    installSignalHandlers();
    startWatchdog();

    // Seeds are only needed to generate inputs.
    const bool generating = options.replayDir.empty() && options.checkFile.empty() && options.minimizeFile.empty();
    auto seeds = std::make_shared<SeedSets>();
    if (generating)
        *seeds = collectSeeds(QRAW_SOURCE_DIR, options.maxLength);
    const std::vector<Target> targets = makeTargets(seeds, options.maxLength);

    if (!options.minimizeFile.empty())
        return minimize(options, targets, QCoreApplication::applicationFilePath().toStdString());
    if (!options.checkFile.empty())
        return check(options, targets);
    if (!options.replayDir.empty())
        return replay(options, targets);
    if (!options.dump.empty())
        return dump(options, targets);
    return fuzz(options, targets, *seeds);
}
