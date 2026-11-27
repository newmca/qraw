// The image shape.
//
// Port of mxgraph/src/shape/mxImageShape.js (draw.io, Apache-2.0), modified:
// `clipPath` is not applied (Canvas::image has no clip), and an image that
// cannot be resolved draws a gray placeholder frame instead of the browser's
// broken-image icon.

#include "shapes_common.h"

namespace qraw::render::detail {

namespace {

// mxImageShape.apply: the shape's fill, stroke and gradient are replaced by
// `imageBackground` and `imageBorder`.
void clearShapeColors(Canvas& c)
{
    c.setFillColor(std::nullopt);
    c.setStrokeColor(std::nullopt);
}

void rectPath(Canvas& c, double x, double y, double w, double h, double r)
{
    if (r > 0)
        c.roundrect(x, y, w, h, r, r);
    else
        c.rect(x, y, w, h);
}

class ImageShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext& context, double x, double y, double w, double h) const override
    {
        const ResolvedStyle& style = context.style;
        if (style.image().empty())
            return; // upstream paints the (colorless) rectangle: nothing

        clearShapeColors(c);
        const double r = style.rounded() ? rectangleArcSize(style, w, h) : 0;

        std::optional<ImageData> image;
        if (context.images)
            image = cachedStyleImage(style, *context.images);
        if (!image) {
            paintPlaceholder(c, x, y, w, h);
            return;
        }

        const std::optional<QColor> background = style.imageBackground();
        const std::optional<QColor> border = style.imageBorder();
        if (background) {
            // Stroke rendering is required for the shadow.
            c.setFillColor(background);
            c.setStrokeColor(border);
            rectPath(c, x, y, w, h, r);
            c.fillAndStroke();
        }

        // The flips of the shape are part of the transform (paintVertex).
        // imageFlipH/V are the legacy per-image flips.
        c.image(x, y, w, h, *image, style.imageAspect(), style.flag("imageFlipH", false),
                style.flag("imageFlipV", false));

        if (border) {
            c.setShadow(false);
            c.setStrokeColor(border);
            rectPath(c, x, y, w, h, r);
            c.stroke();
        }
    }

private:
    // A light gray frame with a cross marks an image that is not available.
    static void paintPlaceholder(Canvas& c, double x, double y, double w, double h)
    {
        c.setShadow(false);
        c.setFillColor(QColor(0xF2, 0xF2, 0xF2));
        c.setStrokeColor(QColor(0xB3, 0xB3, 0xB3));
        c.rect(x, y, w, h);
        c.fillAndStroke();
        c.begin();
        c.moveTo(x, y);
        c.lineTo(x + w, y + h);
        c.moveTo(x + w, y);
        c.lineTo(x, y + h);
        c.stroke();
    }
};

} // namespace

void registerImageShapes(ShapeRegistry& registry)
{
    registry.add("image", std::make_unique<ImageShape>());
}

} // namespace qraw::render::detail
