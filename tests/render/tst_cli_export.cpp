// `qraw-cli --export` driven through cli::run: plumbing, naming, exit codes.

#include "qraw/cli/cli.h"
#include "qraw/format/codec.h"
#include "qraw/format/embed.h"
#include "qraw/format/semantic.h"


#include <QFile>
#include <QImage>
#include <QTemporaryDir>
#include <QTest>
#include <QTextStream>

using namespace qraw;

namespace {

const char* kTwoPages =
    "<mxfile>"
    "<diagram id=\"p1\" name=\"One\"><mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
    "<mxCell id=\"a\" value=\"A\" style=\"rounded=0\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"10\" y=\"10\" width=\"80\" height=\"40\" as=\"geometry\"/></mxCell>"
    "</root></mxGraphModel></diagram>"
    "<diagram id=\"p2\" name=\"Two\"><mxGraphModel><root><mxCell id=\"0\"/><mxCell id=\"1\" parent=\"0\"/>"
    "<mxCell id=\"b\" value=\"B\" style=\"ellipse\" vertex=\"1\" parent=\"1\"><mxGeometry x=\"0\" y=\"0\" width=\"60\" height=\"60\" as=\"geometry\"/></mxCell>"
    "</root></mxGraphModel></diagram></mxfile>";

struct Outcome {
    int code = -1;
    QString out;
    QString err;
};

} // namespace

class CliExportTest : public QObject {
    Q_OBJECT

    QTemporaryDir dir_;

    QString path(const QString& name) const { return dir_.filePath(name); }

    Outcome run(const QStringList& args) const
    {
        Outcome o;
        QTextStream out(&o.out);
        QTextStream err(&o.err);
        o.code = cli::run(QStringList{QStringLiteral("qraw-cli")} + args, out, err);
        out.flush();
        err.flush();
        return o;
    }

    QByteArray read(const QString& name) const
    {
        QFile f(path(name));
        return f.open(QIODevice::ReadOnly) ? f.readAll() : QByteArray();
    }

private slots:
    void initTestCase()
    {
        QVERIFY(dir_.isValid());
        QFile f(path("in.drawio"));
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(kTwoPages);
    }

    void exportsSvgPngAndPdf()
    {
        QCOMPARE(run({"--export", path("in.drawio"), "--output", path("o.svg")}).code, cli::Success);
        QVERIFY(QFile::exists(path("o.svg")));

        QCOMPARE(run({"--export", path("in.drawio"), "--output", path("o.png"), "--scale", "2", "--border", "4"}).code,
                 cli::Success);
        QVERIFY(!QImage::fromData(read("o.png")).isNull());

        QCOMPARE(run({"--export", path("in.drawio"), "--output", path("o.pdf")}).code, cli::Success);
        QVERIFY(read("o.pdf").startsWith("%PDF"));
    }

    void formatOptionOverridesTheExtension()
    {
        QCOMPARE(run({"--export", path("in.drawio"), "--format", "png", "--output", path("odd.bin")}).code, cli::Success);
        QVERIFY(!QImage::fromData(read("odd.bin")).isNull());
    }

    void allPagesNaming()
    {
        QCOMPARE(run({"--export", path("in.drawio"), "--all-pages", "--output", path("pages.png")}).code, cli::Success);
        QVERIFY(QFile::exists(path("pages-1.png")));
        QVERIFY(QFile::exists(path("pages-2.png")));
        QVERIFY(!QFile::exists(path("pages.png")));

        // PDF is one file for all pages
        QCOMPARE(run({"--export", path("in.drawio"), "--all-pages", "--output", path("all.pdf")}).code, cli::Success);
        QVERIFY(QFile::exists(path("all.pdf")));
        QVERIFY(!QFile::exists(path("all-1.pdf")));
    }

    void secondPageIsSelectable()
    {
        QCOMPARE(run({"--export", path("in.drawio"), "--page", "2", "--output", path("p2.png")}).code, cli::Success);
        const QImage first = QImage::fromData(read("pages-1.png"));
        const QImage second = QImage::fromData(read("p2.png"));
        QVERIFY(!second.isNull());
        if (!first.isNull())
            QVERIFY(first.size() != second.size());
    }

    void badInputsGiveFailureOrUsage()
    {
        QCOMPARE(run({"--export", path("in.drawio"), "--page", "3", "--output", path("x.png")}).code, cli::Failure);
        QVERIFY(!QFile::exists(path("x.png")));
        QCOMPARE(run({"--export", path("in.drawio"), "--page", "0", "--output", path("x.png")}).code, cli::Usage);
        QCOMPARE(run({"--export", path("in.drawio"), "--format", "gif", "--output", path("x.gif")}).code, cli::Usage);
        QCOMPARE(run({"--export", path("in.drawio"), "--output", path("x.unknown")}).code, cli::Usage);
        QCOMPARE(run({"--export", path("in.drawio")}).code, cli::Usage);
        QCOMPARE(run({"--export", path("in.drawio"), "--scale", "-1", "--output", path("x.png")}).code, cli::Usage);
        QCOMPARE(run({"--export", path("missing.drawio"), "--output", path("x.png")}).code, cli::Failure);
    }

    void embedThenReexportFromThePng()
    {
        QCOMPARE(run({"--export", path("in.drawio"), "--embed", "--output", path("emb.png")}).code, cli::Success);
        const auto original = format::readFile(path("in.drawio"));
        const auto embedded = format::readAnyFile(path("emb.png"));
        QVERIFY(embedded.ok());
        std::string difference;
        QVERIFY2(format::semanticEqual(*original.document, *embedded.document, &difference), difference.c_str());

        QCOMPARE(run({"--export", path("emb.png"), "--output", path("again.svg")}).code, cli::Success);
        QVERIFY(QFile::exists(path("again.svg")));
    }
};

QTEST_MAIN(CliExportTest)
#include "tst_cli_export.moc"
