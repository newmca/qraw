#pragma once
// Logging categories of the application and the command-line tools. Every Qt-using module declares
// its own in its logging header; the names are qraw.<module>[.<sub>] so that
//   QT_LOGGING_RULES="qraw.*.debug=true"           everything of Qraw
//   QT_LOGGING_RULES="qraw.ui.session.debug=true"  one subsystem
// switch them (BUILDING.md, "Logging"). Informational messages and above are on by default and go
// to stderr and, in the GUI, to the log file.

#include <QLoggingCategory>

namespace qraw::diag {

Q_DECLARE_LOGGING_CATEGORY(lcApp)        // qraw.app: startup, shutdown, environment
Q_DECLARE_LOGGING_CATEGORY(lcAppLog)     // qraw.app.log: the log file itself
Q_DECLARE_LOGGING_CATEGORY(lcAppCrash)   // qraw.app.crash: crash handler, previous crashes
Q_DECLARE_LOGGING_CATEGORY(lcAppSession) // qraw.app.session: session markers

} // namespace qraw::diag
