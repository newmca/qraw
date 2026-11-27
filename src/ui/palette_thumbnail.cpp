#include "palette_thumbnail.h"

#include "qraw/format/codec.h"
#include "qraw/render/page_view.h"
#include "qraw/render/painter_canvas.h"
#include "qraw/render/scene_renderer.h"

#include <QPainter>

#include <algorithm>

namespace qraw::ui {

QImage renderPaletteThumbnail(const PaletteEntry& entry, QSize logicalSize, qreal devicePixelRatio, int border,
                              QColor background)
{
    const std::string xml = entry.cellsXml();
    const format::ReadResult read =
        format::readDocument(QByteArrayView(xml.data(), static_cast<qsizetype>(xml.size())));
    if (!read.ok() || read.document->pageCount() == 0)
        return {};

    const QSize pixels = (QSizeF(logicalSize) * devicePixelRatio).toSize();
    QImage image(pixels, QImage::Format_ARGB32_Premultiplied);
    image.setDevicePixelRatio(devicePixelRatio);
    image.fill(background);

    const render::RenderContext context = render::RenderContext::defaults();
    const render::PageView view = render::PageView::build(*read.document->page(0), context);
    if (view.states().empty())
        return image;

    // A zero-sized extent (a horizontal line) still gets a scale.
    const geometry::Rect bounds = view.bounds();
    const double width = std::max(bounds.width, 1.0);
    const double height = std::max(bounds.height, 1.0);
    const double scale = std::min((logicalSize.width() - 2.0 * border) / width,
                                  (logicalSize.height() - 2.0 * border) / height);

    QPainter painter(&image);
    painter.translate((logicalSize.width() - width * scale) / 2 - bounds.x * scale,
                      (logicalSize.height() - height * scale) / 2 - bounds.y * scale);
    painter.scale(scale, scale);
    {
        render::PainterCanvas canvas(painter);
        render::PaintOptions options;
        options.labels = entry.showLabel;
        options.groups = false;
        render::SceneRenderer(context).paintPage(canvas, view, options);
    }
    painter.end();
    return image;
}

} // namespace qraw::ui
