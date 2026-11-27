#include "qraw/ui/diagram_scheme.h"

#include <QGuiApplication>
#include <QStyleHints>

namespace qraw::ui {

bool resolveDiagramDark(ThemeKind theme, DiagramAppearance appearance, Qt::ColorScheme osScheme)
{
    switch (appearance) {
    case DiagramAppearance::Light:
        return false;
    case DiagramAppearance::Dark:
        return true;
    case DiagramAppearance::FollowTheme:
        break;
    }
    switch (theme) {
    case ThemeKind::Light:
        return false;
    case ThemeKind::Dark:
    case ThemeKind::HighContrast:
        return true;
    case ThemeKind::System:
        return osScheme == Qt::ColorScheme::Dark;
    }
    return false;
}

bool resolveDiagramDark(ThemeKind theme, DiagramAppearance appearance)
{
    const QStyleHints* hints = QGuiApplication::styleHints();
    return resolveDiagramDark(theme, appearance, hints != nullptr ? hints->colorScheme() : Qt::ColorScheme::Unknown);
}

} // namespace qraw::ui
