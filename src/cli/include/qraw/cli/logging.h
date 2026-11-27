#pragma once
// Logging category of qraw::cli (also linked into the qraw binary for its command-line flags).
// QT_LOGGING_RULES="qraw.*.debug=true" switches debug output on (BUILDING.md, "Logging"). The
// commands' own results and errors are not log messages: they go to stdout and stderr as documented.

#include <QLoggingCategory>

namespace qraw::cli {

Q_DECLARE_LOGGING_CATEGORY(lcCli) // qraw.cli

} // namespace qraw::cli
