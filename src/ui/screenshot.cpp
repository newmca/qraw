// `qraw --screenshot` harness (app_setup.h).
#include "qraw/ui/app_setup.h"

#include "main_window_prompts.h"

#include "qraw/ui/app_settings.h"
#include "qraw/ui/data_actions.h"
#include "qraw/ui/document_session.h"
#include "qraw/format/codec.h"
#include "qraw/diag/data_locations.h"
#include "qraw/ui/edit_data_dialog.h"
#include "qraw/ui/edit_diagram_dialog.h"
#include "qraw/ui/edit_geometry_dialog.h"
#include "qraw/ui/edit_image_dialog.h"
#include "qraw/ui/edit_link_dialog.h"
#include "qraw/ui/edit_tooltip_dialog.h"
#include "qraw/ui/export_dialog.h"
#include "qraw/ui/import_dialog.h"
#include "qraw/ui/insert_actions.h"
#include "qraw/ui/main_window.h"
#include "qraw/ui/palette_dock.h"
#include "qraw/ui/preferences_dialog.h"
#include "qraw/ui/recovery_dialog.h"
#include "qraw/ui/shortcuts_dialog.h"
#include "qraw/ui/welcome_dialog.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QRegularExpression>
#include <QSettings>
#include <QTemporaryDir>
#include <QTabWidget>
#include <QTimer>
#include <QTextStream>

#include <array>
#include <string>
#include <utility>
#include <vector>

namespace qraw::ui {

namespace {

using Panel = MainWindow::Panel;

constexpr std::array<std::pair<const char*, Panel>, 6> kPanels{{
    {"palette", Panel::Palette},
    {"format", Panel::Format},
    {"layers", Panel::Layers},
    {"outline", Panel::Outline},
    {"find", Panel::Find},
    {"cells", Panel::Cells},
}};

std::optional<Panel> panelByName(const QString& name)
{
    for (const auto& [panelName, panel] : kPanels) {
        if (name == QLatin1String(panelName))
            return panel;
    }
    return std::nullopt;
}

std::optional<QSize> parseSize(const QString& text)
{
    static const QRegularExpression pattern(QStringLiteral("^(\\d{1,5})[xX](\\d{1,5})$"));
    const QRegularExpressionMatch match = pattern.match(text);
    if (!match.hasMatch())
        return std::nullopt;
    const QSize size(match.captured(1).toInt(), match.captured(2).toInt());
    if (size.width() < 1 || size.height() < 1)
        return std::nullopt;
    return size;
}

// Shows the first tab with this title (the Format panel's Arrange tab, say); a no-op when none has it.
void selectTab(MainWindow& window, const QString& title)
{
    for (QTabWidget* tabs : window.findChildren<QTabWidget*>()) {
        for (int i = 0; i < tabs->count(); ++i) {
            if (tabs->tabText(i) == title) {
                tabs->setCurrentIndex(i);
                return;
            }
        }
    }
}

void printUsage(QTextStream& err)
{
    err << "usage: qraw --screenshot OUT.png [FILE] [--panel NAME]... [--size WxH] [--select ID]...\n"
        << "       [--theme light|dark] [--search TEXT] [--dialog export|welcome|data|link|tooltip|image|import-text|import-csv|import-mermaid|diagram|recovery|preferences|shortcuts|geometry]\n"
        << "       [--units points|inches|millimeters|centimeters|meters] [--dark-diagram|--light-diagram] [--tab TITLE] [--import list|diagram|csv|mermaid TEXTFILE]\n"
        << "panels: palette, format, layers, outline, find, cells (default: palette, format)\n"
        << "--size is the window size in logical pixels; the PNG has the window's device pixels (WxH times the\n"
        << "device pixel ratio: set QT_SCALE_FACTOR=2 for a HiDPI picture).\n";
}

// Keeps the harness from touching the user's settings: openPath records
// recent files and the window would restore the saved geometry.
class SettingsSandbox {
public:
    SettingsSandbox()
    {
        if (!directory_.isValid())
            return;
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, directory_.path());
    }

    ~SettingsSandbox()
    {
        // Qt's own default (qsettings_unix.cpp make_user_path).
        QString config = qEnvironmentVariable("XDG_CONFIG_HOME");
        if (config.isEmpty())
            config = QDir::homePath() + QStringLiteral("/.config");
        QSettings::setPath(QSettings::NativeFormat, QSettings::UserScope, config);
    }

    SettingsSandbox(const SettingsSandbox&) = delete;
    SettingsSandbox& operator=(const SettingsSandbox&) = delete;

private:
    QTemporaryDir directory_;
};

// Lets queued and timer-driven work (lazy palette thumbnails) finish before the grab.
void settle(MainWindow& window)
{
    for (int i = 0; i < 5; ++i) {
        QCoreApplication::processEvents();
        window.repaint();
    }
    QEventLoop pause;
    QTimer::singleShot(400, &pause, &QEventLoop::quit);
    pause.exec();
    QCoreApplication::processEvents();
    window.repaint();
}

// The requested panels are shown, every other one hidden.
void applyPanels(MainWindow& window, const QStringList& requested)
{
    QStringList names = requested;
    if (names.isEmpty())
        names = {QStringLiteral("palette"), QStringLiteral("format")};
    for (const auto& [panelName, panel] : kPanels)
        window.showPanel(panel, names.contains(QLatin1String(panelName)));
}

// The pages of the document, for the page choice of Edit Link.
std::vector<EditLinkDialog::PageChoice> linkPages(MainWindow& window)
{
    std::vector<EditLinkDialog::PageChoice> pages;
    const core::Document& document = window.session().document();
    for (std::size_t i = 0; i < document.pageCount(); ++i)
        pages.push_back({document.page(i)->id(), QString::fromStdString(document.page(i)->name())});
    return pages;
}

// Two autosaved documents as the recovery dialog lists them: made up, nothing is read from disk.
std::vector<RecoveryEntry> sampleRecoveries()
{
    const QDateTime now = QDateTime::currentDateTime();
    return {{QStringLiteral("a"), QString(), QStringLiteral("/home/user/Documents/network-overview.drawio"),
             now.addSecs(-95)},
            {QStringLiteral("b"), QString(), QString(), now.addSecs(-400)}};
}

// The dialog of `--dialog NAME`; unknown names give the Welcome dialog.
std::unique_ptr<QDialog> makeDialog(MainWindow& window, AppSettings& settings, const QString& name)
{
    if (name == QLatin1String("export"))
        return std::make_unique<ExportDialog>(!window.session().selection().isEmpty(), QStringLiteral("diagram.png"),
                                              &window);
    if (name == QLatin1String("data"))
        return std::make_unique<EditDataDialog>(actions::readDataSheet(window.session()), &window);
    if (name == QLatin1String("link"))
        return std::make_unique<EditLinkDialog>(actions::readLinkSheet(window.session()), linkPages(window), &window);
    if (name == QLatin1String("tooltip"))
        return std::make_unique<EditTooltipDialog>(
            actions::readTooltipSheet(window.session()).value_or(actions::TooltipSheet{}), &window);
    if (name == QLatin1String("image"))
        return std::make_unique<EditImageDialog>(actions::readImageSheet(window.session()),
                                                 window.session().baseDirectory(), &window);
    if (name == QLatin1String("diagram") && window.session().currentPage() != nullptr)
        return std::make_unique<EditDiagramDialog>(format::pageModelText(*window.session().currentPage()), &window);
    if (name == QLatin1String("import-text"))
        return std::make_unique<ImportDialog>(ImportDialog::Kind::FromText, actions::importLabelMeasure(window.session()), &window);
    if (name == QLatin1String("import-csv"))
        return std::make_unique<ImportDialog>(ImportDialog::Kind::Csv, actions::importLabelMeasure(window.session()), &window);
    if (name == QLatin1String("import-mermaid"))
        return std::make_unique<ImportDialog>(ImportDialog::Kind::Mermaid, actions::importLabelMeasure(window.session()), &window);
    if (name == QLatin1String("recovery"))
        return std::make_unique<RecoveryDialog>(sampleRecoveries(), diag::crashDirectory(), &window);
    if (name == QLatin1String("preferences"))
        return std::make_unique<PreferencesDialog>(settings, &window);
    if (name == QLatin1String("shortcuts"))
        return std::make_unique<ShortcutsDialog>(window.actions(), &window);
    if (name == QLatin1String("geometry"))
        return std::make_unique<EditGeometryDialog>(std::vector<core::Rect>{{40, 40, 120, 60}}, &window);
    return std::make_unique<WelcomeDialog>(settings, &window);
}

// A dialog shown over `window` (never exec'd: it is shown, painted and grabbed).
QImage grabDialog(MainWindow& window, const QString& name)
{
    AppSettings settings;
    const std::unique_ptr<QDialog> dialog = makeDialog(window, settings, name);
    dialog->show();
    QEventLoop pause;
    QTimer::singleShot(300, &pause, &QEventLoop::quit);
    pause.exec();
    dialog->repaint();
    return dialog->grab().toImage();
}

// Insert > Advanced with the text of a file, as the dialog does: at the centre of the view, as one edit.
bool importText(MainWindow& window, const ScreenshotOptions& options, QTextStream& err)
{
    QFile file(options.importFile);
    if (!file.open(QIODevice::ReadOnly)) {
        err << "qraw: cannot read " << options.importFile << '\n';
        return false;
    }
    const std::string text = file.readAll().toStdString();
    commands::imports::Options importOptions;
    importOptions.measure = actions::importLabelMeasure(window.session());
    commands::imports::Result result;
    if (options.importKind == QLatin1String("csv"))
        result = commands::imports::importCsv(text, importOptions);
    else if (options.importKind == QLatin1String("mermaid"))
        result = commands::imports::importMermaid(text, importOptions);
    else if (options.importKind == QLatin1String("list"))
        result = commands::imports::importText(text, commands::imports::TextType::List, commands::imports::TextLayout::VerticalTree, importOptions);
    else
        result = commands::imports::importText(text, commands::imports::TextType::Diagram, commands::imports::TextLayout::VerticalFlow, importOptions);
    for (const commands::imports::Diagnostic& warning : result.warnings)
        err << "qraw: warning: " << QString::fromStdString(commands::imports::describe(warning)) << '\n';
    for (const commands::imports::Diagnostic& error : result.errors)
        err << "qraw: " << QString::fromStdString(commands::imports::describe(error)) << '\n';
    if (!result.ok() || !actions::insertImported(window.session(), result, window.canvas().insertPoint(), QStringLiteral("Insert")))
        return false;
    return true;
}

} // namespace

QStringList screenshotDialogNames()
{
    return {QStringLiteral("export"),      QStringLiteral("welcome"),       QStringLiteral("data"),
            QStringLiteral("link"),        QStringLiteral("tooltip"),       QStringLiteral("image"),
            QStringLiteral("import-text"), QStringLiteral("import-csv"),    QStringLiteral("import-mermaid"),
            QStringLiteral("diagram"),     QStringLiteral("recovery"),      QStringLiteral("preferences"),
            QStringLiteral("shortcuts"),   QStringLiteral("geometry")};
}

std::unique_ptr<QDialog> createScreenshotDialog(MainWindow& window, AppSettings& settings, const QString& name)
{
    return makeDialog(window, settings, name);
}

std::optional<ScreenshotOptions> parseScreenshotArguments(const QStringList& arguments)
{
    const qsizetype start = arguments.indexOf(QStringLiteral("--screenshot"));
    if (start < 0)
        return std::nullopt;

    ScreenshotOptions options; // output stays empty on a usage error
    ScreenshotOptions parsed;
    QStringList positional;

    auto needValue = [&](qsizetype& i) -> std::optional<QString> {
        if (i + 1 >= arguments.size())
            return std::nullopt;
        return arguments.at(++i);
    };

    // Arguments before --screenshot are the program name (and nothing else).
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        const QString& argument = arguments.at(i);
        if (i == start)
            continue;
        if (argument == QLatin1String("--panel")) {
            const std::optional<QString> name = needValue(i);
            if (!name || !panelByName(*name))
                return options;
            parsed.panels.append(*name);
        } else if (argument == QLatin1String("--select")) {
            const std::optional<QString> id = needValue(i);
            if (!id || id->isEmpty())
                return options;
            parsed.select.append(*id);
        } else if (argument == QLatin1String("--size")) {
            const std::optional<QString> text = needValue(i);
            const std::optional<QSize> size = text ? parseSize(*text) : std::nullopt;
            if (!size)
                return options;
            parsed.size = *size;
        } else if (argument == QLatin1String("--theme")) {
            const std::optional<QString> name = needValue(i);
            if (name == QLatin1String("light"))
                parsed.theme = ThemeKind::Light;
            else if (name == QLatin1String("dark"))
                parsed.theme = ThemeKind::Dark;
            else
                return options;
        } else if (argument == QLatin1String("--search")) {
            const std::optional<QString> text = needValue(i);
            if (!text)
                return options;
            parsed.search = *text;
        } else if (argument == QLatin1String("--units")) {
            const std::optional<QString> name = needValue(i);
            const std::optional<MeasureUnit> unit = name ? units::fromKey(*name) : std::nullopt;
            if (!unit)
                return options;
            parsed.unit = *unit;
        } else if (argument == QLatin1String("--dark-diagram")) {
            parsed.diagramAppearance = DiagramAppearance::Dark;
        } else if (argument == QLatin1String("--light-diagram")) {
            parsed.diagramAppearance = DiagramAppearance::Light;
        } else if (argument == QLatin1String("--tab")) {
            const std::optional<QString> title = needValue(i);
            if (!title || title->isEmpty())
                return options;
            parsed.tab = *title;
        } else if (argument == QLatin1String("--import")) {
            const std::optional<QString> kind = needValue(i);
            const std::optional<QString> file = needValue(i);
            static const QStringList kinds{QStringLiteral("list"), QStringLiteral("diagram"), QStringLiteral("csv"),
                                           QStringLiteral("mermaid")};
            if (!kind || !file || !kinds.contains(*kind) || file->isEmpty())
                return options;
            parsed.importKind = *kind;
            parsed.importFile = *file;
        } else if (argument == QLatin1String("--dialog")) {
            const std::optional<QString> name = needValue(i);
            const QStringList known = screenshotDialogNames();
            if (!name || !known.contains(*name))
                return options;
            parsed.dialog = *name;
        } else if (argument.startsWith(QLatin1String("--"))) {
            return options; // unknown option
        } else {
            positional.append(argument);
        }
    }

    // OUT.png first, then an optional FILE.
    if (positional.isEmpty() || positional.size() > 2)
        return options;
    parsed.output = positional.at(0);
    if (positional.size() == 2)
        parsed.file = positional.at(1);
    return parsed;
}

int runScreenshot(const ScreenshotOptions& options)
{
    QTextStream err(stderr);
    if (options.output.isEmpty()) {
        printUsage(err);
        return 2;
    }

    const SettingsSandbox sandbox;
    if (options.theme) {
        applyTheme(*qobject_cast<QApplication*>(QCoreApplication::instance()), *options.theme);
        AppSettings().setTheme(*options.theme); // the sandbox keeps it out of the user's settings; the canvas follows it
    }
    if (options.unit)
        AppSettings().setMeasureUnit(*options.unit); // the sandbox keeps it out of the user's settings
    if (options.diagramAppearance)
        AppSettings().setDiagramAppearance(*options.diagramAppearance);
    MainWindow window;
    NonInteractivePrompts prompts;
    window.setPrompts(&prompts);
    window.resize(options.size);
    applyPanels(window, options.panels);
    window.show();
    window.canvas().setSynchronousRasterization(true); // deterministic pixels, no worker threads

    if (!options.file.isEmpty() && !window.controller().openPath(options.file)) {
        err << "qraw: cannot open " << options.file << '\n';
        for (const QString& message : prompts.errors())
            err << "  " << message << '\n';
        return 1;
    }

    // Opening a file can change the window (title, scene size); keep the
    // requested size and the requested panels.
    window.resize(options.size);
    applyPanels(window, options.panels);
    std::vector<std::string> ids;
    for (const QString& id : options.select)
        ids.push_back(id.toStdString());
    window.session().selection().set(std::move(ids));
    if (!options.importKind.isEmpty() && !importText(window, options, err))
        return 1;
    if (!options.tab.isEmpty())
        selectTab(window, options.tab);
    if (!options.search.isEmpty() && window.paletteDock() != nullptr)
        window.paletteDock()->setSearchText(options.search);
    settle(window);

    const QImage image = options.dialog.isEmpty() ? window.grab().toImage() : grabDialog(window, options.dialog);
    if (image.isNull() || !image.save(options.output)) {
        err << "qraw: cannot write " << options.output << '\n';
        return 1;
    }
    return 0;
}

} // namespace qraw::ui
