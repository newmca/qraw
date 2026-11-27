// Diagram text does not follow the UI font DPI: draw.io font sizes are CSS pixels. The same file
// is exported by child processes of qraw-cli with different QT_FONT_DPI and QT_SCALE_FACTOR (the
// offscreen platform reads both; QT_FONT_DPI also gives a device pixel ratio of DPI / 96), and
// the pictures have to be the same pixels.

#include <QDir>
#include <QImage>
#include <QProcess>
#include <QProcessEnvironment>
#include <QTemporaryDir>
#include <QTest>

namespace {

// A plain label, a rich one, point and pixel sizes inside HTML, a relative <font size>, a
// monospace family, a wrapped label, a rotated one and a label on an edge.
constexpr char kDiagram[] =
    R"(<mxfile><diagram id="p" name="P"><mxGraphModel grid="0" page="0"><root><mxCell id="0"/><mxCell id="1" parent="0"/>)"
    R"(<mxCell id="a" value="Plain label text" style="fontSize=14;" vertex="1" parent="1"><mxGeometry x="20" y="20" width="160" height="50" as="geometry"/></mxCell>)"
    R"(<mxCell id="b" value="&lt;b&gt;Rich&lt;/b&gt; label &lt;i&gt;with&lt;/i&gt; spans" style="html=1;whiteSpace=wrap;fontSize=16;" vertex="1" parent="1"><mxGeometry x="200" y="20" width="160" height="50" as="geometry"/></mxCell>)"
    R"(<mxCell id="c" value="&lt;span style=&quot;font-size: 12pt;&quot;&gt;Points&lt;/span&gt; and &lt;font style=&quot;font-size: 20px;&quot;&gt;px&lt;/font&gt; &lt;font size=&quot;5&quot;&gt;size5&lt;/font&gt;" style="html=1;whiteSpace=wrap;fontSize=11;" vertex="1" parent="1"><mxGeometry x="20" y="100" width="340" height="60" as="geometry"/></mxCell>)"
    R"(<mxCell id="d" value="Wrapped plain text that goes over several lines in a narrow box" style="whiteSpace=wrap;fontSize=12;fontFamily=Courier New;" vertex="1" parent="1"><mxGeometry x="380" y="20" width="120" height="140" as="geometry"/></mxCell>)"
    R"(<mxCell id="r" value="Rotated" style="rotation=30;fontSize=13;fontStyle=1;" vertex="1" parent="1"><mxGeometry x="20" y="190" width="100" height="40" as="geometry"/></mxCell>)"
    R"(<mxCell id="e" value="edge label" style="endArrow=classic;fontSize=11;" edge="1" parent="1" source="a" target="r"><mxGeometry relative="1" as="geometry"/></mxCell>)"
    R"(</root></mxGraphModel></diagram></mxfile>)";

struct Setting {
    const char* fontDpi;      // QT_FONT_DPI, or nullptr for the platform's
    const char* scaleFactor;  // QT_SCALE_FACTOR, or nullptr
};

// Exports `input` to `output` in a child process with the given environment; returns the exit code.
int exportWith(const QString& input, const QString& output, const QString& format, const Setting& setting,
               const QString& extra = {})
{
    QProcessEnvironment env = QProcessEnvironment::systemEnvironment();
    env.remove(QStringLiteral("QT_FONT_DPI"));
    env.remove(QStringLiteral("QT_SCALE_FACTOR"));
    if (setting.fontDpi != nullptr)
        env.insert(QStringLiteral("QT_FONT_DPI"), QString::fromLatin1(setting.fontDpi));
    if (setting.scaleFactor != nullptr)
        env.insert(QStringLiteral("QT_SCALE_FACTOR"), QString::fromLatin1(setting.scaleFactor));
    QProcess process;
    process.setProcessEnvironment(env);
    QStringList arguments{QStringLiteral("--export"), input, QStringLiteral("--output"), output,
                          QStringLiteral("--format"), format};
    if (!extra.isEmpty())
        arguments << extra.split(QLatin1Char(' '));
    process.start(QStringLiteral(QRAW_CLI_PATH), arguments);
    if (!process.waitForFinished(60000)) {
        process.kill();
        return -1;
    }
    return process.exitStatus() == QProcess::NormalExit ? process.exitCode() : -2;
}

} // namespace

class TestFontDpi : public QObject {
    Q_OBJECT

private slots:
    void diagramTextIsTheSameAtEveryFontDpi_data()
    {
        QTest::addColumn<QString>("format");
        QTest::addColumn<QString>("extra");
        QTest::newRow("png") << "png" << "";
        QTest::newRow("png at 2x") << "png" << "--scale 2";
        QTest::newRow("svg") << "svg" << "";
    }

    void diagramTextIsTheSameAtEveryFontDpi()
    {
        QFETCH(QString, format);
        QFETCH(QString, extra);
        QTemporaryDir directory;
        QVERIFY(directory.isValid());
        const QString input = directory.filePath(QStringLiteral("labels.drawio"));
        QFile file(input);
        QVERIFY(file.open(QIODevice::WriteOnly));
        file.write(kDiagram);
        file.close();

        // The reference is the platform's own setting; the others raise the logical DPI (and with
        // it the device pixel ratio), lower it below the 96 that browsers assume, and scale the UI.
        const Setting settings[] = {{nullptr, nullptr}, {"96", nullptr}, {"120", nullptr}, {"144", nullptr},
                                    {"72", nullptr},    {"96", "2"},     {"120", "1.5"}};
        QByteArray reference;
        QImage referenceImage;
        for (const Setting& setting : settings) {
            const QString label = QStringLiteral("QT_FONT_DPI=%1 QT_SCALE_FACTOR=%2")
                                      .arg(setting.fontDpi ? setting.fontDpi : "-", setting.scaleFactor ? setting.scaleFactor : "-");
            const QString output = directory.filePath(QStringLiteral("out.") + format);
            QFile::remove(output);
            QVERIFY2(exportWith(input, output, format, setting, extra) == 0, qPrintable(label));
            QFile out(output);
            QVERIFY2(out.open(QIODevice::ReadOnly), qPrintable(label));
            const QByteArray bytes = out.readAll();
            if (format == QLatin1String("svg")) {
                if (reference.isEmpty())
                    reference = bytes;
                QVERIFY2(bytes == reference, qPrintable(label));
                continue;
            }
            // PNG: the same pixels (the file may carry the device's resolution as metadata).
            const QImage image = QImage::fromData(bytes, "PNG").convertToFormat(QImage::Format_ARGB32);
            QVERIFY2(!image.isNull(), qPrintable(label));
            if (referenceImage.isNull())
                referenceImage = image;
            QCOMPARE(image.size(), referenceImage.size());
            QVERIFY2(image == referenceImage, qPrintable(label + QStringLiteral(": pixels differ")));
        }
    }
};

QTEST_GUILESS_MAIN(TestFontDpi)
#include "tst_font_dpi.moc"
