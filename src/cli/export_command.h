#pragma once

// `--export` (milestone M3). Private to qraw_cli; cli.cpp dispatches here
// before its own option parsing.

#include <QStringList>

class QTextStream;

namespace qraw::cli {

// Runs `--export IN --format svg|png|jpg|pdf --output OUT [--page N |
// --all-pages] [--scale S] [--transparent] [--dark] [--border N] [--embed]`.
// `arguments` includes the program name first. Returns an ExitCode.
// Requires a QGuiApplication (fonts); without one it fails with Usage.
int runExport(const QStringList& arguments, QTextStream& out, QTextStream& err);

} // namespace qraw::cli
