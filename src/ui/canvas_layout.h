#pragma once
// Pure canvas geometry shared by DiagramScene and CanvasView:
// draw.io's page layout, zoom steps, fit scale and grid snapping. No Qt
// widgets, no model mutation. Private to src/ui (not a public header).

#include "qraw/core/page.h"

#include <QPointF>
#include <QRectF>
#include <QSizeF>

#include <optional>
#include <vector>

namespace qraw::ui::canvas {

// draw.io's default page format (mxGraph 'pageFormat' = A4 portrait).
inline constexpr double kDefaultPageWidth = 827;
inline constexpr double kDefaultPageHeight = 1169;

// One page tile: pageWidth/pageHeight times pageScale; draw.io's defaults
// when a dimension is missing or not positive.
[[nodiscard]] QSizeF pageSize(const core::Page& page);

// Graph.getPageLayout (js/grapheditor/Graph.js:15483) without the view
// translate. `content` is PageView::bounds() (nullopt: the page is empty).
// pageVisible: the union of whole page tiles covering the content and the
// tile at (0, 0). Otherwise the content bounds themselves ((0, 0, W, H) when
// empty).
[[nodiscard]] QRectF pageArea(const core::Page& page, const std::optional<QRectF>& content);

// Graph.getZoomSteps (Graph.js:21672, non-fine branch) for zoomFactor 1.2:
// ascending stops on the 5 % grid from 5 % to 3200/20 = 16000 %, 100 % included.
[[nodiscard]] const std::vector<double>& zoomSteps();
// Graph.getZoomStep (Graph.js:21763): the first stop strictly above (zoomIn)
// or below `zoom`, clamped to the first/last stop.
[[nodiscard]] double nextZoomStep(double zoom, bool zoomIn);
// Graph.getFitScale (Graph.js:21652): floors to the 5 % grid.
[[nodiscard]] double fitScale(double scale);

// Graph.snap with the page grid: the point rounded to multiples of the grid
// size when the page grid is on, rounded to integers otherwise
// (Graph.getCenterInsertPoint rounds first).
[[nodiscard]] QPointF snapToGrid(const core::Page& page, QPointF p);

} // namespace qraw::ui::canvas
