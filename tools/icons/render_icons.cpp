// Rasterises the Qraw application icon into the freedesktop hicolor tree.
//
//   qraw-render-icons <icons-dir> <app-id>
//
// Reads <icons-dir>/qraw.svg (master) and qraw-small.svg (used up to
// 24 px, where the master is unreadable) and writes
// <icons-dir>/hicolor/<N>x<N>/apps/<app-id>.png plus
// <icons-dir>/hicolor/scalable/apps/<app-id>.svg. Not installed; built when
// QRAW_BUILD_TOOLS is ON. tools/icons/pack_icons.py then packs the PNGs into
// qraw.ico and qraw.icns (see resources/README.md).

#include <QColor>
#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QPainter>
#include <QString>
#include <QSvgRenderer>
#include <QTextStream>

namespace {

constexpr int kHicolorSizes[] = {16, 22, 24, 32, 48, 64, 128, 256, 512};
constexpr int kLargestSimplifiedSize = 24;

bool renderPng(QSvgRenderer& renderer, int size, const QString& path)
{
    QImage image(size, size, QImage::Format_ARGB32);
    image.fill(Qt::transparent);
    QPainter painter(&image);
    painter.setRenderHints(QPainter::Antialiasing | QPainter::SmoothPixmapTransform);
    renderer.render(&painter);
    painter.end();
    return image.save(path, "PNG");
}

} // namespace

int main(int argc, char* argv[])
{
    if (qEnvironmentVariableIsEmpty("QT_QPA_PLATFORM"))
        qputenv("QT_QPA_PLATFORM", "offscreen");
    QGuiApplication app(argc, argv);
    QTextStream err(stderr);
    if (argc != 3) {
        err << "usage: qraw-render-icons <icons-dir> <app-id>\n";
        return 2;
    }
    const QDir icons(QString::fromLocal8Bit(argv[1]));
    const QString appId = QString::fromLocal8Bit(argv[2]);

    QSvgRenderer master(icons.filePath(QStringLiteral("qraw.svg")));
    QSvgRenderer simplified(icons.filePath(QStringLiteral("qraw-small.svg")));
    if (!master.isValid() || !simplified.isValid()) {
        err << "cannot load qraw.svg and qraw-small.svg from " << icons.path() << '\n';
        return 1;
    }

    for (const int size : kHicolorSizes) {
        const QString dir = icons.filePath(QStringLiteral("hicolor/%1x%1/apps").arg(size));
        QDir().mkpath(dir);
        QSvgRenderer& source = size <= kLargestSimplifiedSize ? simplified : master;
        if (!renderPng(source, size, dir + QStringLiteral("/%1.png").arg(appId))) {
            err << "cannot write " << dir << '\n';
            return 1;
        }
    }

    const QString scalable = icons.filePath(QStringLiteral("hicolor/scalable/apps"));
    QDir().mkpath(scalable);
    const QString scalableFile = scalable + QStringLiteral("/%1.svg").arg(appId);
    QFile::remove(scalableFile);
    if (!QFile::copy(icons.filePath(QStringLiteral("qraw.svg")), scalableFile)) {
        err << "cannot write " << scalableFile << '\n';
        return 1;
    }
    return 0;
}
