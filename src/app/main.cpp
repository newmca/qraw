#include "qraw/cli/cli.h"
#include "qraw/diag/build_info.h"
#include "qraw/diag/crash_handler.h"
#include "qraw/diag/data_locations.h"
#include "qraw/diag/debug_crash.h"
#include "qraw/diag/log_file.h"
#include "qraw/diag/logging.h"
#include "qraw/diag/session_marker.h"
#include "qraw/ui/app_settings.h"
#include "qraw/ui/app_setup.h"
#include "qraw/ui/file_open_handler.h"
#include "qraw/ui/main_window.h"

#include <QApplication>
#include <QCoreApplication>
#include <QGuiApplication>
#include <QTimer>
#include <QTextStream>

#include <algorithm>
#include <memory>

namespace {

QStringList rawArguments(int argc, char* argv[])
{
    QStringList args;
    for (int i = 0; i < argc; ++i)
        args << QString::fromLocal8Bit(argv[i]);
    return args;
}

void setIdentity()
{
    QCoreApplication::setApplicationName(QStringLiteral("Qraw"));
    QCoreApplication::setApplicationVersion(QStringLiteral(QRAW_VERSION));
    QCoreApplication::setOrganizationName(QStringLiteral("Qraw"));
}

// The positional arguments left after QApplication consumed Qt's own options
// (-platform ...): the files to open. Anything starting with '-' is an option
// nobody here handles and is skipped.
QStringList filesToOpen()
{
    QStringList files = QCoreApplication::arguments();
    files.removeFirst(); // program name
    files.erase(std::remove_if(files.begin(), files.end(), [](const QString& a) { return a.startsWith(u'-'); }),
                files.end());
    return files;
}

// What the GUI process keeps for its whole life: the log file, the crash handler and the session
// marker (a normal exit removes it, a crash leaves it behind; diag/session_marker.h).
class GuiDiagnostics {
public:
    explicit GuiDiagnostics(const qraw::ui::AppSettings& settings)
        : logFile_(qraw::diag::logFilePath()), marker_(qraw::diag::sessionDirectory())
    {
        using namespace qraw::diag;
        if (logFile_.open())
            installLogHandler(logFile_);
        installCrashHandler({crashDirectory(), ProcessKind::Gui, QGuiApplication::platformName()});
        qCInfo(lcApp).noquote() << QStringLiteral("Qraw %1 (%2), Qt %3, platform %4, %5")
                                       .arg(versionString(), gitCommit(), qtVersionString(),
                                            QGuiApplication::platformName(), systemDescription());
        qCInfo(lcApp).noquote() << "log file:" << logFile_.path();
        // Before this session's own marker exists: whatever markers are there belong to others.
        previous_ = collectPreviousRun(sessionDirectory(), crashDirectory(), settings.crashReportsSeenUntil());
        marker_.begin();
    }

    ~GuiDiagnostics() { qraw::diag::uninstallLogHandler(); }

    GuiDiagnostics(const GuiDiagnostics&) = delete;
    GuiDiagnostics& operator=(const GuiDiagnostics&) = delete;

    [[nodiscard]] const qraw::diag::PreviousRun& previousRun() const { return previous_; }

private:
    qraw::diag::LogFile logFile_;
    qraw::diag::SessionMarker marker_;
    qraw::diag::PreviousRun previous_;
};

} // namespace

int main(int argc, char* argv[])
{
    // One binary for both: CLI flags run headless without opening a window.
    const QStringList args = rawArguments(argc, argv);
    if (qraw::cli::isCliInvocation(args)) {
        // --export paints text, which needs a QGuiApplication (on the offscreen platform by default).
        if (qraw::cli::requiresGuiApplication(args) && qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
            qputenv("QT_QPA_PLATFORM", "offscreen");
        std::unique_ptr<QCoreApplication> app = qraw::cli::requiresGuiApplication(args)
            ? std::make_unique<QGuiApplication>(argc, argv)
            : std::make_unique<QCoreApplication>(argc, argv);
        setIdentity();
        qraw::diag::installCrashHandler({qraw::diag::crashDirectory(), qraw::diag::ProcessKind::Cli});
        if (const auto crash = qraw::diag::debugCrashFromEnvironment())
            qraw::diag::triggerDebugCrash(*crash);
        QTextStream out(stdout);
        QTextStream err(stderr);
        return qraw::cli::run(QCoreApplication::arguments(), out, err);
    }

    QApplication app(argc, argv);
    setIdentity();
    qraw::ui::setupApplicationIdentity();

    const qraw::ui::AppSettings settings;
    qraw::ui::installTranslations(app, settings.language());
    qraw::ui::applyTheme(app, settings.theme());

    if (const auto screenshot = qraw::ui::parseScreenshotArguments(QCoreApplication::arguments()))
        return qraw::ui::runScreenshot(*screenshot);

    // macOS delivers documents as QFileOpenEvents, also before the window exists.
    qraw::ui::FileOpenHandler fileOpen;
    app.installEventFilter(&fileOpen);

    GuiDiagnostics diagnostics(settings);

    qraw::ui::MainWindow window;
    window.setPreviousRun(diagnostics.previousRun());
    window.show();
    QCoreApplication::processEvents(); // a launch by double click already has its QFileOpenEvent posted
    // Armed before startup(): its dialogs (welcome, recovery) run their own event loops.
    if (const auto crash = qraw::diag::debugCrashFromEnvironment())
        QTimer::singleShot(0, [kind = *crash] { qraw::diag::triggerDebugCrash(kind); });
    window.startup(filesToOpen() + fileOpen.takePendingFiles());
    fileOpen.setWindow(&window);
    return QApplication::exec();
}
