// File loading and saving for the editor (docs/design/ui.md section 7).
#include "qraw/ui/file_service.h"

#include "qraw/commands/ops_pages.h"
#include "qraw/format/codec.h"
#include "qraw/format/embed.h"
#include "qraw/format/library.h"
#include "qraw/render/export.h"
#include "qraw/ui/logging.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <algorithm>

namespace qraw::ui {

namespace {

// Gives lupdate the translation context of the strings below.
struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::FileService)
};

constexpr char kBlankDiagramPrefix[] = R"(<mxfile><diagram id=")";
constexpr char kBlankDiagramSuffix[] =
    R"(" name="Page-1"><mxGraphModel dx="1000" dy="800" grid="1" gridSize="10" guides="1" tooltips="1" connect="1" arrows="1" fold="1" page="1" pageScale="1" pageWidth="827" pageHeight="1169" math="0" shadow="0"><root><mxCell id="0"/><mxCell id="1" parent="0"/></root></mxGraphModel></diagram></mxfile>)";

FileKind kindForContainer(format::ContainerKind container, const QString& path)
{
    switch (container) {
    case format::ContainerKind::Png:
        return FileKind::DrawioPng;
    case format::ContainerKind::Svg:
        return FileKind::DrawioSvg;
    case format::ContainerKind::Pdf:
        return FileKind::Pdf;
    default:
        return path.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive) ? FileKind::Xml : FileKind::Drawio;
    }
}

bool isImageContainer(format::ContainerKind container)
{
    return container == format::ContainerKind::Png || container == format::ContainerKind::Svg ||
           container == format::ContainerKind::Pdf;
}

QString firstErrorMessage(const std::vector<core::Diagnostic>& diagnostics)
{
    for (const core::Diagnostic& d : diagnostics) {
        if (d.severity != core::Diagnostic::Severity::Error)
            continue;
        QString message = QString::fromStdString(d.message);
        if (d.line > 0)
            message += QStringLiteral(" (%1:%2)").arg(d.line).arg(d.column);
        return message;
    }
    return {};
}

bool writeBytes(const QString& path, const QByteArray& bytes, QString* error)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(bytes) != bytes.size() || !file.commit()) {
        if (error != nullptr)
            *error = Text::tr("The file could not be written: %1").arg(file.errorString());
        return false;
    }
    return true;
}

} // namespace

LoadedFile FileService::load(const QString& path)
{
    LoadedFile loaded;
    // QFile (not format::readAnyFile) so that ':/...' resource paths work.
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly)) {
        loaded.error = Text::tr("The file could not be read: %1").arg(file.errorString());
        qCWarning(lcUiSession) << "cannot open" << path << file.errorString();
        return loaded;
    }
    const QByteArray bytes = file.readAll();
    const format::ContainerKind container = format::sniffContainer(bytes);
    loaded.kind = kindForContainer(container, path);

    format::ReadResult result = format::readAnyDocument(bytes);
    loaded.isLibrary = std::any_of(result.diagnostics.begin(), result.diagnostics.end(), format::isLibraryDiagnostic);
    loaded.diagnostics = std::move(result.diagnostics);
    const QString name = QFileInfo(path).fileName();
    if (!result.document) {
        if (loaded.isLibrary) {
            loaded.error = Text::tr("%1 is a shape library, not a diagram.").arg(name);
        } else if (isImageContainer(container)) {
            loaded.error = Text::tr("%1 contains no diagram.").arg(name);
        } else {
            const QString reason = firstErrorMessage(loaded.diagnostics);
            loaded.error = Text::tr("The file could not be read: %1")
                               .arg(reason.isEmpty() ? Text::tr("it is not a draw.io diagram") : reason);
        }
        qCWarning(lcUiSession) << "cannot load" << path << ":" << loaded.error;
        return loaded;
    }
    if (result.document->pageCount() == 0) {
        loaded.error = Text::tr("%1 contains no diagram.").arg(name);
        return loaded;
    }
    loaded.document = std::move(result.document);
    qCDebug(lcUiSession) << "loaded" << path << loaded.document->pageCount() << "page(s)," << loaded.diagnostics.size()
                         << "diagnostic(s)";
    return loaded;
}

bool FileService::isLibraryFile(const QString& path)
{
    QFile file(path);
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray head = file.read(4096);
    return format::isLibraryXml(std::string_view(head.constData(), static_cast<std::size_t>(head.size())));
}

bool FileService::save(const core::Document& document, const QString& path, FileKind kind, QString* error)
{
    switch (kind) {
    case FileKind::Drawio:
    case FileKind::Xml:
        return format::writeFile(document, path, {}, error);
    case FileKind::DrawioPng:
    case FileKind::DrawioSvg: {
        render::ExportOptions options;
        options.format = kind == FileKind::DrawioPng ? render::ExportFormat::Png : render::ExportFormat::Svg;
        options.scale = 1;
        options.border = 10;
        options.embed = true;
        const render::ExportResult exported = render::exportPage(document, 0, options);
        if (!exported.ok()) {
            if (error != nullptr)
                *error = Text::tr("The image could not be rendered.");
            return false;
        }
        return writeBytes(path, exported.data, error);
    }
    case FileKind::Pdf:
        break;
    }
    if (error != nullptr)
        *error = Text::tr("PDF files cannot be saved as diagrams; save as a .drawio file instead.");
    return false;
}

FileKind FileService::kindForPath(const QString& path)
{
    if (path.endsWith(QStringLiteral(".png"), Qt::CaseInsensitive))
        return FileKind::DrawioPng;
    if (path.endsWith(QStringLiteral(".svg"), Qt::CaseInsensitive))
        return FileKind::DrawioSvg;
    if (path.endsWith(QStringLiteral(".xml"), Qt::CaseInsensitive))
        return FileKind::Xml;
    return FileKind::Drawio;
}

QString FileService::openFilter()
{
    return Text::tr("draw.io diagrams (*.drawio *.xml);;Editable images (*.png *.svg);;PDF (*.pdf);;All files (*)");
}

QString FileService::saveFilter()
{
    return Text::tr("draw.io diagram (*.drawio);;XML diagram (*.xml);;Editable PNG image (*.drawio.png);;"
              "Editable SVG image (*.drawio.svg)");
}

core::Document FileService::blankDocument()
{
    const std::string xml = std::string(kBlankDiagramPrefix) + commands::ops::randomPageId() + kBlankDiagramSuffix;
    format::ReadResult result = format::readDocument(QByteArrayView(xml.data(), static_cast<qsizetype>(xml.size())));
    return result.document ? std::move(*result.document) : core::Document();
}

} // namespace qraw::ui
