#pragma once
// Crash reports (D-049). A plain signal handler, no third-party library and no KCrash: when the
// process dies from SIGSEGV, SIGBUS, SIGFPE, SIGILL or SIGABRT (POSIX) or an unhandled exception
// (Windows), or from an uncaught C++ exception (std::terminate), it writes a text file
//   <crashDirectory>/crash-<UTC time>-<pid>.txt
// with the signal, version, git commit, Qt version, operating system and a backtrace, restores the
// default action and re-raises, so that the system's own handler (systemd-coredump, DrKonqi, ABRT,
// Windows Error Reporting, CrashReporter) still gets the core dump.
//
// The report contains no document content. The handler only does what POSIX allows in a signal
// handler: everything that needs memory or Qt (the header text, the path of the folder) is prepared
// by installCrashHandler() before any crash.

#include <QString>

namespace qraw::diag {

// Which program wrote a report: only the GUI's reports make the next GUI start say "Qraw closed
// unexpectedly".
enum class ProcessKind { Gui, Cli };

struct CrashHandlerOptions {
    CrashHandlerOptions(QString reportDirectory, ProcessKind processKind, QString platform = {})
        : directory(std::move(reportDirectory)), kind(processKind), platformName(std::move(platform))
    {
    }

    QString directory;    // where the reports go (created if missing)
    ProcessKind kind;
    QString platformName; // the Qt platform plugin ("xcb", "wayland", ...), "" for none
};

// Installs the handlers (once; later calls are ignored while installed). Call it after the
// QCoreApplication exists and its name is set. False when the report folder cannot be created or
// the platform has no handler.
bool installCrashHandler(const CrashHandlerOptions& options);

// Restores the handlers that were active before. For tests; the application never needs it.
void uninstallCrashHandler();

// The first lines of every report ("Qraw crash report", "Process: gui|cli") are a contract with
// the code that finds reports of the last run (session_marker.h).
inline constexpr char kCrashReportTitle[] = "Qraw crash report";
inline constexpr char kCrashReportGuiLine[] = "Process: gui";

// The part of the report that does not change while the program runs: title, process kind,
// version, commit, Qt, operating system, platform plugin, start time. Public for tests.
[[nodiscard]] QString crashReportHeader(const CrashHandlerOptions& options);

} // namespace qraw::diag
