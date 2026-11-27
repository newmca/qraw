#include "qraw/ui/print_support.h"

#include "qraw/render/painter_canvas.h"
#include "qraw/render/page_view.h"
#include "qraw/render/scene_renderer.h"

#include <QColor>
#include <QPainter>
#include <QPrintDialog>
#include <QPrintPreviewDialog>
#include <QPrinter>

#include <algorithm>
#include <numeric>

namespace qraw::ui {
namespace {

// draw.io's mxGraph scale: 1 diagram unit = 1/96 inch.
constexpr double kUnitsPerInch = 96.0;

// Device pixels per diagram unit for the page's content.
double deviceScale(const QPrinter& printer, const QRectF& pageRect, const PrintOptions& options,
                   const core::Rect& content)
{
    if (!options.fitToPage)
        return options.scale * printer.resolution() / kUnitsPerInch;
    // An empty page has zero content size; treat it as one unit so the
    // division stays finite (it paints nothing anyway).
    const double width = std::max(content.width, 1.0);
    const double height = std::max(content.height, 1.0);
    return std::min(pageRect.width() / width, pageRect.height() / height);
}

// Paints one diagram page centred on the printer's page rectangle.
void paintPage(QPainter& painter, const QPrinter& printer, const core::Page& page, const PrintOptions& options)
{
    const render::RenderContext context = render::RenderContext::defaults();
    const render::PageView view = render::PageView::build(page, context);
    const core::Rect content = view.bounds();
    const QRectF pageRect = printer.pageRect(QPrinter::DevicePixel);

    const QColor background(QString::fromStdString(page.background()));
    if (background.isValid())
        painter.fillRect(pageRect, background);

    const double scale = deviceScale(printer, pageRect, options, content);
    const double left = pageRect.x() + (pageRect.width() - content.width * scale) / 2;
    const double top = pageRect.y() + (pageRect.height() - content.height * scale) / 2;

    painter.save();
    painter.translate(left, top);
    painter.scale(scale, scale);
    painter.translate(-content.x, -content.y);
    render::PainterCanvas canvas(painter);
    render::SceneRenderer(context).paintPage(canvas, view);
    painter.restore();
}

std::vector<std::size_t> pagesToPrint(const core::Document& document, const std::vector<std::size_t>& requested)
{
    if (!requested.empty()) {
        std::vector<std::size_t> valid;
        for (const std::size_t index : requested) {
            if (index < document.pageCount())
                valid.push_back(index);
        }
        return valid;
    }
    std::vector<std::size_t> all(document.pageCount());
    std::iota(all.begin(), all.end(), std::size_t{0});
    return all;
}

} // namespace

bool printDocument(const core::Document& document, QPrinter& printer, const PrintOptions& options)
{
    const std::vector<std::size_t> pages = pagesToPrint(document, options.pages);
    if (pages.empty())
        return false;

    QPainter painter;
    if (!painter.begin(&printer))
        return false;
    for (std::size_t i = 0; i < pages.size(); ++i) {
        if (i > 0)
            printer.newPage();
        paintPage(painter, printer, *document.page(pages[i]), options);
    }
    return painter.end();
}

void showPrintPreview(const core::Document& document, QWidget* parent)
{
    QPrinter printer(QPrinter::HighResolution);
    QPrintPreviewDialog preview(&printer, parent);
    QObject::connect(&preview, &QPrintPreviewDialog::paintRequested, &preview,
                     [&document](QPrinter* target) { printDocument(document, *target, PrintOptions{}); });
    preview.exec();
}

void showPrintDialog(const core::Document& document, QWidget* parent)
{
    QPrinter printer(QPrinter::HighResolution);
    QPrintDialog dialog(&printer, parent);
    dialog.setOption(QAbstractPrintDialog::PrintPageRange, true);
    if (dialog.exec() != QDialog::Accepted)
        return;

    PrintOptions options;
    if (printer.printRange() == QPrinter::PageRange) {
        // QPrinter pages are 1-based and inclusive.
        for (int page = printer.fromPage(); page <= printer.toPage(); ++page) {
            if (page >= 1)
                options.pages.push_back(static_cast<std::size_t>(page - 1));
        }
    }
    printDocument(document, printer, options);
}

} // namespace qraw::ui
