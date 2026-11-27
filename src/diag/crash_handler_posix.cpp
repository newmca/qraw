// Crash handler for POSIX systems (Linux, macOS, the BSDs). The signal handler runs on an alternate
// stack and calls only open(2), write(2), close(2), time(2), getpid(2), sigaction(2), raise(3) and,
// primed at install time, backtrace()/backtrace_symbols_fd() from <execinfo.h> where it exists
// (glibc, macOS, the BSDs; not musl). Best effort by nature: a crash that corrupted the heap or the
// stack of the faulting thread can still stop the report half way.
#include "crash_internal.h"

#include "qraw/diag/async_safe_text.h"
#include "qraw/diag/crash_handler.h"
#include "qraw/diag/logging.h"

#include <QByteArray>
#include <QDir>

#include <array>
#include <cerrno>
#include <csignal>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <exception>
#include <string>

#include <fcntl.h>
#include <signal.h>
#include <unistd.h>

#if __has_include(<execinfo.h>)
#include <execinfo.h>
#define QRAW_HAS_BACKTRACE 1
#endif

namespace qraw::diag {

namespace {

constexpr std::array<int, 5> kFatalSignals{SIGSEGV, SIGBUS, SIGFPE, SIGILL, SIGABRT};
constexpr std::size_t kPathCapacity = 1280;
constexpr std::size_t kHeaderCapacity = 2048;
constexpr std::size_t kAltStackSize = 128 * 1024;
constexpr int kMaxFrames = 64;

// Everything the handler touches, prepared by installCrashHandler(). Static storage: no allocation
// and no Qt call is needed when the time comes.
struct State {
    bool installed = false;
    char directory[kPathCapacity - 64] = {};
    char header[kHeaderCapacity] = {};
    std::size_t headerLength = 0;
    alignas(16) char alternateStack[kAltStackSize] = {};
    volatile sig_atomic_t handling = 0;       // a fatal signal is being reported
    volatile sig_atomic_t reportWritten = 0;  // std::terminate already wrote it: SIGABRT only re-raises
    struct sigaction previous[kFatalSignals.size()] = {};
    std::terminate_handler previousTerminate = nullptr;
    stack_t previousStack = {};
};

State& state()
{
    static State instance;
    return instance;
}

void writeAll(int fd, const char* data, std::size_t length)
{
    while (length > 0) {
        const ssize_t written = ::write(fd, data, length);
        if (written < 0) {
            if (errno == EINTR)
                continue;
            return;
        }
        data += written;
        length -= static_cast<std::size_t>(written);
    }
}

template <std::size_t N>
void writeText(int fd, const FixedText<N>& text)
{
    writeAll(fd, text.c_str(), text.size());
}

// Writes the whole report; `reason` is the line that says what happened. Returns after the file is
// closed. Callable from the signal handler and from the terminate handler.
void writeReport(const char* reason, int signalNumber, const void* faultAddress)
{
    State& s = state();
    const std::int64_t now = static_cast<std::int64_t>(::time(nullptr));

    FixedText<kPathCapacity> path;
    path.append(s.directory).append("/crash-");
    appendUtcTime(path, now, true);
    path.append('-').appendDecimal(static_cast<std::uint64_t>(::getpid())).append(".txt");
    const int opened = ::open(path.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0600);
    const int fd = opened >= 0 ? opened : STDERR_FILENO; // no folder: the terminal at least

    writeAll(fd, s.header, s.headerLength);
    FixedText<512> body;
    body.append("Reason: ").append(reason);
    if (signalNumber != 0)
        body.append(" (signal ").appendDecimal(static_cast<std::uint64_t>(signalNumber)).append(", ")
            .append(fatalSignalName(signalNumber)).append(")");
    body.append('\n');
    if (faultAddress != nullptr || signalNumber == SIGSEGV || signalNumber == SIGBUS)
        body.append("Fault address: ").appendHex(reinterpret_cast<std::uintptr_t>(faultAddress)).append('\n');
    body.append("Time: ");
    appendUtcTime(body, now, false);
    body.append("\nProcess id: ").appendDecimal(static_cast<std::uint64_t>(::getpid())).append('\n');
    writeText(fd, body);

#ifdef QRAW_HAS_BACKTRACE
    // Innermost first. The first frames are the handler itself and the signal trampoline. The offsets
    // are relative to each module: addr2line -f -C -e <module> <offset> names the function when the
    // report comes from the build named in the header.
    void* frames[kMaxFrames];
    const int count = ::backtrace(frames, kMaxFrames);
    static constexpr char kBacktraceHeader[] = "Backtrace (innermost frame first):\n";
    writeAll(fd, kBacktraceHeader, sizeof kBacktraceHeader - 1);
    ::backtrace_symbols_fd(frames, count, fd);
#else
    static constexpr char kNoBacktrace[] = "Backtrace: not available on this platform\n";
    writeAll(fd, kNoBacktrace, sizeof kNoBacktrace - 1);
#endif
    if (opened >= 0)
        ::close(fd);

    FixedText<kPathCapacity + 128> note;
    note.append("Qraw crashed: ").append(reason).append(". Report: ").append(opened >= 0 ? path.c_str() : "(none)")
        .append('\n');
    writeText(STDERR_FILENO, note);
    s.reportWritten = 1;
}

void onFatalSignal(int signalNumber, siginfo_t* info, void*)
{
    State& s = state();
    if (s.handling == 0) {
        s.handling = 1;
        if (s.reportWritten == 0)
            writeReport("fatal signal", signalNumber, info != nullptr ? info->si_addr : nullptr);
    }
    // Back to the default action and again: the process dies from this signal, with a core dump where
    // the system makes one. (SA_RESETHAND did this already; it is repeated for a second, nested
    // signal.)
    ::signal(signalNumber, SIG_DFL);
    ::raise(signalNumber);
}

void onTerminate()
{
    State& s = state();
    if (s.reportWritten == 0 && s.handling == 0) {
        s.handling = 1;
        const std::string reason = detail::describeCurrentException();
        writeReport(reason.c_str(), 0, nullptr);
    }
    if (s.previousTerminate != nullptr && s.previousTerminate != &onTerminate)
        s.previousTerminate();
    std::abort(); // SIGABRT: the handler above sees reportWritten and only re-raises
}

} // namespace

bool installCrashHandler(const CrashHandlerOptions& options)
{
    State& s = state();
    if (s.installed)
        return true;
    if (!QDir().mkpath(options.directory))
        return false;
    const QByteArray directory = QDir::toNativeSeparators(options.directory).toLocal8Bit();
    const QByteArray header = crashReportHeader(options).toUtf8();
    if (static_cast<std::size_t>(directory.size()) >= sizeof s.directory
        || static_cast<std::size_t>(header.size()) >= sizeof s.header)
        return false;
    std::memcpy(s.directory, directory.constData(), static_cast<std::size_t>(directory.size()));
    std::memcpy(s.header, header.constData(), static_cast<std::size_t>(header.size()));
    s.headerLength = static_cast<std::size_t>(header.size());

#ifdef QRAW_HAS_BACKTRACE
    // The first backtrace() call loads libgcc: do it now, not while crashing.
    void* warmUp[4];
    ::backtrace(warmUp, 4);
#endif

    stack_t stack = {};
    stack.ss_sp = s.alternateStack;
    stack.ss_size = sizeof s.alternateStack;
    ::sigaltstack(&stack, &s.previousStack); // this thread; a fault on another thread runs on its own stack

    struct sigaction action = {};
    action.sa_sigaction = &onFatalSignal;
    sigemptyset(&action.sa_mask);
    action.sa_flags = SA_SIGINFO | SA_ONSTACK | SA_RESETHAND | SA_NODEFER;
    for (std::size_t i = 0; i < kFatalSignals.size(); ++i)
        ::sigaction(kFatalSignals[i], &action, &s.previous[i]);
    s.previousTerminate = std::set_terminate(&onTerminate);
    s.installed = true;
    qCDebug(lcAppCrash) << "crash reports go to" << options.directory;
    return true;
}

void uninstallCrashHandler()
{
    State& s = state();
    if (!s.installed)
        return;
    for (std::size_t i = 0; i < kFatalSignals.size(); ++i)
        ::sigaction(kFatalSignals[i], &s.previous[i], nullptr);
    std::set_terminate(s.previousTerminate);
    s.installed = false;
    s.handling = 0;
    s.reportWritten = 0;
}

} // namespace qraw::diag
