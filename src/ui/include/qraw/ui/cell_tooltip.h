#pragma once
// The tooltip of a cell (Graph.getTooltipForCell, js/grapheditor/Graph.js:21997):
// its `tooltip` property when it has one (HTML, sanitised, with %placeholders%
// replaced when the cell uses them), else its custom properties as
// "name: value" lines. In edit mode upstream leaves the link out of the
// properties; here a linked cell shows its link first, so that the address behind
// the link icon can be read before it is opened (an affordance beyond draw.io's).
// Pure: the canvas shows the result in a QToolTip.

#include "qraw/render/page_view.h"

#include <QString>
#include <QStringList>

#include <functional>
#include <string>

namespace qraw::core {
class Cell;
class Page;
} // namespace qraw::core

namespace qraw::ui {

// Rich text for QToolTip, or "" when the cell has nothing to show.
// `linkText` turns a link into the text shown for it (a page link into the page's name).
[[nodiscard]] QString cellTooltip(const core::Page& page, const core::Cell& cell,
                                  const render::PlaceholderVariables& variables,
                                  const std::function<QString(const std::string&)>& linkText = {});

} // namespace qraw::ui
