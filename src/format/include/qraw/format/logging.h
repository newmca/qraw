#pragma once
// Logging categories of qraw::format: reading and writing files
// QT_LOGGING_RULES="qraw.*.debug=true" switches the debug output of every module on (BUILDING.md,
// "Logging"); informational messages and above are on by default.

#include <QLoggingCategory>

namespace qraw::format {

Q_DECLARE_LOGGING_CATEGORY(lcFormat) // qraw.format

} // namespace qraw::format
