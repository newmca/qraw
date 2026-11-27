#include "qraw/ui/export_dialog.h"

#include <QButtonGroup>
#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QDir>
#include <QDoubleSpinBox>
#include <QFileDialog>
#include <QFileInfo>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QRadioButton>
#include <QSaveFile>
#include <QSpinBox>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <string_view>

namespace qraw::ui {
namespace {

// draw.io's Editor.defaultResolution: 96 DPI is 100 % (scale 1).
constexpr double kDpiAtFullScale = 96.0;
constexpr double kMinZoom = 1;
constexpr double kMaxZoom = 1000;

QString toQString(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

QString extensionOf(render::ExportFormat format)
{
    return toQString(render::extensionFor(format));
}

bool supportsDpi(render::ExportFormat format)
{
    return format == render::ExportFormat::Png || format == render::ExportFormat::Jpeg;
}

bool supportsTransparency(render::ExportFormat format)
{
    return format == render::ExportFormat::Png || format == render::ExportFormat::Svg;
}

bool supportsEmbedding(render::ExportFormat format)
{
    return format != render::ExportFormat::Jpeg;
}

void setError(QString* error, const QString& message)
{
    if (error != nullptr)
        *error = message;
}

QString errorOf(const render::ExportResult& result)
{
    for (const core::Diagnostic& diagnostic : result.diagnostics) {
        if (diagnostic.severity == core::Diagnostic::Severity::Error)
            return toQString(diagnostic.message);
    }
    return QCoreApplication::translate("qraw::ui::runExport", "The diagram could not be exported.");
}

// Writes atomically (QSaveFile): the target is replaced only on success.
bool writeFile(const QString& path, const QByteArray& data, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly)) {
        setError(error, file.errorString());
        return false;
    }
    if (file.write(data) != data.size() || !file.commit()) {
        setError(error, file.errorString());
        return false;
    }
    return true;
}

bool exportSinglePage(const core::Document& document, std::size_t pageIndex, const render::ExportOptions& options,
                      const QString& path, QString* error)
{
    const render::ExportResult result = render::exportPage(document, pageIndex, options);
    if (!result.ok()) {
        setError(error, errorOf(result));
        return false;
    }
    return writeFile(path, result.data, error);
}

// "dir/drawing.png" -> "dir/drawing-2.png" (qraw-cli --all-pages naming).
QString numberedPath(const QString& path, std::size_t number)
{
    const QFileInfo info(path);
    QString name = info.completeBaseName() + QStringLiteral("-%1").arg(static_cast<qulonglong>(number));
    if (!info.suffix().isEmpty())
        name += QLatin1Char('.') + info.suffix();
    return QDir(info.absolutePath()).filePath(name);
}

bool exportAllPages(const core::Document& document, const render::ExportOptions& options, const QString& path,
                    QString* error)
{
    if (options.format == render::ExportFormat::Pdf) {
        const render::ExportResult result = render::exportPdf(document, {}, options);
        if (!result.ok()) {
            setError(error, errorOf(result));
            return false;
        }
        return writeFile(path, result.data, error);
    }
    const std::size_t pageCount = document.pageCount();
    for (std::size_t index = 0; index < pageCount; ++index) {
        if (!exportSinglePage(document, index, options, numberedPath(path, index + 1), error))
            return false;
    }
    return true;
}

} // namespace

bool runExport(const core::Document& document, std::size_t currentPage, const std::vector<std::string>& selection,
               const ExportRequest& request, QString* error)
{
    render::ExportOptions options = request.options;
    switch (request.scope) {
    case ExportScope::CurrentPage:
        return exportSinglePage(document, currentPage, options, request.path, error);
    case ExportScope::Selection:
        if (selection.empty()) {
            setError(error, QCoreApplication::translate("qraw::ui::runExport", "Nothing is selected."));
            return false;
        }
        options.cellIds = selection;
        return exportSinglePage(document, currentPage, options, request.path, error);
    case ExportScope::AllPages:
        return exportAllPages(document, options, request.path, error);
    }
    return false;
}

// The controls live here so that the dialog's private state stays out of the
// header. Widgets are children of the dialog; `dialog` outlives them.
struct ExportDialog::Impl {
    Impl(ExportDialog& owner, bool hasSelection, const QString& suggestedPath)
        : dialog(owner), selectionAvailable(hasSelection), suggested(suggestedPath)
    {
    }

    ExportDialog& dialog;
    bool selectionAvailable;
    QString suggested;
    QString outputPath;
    bool syncingZoomDpi = false;

    QComboBox* format = nullptr;
    QButtonGroup* scopeGroup = nullptr;
    QRadioButton* scopeCurrent = nullptr;
    QRadioButton* scopeAll = nullptr;
    QRadioButton* scopeSelection = nullptr;
    QDoubleSpinBox* zoom = nullptr;
    QComboBox* dpi = nullptr;
    QSpinBox* border = nullptr;
    QCheckBox* transparent = nullptr;
    QCheckBox* dark = nullptr;
    QCheckBox* embed = nullptr;
    QSpinBox* jpegQuality = nullptr;

    void build()
    {
        auto* layout = new QVBoxLayout(&dialog);
        layout->addLayout(buildForm());
        layout->addWidget(buildScopeBox());
        layout->addWidget(buildButtons());
        connectControls();
        updateEnabledState();
    }

    QFormLayout* buildForm()
    {
        auto* form = new QFormLayout;

        format = new QComboBox(&dialog);
        format->setObjectName(QStringLiteral("export.format"));
        format->addItem(QStringLiteral("PNG"), static_cast<int>(render::ExportFormat::Png));
        format->addItem(QStringLiteral("JPEG"), static_cast<int>(render::ExportFormat::Jpeg));
        format->addItem(QStringLiteral("SVG"), static_cast<int>(render::ExportFormat::Svg));
        format->addItem(QStringLiteral("PDF"), static_cast<int>(render::ExportFormat::Pdf));
        form->addRow(dialog.tr("Format:"), format);

        zoom = new QDoubleSpinBox(&dialog);
        zoom->setObjectName(QStringLiteral("export.zoom"));
        zoom->setRange(kMinZoom, kMaxZoom);
        zoom->setDecimals(1);
        zoom->setSuffix(QStringLiteral(" %"));
        zoom->setValue(100);
        form->addRow(dialog.tr("Zoom:"), zoom);

        dpi = new QComboBox(&dialog);
        dpi->setObjectName(QStringLiteral("export.dpi"));
        dpi->setEditable(true);
        for (const int preset : {72, 96, 150, 192, 300})
            dpi->addItem(QString::number(preset));
        form->addRow(dialog.tr("DPI:"), dpi);

        border = new QSpinBox(&dialog);
        border->setObjectName(QStringLiteral("export.border"));
        border->setRange(0, 500);
        border->setSuffix(QStringLiteral(" px"));
        form->addRow(dialog.tr("Border width:"), border);

        jpegQuality = new QSpinBox(&dialog);
        jpegQuality->setObjectName(QStringLiteral("export.jpegQuality"));
        jpegQuality->setRange(0, 100);
        jpegQuality->setValue(90);
        form->addRow(dialog.tr("JPEG quality:"), jpegQuality);

        transparent = new QCheckBox(dialog.tr("Transparent background"), &dialog);
        transparent->setObjectName(QStringLiteral("export.transparent"));
        form->addRow(QString(), transparent);

        dark = new QCheckBox(dialog.tr("Dark appearance"), &dialog);
        dark->setObjectName(QStringLiteral("export.dark"));
        dark->setToolTip(dialog.tr("Draw the diagram in the dark scheme, as View > Dark Diagram shows it."));
        form->addRow(QString(), dark);

        embed = new QCheckBox(dialog.tr("Include a copy of my diagram"), &dialog);
        embed->setObjectName(QStringLiteral("export.embed"));
        form->addRow(QString(), embed);
        return form;
    }

    QGroupBox* buildScopeBox()
    {
        auto* box = new QGroupBox(dialog.tr("Pages"), &dialog);
        auto* column = new QVBoxLayout(box);
        scopeGroup = new QButtonGroup(box);
        scopeCurrent = new QRadioButton(dialog.tr("Current page"), box);
        scopeCurrent->setObjectName(QStringLiteral("export.scope.current"));
        scopeAll = new QRadioButton(dialog.tr("All pages"), box);
        scopeAll->setObjectName(QStringLiteral("export.scope.all"));
        scopeSelection = new QRadioButton(dialog.tr("Selection only"), box);
        scopeSelection->setObjectName(QStringLiteral("export.scope.selection"));
        scopeGroup->addButton(scopeCurrent);
        scopeGroup->addButton(scopeAll);
        scopeGroup->addButton(scopeSelection);
        scopeCurrent->setChecked(true);
        scopeSelection->setEnabled(selectionAvailable);
        column->addWidget(scopeCurrent);
        column->addWidget(scopeAll);
        column->addWidget(scopeSelection);
        return box;
    }

    QDialogButtonBox* buildButtons()
    {
        auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, &dialog);
        QObject::connect(buttons, &QDialogButtonBox::accepted, &dialog, [this] { onAccepted(); });
        QObject::connect(buttons, &QDialogButtonBox::rejected, &dialog, &QDialog::reject);
        return buttons;
    }

    void connectControls()
    {
        QObject::connect(format, &QComboBox::currentIndexChanged, &dialog, [this] { updateEnabledState(); });
        QObject::connect(zoom, &QDoubleSpinBox::valueChanged, &dialog, [this](double) { zoomChanged(); });
        QObject::connect(dpi, &QComboBox::currentTextChanged, &dialog, [this](const QString&) { dpiChanged(); });
    }

    render::ExportFormat currentFormat() const
    {
        return static_cast<render::ExportFormat>(format->currentData().toInt());
    }

    ExportScope currentScope() const
    {
        if (scopeSelection->isChecked())
            return ExportScope::Selection;
        if (scopeAll->isChecked())
            return ExportScope::AllPages;
        return ExportScope::CurrentPage;
    }

    // Enables the controls that apply to the chosen format (draw.io's
    // ExportDialog hides them; disabling keeps the values for the next format).
    void updateEnabledState()
    {
        const render::ExportFormat chosen = currentFormat();
        dpi->setEnabled(supportsDpi(chosen));
        transparent->setEnabled(supportsTransparency(chosen));
        embed->setEnabled(supportsEmbedding(chosen));
        jpegQuality->setEnabled(chosen == render::ExportFormat::Jpeg);
        if (supportsDpi(chosen))
            setDpiFromZoom();
    }

    // Zoom (percent) and DPI express the same scale: 96 DPI = 100 %.
    void zoomChanged()
    {
        if (!syncingZoomDpi)
            setDpiFromZoom();
    }

    void dpiChanged()
    {
        if (syncingZoomDpi)
            return;
        bool ok = false;
        const double value = dpi->currentText().toDouble(&ok);
        if (!ok || value <= 0)
            return;
        setZoomQuietly(value / kDpiAtFullScale * 100);
    }

    void setDpiFromZoom()
    {
        const double value = std::round(zoom->value() * kDpiAtFullScale / 100);
        syncingZoomDpi = true;
        dpi->setCurrentText(QString::number(value));
        syncingZoomDpi = false;
    }

    void setZoomQuietly(double percent)
    {
        syncingZoomDpi = true;
        zoom->setValue(std::clamp(percent, kMinZoom, kMaxZoom));
        syncingZoomDpi = false;
    }

    ExportRequest request() const
    {
        ExportRequest result;
        const render::ExportFormat chosen = currentFormat();
        result.options.format = chosen;
        result.options.scale = zoom->value() / 100;
        result.options.border = border->value();
        result.options.transparent = supportsTransparency(chosen) && transparent->isChecked();
        result.options.dark = dark->isChecked();
        result.options.embed = supportsEmbedding(chosen) && embed->isChecked();
        result.options.jpegQuality = jpegQuality->value();
        result.scope = currentScope();
        result.path = outputPath;
        return result;
    }

    QString suggestedFileName() const
    {
        const QFileInfo info(suggested);
        const QString name = info.completeBaseName() + QLatin1Char('.') + extensionOf(currentFormat());
        return info.absolutePath().isEmpty() ? name : QDir(info.absolutePath()).filePath(name);
    }

    // OK: asks for the output path unless one was preset; the caller writes.
    void onAccepted()
    {
        if (outputPath.isEmpty()) {
            const QString extension = extensionOf(currentFormat());
            const QString filter = dialog.tr("%1 file (*.%2)").arg(format->currentText(), extension);
            const QString chosen = QFileDialog::getSaveFileName(&dialog, dialog.tr("Export As"),
                                                               suggestedFileName(), filter);
            if (chosen.isEmpty())
                return;
            outputPath = chosen;
        }
        dialog.accept();
    }
};

ExportDialog::ExportDialog(bool hasSelection, const QString& suggestedPath, QWidget* parent)
    : QDialog(parent), d(std::make_unique<Impl>(*this, hasSelection, suggestedPath))
{
    setWindowTitle(tr("Export As"));
    d->build();
}

ExportDialog::~ExportDialog() = default;

ExportRequest ExportDialog::request() const
{
    return d->request();
}

void ExportDialog::setFormat(render::ExportFormat format)
{
    const int index = d->format->findData(static_cast<int>(format));
    if (index >= 0)
        d->format->setCurrentIndex(index);
}

void ExportDialog::setOutputPath(const QString& path)
{
    d->outputPath = path;
}

void ExportDialog::setDark(bool dark)
{
    d->dark->setChecked(dark);
}

} // namespace qraw::ui
