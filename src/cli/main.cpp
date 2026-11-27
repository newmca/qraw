#include "qraw/cli/cli.h"
#include "qraw/diag/crash_handler.h"
#include "qraw/diag/data_locations.h"
#include "qraw/diag/debug_crash.h"

#include <QCoreApplication>
#include <QGuiApplication>
#include <QTextStream>

#include <cstdio>
#include <memory>

int main(int argc, char* argv[])
{
    // Headless by default; rendering (M3) must not need a display.
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    // --export paints text, which needs a QGuiApplication; the other commands
    // only need QtCore.
    QStringList rawArguments;
    for (int i = 0; i < argc; ++i)
        rawArguments << QString::fromLocal8Bit(argv[i]);
    std::unique_ptr<QCoreApplication> app = qraw::cli::requiresGuiApplication(rawArguments)
        ? std::make_unique<QGuiApplication>(argc, argv)
        : std::make_unique<QCoreApplication>(argc, argv);
    QCoreApplication::setApplicationName(QStringLiteral("qraw-cli"));
    QCoreApplication::setApplicationVersion(QStringLiteral(QRAW_VERSION));
    // A crash leaves a report next to the GUI's (docs: BUILDING.md, "Crash reports").
    qraw::diag::installCrashHandler({qraw::diag::crashDirectory(), qraw::diag::ProcessKind::Cli});
    if (const auto crash = qraw::diag::debugCrashFromEnvironment())
        qraw::diag::triggerDebugCrash(*crash);

    QTextStream out(stdout);
    QTextStream err(stderr);
    return qraw::cli::run(QCoreApplication::arguments(), out, err);
}
