#pragma once
// Renders palette entries to thumbnails with the M3 renderer. Private to the
// palette package (the dock and its tests).

#include "qraw/ui/palette_library.h"

#include <QColor>
#include <QImage>
#include <QSize>

namespace qraw::ui {

// The entry as a transparent image of `logicalSize` * `devicePixelRatio`
// pixels (image.devicePixelRatio() is set): its cells (cellsXml()) are laid
// out by render::PageView and painted with render::SceneRenderer, scaled to
// fit with a `border` px margin and centred; the label is painted only when
// entry.showLabel (createThumb, js/grapheditor/Sidebar.js:4601). A null image
// when the entry's cells do not parse; a blank one when nothing is visible.
// `background` fills the tile first. Dark UI themes pass a light tile so that
// the shapes' default black strokes stay visible (the diagram itself is not
// recoloured).
[[nodiscard]] QImage renderPaletteThumbnail(const PaletteEntry& entry, QSize logicalSize, qreal devicePixelRatio,
                                            int border = 1, QColor background = Qt::transparent);

} // namespace qraw::ui
