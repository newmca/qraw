#include "qraw/cli/cli.h"

#include "export_command.h"

#include "qraw/cli/logging.h"
#include "qraw/core/document.h"
#include "qraw/format/codec.h"

#include <QCommandLineParser>
#include <QCoreApplication>
#include <QTextStream>

namespace qraw::cli {

namespace {

const QStringList kCommandFlags = {"--validate", "--convert", "--info", "--export", "--help", "-h", "--version"};

QString q(const std::string& s) { return QString::fromStdString(s); }

const char* encodingName(core::PageEncoding e)
{
    switch (e) {
    case core::PageEncoding::Inline: return "uncompressed";
    case core::PageEncoding::Compressed: return "compressed";
    case core::PageEncoding::Opaque: return "undecodable (kept verbatim)";
    }
    return "?";
}

core::PageStats totals(const core::Document& doc)
{
    core::PageStats total;
    for (const core::Page* page : doc.pages()) {
        const auto s = page->stats();
        total.cells += s.cells;
        total.vertices += s.vertices;
        total.edges += s.edges;
        total.layers += s.layers;
    }
    return total;
}

QString statsText(const core::PageStats& s)
{
    return QStringLiteral("%1 cells, %2 vertices, %3 edges, %4 layers")
        .arg(s.cells).arg(s.vertices).arg(s.edges).arg(s.layers);
}

void printDiagnostics(QTextStream& stream, const QString& path, const std::vector<core::Diagnostic>& diagnostics)
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

int validate(const QString& path, QTextStream& out)
{
    const auto result = format::readFile(path);
    printDiagnostics(out, path, result.diagnostics);
    if (result.hasErrors()) {
        out << path << ": invalid\n";
        return Failure;
    }
    const auto& doc = *result.document;
    out << path << ": ok (" << doc.pageCount() << " pages, " << statsText(totals(doc)) << ", "
        << result.diagnostics.size() << " diagnostics)\n";
    return Success;
}

int info(const QString& path, QTextStream& out, QTextStream& err)
{
    const auto result = format::readFile(path);
    if (!result.ok()) {
        printDiagnostics(err, path, result.diagnostics);
        return Failure;
    }
    const auto& doc = *result.document;
    out << "File:   " << path << '\n';
    out << "Format: "
        << (doc.kind() == core::DocumentKind::MxFile ? "mxfile (.drawio)" : "bare mxGraphModel (.xml)") << '\n';
    out << "Pages:  " << doc.pageCount() << '\n';
    std::size_t index = 0;
    for (const core::Page* page : doc.pages()) {
        out << "  [" << index++ << "] \"" << q(page->name()) << "\" id=" << q(page->id()) << ", "
            << encodingName(page->encoding()) << ": " << statsText(page->stats()) << '\n';
    }
    out << "Total:  " << statsText(totals(doc)) << '\n';
    return Success;
}

int convert(const QString& input, const QString& output, format::PageEncodingPolicy policy, QTextStream& err)
{
    const auto result = format::readFile(input);
    printDiagnostics(err, input, result.diagnostics);
    if (!result.ok())
        return Failure;
    format::WriteOptions options;
    options.pageEncoding = policy;
    QString error;
    if (!format::writeFile(*result.document, output, options, &error)) {
        err << output << ": cannot write: " << error << '\n';
        return Failure;
    }
    return Success;
}

} // namespace

bool requiresGuiApplication(const QStringList& arguments)
{
    for (const auto& arg : arguments) {
        if (arg == u"--export" || arg.startsWith(u"--export="))
            return true;
    }
    return false;
}

bool isCliInvocation(const QStringList& arguments)
{
    for (qsizetype i = 1; i < arguments.size(); ++i) {
        const QString& arg = arguments.at(i);
        for (const auto& flag : kCommandFlags) {
            if (arg == flag || arg.startsWith(flag + u'='))
                return true;
        }
    }
    return false;
}

int run(const QStringList& arguments, QTextStream& out, QTextStream& err)
{
    qCDebug(lcCli) << "arguments:" << arguments;
    if (requiresGuiApplication(arguments))
        return runExport(arguments, out, err);

    QCommandLineParser parser;
    parser.setApplicationDescription(QStringLiteral(
        "Qraw command-line tools for .drawio/.xml diagrams.\n\n"
        "Export (run `--export IN --help` for all options):\n"
        "  qraw-cli --export IN --output OUT [--format svg|png|jpg|pdf] [--page N | --all-pages]\n"
        "             [--scale S] [--border N] [--transparent] [--embed] [--quality Q]\n"
        "IN may be a .drawio/.xml file or a .png/.svg/.pdf with an embedded diagram."));
    const QCommandLineOption validateOpt("validate", "Check FILE; exit 1 if it cannot be read.", "FILE");
    const QCommandLineOption infoOpt("info", "Print pages, cells, vertices, edges and layers of FILE.", "FILE");
    const QCommandLineOption convertOpt("convert", "Read IN and write it to --output.", "IN");
    // Documentation only: --export is handled by runExport() before this parser runs.
    const QCommandLineOption exportOpt("export", "Render IN to SVG, PNG, JPEG or PDF at --output.", "IN");
    const QCommandLineOption outputOpt("output", "Output file for --convert and --export.", "OUT");
    const QCommandLineOption compressedOpt("compressed", "With --convert: write all pages compressed.");
    const QCommandLineOption uncompressedOpt("uncompressed", "With --convert: write all pages uncompressed.");
    parser.addOptions({validateOpt, infoOpt, convertOpt, exportOpt, outputOpt, compressedOpt, uncompressedOpt});
    const QCommandLineOption helpOpt = parser.addHelpOption();
    const QCommandLineOption versionOpt = parser.addVersionOption();

    if (!parser.parse(arguments)) {
        err << parser.errorText() << '\n';
        return Usage;
    }
    if (parser.isSet(helpOpt)) {
        out << parser.helpText();
        return Success;
    }
    if (parser.isSet(versionOpt)) {
        out << QCoreApplication::applicationName() << ' ' << QCoreApplication::applicationVersion() << '\n';
        return Success;
    }

    const int commands = int(parser.isSet(validateOpt)) + int(parser.isSet(infoOpt)) + int(parser.isSet(convertOpt));
    if (commands != 1) {
        err << "Specify exactly one of --validate, --info, --convert (see --help).\n";
        return Usage;
    }
    if (parser.isSet(validateOpt))
        return validate(parser.value(validateOpt), out);
    if (parser.isSet(infoOpt))
        return info(parser.value(infoOpt), out, err);

    if (!parser.isSet(outputOpt)) {
        err << "--convert requires --output OUT.\n";
        return Usage;
    }
    if (parser.isSet(compressedOpt) && parser.isSet(uncompressedOpt)) {
        err << "--compressed and --uncompressed are mutually exclusive.\n";
        return Usage;
    }
    auto policy = format::PageEncodingPolicy::Preserve;
    if (parser.isSet(compressedOpt))
        policy = format::PageEncodingPolicy::Compressed;
    else if (parser.isSet(uncompressedOpt))
        policy = format::PageEncodingPolicy::Uncompressed;
    return convert(parser.value(convertOpt), parser.value(outputOpt), policy, err);
}

} // namespace qraw::cli
