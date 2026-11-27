// Export sizing follows draw.io's Graph.getSvg (js/grapheditor/Graph.js:40861,
// draw.io, Apache-2.0): w = max(1, ceil(bounds.width * scale) + 2 * border) and
// a translate that floors the crop origin on the output pixel grid.

#include "qraw/render/export.h"

#include "qraw/format/codec.h"
#include "qraw/format/embed.h"
#include "qraw/render/color.h"
#include "qraw/render/logging.h"
#include "qraw/render/painter_canvas.h"
#include "qraw/render/scene_renderer.h"
#include "qraw/render/svg_canvas.h"

#include <QBuffer>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QPageLayout>
#include <QPageSize>
#include <QPainter>
#include <QPdfWriter>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <limits>

namespace qraw::render {

namespace {

using Severity = core::Diagnostic::Severity;

constexpr double kPointsPerUnit = 0.75; // 96 units per inch, 72 points per inch

void report(ExportResult& result, Severity severity, std::string message, const core::Page* page = nullptr)
{
    core::Diagnostic d;
    d.severity = severity;
    d.message = std::move(message);
    if (page)
        d.pageId = page->id();
    result.diagnostics.push_back(std::move(d));
}

ExportResult failure(Severity severity, std::string message)
{
    ExportResult result;
    report(result, severity, std::move(message));
    return result;
}

bool hasGuiApplication() { return qobject_cast<QGuiApplication*>(QCoreApplication::instance()) != nullptr; }

// --- layout -----------------------------------------------------------------------

// Where the content goes in the output, in output units.
struct Layout {
    double scale = 1;
    double border = 0;
    double width = 1;  // output size
    double height = 1;
    double dx = 0;     // translate in diagram units, applied before the scale
    double dy = 0;
};

Layout computeLayout(const Rect& bounds, double scale, double border)
{
    Layout layout;
    layout.scale = scale;
    layout.border = border;
    layout.width = std::max(1.0, std::ceil(bounds.width * scale) + 2 * border);
    layout.height = std::max(1.0, std::ceil(bounds.height * scale) + 2 * border);
    // getSvg: dx = floor((border / scale - bounds.x) * scale) / scale, so that
    // content moves by whole output pixels and strokes stay crisp.
    layout.dx = std::floor((border / scale - bounds.x) * scale) / scale;
    layout.dy = std::floor((border / scale - bounds.y) * scale) / scale;
    return layout;
}

// Everything known about one page before painting.
struct PagePlan {
    const core::Page* page = nullptr;
    PageView view;
    Rect bounds;
    Layout layout;
};

// A raster image side in pixels.
// Saturates instead of overflowing (the cast of a huge double is undefined);
// QImage then reports the size as unallocatable.
int pixels(double units)
{
    return static_cast<int>(std::min(std::ceil(units), static_cast<double>(std::numeric_limits<int>::max())));
}

std::optional<QColor> parseBackground(const std::string& text)
{
    if (text.empty())
        return std::nullopt;
    const QColor color = parseStyleColor(text, QColor());
    if (!color.isValid() || color.alpha() == 0)
        return std::nullopt;
    return color;
}

// The fill behind the content (nullopt: none).
std::optional<QColor> resolveBackground(const core::Page& page, const ExportOptions& options, const RenderContext& context)
{
    const bool opaqueFormat = options.format != ExportFormat::Svg;
    if (options.transparent)
        return options.format == ExportFormat::Jpeg ? std::optional<QColor>(QColor(Qt::white)) : std::nullopt;
    if (options.background) {
        if (options.background->isValid() && options.background->alpha() > 0)
            return options.background;
        return options.format == ExportFormat::Jpeg ? std::optional<QColor>(QColor(Qt::white)) : std::nullopt;
    }
    // The page's colors as the scheme shows them: a dark export inverts an explicit background (adaptiveColors
    // Auto) and paints the opaque formats on the dark page color.
    const Theme theme = context.theme.forPage(parseAdaptiveColors(page.adaptiveColors()));
    if (parseBackground(page.background()))
        return theme.pageBackground(page.background());
    return opaqueFormat ? std::optional<QColor>(theme.background) : std::nullopt;
}

RenderContext contextOf(const ExportOptions& options)
{
    RenderContext context = options.context.value_or(RenderContext::defaults());
    if (options.dark)
        context.theme = Theme::darkScheme();
    if (!context.stylesheet || !context.shapes || !context.stencils) {
        const RenderContext defaults = RenderContext::defaults();
        if (!context.stylesheet)
            context.stylesheet = defaults.stylesheet;
        if (!context.shapes)
            context.shapes = defaults.shapes;
        if (!context.stencils)
            context.stencils = defaults.stencils;
    }
    return context;
}

bool validOptions(const ExportOptions& options, ExportResult& result)
{
    if (!(options.scale > 0) || !std::isfinite(options.scale)) {
        report(result, Severity::Error, "export scale must be a positive number");
        return false;
    }
    if (!(options.border >= 0) || !std::isfinite(options.border)) {
        report(result, Severity::Error, "export border must be zero or more");
        return false;
    }
    return true;
}

// Builds the view of a page and its layout at `scale`.
PagePlan planPage(const core::Page& page, const RenderContext& context, const ExportOptions& options, double scale,
                  ExportResult& result)
{
    PagePlan plan;
    plan.page = &page;
    plan.view = PageView::build(page, context);
    for (const core::Diagnostic& d : plan.view.diagnostics())
        result.diagnostics.push_back(d);
    plan.bounds = options.cellIds.empty() ? plan.view.bounds() : plan.view.boundsOf(options.cellIds);
    plan.layout = computeLayout(plan.bounds, scale, options.border);
    return plan;
}

// Reduces the scale of a raster export until both sides fit maxPixelSize.
void limitPixelSize(PagePlan& plan, const ExportOptions& options, ExportResult& result)
{
    const double limit = options.maxPixelSize;
    const double content = std::max(plan.bounds.width, plan.bounds.height);
    if (options.maxPixelSize <= 0 || std::max(plan.layout.width, plan.layout.height) <= limit || !(content > 0))
        return;
    // Start from the exact fit; rounding up to whole pixels may overshoot by one.
    double scale = std::min(plan.layout.scale, std::max(1.0, limit - 2 * options.border) / content);
    Layout layout = computeLayout(plan.bounds, scale, options.border);
    for (int attempt = 0; attempt < 64 && std::max(layout.width, layout.height) > limit; ++attempt) {
        scale *= 0.999;
        layout = computeLayout(plan.bounds, scale, options.border);
    }
    report(result, Severity::Warning,
           "image would exceed " + std::to_string(options.maxPixelSize) + " pixels; scale reduced from " +
               std::to_string(plan.layout.scale) + " to " + std::to_string(scale),
           plan.page);
    plan.layout = layout;
}

// Paints the page onto a canvas that is at its origin.
void paintPlan(Canvas& canvas, const PagePlan& plan, const RenderContext& context, const ExportOptions& options)
{
    // draw.io: svgCanvas.translate(dx, dy) first, then the shapes scale.
    canvas.translate(plan.layout.dx, plan.layout.dy);
    canvas.scale(plan.layout.scale);
    PaintOptions paint;
    paint.cellIds = options.cellIds;
    SceneRenderer(context).paintPage(canvas, plan.view, paint);
}

// --- formats ------------------------------------------------------------------------

ExportResult exportSvg(const core::Document& document, const core::Page& page, const ExportOptions& options)
{
    ExportResult result;
    const RenderContext context = contextOf(options);
    const PagePlan plan = planPage(page, context, options, options.scale, result);

    SvgDocumentOptions svg;
    svg.width = plan.layout.width;
    svg.height = plan.layout.height;
    svg.originX = -0.5;
    svg.originY = -0.5;
    svg.background = resolveBackground(page, options, context);
    if (options.embed)
        svg.content = embeddedXml(document);
    svg.idPrefix = "qraw";
    SvgCanvas canvas(svg);
    paintPlan(canvas, plan, context, options);
    result.data = QByteArray::fromStdString(canvas.toSvg());
    return result;
}

QImage renderImage(const PagePlan& plan, const RenderContext& context, const ExportOptions& options,
                   const std::optional<QColor>& background)
{
    QImage image(pixels(plan.layout.width), pixels(plan.layout.height), QImage::Format_ARGB32_Premultiplied);
    if (image.isNull())
        return image;
    image.fill(background ? *background : QColor(Qt::transparent));
    QPainter painter(&image);
    PainterCanvas canvas(painter);
    paintPlan(canvas, plan, context, options);
    painter.end();
    return image;
}

ExportResult exportRaster(const core::Document& document, const core::Page& page, const ExportOptions& options)
{
    ExportResult result;
    if (!hasGuiApplication()) {
        report(result, Severity::Error, "PNG and JPEG export need a QGuiApplication (fonts)");
        return result;
    }
    const RenderContext context = contextOf(options);
    PagePlan plan = planPage(page, context, options, options.scale, result);
    limitPixelSize(plan, options, result);

    const QImage image = renderImage(plan, context, options, resolveBackground(page, options, context));
    if (image.isNull()) {
        report(result, Severity::Error,
               "cannot allocate a " + std::to_string(pixels(plan.layout.width)) + "x" +
                   std::to_string(pixels(plan.layout.height)) + " image; use a smaller scale or maxPixelSize",
               &page);
        return result;
    }

    const bool png = options.format == ExportFormat::Png;
    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, png ? "png" : "jpg");
    if (!png)
        writer.setQuality(std::clamp(options.jpegQuality, 0, 100));
    if (!writer.write(image)) {
        report(result, Severity::Error, "cannot encode the image: " + writer.errorString().toStdString(), &page);
        return result;
    }
    buffer.close();

    if (png && options.embed) {
        if (auto embedded = format::embedInPng(bytes, embeddedXml(document)))
            bytes = std::move(*embedded);
        else
            report(result, Severity::Warning, "the diagram could not be embedded in the PNG", &page);
    }
    result.data = std::move(bytes);
    return result;
}

} // namespace

std::optional<ExportFormat> parseExportFormat(std::string_view name)
{
    std::string lower(name);
    std::transform(lower.begin(), lower.end(), lower.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (lower == "svg")
        return ExportFormat::Svg;
    if (lower == "png")
        return ExportFormat::Png;
    if (lower == "jpg" || lower == "jpeg")
        return ExportFormat::Jpeg;
    if (lower == "pdf")
        return ExportFormat::Pdf;
    return std::nullopt;
}

std::string_view extensionFor(ExportFormat format)
{
    switch (format) {
    case ExportFormat::Svg: return "svg";
    case ExportFormat::Png: return "png";
    case ExportFormat::Jpeg: return "jpg";
    case ExportFormat::Pdf: return "pdf";
    }
    return {};
}

std::string embeddedXml(const core::Document& document)
{
    format::WriteOptions options;
    options.pageEncoding = format::PageEncodingPolicy::Uncompressed;
    const QByteArray bytes = format::writeDocument(document, options);
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

ExportResult exportPage(const core::Document& document, std::size_t pageIndex, const ExportOptions& options)
{
    ExportResult invalid;
    if (!validOptions(options, invalid))
        return invalid;
    const core::Page* page = document.page(pageIndex);
    if (!page) {
        return failure(Severity::Error, "page " + std::to_string(pageIndex + 1) + " does not exist (the document has " +
                                            std::to_string(document.pageCount()) + ")");
    }
    qCDebug(lcRender) << "exporting page" << pageIndex + 1 << "of" << document.pageCount() << "as format"
                      << static_cast<int>(options.format);
    switch (options.format) {
    case ExportFormat::Svg: return exportSvg(document, *page, options);
    case ExportFormat::Png:
    case ExportFormat::Jpeg: return exportRaster(document, *page, options);
    case ExportFormat::Pdf: {
        const std::size_t only[] = {pageIndex};
        return exportPdf(document, only, options);
    }
    }
    return failure(Severity::Error, "unknown export format");
}

ExportResult exportPdf(const core::Document& document, std::span<const std::size_t> pageIndices,
                       const ExportOptions& options)
{
    ExportResult result;
    if (!validOptions(options, result))
        return result;
    if (!hasGuiApplication()) {
        report(result, Severity::Error, "PDF export needs a QGuiApplication (fonts)");
        return result;
    }

    std::vector<std::size_t> indices(pageIndices.begin(), pageIndices.end());
    if (indices.empty()) {
        for (std::size_t i = 0; i < document.pageCount(); ++i)
            indices.push_back(i);
    }
    if (indices.empty()) {
        report(result, Severity::Error, "the document has no pages");
        return result;
    }
    for (const std::size_t index : indices) {
        if (!document.page(index)) {
            report(result, Severity::Error,
                   "page " + std::to_string(index + 1) + " does not exist (the document has " +
                       std::to_string(document.pageCount()) + ")");
            return result;
        }
    }

    const RenderContext context = contextOf(options);
    const auto pageSize = [](const Layout& layout) {
        return QPageSize(QSizeF(layout.width * kPointsPerUnit, layout.height * kPointsPerUnit), QPageSize::Point);
    };

    QByteArray bytes;
    QBuffer buffer(&bytes);
    buffer.open(QIODevice::WriteOnly);
    QPdfWriter writer(&buffer);
    writer.setResolution(96); // one painter unit is one diagram unit at scale 1
    writer.setCreator(QStringLiteral("Qraw"));
    writer.setTitle(QString::fromStdString(document.page(indices.front())->name()));
    writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);

    QPainter painter;
    for (std::size_t k = 0; k < indices.size(); ++k) {
        const core::Page& page = *document.page(indices[k]);
        const PagePlan plan = planPage(page, context, options, options.scale, result);
        writer.setPageSize(pageSize(plan.layout));
        writer.setPageMargins(QMarginsF(0, 0, 0, 0), QPageLayout::Point);
        if (k == 0) {
            if (!painter.begin(&writer)) {
                report(result, Severity::Error, "cannot start the PDF writer");
                return result;
            }
        } else {
            writer.newPage();
        }
        if (const auto background = resolveBackground(page, options, context))
            painter.fillRect(QRectF(0, 0, plan.layout.width, plan.layout.height), *background);
        PainterCanvas canvas(painter);
        paintPlan(canvas, plan, context, options);
    }
    painter.end();
    buffer.close();

    if (options.embed) {
        if (auto embedded = format::embedInPdf(bytes, embeddedXml(document)))
            bytes = std::move(*embedded);
        else
            report(result, Severity::Warning, "the diagram could not be embedded in the PDF");
    }
    result.data = std::move(bytes);
    return result;
}

} // namespace qraw::render
