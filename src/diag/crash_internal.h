#pragma once
// Shared by the platform files of the crash handler (not public).

#include <string>

namespace qraw::diag::detail {

// "std::runtime_error: message" for the exception being handled by std::terminate, or a note when
// there is none. Allocates: only for std::terminate, never inside a signal handler.
[[nodiscard]] std::string describeCurrentException();

} // namespace qraw::diag::detail
