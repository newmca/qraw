// The diagram canvas follows the application theme (D-053): the appearance setting and its migration of
// the old Dark Diagram boolean, the resolution (theme x appearance x OS scheme), the main window driving the
// canvas from it, and the overlay colours on the dark page.
#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/render/stats.h"
#include "qraw/ui/app_setup.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/diagram_scheme.h"
#include "qraw/ui/interaction.h"
#include "qraw/ui/main_window.h"

#include <QAction>
#include <QActionGroup>
#include <QGuiApplication>
#include <QMenu>
#include <QSettings>
#include <QStyleHints>
#include <QtTest>

#include <cmath>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;
namespace id = qraw::ui::action_id;

namespace {

constexpr auto kLegacyKey = "appearance/diagramDark";
constexpr auto kKey = "appearance/diagramAppearance";

// WCAG relative luminance and contrast ratio.
double luminance(const QColor& color)
{
    const auto channel = [](double c) { return c <= 0.03928 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4); };
    return 0.2126 * channel(color.redF()) + 0.7152 * channel(color.greenF()) + 0.0722 * channel(color.blueF());
}

double contrast(const QColor& a, const QColor& b)
{
    const double la = luminance(a);
    const double lb = luminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

// The canvas corner is the backdrop or the page, both of the scheme.
bool canvasLooksDark(MainWindow& window)
{
    window.canvas().viewport()->repaint();
    return window.canvas().viewport()->grab().toImage().pixelColor(2, 2).lightness() < 64;
}

void showAndSettle(MainWindow& window)
{
    window.resize(900, 600);
    window.show();
    window.canvas().setSynchronousRasterization(true);
    QTest::qWait(30);
}

} // namespace

class TestDiagramScheme : public QObject {
    Q_OBJECT

private slots:
    void init() { QSettings().clear(); }

    // --- settings -----------------------------------------------------------------

    void defaultsToFollowTheme() { QCOMPARE(AppSettings().diagramAppearance(), DiagramAppearance::FollowTheme); }

    void theOldBooleanMigrates_data()
    {
        QTest::addColumn<bool>("legacy");
        QTest::addColumn<DiagramAppearance>("expected");
        QTest::newRow("dark on") << true << DiagramAppearance::Dark;
        QTest::newRow("dark off") << false << DiagramAppearance::FollowTheme;
    }

    void theOldBooleanMigrates()
    {
        QFETCH(bool, legacy);
        QFETCH(DiagramAppearance, expected);
        QSettings().setValue(kLegacyKey, legacy);
        QCOMPARE(AppSettings().diagramAppearance(), expected);
    }

    void writingTheNewValueRetiresTheOldKey()
    {
        QSettings().setValue(kLegacyKey, true);
        AppSettings().setDiagramAppearance(DiagramAppearance::Light);
        QVERIFY(!QSettings().contains(kLegacyKey));
        QCOMPARE(AppSettings().diagramAppearance(), DiagramAppearance::Light);
        // The explicit value outranks a stale old key (an older build wrote it again).
        QSettings().setValue(kLegacyKey, true);
        QCOMPARE(AppSettings().diagramAppearance(), DiagramAppearance::Light);
    }

    void everyAppearanceRoundTrips()
    {
        for (const DiagramAppearance appearance :
             {DiagramAppearance::Dark, DiagramAppearance::Light, DiagramAppearance::FollowTheme}) {
            AppSettings().setDiagramAppearance(appearance);
            QCOMPARE(AppSettings().diagramAppearance(), appearance);
        }
        QSettings().setValue(kKey, QStringLiteral("garbage"));
        QCOMPARE(AppSettings().diagramAppearance(), DiagramAppearance::FollowTheme);
    }

    // --- resolution ---------------------------------------------------------------

    void resolutionCoversThemeAppearanceAndOsScheme_data()
    {
        QTest::addColumn<ThemeKind>("theme");
        QTest::addColumn<DiagramAppearance>("appearance");
        QTest::addColumn<Qt::ColorScheme>("os");
        QTest::addColumn<bool>("dark");

        const struct {
            ThemeKind theme;
            const char* name;
        } themes[] = {{ThemeKind::System, "system"},
                      {ThemeKind::Light, "light"},
                      {ThemeKind::Dark, "dark"},
                      {ThemeKind::HighContrast, "contrast"}};
        const struct {
            DiagramAppearance appearance;
            const char* name;
        } appearances[] = {{DiagramAppearance::FollowTheme, "follow"},
                           {DiagramAppearance::Light, "pin-light"},
                           {DiagramAppearance::Dark, "pin-dark"}};
        const struct {
            Qt::ColorScheme scheme;
            const char* name;
        } systems[] = {{Qt::ColorScheme::Unknown, "os-unknown"},
                       {Qt::ColorScheme::Light, "os-light"},
                       {Qt::ColorScheme::Dark, "os-dark"}};
        for (const auto& theme : themes) {
            for (const auto& appearance : appearances) {
                for (const auto& os : systems) {
                    bool dark = false;
                    if (appearance.appearance == DiagramAppearance::Dark)
                        dark = true;
                    else if (appearance.appearance == DiagramAppearance::FollowTheme)
                        dark = theme.theme == ThemeKind::Dark || theme.theme == ThemeKind::HighContrast
                            || (theme.theme == ThemeKind::System && os.scheme == Qt::ColorScheme::Dark);
                    QTest::addRow("%s/%s/%s", theme.name, appearance.name, os.name)
                        << theme.theme << appearance.appearance << os.scheme << dark;
                }
            }
        }
    }

    void resolutionCoversThemeAppearanceAndOsScheme()
    {
        QFETCH(ThemeKind, theme);
        QFETCH(DiagramAppearance, appearance);
        QFETCH(Qt::ColorScheme, os);
        QFETCH(bool, dark);
        QCOMPARE(resolveDiagramDark(theme, appearance, os), dark);
    }

    // --- the window -----------------------------------------------------------------

    void switchingTheThemeSwitchesTheCanvas()
    {
        AppSettings().setTheme(ThemeKind::Light);
        MainWindow window;
        showAndSettle(window);
        QVERIFY(!window.session().diagramDark());
        QVERIFY(!canvasLooksDark(window));
        QVERIFY(window.actions()[id::kDiagramFollowTheme]->isChecked());

        AppSettings settings;
        render::stats::reset();
        settings.setTheme(ThemeKind::Dark);
        window.applySettings(); // what Preferences > OK does
        QTest::qWait(30);
        QVERIFY(window.session().diagramDark());
        QVERIFY(window.canvas().diagramScene().pageView().theme().dark);
        QVERIFY(render::stats::get(render::stats::Counter::tilesRasterized) > 0); // the tiles were drawn again
        QVERIFY(canvasLooksDark(window));
        QVERIFY(window.session().currentPage() != nullptr);

        settings.setTheme(ThemeKind::Light);
        window.applySettings();
        QTest::qWait(30);
        QVERIFY(!window.session().diagramDark());
        QVERIFY(!canvasLooksDark(window));

        settings.setTheme(ThemeKind::HighContrast);
        window.applySettings();
        QVERIFY(window.session().diagramDark());

        // Back to the application's own theme for the tests that follow.
        settings.setTheme(ThemeKind::System);
        window.applySettings();
    }

    void theSystemThemeFollowsTheOsSchemeLive()
    {
        AppSettings().setTheme(ThemeKind::System);
        MainWindow window;
        showAndSettle(window);

        // QStyleHints is what the platform reports; the offscreen platform cannot change it, so the signal
        // carries the news as it does on a desktop.
        emit QGuiApplication::styleHints()->colorSchemeChanged(Qt::ColorScheme::Light);
        QVERIFY(!window.session().diagramDark());
        emit QGuiApplication::styleHints()->colorSchemeChanged(Qt::ColorScheme::Dark);
        QTest::qWait(30);
        QVERIFY(window.session().diagramDark());
        QVERIFY(canvasLooksDark(window));

        emit QGuiApplication::styleHints()->colorSchemeChanged(Qt::ColorScheme::Light);
        QTest::qWait(30);
        QVERIFY(!window.session().diagramDark());
        QVERIFY(!canvasLooksDark(window));

        // A pinned appearance ignores the OS.
        AppSettings().setDiagramAppearance(DiagramAppearance::Light);
        window.applySettings();
        emit QGuiApplication::styleHints()->colorSchemeChanged(Qt::ColorScheme::Dark);
        QVERIFY(!window.session().diagramDark());
        emit QGuiApplication::styleHints()->colorSchemeChanged(Qt::ColorScheme::Light);
        AppSettings().setTheme(ThemeKind::System);
    }

    void aDarkThemeCanKeepALightCanvasThroughTheMenu()
    {
        AppSettings().setTheme(ThemeKind::Dark);
        MainWindow window;
        showAndSettle(window);
        QVERIFY(window.session().diagramDark());

        QMenu* menu = window.findChild<QMenu*>(QStringLiteral("menu.diagramAppearance"));
        QVERIFY(menu != nullptr);
        QCOMPARE(menu->actions().size(), 3);
        QVERIFY(window.actions()[id::kDiagramFollowTheme]->isChecked());

        window.actions()[id::kDiagramLight]->trigger();
        QVERIFY(!window.session().diagramDark());
        QVERIFY(!canvasLooksDark(window));
        QVERIFY(window.actions()[id::kDiagramLight]->isChecked());
        QVERIFY(!window.actions()[id::kDiagramFollowTheme]->isChecked()); // exclusive
        QCOMPARE(AppSettings().diagramAppearance(), DiagramAppearance::Light);

        window.actions()[id::kDiagramDark]->trigger();
        QVERIFY(window.session().diagramDark());
        QCOMPARE(AppSettings().diagramAppearance(), DiagramAppearance::Dark);

        window.actions()[id::kDiagramFollowTheme]->trigger();
        QVERIFY(window.session().diagramDark()); // the theme is dark
        QCOMPARE(AppSettings().diagramAppearance(), DiagramAppearance::FollowTheme);

        // A new window starts from the stored choice.
        window.actions()[id::kDiagramLight]->trigger();
        MainWindow next;
        QVERIFY(!next.session().diagramDark());
        QVERIFY(next.actions()[id::kDiagramLight]->isChecked());
        AppSettings().setTheme(ThemeKind::System);
    }

    void theOldDarkDiagramSettingStartsADarkCanvasOnALightTheme()
    {
        AppSettings().setTheme(ThemeKind::Light);
        QSettings().setValue(kLegacyKey, true);
        MainWindow window;
        QVERIFY(window.session().diagramDark());
        QVERIFY(window.actions()[id::kDiagramDark]->isChecked());
        AppSettings().setTheme(ThemeKind::System);
    }

    // --- the overlay ------------------------------------------------------------------

    void overlayColoursAreLegibleOnTheDarkPage()
    {
        const QColor page(0x12, 0x12, 0x12);
        const QColor backdrop(0x0a, 0x0a, 0x0a);
        const struct {
            const char* name;
            QColor color;
        } colours[] = {{"selection", overlay::kSelection}, {"handle fill", overlay::kHandleFill},
                       {"handle stroke", overlay::kHandleStroke}, {"guide", overlay::kGuide},
                       {"rubber band", overlay::kRubberband}, {"drop target", overlay::dropTarget(true)}};
        for (const auto& entry : colours) {
            QVERIFY2(contrast(entry.color, page) >= 3, entry.name);
            QVERIFY2(contrast(entry.color, backdrop) >= 3, entry.name);
        }
    }

    void theDropTargetIsGreenInTheDarkSchemeAndBlueInTheLightOne()
    {
        QCOMPARE(overlay::dropTarget(true), QColor(0x00, 0xff, 0x00));
        QCOMPARE(overlay::dropTarget(false), QColor(0x00, 0x00, 0xff));
        QVERIFY(contrast(overlay::dropTarget(false), Qt::white) >= 3);
    }
};

SHELL_TEST_MAIN(TestDiagramScheme)
#include "tst_diagram_scheme.moc"
