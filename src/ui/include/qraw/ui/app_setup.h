#pragma once
// Application-wide setup used by src/app/main.cpp: identity, themes,
// translations and the screenshot harness (src/ui/app_identity.cpp, theme.cpp,
// translations.cpp, screenshot.cpp).

#include "qraw/ui/app_settings.h"

#include <QSize>
#include <QString>
#include <QStringList>

#include <memory>
#include <optional>

class QApplication;
class QCoreApplication;
class QDialog;

namespace qraw::ui {

// The reverse-DNS application id (CMake QRAW_APP_ID): desktop file name,
// AppStream id and icon name.
[[nodiscard]] QString applicationId();

// Sets the desktop file name (the Wayland app_id; must be called before any
// window is created) and the window icon: the icon theme's entry for the
// application id, else the bundled PNGs. Does not touch the organization or
// application name, which key the QSettings.
void setupApplicationIdentity();

// Applies a widget theme to the running QApplication: Light = Fusion with
// the default light palette; Dark = Fusion with a dark QPalette (window
// #2a2a2a, base #1e1e1e, text #e0e0e0, highlight #29b6f2); HighContrast =
// Fusion, black/white palette, yellow highlight, 2 px focus frames via a
// small style sheet; System = the platform style and palette as at startup.
// The diagram canvas itself stays light (diagram dark mode is out of scope,
// render.md §10).
void applyTheme(QApplication& app, ThemeKind theme);

// Installs QTranslators for `language` ("" = QLocale::system()): Qraw's
// :/qraw/i18n/qraw_<lang>.qm and Qt's own qtbase_<lang>.qm (from
// QLibraryInfo::TranslationsPath) when present. Safe to call once at
// startup; returns the language actually loaded ("en" fallback).
QString installTranslations(QCoreApplication& app, const QString& language);

// qraw --screenshot OUT.png [FILE] [--panel NAME]... [--size WxH]
//   [--select ID]... [--theme light|dark] [--search TEXT] [--dialog export|welcome|data|link|tooltip|image]
//   [--tab TITLE]
// Opens a MainWindow at the given size (default 1280x800), loads FILE (or a
// blank diagram), selects the given cell ids, shows the named panels
// (palette, format, layers, outline, find; default palette + format),
// processes events until the canvas painted, grabs the window
// (QWidget::grab) and writes OUT.png. Runs on any platform plugin
// (offscreen in CI). No welcome dialog, no recovery prompt, no settings
// written. --theme applies a widget theme first, --search types TEXT into
// the palette search field, --dialog grabs that dialog (offscreen, over a
// window showing FILE) instead of the main window.
struct ScreenshotOptions {
    QString output;
    QString file;
    QStringList panels;
    QStringList select;
    QSize size{1280, 800};
    std::optional<ThemeKind> theme;
    QString search;
    QString dialog; // "", "export", "welcome", "data", "link", "tooltip", "image", "import-text", "import-csv", "import-mermaid", "diagram" (Extras > Edit Diagram), "recovery", "preferences", "shortcuts" or "geometry"
    QString importKind; // "", "list", "diagram", "csv" or "mermaid": insert the text of importFile first (Insert > Advanced)
    QString importFile;
    bool darkDiagram = false;        // --dark-diagram: View > Dark Diagram is on
    std::optional<MeasureUnit> unit; // --units NAME: the unit of the length fields (View > Units)
    QString tab;    // a tab of the Format panel to show by its title (e.g. "Arrange"), "" for none
};
// The names `--dialog` accepts, and a new dialog of one of them over `window` (not shown; the Welcome
// dialog for a name that is not on the list). The accessibility test walks all of them.
[[nodiscard]] QStringList screenshotDialogNames();
class MainWindow;
[[nodiscard]] std::unique_ptr<QDialog> createScreenshotDialog(MainWindow& window, AppSettings& settings,
                                                              const QString& name);
// nullopt when `arguments` (program name first) has no --screenshot; an
// options object with an empty output on a usage error.
[[nodiscard]] std::optional<ScreenshotOptions> parseScreenshotArguments(const QStringList& arguments);
// Requires a QApplication. Returns a process exit code (0 = written).
int runScreenshot(const ScreenshotOptions& options);

} // namespace qraw::ui
