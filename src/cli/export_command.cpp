// `qraw-cli --export`: renders the pages of a diagram to SVG, PNG, JPEG or
// PDF (docs/design/render.md section 8).

#include "export_command.h"

#include "qraw/cli/cli.h"
#include "qraw/format/embed.h"
#include "qraw/render/export.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QFileInfo>
#include <QGuiApplication>
#include <QSaveFile>
#include <QTextStream>

#include <optional>

namespace qraw::cli {

namespace {

using core::Diagnostic;

QString q(const std::string& s) { return QString::fromStdString(s); }

// Same output as cli.cpp's printDiagnostics.
void printDiagnostics(QTextStream& stream, const QString& path, const std::vector<Diagnostic>& diagnostics)
{
    for (const auto& d : diagnostics) {
        stream << path;
        if (d.line > 0)
            stream << ':' << d.line << ':' << d.column;
        stream << ": " << core::severityName(d.severity) << ": " << q(d.message);
        if (!d.pageId.empty())
            stream << " [page " << q(d.pageId) << (d.cellId.empty() ? "" : ", cell ") << q(d.cellId) << ']';
        stream << '\n';
    }
}

bool hasError(const std::vector<Diagnostic>& diagnostics)
{
    for (const auto& d : diagnostics) {
        if (d.severity == Diagnostic::Severity::Error)
            return true;
    }
    return false;
}

// The parsed command line of one export.
struct ExportRequest {
    QString input;
    QString output;
    render::ExportOptions options;
    std::size_t page = 0; // 0-based
    bool allPages = false;
};

// "out.png" -> "out-3.png" (the extension of OUT is kept; without one, the
// format's default extension is added).
QString numberedPath(const QString& output, int number, render::ExportFormat format)
{
    const QFileInfo info(output);
    const QString suffix = info.suffix();
    if (suffix.isEmpty())
        return QStringLiteral("%1-%2.%3").arg(output).arg(number).arg(QString::fromUtf8(render::extensionFor(format).data()));
    const QString stem = output.left(output.size() - suffix.size() - 1);
    return QStringLiteral("%1-%2.%3").arg(stem).arg(number).arg(suffix);
}

std::optional<double> parseDouble(const QString& text)
{
    bool ok = false;
    const double value = text.toDouble(&ok);
    return ok ? std::optional<double>(value) : std::nullopt;
}

// Parses the command line. On a bad value returns nullopt after writing the
// reason to `err`.
std::optional<ExportRequest> parseRequest(const QStringList& arguments, QTextStream& out, QTextStream& err, int& exitCode)
{
    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral("Export the pages of a diagram to SVG, PNG, JPEG or PDF."));
    const QCommandLineOption exportOpt("export", "Diagram to export (.drawio/.xml, or a .png/.svg/.pdf with an embedded diagram).", "IN");
    const QCommandLineOption formatOpt("format", "svg, png, jpg or pdf (default: from the --output extension).", "FORMAT");
    const QCommandLineOption outputOpt("output", "Output file.", "OUT");
    const QCommandLineOption pageOpt("page", "Page to export, starting at 1 (default 1).", "N");
    const QCommandLineOption allPagesOpt("all-pages", "Export every page: one multi-page PDF, or OUT-<n>.<ext> files.");
    const QCommandLineOption scaleOpt("scale", "Output units per diagram unit (default 1).", "S");
    const QCommandLineOption transparentOpt("transparent", "No background (PNG and SVG).");
    const QCommandLineOption darkOpt("dark", "Draw the diagram in the dark scheme (see adaptiveColors).");
    const QCommandLineOption borderOpt("border", "Margin around the content (default 0).", "N");
    const QCommandLineOption embedOpt("embed", "Embed the editable diagram in the output.");
    const QCommandLineOption qualityOpt("quality", "JPEG quality 0-100 (default 90).", "Q");
    parser.addOptions({exportOpt, formatOpt, outputOpt, pageOpt, allPagesOpt, scaleOpt, transparentOpt, darkOpt, borderOpt,
                       embedOpt, qualityOpt});
    const QCommandLineOption helpOpt = parser.addHelpOption();

    const auto usage = [&](const QString& message) -> std::optional<ExportRequest> {
        err << message << '\n';
        exitCode = Usage;
        return std::nullopt;
    };

    if (!parser.parse(arguments))
        return usage(parser.errorText());
    if (parser.isSet(helpOpt)) {
        out << parser.helpText();
        exitCode = Success;
        return std::nullopt;
    }

    ExportRequest request;
    request.input = parser.value(exportOpt);
    if (request.input.isEmpty())
        return usage(QStringLiteral("--export requires an input file."));
    if (!parser.isSet(outputOpt))
        return usage(QStringLiteral("--export requires --output OUT."));
    request.output = parser.value(outputOpt);

    std::optional<render::ExportFormat> format;
    if (parser.isSet(formatOpt)) {
        format = render::parseExportFormat(parser.value(formatOpt).toStdString());
        if (!format)
            return usage(QStringLiteral("Unknown format '%1' (use svg, png, jpg or pdf).").arg(parser.value(formatOpt)));
    } else {
        format = render::parseExportFormat(QFileInfo(request.output).suffix().toStdString());
        if (!format)
            return usage(QStringLiteral("Cannot tell the format from '%1'; use --format svg|png|jpg|pdf.").arg(request.output));
    }
    request.options.format = *format;

    if (parser.isSet(pageOpt) && parser.isSet(allPagesOpt))
        return usage(QStringLiteral("--page and --all-pages are mutually exclusive."));
    request.allPages = parser.isSet(allPagesOpt);
    if (parser.isSet(pageOpt)) {
        bool ok = false;
        const int page = parser.value(pageOpt).toInt(&ok);
        if (!ok || page < 1)
            return usage(QStringLiteral("--page must be a number starting at 1."));
        request.page = static_cast<std::size_t>(page - 1);
    }
    if (parser.isSet(scaleOpt)) {
        const auto scale = parseDouble(parser.value(scaleOpt));
        if (!scale || !(*scale > 0))
            return usage(QStringLiteral("--scale must be a positive number."));
        request.options.scale = *scale;
    }
    if (parser.isSet(borderOpt)) {
        const auto border = parseDouble(parser.value(borderOpt));
        if (!border || !(*border >= 0))
            return usage(QStringLiteral("--border must be zero or more."));
        request.options.border = *border;
    }
    if (parser.isSet(qualityOpt)) {
        bool ok = false;
        const int quality = parser.value(qualityOpt).toInt(&ok);
        if (!ok || quality < 0 || quality > 100)
            return usage(QStringLiteral("--quality must be between 0 and 100."));
        request.options.jpegQuality = quality;
    }
    request.options.transparent = parser.isSet(transparentOpt);
    request.options.dark = parser.isSet(darkOpt);
    request.options.embed = parser.isSet(embedOpt);
    return request;
}

// Writes `data` atomically.
bool writeOutput(const QString& path, const QByteArray& data, QTextStream& err)
{
    QSaveFile file(path);
    if (!file.open(QIODevice::WriteOnly) || file.write(data) != data.size() || !file.commit()) {
        err << path << ": cannot write: " << file.errorString() << '\n';
        return false;
    }
    return true;
}

// Prints the diagnostics of `result`; true when it holds data and no error.
bool deliver(const render::ExportResult& result, const QString& label, const QString& path, QTextStream& err)
{
    printDiagnostics(err, label, result.diagnostics);
    if (hasError(result.diagnostics) || !result.ok()) {
        if (!hasError(result.diagnostics))
            err << label << ": error: export produced no data\n";
        return false;
    }
    return writeOutput(path, result.data, err);
}

} // namespace

int runExport(const QStringList& arguments, QTextStream& out, QTextStream& err)
{
    int exitCode = Success;
    const auto request = parseRequest(arguments, out, err, exitCode);
    if (!request)
        return exitCode;
    if (!qobject_cast<QGuiApplication*>(QCoreApplication::instance())) {
        err << "--export needs a QGuiApplication (it lays out text with fonts); run it through qraw-cli.\n";
        return Usage;
    }

    const format::ReadResult read = format::readAnyFile(request->input);
    printDiagnostics(err, request->input, read.diagnostics);
    if (!read.ok() || read.hasErrors())
        return Failure;
    const core::Document& document = *read.document;

    render::ExportOptions options = request->options;
    render::RenderContext context = render::RenderContext::defaults();
    context.images.baseDirectory = QFileInfo(request->input).absolutePath();
    options.context = context;

    if (request->allPages && options.format == render::ExportFormat::Pdf) {
        return deliver(render::exportPdf(document, {}, options), request->input, request->output, err) ? Success
                                                                                                         : Failure;
    }
    if (request->allPages) {
        if (document.pageCount() == 0) {
            err << request->input << ": error: the document has no pages\n";
            return Failure;
        }
        int status = Success;
        for (std::size_t i = 0; i < document.pageCount(); ++i) {
            const QString path = numberedPath(request->output, static_cast<int>(i) + 1, options.format);
            if (!deliver(render::exportPage(document, i, options), request->input, path, err))
                status = Failure;
        }
        return status;
    }
    return deliver(render::exportPage(document, request->page, options), request->input, request->output, err)
               ? Success
               : Failure;
}

} // namespace qraw::cli
