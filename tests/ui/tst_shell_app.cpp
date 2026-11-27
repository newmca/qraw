// Application setup: themes, translations, the screenshot harness.
#include "support/shell_env.h"
#include "support/test_support.h"

#include "qraw/ui/app_setup.h"

#include <QGuiApplication>
#include <QImage>
#include <QScreen>
#include <QStyle>
#include <QTemporaryDir>

#include <cmath>

using namespace qraw::ui;

namespace {

QStringList args(std::initializer_list<const char*> list)
{
    QStringList result{QStringLiteral("qraw")};
    for (const char* item : list)
        result << QString::fromLatin1(item);
    return result;
}

// Number of distinct colours in a coarse sample of the image: > 1 means the
// picture is not a flat fill.
int distinctColors(const QImage& image)
{
    QSet<QRgb> colors;
    for (int y = 0; y < image.height(); y += 4) {
        for (int x = 0; x < image.width(); x += 4)
            colors.insert(image.pixel(x, y));
    }
    return static_cast<int>(colors.size());
}

} // namespace

class TestShellApp : public QObject {
    Q_OBJECT

private slots:
    void parseIgnoresInvocationsWithoutScreenshot()
    {
        QVERIFY(!parseScreenshotArguments(args({})).has_value());
        QVERIFY(!parseScreenshotArguments(args({"diagram.drawio"})).has_value());
    }

    void parseReadsTheDiagramAppearanceOptions()
    {
        QCOMPARE(parseScreenshotArguments(args({"--screenshot", "o.png"}))->diagramAppearance, std::nullopt);
        QCOMPARE(parseScreenshotArguments(args({"--screenshot", "o.png", "--dark-diagram"}))->diagramAppearance,
                 std::optional<DiagramAppearance>(DiagramAppearance::Dark));
        QCOMPARE(parseScreenshotArguments(args({"--screenshot", "o.png", "--theme", "dark", "--light-diagram"}))
                     ->diagramAppearance,
                 std::optional<DiagramAppearance>(DiagramAppearance::Light));
    }

    void aDarkThemeScreenshotHasADarkCanvasUnlessPinnedLight()
    {
        QTemporaryDir directory;
        const auto shoot = [&](std::initializer_list<const char*> extra) {
            QStringList list = args({"--screenshot"});
            list << directory.path() + QStringLiteral("/shot.png");
            for (const char* item : extra)
                list << QString::fromLatin1(item);
            const auto options = parseScreenshotArguments(list);
            if (!options || runScreenshot(*options) != 0)
                return QImage();
            return QImage(directory.path() + QStringLiteral("/shot.png"));
        };
        const auto canvasPixel = [](const QImage& image) { return image.pixelColor(image.width() / 2, image.height() / 2); };
        // The window centre is the canvas (default panels sit at the sides): dark under the dark theme, as the
        // canvas follows it; a light canvas when pinned.
        const QImage followed = shoot({"--theme", "dark"});
        QVERIFY(!followed.isNull());
        QVERIFY(canvasPixel(followed).lightness() < 64);
        const QImage pinned = shoot({"--theme", "dark", "--light-diagram"});
        QVERIFY(!pinned.isNull());
        QVERIFY(canvasPixel(pinned).lightness() > 192);
        applyTheme(*static_cast<QApplication*>(QCoreApplication::instance()), ThemeKind::System);
    }

    void parseReadsOutputFileAndOptions()
    {
        const auto options = parseScreenshotArguments(args({"--screenshot", "out.png", "in.drawio", "--panel", "layers",
                                                            "--panel", "find", "--size", "640x480", "--select", "a",
                                                            "--select", "b"}));
        QVERIFY(options.has_value());
        QCOMPARE(options->output, QStringLiteral("out.png"));
        QCOMPARE(options->file, QStringLiteral("in.drawio"));
        QCOMPARE(options->panels, (QStringList{"layers", "find"}));
        QCOMPARE(options->select, (QStringList{"a", "b"}));
        QCOMPARE(options->size, QSize(640, 480));
    }

    void parseReadsThemeSearchAndDialog()
    {
        const auto options = parseScreenshotArguments(
            args({"--screenshot", "out.png", "--theme", "dark", "--search", "arrow", "--dialog", "export"}));
        QVERIFY(options.has_value());
        QCOMPARE(options->theme, std::optional<ThemeKind>(ThemeKind::Dark));
        QCOMPARE(options->search, QStringLiteral("arrow"));
        QCOMPARE(options->dialog, QStringLiteral("export"));
        QVERIFY(parseScreenshotArguments(args({"--screenshot", "o.png", "--theme", "x"}))->output.isEmpty());
        QVERIFY(parseScreenshotArguments(args({"--screenshot", "o.png", "--dialog", "x"}))->output.isEmpty());
    }

    void parseDefaultsAndPlainOutput()
    {
        const auto options = parseScreenshotArguments(args({"--screenshot", "out.png"}));
        QVERIFY(options.has_value());
        QCOMPARE(options->output, QStringLiteral("out.png"));
        QVERIFY(options->file.isEmpty());
        QVERIFY(options->panels.isEmpty());
        QCOMPARE(options->size, QSize(1280, 800));
    }

    void parseReportsUsageErrorsAsEmptyOutput()
    {
        for (const QStringList& bad : {args({"--screenshot"}),
                                       args({"--screenshot", "out.png", "--size", "10"}),
                                       args({"--screenshot", "out.png", "--size", "0x5"}),
                                       args({"--screenshot", "out.png", "--panel", "nope"}),
                                       args({"--screenshot", "out.png", "--panel"}),
                                       args({"--screenshot", "out.png", "--bogus"}),
                                       args({"--screenshot", "out.png", "a.drawio", "b.drawio"})}) {
            const auto options = parseScreenshotArguments(bad);
            QVERIFY2(options.has_value(), qPrintable(bad.join(' ')));
            QVERIFY2(options->output.isEmpty(), qPrintable(bad.join(' ')));
        }
        ScreenshotOptions usage;
        QCOMPARE(runScreenshot(usage), 2);
    }

    void screenshotOfABlankWindowIsWrittenAtTheRequestedSize()
    {
        QTemporaryDir directory;
        ScreenshotOptions options;
        options.output = directory.path() + QStringLiteral("/blank.png");
        options.size = QSize(900, 600);
        options.panels = {QStringLiteral("palette"), QStringLiteral("layers")};
        QCOMPARE(runScreenshot(options), 0);
        const QImage image(options.output);
        QVERIFY(!image.isNull());
        // --size is the window size in logical pixels; the PNG has the window's device pixels, as the
        // screen shows it (at QT_SCALE_FACTOR=2, or the 1.25 that QT_FONT_DPI=120 gives on the
        // offscreen platform, it is larger than the option).
        const double dpr = QGuiApplication::primaryScreen()->devicePixelRatio();
        QCOMPARE(image.size(), QSize(int(std::lround(options.size.width() * dpr)), int(std::lround(options.size.height() * dpr))));
        QVERIFY(distinctColors(image) > 3);
    }

    void screenshotOfAMissingFileFails()
    {
        QTemporaryDir directory;
        ScreenshotOptions options;
        options.output = directory.path() + QStringLiteral("/never.png");
        options.file = directory.path() + QStringLiteral("/missing.drawio");
        QCOMPARE(runScreenshot(options), 1);
        QVERIFY(!QFileInfo::exists(options.output));
    }

    void screenshotOfACorpusFileShowsTheDiagram()
    {
        if (!test::sessionFilesAvailable())
            QSKIP("FileService/DocumentController not available");
        QTemporaryDir directory;
        ScreenshotOptions options;
        options.output = directory.path() + QStringLiteral("/flowchart.png");
        options.file = QStringLiteral(QRAW_SOURCE_DIR "/tests/corpus/upstream-templates/basic/flowchart.xml");
        QCOMPARE(runScreenshot(options), 0);
        const QImage image(options.output);
        QVERIFY(!image.isNull());
        QVERIFY(distinctColors(image) > 8);
    }

    void darkThemeChangesThePaletteAndSystemRestoresIt()
    {
        auto& app = *static_cast<QApplication*>(QCoreApplication::instance());
        const QColor startup = app.palette().window().color();

        applyTheme(app, ThemeKind::Dark);
        QCOMPARE(app.palette().window().color(), QColor(0x2a, 0x2a, 0x2a));
        QCOMPARE(app.palette().base().color(), QColor(0x1e, 0x1e, 0x1e));
        QCOMPARE(app.palette().highlight().color(), QColor(0x29, 0xb6, 0xf2));

        applyTheme(app, ThemeKind::HighContrast);
        QCOMPARE(app.palette().window().color(), QColor(Qt::black));
        QCOMPARE(app.palette().highlight().color(), QColor(0xff, 0xff, 0x00));
        QVERIFY(!app.styleSheet().isEmpty());

        applyTheme(app, ThemeKind::Light);
        QVERIFY(app.styleSheet().isEmpty());
        QCOMPARE(app.style()->name().toLower(), QStringLiteral("fusion"));

        applyTheme(app, ThemeKind::System);
        QCOMPARE(app.palette().window().color(), startup);
    }

    void englishNeedsNoTranslatorAndUnknownLanguagesFallBack()
    {
        QCOMPARE(installTranslations(*QCoreApplication::instance(), QStringLiteral("en")), QStringLiteral("en"));
        QCOMPARE(installTranslations(*QCoreApplication::instance(), QStringLiteral("xx")), QStringLiteral("en"));
        // Twice in a row must not stack translators or crash.
        QCOMPARE(installTranslations(*QCoreApplication::instance(), QString()).isEmpty(), false);
    }
};

SHELL_TEST_MAIN(TestShellApp)
#include "tst_shell_app.moc"
