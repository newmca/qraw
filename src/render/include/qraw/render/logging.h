#pragma once
// Logging categories of qraw::render: rendering, and the tile rasteriser and its worker threads
// QT_LOGGING_RULES="qraw.*.debug=true" switches the debug output of every module on (BUILDING.md,
// "Logging"); informational messages and above are on by default.

#include <QLoggingCategory>

namespace qraw::render {

Q_DECLARE_LOGGING_CATEGORY(lcRender) // qraw.render
Q_DECLARE_LOGGING_CATEGORY(lcRenderTiles) // qraw.render.tiles

} // namespace qraw::render
