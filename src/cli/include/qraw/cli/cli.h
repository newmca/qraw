#pragma once

#include <QStringList>

class QTextStream;

namespace qraw::cli {

enum ExitCode : int {
    Success = 0,
    Failure = 1, // the document could not be read/written or has errors
    Usage = 2,   // bad command line or unavailable feature
};

// True when the arguments ask for a headless command (--validate, --convert,
// --info, --export, --help, --version). Used by the GUI binary to dispatch.
[[nodiscard]] bool isCliInvocation(const QStringList& arguments);

// True when the command needs a QGuiApplication (fonts, QPainter): --export.
// Callers create one (with the offscreen platform) instead of a
// QCoreApplication.
[[nodiscard]] bool requiresGuiApplication(const QStringList& arguments);

// Runs a headless command. `arguments` includes the program name first.
// Requires a QCoreApplication instance (a QGuiApplication for --export).
int run(const QStringList& arguments, QTextStream& out, QTextStream& err);

} // namespace qraw::cli
