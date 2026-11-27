#include "qraw/ui/app_settings.h"

#include <QSettings>

#include <string>

#include <algorithm>

namespace qraw::ui {

namespace {

// Keys are grouped like the dialogs that edit them.
constexpr auto kTheme = "appearance/theme";
constexpr auto kLanguage = "appearance/language";
constexpr auto kAutosave = "editing/autosaveSeconds";
constexpr auto kGridSize = "editing/defaultGridSize";
constexpr auto kGuides = "editing/guides";
constexpr auto kDiagramAppearance = "appearance/diagramAppearance"; // "follow", "light" or "dark"
constexpr auto kLegacyDiagramDark = "appearance/diagramDark";         // bool, before diagramAppearance
constexpr auto kUnit = "view/unit";
constexpr auto kWelcome = "startup/showWelcome";
constexpr auto kCrashReportsSeen = "startup/crashReportsSeenUntil";
constexpr auto kRecentFiles = "files/recent";
constexpr auto kFavorites = "palette/favorites";
constexpr auto kRecentShapes = "palette/recent";
constexpr auto kExpanded = "palette/expanded";
constexpr auto kDefaultVertex = "style/defaultVertex";
constexpr auto kDefaultEdge = "style/defaultEdge";
constexpr auto kDefaultFromText = "style/defaultVertexFromText";
constexpr auto kRecentFonts = "text/recentFonts";
constexpr auto kLibraries = "palette/libraries";
constexpr auto kGeometry = "window/geometry";
constexpr auto kState = "window/state";

QStringList pushFront(QStringList list, const QString& value, int limit)
{
    list.removeAll(value);
    list.prepend(value);
    while (list.size() > limit)
        list.removeLast();
    return list;
}

} // namespace

ThemeKind AppSettings::theme() const
{
    const int value = QSettings().value(kTheme, static_cast<int>(ThemeKind::System)).toInt();
    if (value < static_cast<int>(ThemeKind::System) || value > static_cast<int>(ThemeKind::HighContrast))
        return ThemeKind::System;
    return static_cast<ThemeKind>(value);
}

void AppSettings::setTheme(ThemeKind theme)
{
    QSettings().setValue(kTheme, static_cast<int>(theme));
}

QString AppSettings::language() const
{
    return QSettings().value(kLanguage).toString();
}

void AppSettings::setLanguage(const QString& language)
{
    QSettings().setValue(kLanguage, language);
}

int AppSettings::autosaveIntervalSeconds() const
{
    return std::max(0, QSettings().value(kAutosave, 30).toInt());
}

void AppSettings::setAutosaveIntervalSeconds(int seconds)
{
    QSettings().setValue(kAutosave, std::max(0, seconds));
}

MeasureUnit AppSettings::measureUnit() const
{
    return units::fromKey(QSettings().value(kUnit).toString()).value_or(MeasureUnit::Points);
}

void AppSettings::setMeasureUnit(MeasureUnit unit)
{
    QSettings().setValue(kUnit, units::key(unit));
}

double AppSettings::defaultGridSize() const
{
    const double size = QSettings().value(kGridSize, 10.0).toDouble();
    return size > 0 ? size : 10.0;
}

void AppSettings::setDefaultGridSize(double size)
{
    QSettings().setValue(kGridSize, size);
}

bool AppSettings::guidesEnabled() const
{
    return QSettings().value(kGuides, true).toBool();
}

void AppSettings::setGuidesEnabled(bool on)
{
    QSettings().setValue(kGuides, on);
}

DiagramAppearance AppSettings::diagramAppearance() const
{
    const QSettings settings;
    const QString value = settings.value(kDiagramAppearance).toString();
    if (value == QLatin1String("light"))
        return DiagramAppearance::Light;
    if (value == QLatin1String("dark"))
        return DiagramAppearance::Dark;
    if (value.isEmpty() && settings.value(kLegacyDiagramDark, false).toBool())
        return DiagramAppearance::Dark; // the old View > Dark Diagram toggle was on
    return DiagramAppearance::FollowTheme;
}

void AppSettings::setDiagramAppearance(DiagramAppearance appearance)
{
    QSettings settings;
    settings.remove(kLegacyDiagramDark);
    switch (appearance) {
    case DiagramAppearance::FollowTheme:
        settings.setValue(kDiagramAppearance, QStringLiteral("follow"));
        break;
    case DiagramAppearance::Light:
        settings.setValue(kDiagramAppearance, QStringLiteral("light"));
        break;
    case DiagramAppearance::Dark:
        settings.setValue(kDiagramAppearance, QStringLiteral("dark"));
        break;
    }
}

bool AppSettings::showWelcomeOnStart() const
{
    return QSettings().value(kWelcome, true).toBool();
}

void AppSettings::setShowWelcomeOnStart(bool on)
{
    QSettings().setValue(kWelcome, on);
}

QDateTime AppSettings::crashReportsSeenUntil() const
{
    return QDateTime::fromString(QSettings().value(kCrashReportsSeen).toString(), Qt::ISODateWithMs);
}

void AppSettings::setCrashReportsSeenUntil(const QDateTime& time)
{
    QSettings().setValue(kCrashReportsSeen, time.toUTC().toString(Qt::ISODateWithMs));
}

QStringList AppSettings::recentFiles() const
{
    return QSettings().value(kRecentFiles).toStringList();
}

void AppSettings::addRecentFile(const QString& path)
{
    QSettings().setValue(kRecentFiles, pushFront(recentFiles(), path, kMaxRecentFiles));
}

void AppSettings::removeRecentFile(const QString& path)
{
    QStringList list = recentFiles();
    list.removeAll(path);
    QSettings().setValue(kRecentFiles, list);
}

void AppSettings::clearRecentFiles()
{
    QSettings().remove(kRecentFiles);
}

QStringList AppSettings::favoriteShapes() const
{
    return QSettings().value(kFavorites).toStringList();
}

void AppSettings::setFavoriteShapes(const QStringList& keys)
{
    QSettings().setValue(kFavorites, keys);
}

QStringList AppSettings::recentShapes() const
{
    return QSettings().value(kRecentShapes).toStringList();
}

void AppSettings::addRecentShape(const QString& key)
{
    QSettings().setValue(kRecentShapes, pushFront(recentShapes(), key, kMaxRecentShapes));
}

commands::DefaultStyles AppSettings::defaultStyles() const
{
    const QSettings settings;
    commands::DefaultStyles styles;
    styles.vertex = core::Style(settings.value(kDefaultVertex).toString().toStdString());
    styles.edge = core::Style(settings.value(kDefaultEdge).toString().toStdString());
    styles.vertexFromText = settings.value(kDefaultFromText, false).toBool();
    return styles;
}

void AppSettings::setDefaultStyles(const commands::DefaultStyles& styles)
{
    QSettings settings;
    if (styles.empty()) {
        // Back to draw.io's defaults: nothing is stored.
        for (const char* key : {kDefaultVertex, kDefaultEdge, kDefaultFromText})
            settings.remove(QString::fromLatin1(key));
        return;
    }
    settings.setValue(kDefaultVertex, QString::fromStdString(styles.vertex.toString()));
    settings.setValue(kDefaultEdge, QString::fromStdString(styles.edge.toString()));
    settings.setValue(kDefaultFromText, styles.vertexFromText);
}

QStringList AppSettings::recentFonts() const
{
    return QSettings().value(kRecentFonts).toStringList();
}

void AppSettings::addRecentFont(const QString& family)
{
    if (!family.trimmed().isEmpty())
        QSettings().setValue(kRecentFonts, pushFront(recentFonts(), family, kMaxRecentFonts));
}

QStringList AppSettings::expandedPalettes() const
{
    return QSettings().value(kExpanded, QStringList{QStringLiteral("general")}).toStringList();
}

void AppSettings::setExpandedPalettes(const QStringList& ids)
{
    QSettings().setValue(kExpanded, ids);
}

QStringList AppSettings::openLibraries() const
{
    return QSettings().value(kLibraries).toStringList();
}

void AppSettings::setOpenLibraries(const QStringList& paths)
{
    QSettings().setValue(kLibraries, paths);
}

QByteArray AppSettings::windowGeometry() const
{
    return QSettings().value(kGeometry).toByteArray();
}

void AppSettings::setWindowGeometry(const QByteArray& geometry)
{
    QSettings().setValue(kGeometry, geometry);
}

QByteArray AppSettings::windowState() const
{
    return QSettings().value(kState).toByteArray();
}

void AppSettings::setWindowState(const QByteArray& state)
{
    QSettings().setValue(kState, state);
}

} // namespace qraw::ui
