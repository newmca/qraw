#pragma once
// A way to provoke a crash on purpose, for the tests and for anyone who wants to see a report: the
// environment variable QRAW_DEBUG_CRASH selects it (BUILDING.md, "Crash reports"). There is no
// menu entry or button.
//   segv       write through a null pointer (a real SIGSEGV)
//   abort      std::abort() (SIGABRT, as a failed assertion or qFatal does)
//   exception  throw a std::runtime_error that nothing catches (std::terminate)

#include <optional>
#include <string_view>

namespace qraw::diag {

enum class DebugCrash { Segv, Abort, Exception };

[[nodiscard]] std::optional<DebugCrash> parseDebugCrash(std::string_view name);
// The value of QRAW_DEBUG_CRASH; nothing when it is unset or not one of the names above.
[[nodiscard]] std::optional<DebugCrash> debugCrashFromEnvironment();
[[noreturn]] void triggerDebugCrash(DebugCrash kind);

} // namespace qraw::diag
