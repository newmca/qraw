// runExport (every format, all pages, selection) and the ExportDialog controls.

#include "support/dialogs_documents.h"

#include "qraw/ui/export_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDoubleSpinBox>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QRadioButton>
#include <QSpinBox>
#include <QTemporaryDir>
#include <QtTest>

using namespace qraw;
using namespace qraw::ui;
using namespace qraw::ui::test;

namespace {

QByteArray readAll(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return {};
    return file.readAll();
}

QDoubleSpinBox* zoomOf(ExportDialog& dialog)
{
    return dialog.findChild<QDoubleSpinBox*>(QStringLiteral("export.zoom"));
}

QComboBox* dpiOf(ExportDialog& dialog)
{
    return dialog.findChild<QComboBox*>(QStringLiteral("export.dpi"));
}

} // namespace

class TstDialogsExport : public QObject {
    Q_OBJECT

private slots:
    void initTestCase() { qraw::ui::test::isolateSettings(); }

    void exportsEveryFormat()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const core::Document document = simpleDocument();

        ExportRequest png;
        png.options.format = render::ExportFormat::Png;
        png.path = dir.filePath(QStringLiteral("page.png"));
        QVERIFY(runExport(document, 0, {}, png));
        QImage image;
        QVERIFY(image.load(png.path));
        QVERIFY(!image.isNull());

        ExportRequest jpeg = png;
        jpeg.options.format = render::ExportFormat::Jpeg;
        jpeg.path = dir.filePath(QStringLiteral("page.jpg"));
        QVERIFY(runExport(document, 0, {}, jpeg));
        QVERIFY(QImage(jpeg.path).isNull() == false);

        ExportRequest svg = png;
        svg.options.format = render::ExportFormat::Svg;
        svg.path = dir.filePath(QStringLiteral("page.svg"));
        QVERIFY(runExport(document, 0, {}, svg));
        QVERIFY(readAll(svg.path).contains("<svg"));

        ExportRequest pdf = png;
        pdf.options.format = render::ExportFormat::Pdf;
        pdf.path = dir.filePath(QStringLiteral("page.pdf"));
        QVERIFY(runExport(document, 0, {}, pdf));
        QVERIFY(readAll(pdf.path).startsWith("%PDF"));
    }

    void allPagesWritesNumberedImages()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const core::Document document = twoPageDocument();
        QCOMPARE(document.pageCount(), std::size_t{2});

        ExportRequest request;
        request.options.format = render::ExportFormat::Png;
        request.scope = ExportScope::AllPages;
        request.path = dir.filePath(QStringLiteral("all.png"));
        QVERIFY(runExport(document, 0, {}, request));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("all-1.png"))));
        QVERIFY(QFileInfo::exists(dir.filePath(QStringLiteral("all-2.png"))));
    }

    void selectionScopeCropsToSelection()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        const core::Document document = simpleDocument();

        ExportRequest whole;
        whole.options.format = render::ExportFormat::Png;
        whole.path = dir.filePath(QStringLiteral("whole.png"));
        QVERIFY(runExport(document, 0, {}, whole));

        ExportRequest selected = whole;
        selected.scope = ExportScope::Selection;
        selected.path = dir.filePath(QStringLiteral("selected.png"));
        QVERIFY(runExport(document, 0, {"a"}, selected));

        QVERIFY(QImage(selected.path).width() < QImage(whole.path).width());
        QVERIFY(!runExport(document, 0, {}, selected)); // nothing selected
    }

    void dialogReflectsControls()
    {
        QTemporaryDir dir;
        QVERIFY(dir.isValid());
        ExportDialog dialog(true, dir.filePath(QStringLiteral("drawing.drawio")));
        dialog.setFormat(render::ExportFormat::Png);
        dialog.setOutputPath(dir.filePath(QStringLiteral("out.png")));
        grabToPng(dialog, QStringLiteral("dialogs-export"));

        // 96 DPI is 100 %: zoom and DPI follow each other.
        QCOMPARE(dpiOf(dialog)->currentText(), QStringLiteral("96"));
        zoomOf(dialog)->setValue(200);
        QCOMPARE(dpiOf(dialog)->currentText(), QStringLiteral("192"));
        dpiOf(dialog)->setCurrentText(QStringLiteral("300"));
        QCOMPARE(zoomOf(dialog)->value(), 312.5);

        dialog.findChild<QSpinBox*>(QStringLiteral("export.border"))->setValue(5);
        dialog.findChild<QCheckBox*>(QStringLiteral("export.transparent"))->setChecked(true);
        dialog.findChild<QRadioButton*>(QStringLiteral("export.scope.selection"))->setChecked(true);

        ExportRequest request = dialog.request();
        QCOMPARE(request.options.format, render::ExportFormat::Png);
        QCOMPARE(request.options.scale, 312.5 / 100);
        QCOMPARE(request.options.border, 5.0);
        QVERIFY(request.options.transparent);
        QVERIFY(!request.options.embed);
        QCOMPARE(request.scope, ExportScope::Selection);
        QCOMPARE(request.path, dir.filePath(QStringLiteral("out.png")));

        // PDF has no DPI, and no transparency option is applied.
        dialog.setFormat(render::ExportFormat::Pdf);
        QVERIFY(!dpiOf(dialog)->isEnabled());
        request = dialog.request();
        QCOMPARE(request.options.format, render::ExportFormat::Pdf);
        QVERIFY(!request.options.transparent);
        dialog.findChild<QCheckBox*>(QStringLiteral("export.embed"))->setChecked(true);
        QVERIFY(dialog.request().options.embed);
    }

    void selectionScopeNeedsSelection()
    {
        ExportDialog dialog(false, {});
        QVERIFY(!dialog.findChild<QRadioButton*>(QStringLiteral("export.scope.selection"))->isEnabled());
    }
};

QTEST_MAIN(TstDialogsExport)
#include "tst_dialogs_export.moc"
