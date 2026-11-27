// Parts of the crash handler that every platform shares.
#include "qraw/diag/async_safe_text.h"
#include "qraw/diag/build_info.h"
#include "qraw/diag/crash_handler.h"

#include <QDateTime>
#include <QCoreApplication>

#include <csignal>

namespace qraw::diag {

const char* fatalSignalName(int signalNumber)
{
    switch (signalNumber) {
    case SIGSEGV: return "SIGSEGV";
    case SIGFPE: return "SIGFPE";
    case SIGILL: return "SIGILL";
    case SIGABRT: return "SIGABRT";
#ifdef SIGBUS
    case SIGBUS: return "SIGBUS";
#endif
    default: return "signal";
    }
}

QString crashReportHeader(const CrashHandlerOptions& options)
{
    const ProcessKind kind = options.kind;
    QString text = QString::fromLatin1(kCrashReportTitle) + QLatin1Char('\n');
    text += QString::fromLatin1(kind == ProcessKind::Gui ? kCrashReportGuiLine : "Process: cli") + QLatin1Char('\n');
    text += QStringLiteral("Version: %1\n").arg(versionString());
    text += QStringLiteral("Git commit: %1\n").arg(gitCommit());
    text += QStringLiteral("Qt: %1\n").arg(qtVersionString());
    text += QStringLiteral("System: %1\n").arg(systemDescription());
    if (!options.platformName.isEmpty())
        text += QStringLiteral("Platform plugin: %1\n").arg(options.platformName);
    text += QStringLiteral("Started: %1\n").arg(QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    return text;
}

} // namespace qraw::diag

#include "crash_internal.h"

#include <cstdlib>
#include <exception>
#include <typeinfo>

#if __has_include(<cxxabi.h>)
#include <cxxabi.h>
#define QRAW_HAS_CXXABI 1
#endif

namespace qraw::diag::detail {

namespace {

// typeid().name() is mangled with GCC and Clang.
std::string readableTypeName(const std::type_info& type)
{
#ifdef QRAW_HAS_CXXABI
    int status = 0;
    char* demangled = abi::__cxa_demangle(type.name(), nullptr, nullptr, &status);
    if (status == 0 && demangled != nullptr) {
        std::string name = demangled;
        std::free(demangled);
        return name;
    }
    std::free(demangled);
#endif
    return type.name();
}

} // namespace

std::string describeCurrentException()
{
    const std::exception_ptr current = std::current_exception();
    if (!current)
        return "std::terminate called without an active exception";
    try {
        std::rethrow_exception(current);
    } catch (const std::exception& e) {
        return "uncaught exception " + readableTypeName(typeid(e)) + ": " + e.what();
    } catch (...) {
        return "uncaught exception of an unknown type";
    }
}

} // namespace qraw::diag::detail
