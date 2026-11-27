#include "qraw/core/diagnostic.h"

namespace qraw::core {

const char* severityName(Diagnostic::Severity severity)
{
    switch (severity) {
    case Diagnostic::Severity::Info:
        return "info";
    case Diagnostic::Severity::Warning:
        return "warning";
    case Diagnostic::Severity::Error:
        return "error";
    }
    return "unknown";
}

} // namespace qraw::core
