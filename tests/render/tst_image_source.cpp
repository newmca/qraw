// resolveImage: data URIs (standard and draw.io variants), local files and
// MIME detection. Nothing here touches the network.

#include "qraw/render/image_source.h"

#include <QDir>
#include <QFile>
#include <QTemporaryDir>
#include <QTest>
#include <QUrl>

using namespace qraw::render;

namespace {

const QByteArray kPng = QByteArray::fromBase64(
    "iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8z8BQDwAEhQGAhKmMIQAAAABJRU5ErkJggg==");
const QByteArray kSvg = "<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"10\" height=\"10\"/>";

std::string utf8(const QByteArray& bytes) { return bytes.toStdString(); }

bool writeFile(const QString& path, const QByteArray& bytes)
{
    QFile file(path);
    return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
}

} // namespace

class ImageSourceTest : public QObject {
    Q_OBJECT

private slots:
    void base64Png()
    {
        const auto image = resolveImage(utf8("data:image/png;base64," + kPng.toBase64()), {});
        QVERIFY(image);
        QCOMPARE(image->mimeType, QString("image/png"));
        QCOMPARE(image->bytes, kPng);
    }

    // The fast decoder (padded standard base64) and Qt's lenient one give the same bytes,
    // whatever the length (all three padding cases) and the content.
    void fastBase64MatchesQt()
    {
        quint32 state = 7;
        for (int length = 1; length < 200; ++length) {
            QByteArray raw(length, 0);
            for (char& c : raw) {
                state = state * 1664525u + 1013904223u;
                c = static_cast<char>(state >> 24);
            }
            raw.prepend("\x89PNG");
            const auto image = resolveImage(utf8("data:image/png," + raw.toBase64()), {});
            QVERIFY(image);
            QCOMPARE(image->bytes, raw);
        }
    }

    // Input the fast decoder declines (white space, base64url, no padding) still decodes.
    void lenientBase64StillWorks()
    {
        const QByteArray raw = QByteArray("\x89PNG") + QByteArray::fromHex("fbffbfefff00");
        const QByteArray plain = raw.toBase64();
        QCOMPARE(resolveImage(utf8("data:image/png;base64," + plain.left(8) + "\n" + plain.mid(8)), {})->bytes, raw);
        QCOMPARE(resolveImage(utf8("data:image/png;base64," + raw.toBase64(QByteArray::Base64UrlEncoding)), {})->bytes, raw);
        QCOMPARE(resolveImage(utf8("data:image/png;base64," + raw.toBase64(QByteArray::OmitTrailingEquals)), {})->bytes, raw);
        QCOMPARE(resolveImage(utf8("  data:image/png," + plain + "  "), {})->bytes, raw);
        QCOMPARE(resolveImage(utf8("DATA:image/png;BASE64," + plain), {})->bytes, raw);
    }

    void percentEncodedSvg()
    {
        const auto image = resolveImage(utf8("data:image/svg+xml," + QUrl::toPercentEncoding(kSvg)), {});
        QVERIFY(image);
        QCOMPARE(image->mimeType, QString("image/svg+xml"));
        QCOMPARE(image->bytes, kSvg);
    }

    void unencodedSvgWithParameters()
    {
        const auto image = resolveImage(utf8("data:image/svg+xml;charset=utf-8," + kSvg), {});
        QVERIFY(image);
        QCOMPARE(image->bytes, kSvg);
    }

    void drawioBareBase64Svg()
    {
        // draw.io drops ";base64" because ';' separates style keys.
        const auto image = resolveImage(utf8("data:image/svg+xml," + kSvg.toBase64()), {});
        QVERIFY(image);
        QCOMPARE(image->mimeType, QString("image/svg+xml"));
        QCOMPARE(image->bytes, kSvg);
    }

    void drawioBareBase64Png()
    {
        const auto image = resolveImage(utf8("data:image/png," + kPng.toBase64()), {});
        QVERIFY(image);
        QCOMPARE(image->mimeType, QString("image/png"));
        QCOMPARE(image->bytes, kPng);
    }

    void jpgMimeIsNormalized()
    {
        const QByteArray jpeg = QByteArray("\xFF\xD8\xFF\xE0", 4) + "rest";
        const auto image = resolveImage(utf8("data:image/jpg;base64," + jpeg.toBase64()), {});
        QVERIFY(image);
        QCOMPARE(image->mimeType, QString("image/jpeg"));
    }

    void brokenOrEmptyDataIsRejected()
    {
        QVERIFY(!resolveImage("data:image/png;base64,", {}));
        QVERIFY(!resolveImage("data:image/png", {})); // no comma
        QVERIFY(!resolveImage("data:text/plain,hello", {})); // not an image
        QVERIFY(!resolveImage("", {}));
    }

    void remoteSchemesAreRejected()
    {
        const QStringList sources{"http://example.com/a.png", "https://example.com/a.png", "ftp://example.com/a.png",
                                  "javascript:alert(1)", "blob:https://example.com/x", "img/lib/clip_art/x.png"};
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ImageResolveContext context;
        context.baseDirectory = dir.path();
        for (const QString& source : sources)
            QVERIFY2(!resolveImage(source.toStdString(), context), qPrintable(source));
    }

    void localFiles()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(QDir(dir.path()).mkpath("sub"));
        QVERIFY(writeFile(dir.filePath("sub/pic.png"), kPng));

        ImageResolveContext context;
        context.baseDirectory = dir.path();

        const auto relative = resolveImage("sub/pic.png", context);
        QVERIFY(relative);
        QCOMPARE(relative->mimeType, QString("image/png"));
        QCOMPARE(relative->bytes, kPng);

        const auto absolute = resolveImage(dir.filePath("sub/pic.png").toStdString(), {});
        QVERIFY(absolute);
        QCOMPARE(absolute->bytes, kPng);

        const auto url = resolveImage(QUrl::fromLocalFile(dir.filePath("sub/pic.png")).toString().toStdString(), {});
        QVERIFY(url);
        QCOMPARE(url->bytes, kPng);

        QVERIFY(!resolveImage("sub/missing.png", context));
        QVERIFY(!resolveImage("sub", context)); // a directory
        QVERIFY(!resolveImage("sub/pic.png", {})); // relative without a base directory
    }

    void localFilesCanBeDisabled()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath("pic.png"), kPng));

        ImageResolveContext context;
        context.baseDirectory = dir.path();
        context.allowLocalFiles = false;
        QVERIFY(!resolveImage("pic.png", context));
        QVERIFY(!resolveImage(dir.filePath("pic.png").toStdString(), context));
        QVERIFY(!resolveImage(QUrl::fromLocalFile(dir.filePath("pic.png")).toString().toStdString(), context));
        // data URIs are not files
        QVERIFY(resolveImage(utf8("data:image/png;base64," + kPng.toBase64()), context));
    }

    void oversizedPayloadsAreRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        QVERIFY(writeFile(dir.filePath("big.png"), kPng + QByteArray(100, 'x')));

        ImageResolveContext context;
        context.baseDirectory = dir.path();
        context.maxBytes = 50;
        QVERIFY(!resolveImage("big.png", context));
        QVERIFY(!resolveImage(utf8("data:image/png;base64," + (kPng + QByteArray(100, 'x')).toBase64()), context));

        context.maxBytes = 1000;
        QVERIFY(resolveImage("big.png", context));
    }

    void mimeFromExtension()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const QList<std::pair<QString, QString>> cases{{"a.png", "image/png"},   {"a.JPG", "image/jpeg"},
                                                       {"a.jpeg", "image/jpeg"}, {"a.gif", "image/gif"},
                                                       {"a.svg", "image/svg+xml"}, {"a.webp", "image/webp"},
                                                       {"a.bmp", "image/bmp"}};
        ImageResolveContext context;
        context.baseDirectory = dir.path();
        for (const auto& [name, mime] : cases) {
            QVERIFY(writeFile(dir.filePath(name), "x")); // content does not matter when the extension decides
            const auto image = resolveImage(name.toStdString(), context);
            QVERIFY2(image, qPrintable(name));
            QCOMPARE(image->mimeType, mime);
        }
    }

    void mimeSniffing()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        struct Case {
            QByteArray bytes;
            QString mime;
        };
        const QList<Case> cases{{kPng, "image/png"},
                                {QByteArray("\xFF\xD8\xFF\xE0", 4) + "data", "image/jpeg"},
                                {"GIF89a....", "image/gif"},
                                {"BM......", "image/bmp"},
                                {QByteArray("RIFF\x10\0\0\0WEBPVP8 ", 16), "image/webp"},
                                {kSvg, "image/svg+xml"},
                                {"  \n<?xml version=\"1.0\"?><svg/>", "image/svg+xml"}};
        ImageResolveContext context;
        context.baseDirectory = dir.path();
        int index = 0;
        for (const Case& c : cases) {
            const QString name = QString("blob%1.dat").arg(index++);
            QVERIFY(writeFile(dir.filePath(name), c.bytes));
            const auto image = resolveImage(name.toStdString(), context);
            QVERIFY2(image, qPrintable(name));
            QCOMPARE(image->mimeType, c.mime);

            // the same through a data URI of unknown type
            const auto fromData = resolveImage(utf8("data:application/octet-stream;base64," + c.bytes.toBase64()), {});
            QVERIFY2(fromData, qPrintable(name));
            QCOMPARE(fromData->mimeType, c.mime);
        }

        QVERIFY(writeFile(dir.filePath("junk.dat"), "just some text"));
        QVERIFY(!resolveImage("junk.dat", context));
    }
};

QTEST_MAIN(ImageSourceTest)
#include "tst_image_source.moc"
