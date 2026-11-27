#include "qraw/diag/debug_crash.h"

#include "qraw/diag/logging.h"

#include <QByteArray>
#include <QtGlobal>

#include <cstdlib>
#include <stdexcept>

namespace qraw::diag {

std::optional<DebugCrash> parseDebugCrash(std::string_view name)
{
    if (name == "segv")
        return DebugCrash::Segv;
    if (name == "abort")
        return DebugCrash::Abort;
    if (name == "exception")
        return DebugCrash::Exception;
    return std::nullopt;
}

std::optional<DebugCrash> debugCrashFromEnvironment()
{
    const QByteArray value = qgetenv("QRAW_DEBUG_CRASH");
    if (value.isEmpty())
        return std::nullopt;
    const auto kind = parseDebugCrash(std::string_view(value.constData(), static_cast<std::size_t>(value.size())));
    if (!kind)
        qCWarning(lcAppCrash) << "QRAW_DEBUG_CRASH=" << value << "is not one of segv, abort, exception";
    return kind;
}

void triggerDebugCrash(DebugCrash kind)
{
    qCWarning(lcAppCrash) << "QRAW_DEBUG_CRASH: crashing on purpose";
    switch (kind) {
    case DebugCrash::Segv: {
        // The pointer is volatile so that the compiler cannot see the null and turn the store into a trap.
        static volatile int* volatile target = nullptr;
        *target = 1;
        break;
    }
    case DebugCrash::Abort:
        std::abort();
    case DebugCrash::Exception:
        throw std::runtime_error("QRAW_DEBUG_CRASH=exception");
    }
    std::abort(); // a store that did not fault
}

} // namespace qraw::diag
