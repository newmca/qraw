#pragma once
// Which colour scheme the diagram canvas is drawn in. draw.io has one appearance setting (light, dark
// or automatic) that drives both the UI and the canvas (Editor.darkMode, EditorUi.setDarkMode);
// Qraw's canvas follows the effective application theme by default (D-053) and keeps an explicit
// override (AppSettings::diagramAppearance). Implemented in src/ui/diagram_scheme.cpp.

#include "qraw/ui/app_settings.h"

#include <Qt>

namespace qraw::ui {

// True when the canvas is drawn in the dark scheme.
//  * an explicit appearance (Light, Dark) wins;
//  * FollowTheme: the Light theme is light, the Dark theme is dark, the High Contrast theme is dark (its
//    palette is white on black), System is dark when `osScheme` is Qt::ColorScheme::Dark (Unknown counts as
//    light).
[[nodiscard]] bool resolveDiagramDark(ThemeKind theme, DiagramAppearance appearance, Qt::ColorScheme osScheme);

// resolveDiagramDark with the scheme the platform reports now (QStyleHints::colorScheme).
[[nodiscard]] bool resolveDiagramDark(ThemeKind theme, DiagramAppearance appearance);

} // namespace qraw::ui
