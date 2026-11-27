// printDocument to a PDF printer: one printer page per diagram page.

#include "support/dialogs_documents.h"

#include "qraw/ui/print_support.h"

#include <QFile>
#include <QPrinter>
#include <QTemporaryDir>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

// Pages in a PDF written by Qt: the page-tree objects are "/Type /Pages".
int pdfPageCount(const QByteArray& pdf)
{
    const int all = pdf.count("/Type /Page");
    const int trees = pdf.count("/Type /Pages");
    return all - trees;
}

QByteArray readAll(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

} // namespace

class TstDialogsPrint : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qraw::ui::test::isolateSettings(); }

    void printsEveryPageToPdf()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const core::Document document = twoPageDocument();

        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        const QString output = dir.filePath(QStringLiteral("print.pdf"));
        printer.setOutputFileName(output);
        QVERIFY(printDocument(document, printer, PrintOptions{}));

        const QByteArray pdf = readAll(output);
        QVERIFY(pdf.startsWith("%PDF"));
        QCOMPARE(pdfPageCount(pdf), 2);
    }

    void emptyPageRangeIsRejected()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const core::Document document = simpleDocument();

        QPrinter printer(QPrinter::HighResolution);
        printer.setOutputFormat(QPrinter::PdfFormat);
        printer.setOutputFileName(dir.filePath(QStringLiteral("none.pdf")));
        PrintOptions options;
        options.pages = {7}; // out of range
        QVERIFY(!printDocument(document, printer, options));
    }
};

QTEST_MAIN(TstDialogsPrint)
#include "tst_dialogs_print.moc"
