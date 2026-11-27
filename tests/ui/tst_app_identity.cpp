// Application identity: desktop file name and window icon (app_setup.h).

#include "qraw/ui/app_setup.h"

#include <QFile>
#include <QGuiApplication>
#include <QIcon>
#include <QRegularExpression>
#include <QtTest>

using namespace qraw::ui;

namespace {

// The id the build was configured with (QRAW_APP_ID in the root CMakeLists.txt) and the folders
// of the files that carry it (compile definitions of this test).
const QString kConfiguredId = QStringLiteral(QRAW_APP_ID);
const QString kLinuxDir = QStringLiteral(QRAW_LINUX_RESOURCE_DIR);
const QString kIconDir = QStringLiteral(QRAW_ICON_RESOURCE_DIR);

QString readFile(const QString& path)
{
    QFile file(path);
    return file.open(QIODevice::ReadOnly | QIODevice::Text) ? QString::fromUtf8(file.readAll()) : QString();
}

// The first capture of `pattern` in the file, or an empty string.
QString capture(const QString& path, const QString& pattern)
{
    return QRegularExpression(pattern).match(readFile(path)).captured(1);
}

} // namespace

class TstAppIdentity : public QObject {
    Q_OBJECT
private slots:
    void desktopFileNameIsTheApplicationId()
    {
        setupApplicationIdentity();
        // Installed file names and the AppStream id derive from this exact string.
        QCOMPARE(applicationId(), QStringLiteral("io.github.newmca.qraw"));
        QCOMPARE(QGuiApplication::desktopFileName(), applicationId());
    }

    void idIsAllLowercaseReverseDns()
    {
        // AppStream (cid-contains-uppercase-letter) and Flathub want a lowercase id; the repository
        // is github.com/newmca/qraw.
        QCOMPARE(kConfiguredId, kConfiguredId.toLower());
        QVERIFY(QRegularExpression(QStringLiteral("^[a-z][a-z0-9_-]*(\\.[a-z][a-z0-9_-]*){2,}$")).match(kConfiguredId).hasMatch());
    }

    void installedFilesAgreeWithTheId()
    {
        setupApplicationIdentity();
        const QString id = QGuiApplication::desktopFileName();
        QCOMPARE(id, kConfiguredId);

        // The three files exist under the id's name, and the names inside them say the same id.
        for (const char* suffix : {".desktop", ".metainfo.xml", ".xml"})
            QVERIFY2(QFile::exists(kLinuxDir + u'/' + id + QLatin1String(suffix)), suffix);
        const QString desktop = kLinuxDir + u'/' + id + QStringLiteral(".desktop");
        const QString metainfo = kLinuxDir + u'/' + id + QStringLiteral(".metainfo.xml");
        QCOMPARE(capture(metainfo, QStringLiteral("<id>([^<]+)</id>")), id);
        QCOMPARE(capture(metainfo, QStringLiteral("<launchable type=\"desktop-id\">([^<]+)</launchable>")),
                 id + QStringLiteral(".desktop"));
        QCOMPARE(capture(desktop, QStringLiteral("(?m)^Icon=(.+)$")), id);

        // The icon the desktop file names is installed under that name.
        QVERIFY(QFile::exists(kIconDir + QStringLiteral("/hicolor/scalable/apps/") + id + QStringLiteral(".svg")));
        QVERIFY(QFile::exists(kIconDir + QStringLiteral("/hicolor/256x256/apps/") + id + QStringLiteral(".png")));
    }

    void windowIconHasSmallAndLargePixmaps()
    {
        setupApplicationIdentity();
        const QIcon icon = QGuiApplication::windowIcon();
        QVERIFY(!icon.isNull());
        // The bundled PNGs are what the icon can offer: the sizes of the hicolor set are all there,
        // including 32 px, the 16 px icon of a 2x screen, and the large ones.
        for (const int size : {16, 22, 24, 32, 48, 64, 128, 256, 512})
            QVERIFY2(icon.availableSizes().contains(QSize(size, size)), qPrintable(QStringLiteral("no %1 px file").arg(size)));
        // A small pixmap is made from them at the screen's device pixel ratio: at a fractional ratio
        // (QT_FONT_DPI=120 on the offscreen platform) it has more pixels than the logical size and
        // says so with its ratio, so the logical size is what is compared. (Qt's own icon engine
        // answers a request for more than about 22 logical pixels with a small file above ratio 1;
        // that is not Qraw's to test, and the window icon is asked for 16 to 48 px by the shell.)
        const QPixmap small = icon.pixmap(16);
        QVERIFY(!small.isNull());
        QCOMPARE(small.deviceIndependentSize().toSize(), QSize(16, 16));
        QCOMPARE(small.size(), (QSizeF(16, 16) * small.devicePixelRatio()).toSize());
    }

    void doesNotChangeTheSettingsIdentity()
    {
        const QString organization = QCoreApplication::organizationName();
        const QString name = QCoreApplication::applicationName();
        setupApplicationIdentity();
        QCOMPARE(QCoreApplication::organizationName(), organization);
        QCOMPARE(QCoreApplication::applicationName(), name);
    }
};

QTEST_MAIN(TstAppIdentity)
#include "tst_app_identity.moc"
