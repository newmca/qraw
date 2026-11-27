#pragma once

#include <string>

namespace qraw::core {

// A problem found while reading or validating a document. Only `Error`
// means the document could not be (fully) understood; warnings never stop
// loading.
struct Diagnostic {
    enum class Severity { Info, Warning, Error };

    Severity severity = Severity::Warning;
    std::string message;
    std::string pageId; // empty when not page-specific
    std::string cellId; // empty when not cell-specific
    int line = 0;       // 1-based; 0 when unknown
    int column = 0;
};

[[nodiscard]] const char* severityName(Diagnostic::Severity severity);

} // namespace qraw::core
