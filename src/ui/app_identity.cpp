// Application identity (app_setup.h): desktop file name and window icon.
#include "qraw/ui/app_setup.h"

#include <QDir>
#include <QGuiApplication>
#include <QIcon>
#include <QSize>

namespace qraw::ui {

namespace {

// The bundled hicolor PNGs (resources/icons/hicolor, compiled into :/qraw/icons).
QIcon bundledIcon(const QString& appId)
{
    const QString root = QStringLiteral(":/qraw/icons/hicolor");
    QIcon icon;
    for (const QString& sizeDir : QDir(root).entryList(QDir::Dirs | QDir::NoDotAndDotDot)) {
        const QStringList parts = sizeDir.split(u'x'); // "16x16"; "scalable" has no PNG
        bool ok = false;
        const int size = parts.size() == 2 ? parts.first().toInt(&ok) : 0;
        if (ok)
            icon.addFile(QStringLiteral("%1/%2/apps/%3.png").arg(root, sizeDir, appId), QSize(size, size));
    }
    return icon;
}

} // namespace

QString applicationId()
{
    return QStringLiteral(QRAW_APP_ID);
}

void setupApplicationIdentity()
{
    const QString appId = applicationId();
    // The Wayland app_id and the name a desktop environment matches to the .desktop file.
    QGuiApplication::setDesktopFileName(appId);
    // An installed icon theme entry wins; the bundled PNGs cover builds run in place and
    // platforms without freedesktop themes.
    QGuiApplication::setWindowIcon(QIcon::fromTheme(appId, bundledIcon(appId)));
}

} // namespace qraw::ui
