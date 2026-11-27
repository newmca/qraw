// The palette dock: sections, search, thumbnails, favorites,
// click-to-insert and drop.

#include "support/test_support.h"

#include "qraw/ui/palette_dock.h"

#include <QCoreApplication>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QSettings>
#include <QTimer>
#include <QToolButton>

#include <algorithm>

using namespace qraw;
using namespace qraw::ui;

namespace {

// The thumbnail buttons whose tooltip starts with `title`, visible in the dock.
QList<QToolButton*> itemButtons(PaletteDock& dock, const QString& title)
{
    QList<QToolButton*> found;
    for (QToolButton* button : dock.findChildren<QToolButton*>()) {
        if (button->toolTip().startsWith(title + QLatin1String(" (")) && button->isVisibleTo(&dock))
            found.push_back(button);
    }
    return found;
}

QToolButton* sectionHeader(PaletteDock& dock, const QString& title)
{
    for (QToolButton* button : dock.findChildren<QToolButton*>()) {
        if (button->text() == title && button->toolTip().isEmpty())
            return button;
    }
    return nullptr;
}

bool hasVisiblePixels(const QIcon& icon)
{
    const QImage image = icon.pixmap(QSize(32, 32)).toImage();
    for (int y = 0; y < image.height(); ++y) {
        for (int x = 0; x < image.width(); ++x) {
            if (qAlpha(image.pixel(x, y)) > 0)
                return true;
        }
    }
    return false;
}

int cellCount(const DocumentSession& session)
{
    return static_cast<int>(session.currentPage()->cellCount());
}

} // namespace

class TstPaletteDock : public QObject {
    Q_OBJECT

private slots:
    void initTestCase()
    {
        qraw::ui::test::isolateSettings();
        QCoreApplication::setOrganizationName(QStringLiteral("QrawPaletteTest"));
        QCoreApplication::setApplicationName(QStringLiteral("QrawPaletteTest"));
    }

    void init() { QSettings().clear(); }

    void showsExpandedGeneralSectionWithThumbnails()
    {
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        dock.resize(280, 600);
        dock.show();

        const auto rectangle = itemButtons(dock, QStringLiteral("Rectangle"));
        QCOMPARE(rectangle.size(), 1); // only General is expanded; Misc etc. are collapsed
        QCOMPARE(rectangle.first()->toolTip(), QStringLiteral("Rectangle (120 x 60)"));
        // Thumbnails are rendered lazily after the section is shown.
        QTRY_VERIFY(!rectangle.first()->icon().isNull());
        QVERIFY(hasVisiblePixels(rectangle.first()->icon()));
        // The queue renders a few thumbnails per event-loop pass; all of General gets one.
        const auto buttons = dock.findChildren<QToolButton*>();
        QTRY_VERIFY(std::all_of(buttons.begin(), buttons.end(),
                                [](const QToolButton* b) { return b->toolTip().isEmpty() || !b->icon().isNull(); }));
        // Collapsed sections hold no buttons yet.
        QVERIFY(itemButtons(dock, QStringLiteral("Lambda")).isEmpty());
        test::grabToPng(dock, QStringLiteral("palette-dock"));
    }

    // The thumbnails are rendered at the dock's device pixel ratio (not at 1 and scaled), and a move
    // to a screen with another ratio renders them again.
    void thumbnailsFollowTheDevicePixelRatio()
    {
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        dock.resize(280, 600);
        dock.show();
        const auto rectangle = itemButtons(dock, QStringLiteral("Rectangle"));
        QCOMPARE(rectangle.size(), 1);
        QToolButton* button = rectangle.first();
        QTRY_VERIFY(!button->icon().isNull());

        const qreal dpr = dock.devicePixelRatioF();
        const QSize logical = button->iconSize();
        const QList<QSize> sizes = button->icon().availableSizes();
        QVERIFY(!sizes.isEmpty());
        // The pixmap has the logical size times the ratio in pixels (a QIcon of one pixmap lists its pixel size).
        QCOMPARE(sizes.first(), (QSizeF(logical) * dpr).toSize());
        const QPixmap pixmap = button->icon().pixmap(logical);
        QCOMPARE(pixmap.devicePixelRatio(), dpr);
        QCOMPARE(pixmap.deviceIndependentSize().toSize(), logical);

        // A ratio change (QEvent::DevicePixelRatioChange from Qt 6.6, ScreenChangeInternal before it)
        // drops the cache and renders the visible buttons again.
        const qint64 before = button->icon().cacheKey();
        QEvent change(QEvent::ScreenChangeInternal);
        QCoreApplication::sendEvent(&dock, &change);
        QTRY_VERIFY(button->icon().cacheKey() != before);
        QVERIFY(hasVisiblePixels(button->icon()));
    }

    void searchReplacesSectionsWithResults()
    {
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        dock.resize(280, 600);
        dock.show();
        QVERIFY(!itemButtons(dock, QStringLiteral("Rectangle")).isEmpty());

        QTest::keyClicks(dock.searchField(), QStringLiteral("ellipse"));
        // Debounced: the results appear after the timer fired.
        QTRY_VERIFY(itemButtons(dock, QStringLiteral("Rectangle")).isEmpty()); // sections are hidden while searching
        QTRY_VERIFY(!itemButtons(dock, QStringLiteral("Ellipse")).isEmpty());
        QVERIFY(!sectionHeader(dock, QStringLiteral("General"))->isVisibleTo(&dock));
        test::grabToPng(dock, QStringLiteral("palette-dock-search"));

        dock.searchField()->clear();
        QTRY_VERIFY(!itemButtons(dock, QStringLiteral("Rectangle")).isEmpty());
        QTRY_COMPARE(itemButtons(dock, QStringLiteral("Ellipse")).size(), 1);
    }

    void setSearchTextSearchesImmediately()
    {
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        dock.show();
        dock.setSearchText(QStringLiteral("aws lambda"));
        QTRY_VERIFY(!itemButtons(dock, QStringLiteral("Lambda")).isEmpty());
        dock.setSearchText(QStringLiteral("zzzzqqqq"));
        QTRY_VERIFY(itemButtons(dock, QStringLiteral("Lambda")).isEmpty());
    }

    void expandingASectionIsPersisted()
    {
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        QCOMPARE(settings.expandedPalettes(), QStringList{QStringLiteral("general")});
        {
            PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
            dock.resize(280, 600);
            dock.show();
            QToolButton* misc = sectionHeader(dock, QStringLiteral("Misc"));
            QVERIFY(misc != nullptr);
            QVERIFY(itemButtons(dock, QStringLiteral("Curved Text")).isEmpty());
            misc->click();
            QTRY_VERIFY(!itemButtons(dock, QStringLiteral("Curved Text")).isEmpty());
            QVERIFY(settings.expandedPalettes().contains(QStringLiteral("misc")));
            QVERIFY(settings.expandedPalettes().contains(QStringLiteral("general")));
        }
        // A new dock restores the state.
        PaletteDock again(PaletteLibrary::builtin(), session, *canvas, settings);
        again.show();
        QTRY_VERIFY(!itemButtons(again, QStringLiteral("Curved Text")).isEmpty());
    }

    void favoritesPersistAndAreShownFirst()
    {
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        settings.setFavoriteShapes({QStringLiteral("general/ellipse"), QStringLiteral("nope/missing")});
        settings.addRecentShape(QStringLiteral("general/rectangle"));
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        dock.resize(280, 600);
        dock.show();
        QToolButton* favorites = sectionHeader(dock, QStringLiteral("Favorites"));
        QVERIFY(favorites != nullptr && favorites->isVisibleTo(&dock));
        QVERIFY(sectionHeader(dock, QStringLiteral("Recently Used"))->isVisibleTo(&dock));
        // Ellipse appears in Favorites and in General; the unknown key is dropped.
        QTRY_COMPARE(itemButtons(dock, QStringLiteral("Ellipse")).size(), 2);
        QTRY_COMPARE(itemButtons(dock, QStringLiteral("Rectangle")).size(), 2);

        // Context menu on a General item: Add to Favorites (first menu entry).
        QToolButton* circle = itemButtons(dock, QStringLiteral("Circle")).first();
        QTimer::singleShot(100, [] {
            auto* menu = qobject_cast<QMenu*>(QApplication::activePopupWidget());
            if (menu == nullptr)
                return;
            QTest::keyClick(menu, Qt::Key_Down);
            QTest::keyClick(menu, Qt::Key_Return);
        });
        emit circle->customContextMenuRequested(QPoint(4, 4));
        QCOMPARE(settings.favoriteShapes(),
                 (QStringList{QStringLiteral("general/ellipse"), QStringLiteral("nope/missing"),
                              QStringLiteral("general/circle")}));
        QTRY_COMPARE(itemButtons(dock, QStringLiteral("Circle")).size(), 2);
    }

    void unknownKeysAreNotInserted()
    {
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        const QByteArray before = test::savedBytes(session);
        QVERIFY(!dock.insertEntry(QStringLiteral("nope/missing")));
        QVERIFY(!dock.insertEntry(QString()));
        QCOMPARE(test::savedBytes(session), before);
        QVERIFY(settings.recentShapes().isEmpty());
    }

    void canDropAcceptsCellsMimeOnly()
    {
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        QMimeData cells;
        cells.setData(QString::fromLatin1(kCellsMimeType), "<mxGraphModel/>");
        QVERIFY(dock.canDrop(cells));
        QMimeData text;
        text.setText(QStringLiteral("hello"));
        QVERIFY(!dock.canDrop(text));
    }

    void clickInsertsTheEntryAndUndoRestores()
    {
        if (!test::transferAvailable() || !test::editActionsAvailable())
            QSKIP("needs cell_transfer and edit_actions");
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        dock.resize(280, 600);
        dock.show();
        const QByteArray before = test::savedBytes(session);
        const int cells = cellCount(session);

        QToolButton* rectangle = itemButtons(dock, QStringLiteral("Rectangle")).first();
        rectangle->click();
        QCOMPARE(cellCount(session), cells + 1);
        QCOMPARE(session.selection().cells().size(), std::size_t{1});
        QCOMPARE(settings.recentShapes().value(0), QStringLiteral("general/rectangle"));

        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
    }

    void dropInsertsCellsAtThePoint()
    {
        if (!test::transferAvailable() || !test::editActionsAvailable())
            QSKIP("needs cell_transfer and edit_actions");
        DocumentSession session(test::simpleDocument());
        auto canvas = test::makeCanvas(session);
        AppSettings settings;
        PaletteDock dock(PaletteLibrary::builtin(), session, *canvas, settings);
        const PaletteEntry* entry = PaletteLibrary::builtin().entry(QStringLiteral("general/ellipse"));
        QVERIFY(entry != nullptr);
        QMimeData mime;
        mime.setData(QString::fromLatin1(kCellsMimeType), QByteArray::fromStdString(entry->cellsXml()));
        mime.setText(QString::fromStdString(entry->cellsXml()));
        QVERIFY(dock.canDrop(mime));

        const QByteArray before = test::savedBytes(session);
        const int cells = cellCount(session);
        QVERIFY(dock.drop(mime, QPointF(500, 300)));
        QCOMPARE(cellCount(session), cells + 1);
        QVERIFY(session.undo());
        QCOMPARE(test::savedBytes(session), before);
    }
};

QTEST_MAIN(TstPaletteDock)
#include "tst_palette_dock.moc"
