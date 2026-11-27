// Thumbnail rendering of palette entries through the M3 renderer.

// palette_thumbnail.h is private to the palette package (src/ui).
#include "../../src/ui/palette_thumbnail.h"

#include "support/test_support.h"

using namespace qraw;
using namespace qraw::ui;

namespace {

bool hasVisiblePixels(const QImage& image)
{
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0)
                return true;
        }
    }
    return false;
}

const PaletteEntry& entryOf(const QString& key)
{
    const PaletteEntry* entry = PaletteLibrary::builtin().entry(key);
    if (entry == nullptr)
        qFatal("palette entry %s is missing", qPrintable(key));
    return *entry;
}

} // namespace

class TstPaletteThumbnail : public QObject {
    Q_OBJECT

private slots:
    void rectangleRendersVisiblePixels()
    {
        const QImage image = renderPaletteThumbnail(entryOf(QStringLiteral("general/rectangle")), QSize(32, 32), 1);
        QCOMPARE(image.size(), QSize(32, 32));
        QVERIFY(hasVisiblePixels(image));
        // 1 px margin: the border rows/columns stay transparent.
        QCOMPARE(qAlpha(image.pixel(0, 0)), 0);
        QCOMPARE(qAlpha(image.pixel(31, 31)), 0);
        // Rectangle 120x60 fits to the width: it is centred vertically.
        QCOMPARE(qAlpha(image.pixel(16, 2)), 0);
        QVERIFY(qAlpha(image.pixel(16, 16)) > 0 || qAlpha(image.pixel(16, 10)) > 0);
    }

    void highDpiScalesThePixelSize()
    {
        const QImage image = renderPaletteThumbnail(entryOf(QStringLiteral("general/ellipse")), QSize(32, 32), 2);
        QCOMPARE(image.size(), QSize(64, 64));
        QCOMPARE(image.devicePixelRatio(), 2.0);
        QVERIFY(hasVisiblePixels(image));
    }

    void everyKindRenders()
    {
        const PaletteLibrary& library = PaletteLibrary::builtin();
        bool seen[3] = {false, false, false};
        for (const Palette& palette : library.palettes()) {
            if (palette.id != QLatin1String("general") && palette.id != QLatin1String("misc"))
                continue;
            for (const PaletteEntry& entry : palette.entries) {
                const int kind = static_cast<int>(entry.kind);
                if (seen[kind])
                    continue;
                seen[kind] = true;
                const QImage image = renderPaletteThumbnail(entry, QSize(32, 32), 1);
                QVERIFY2(!image.isNull() && hasVisiblePixels(image), qPrintable(entry.key(palette.id)));
            }
        }
        QVERIFY(seen[0] && seen[1] && seen[2]);
    }

    void labelIsPaintedOnlyWhenShown()
    {
        PaletteEntry labelled;
        labelled.id = QStringLiteral("t");
        labelled.kind = PaletteEntry::Kind::Vertex;
        labelled.style = QStringLiteral("text;html=1;strokeColor=none;fillColor=none;");
        labelled.value = QStringLiteral("Hello");
        labelled.width = 60;
        labelled.height = 30;
        const QImage shown = renderPaletteThumbnail(labelled, QSize(64, 32), 1);
        labelled.showLabel = false;
        const QImage hidden = renderPaletteThumbnail(labelled, QSize(64, 32), 1);
        QVERIFY(hasVisiblePixels(shown));
        QVERIFY(!hasVisiblePixels(hidden));
    }

    void stencilAndImageEntriesDoNotCrash()
    {
        // Stencil shapes (AWS), a shape that cannot resolve and an image entry.
        for (const char* key : {"aws4Compute/lambda", "basic/heart"}) {
            if (PaletteLibrary::builtin().entry(QString::fromLatin1(key)) == nullptr)
                continue;
            const QImage image = renderPaletteThumbnail(entryOf(QString::fromLatin1(key)), QSize(32, 32), 1);
            QVERIFY2(!image.isNull(), key);
        }
        PaletteEntry broken;
        broken.id = QStringLiteral("x");
        broken.kind = PaletteEntry::Kind::Cells;
        broken.xml = QStringLiteral("<not-a-model/>");
        QVERIFY(renderPaletteThumbnail(broken, QSize(32, 32), 1).isNull());
    }

    // shape=curvedText has no fill and no stroke: only the text along the arc
    // is there, and it must not be blank (it used to draw nothing at all).
    void curvedTextThumbnailShowsTheArcText()
    {
        const QImage image = renderPaletteThumbnail(entryOf(QStringLiteral("misc/curved-text")), QSize(64, 64), 1);
        QVERIFY(hasVisiblePixels(image));
    }

    void mostCorePaletteEntriesAreVisible()
    {
        int total = 0;
        int blank = 0;
        for (const char* id : {"general", "misc", "advanced", "basic", "flowchart", "uml", "er"}) {
            const Palette* palette = PaletteLibrary::builtin().palette(QString::fromLatin1(id));
            QVERIFY(palette != nullptr);
            for (const PaletteEntry& entry : palette->entries) {
                const QImage image = renderPaletteThumbnail(entry, QSize(32, 32), 1);
                QVERIFY2(!image.isNull(), qPrintable(entry.key(palette->id)));
                ++total;
                if (!hasVisiblePixels(image))
                    ++blank;
            }
        }
        QVERIFY(total > 300);
        QVERIFY2(blank * 20 < total, qPrintable(QStringLiteral("%1 of %2 thumbnails are blank").arg(blank).arg(total)));
    }
};

QTEST_MAIN(TstPaletteThumbnail)
#include "tst_palette_thumbnail.moc"
