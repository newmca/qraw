#pragma once
// Typed access to the persistent application settings (QSettings,
// organization/application "Qraw"). Implemented in
// src/ui/app_settings.cpp.

#include "qraw/commands/default_style.h"
#include "qraw/ui/measure_units.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>
#include <QStringList>

namespace qraw::ui {

enum class ThemeKind { System, Light, Dark, HighContrast };

// Which scheme the diagram canvas is shown in (View > Diagram Appearance). FollowTheme (the default) takes
// it from the effective application theme, as draw.io's single appearance setting does (diagram_scheme.h);
// Light and Dark pin it, for a dark UI around a light canvas or the other way round.
enum class DiagramAppearance { FollowTheme, Light, Dark };

// Every key the application persists lives here, so their names and
// defaults are defined once. Each getter reads QSettings on demand and each
// setter writes through, so several instances (or a test that changes
// QSettings::setDefaultFormat/paths) always agree. Tests isolate settings
// with QStandardPaths::setTestModeEnabled(true).
class AppSettings {
public:
    // --- appearance and language (Preferences dialog) ---------------------
    [[nodiscard]] ThemeKind theme() const;            // default System
    void setTheme(ThemeKind theme);
    [[nodiscard]] QString language() const;           // "" = system locale; else "en", "de", ...
    void setLanguage(const QString& language);

    // --- editing defaults ---------------------------------------------------
    [[nodiscard]] int autosaveIntervalSeconds() const; // default 30; 0 = autosave off
    void setAutosaveIntervalSeconds(int seconds);
    [[nodiscard]] double defaultGridSize() const;      // default 10 (draw.io mxGraph.gridSize)
    void setDefaultGridSize(double size);
    [[nodiscard]] bool guidesEnabled() const;          // default true (View > Guides)
    void setGuidesEnabled(bool on);
    // The unit of the length fields (View > Units); the model stays in pixels. default Points.
    [[nodiscard]] MeasureUnit measureUnit() const;
    void setMeasureUnit(MeasureUnit unit);
    // View > Diagram Appearance (see render::Theme and diagram_scheme.h). default FollowTheme. The old
    // boolean "diagramDark" setting is migrated on read: true is Dark, false is FollowTheme; writing the new
    // value removes the old key.
    [[nodiscard]] DiagramAppearance diagramAppearance() const;
    void setDiagramAppearance(DiagramAppearance appearance);
    [[nodiscard]] bool showWelcomeOnStart() const;     // default true
    void setShowWelcomeOnStart(bool on);
    // Crash reports at or before this time were shown in the "Qraw closed unexpectedly" dialog;
    // invalid = none yet.
    [[nodiscard]] QDateTime crashReportsSeenUntil() const;
    void setCrashReportsSeenUntil(const QDateTime& time);

    // --- recent files (File > Open Recent, Welcome dialog) ----------------
    // Most recent first, at most kMaxRecentFiles, absolute paths, no
    // duplicates. addRecentFile moves an existing entry to the front.
    static constexpr int kMaxRecentFiles = 10;
    [[nodiscard]] QStringList recentFiles() const;
    void addRecentFile(const QString& path);
    void removeRecentFile(const QString& path);
    void clearRecentFiles();

    // --- palette (favorites and recently used shapes) -----------------------
    // Entry keys are PaletteEntry::key() ("<paletteId>/<entryId>").
    static constexpr int kMaxRecentShapes = 24;
    [[nodiscard]] QStringList favoriteShapes() const;
    void setFavoriteShapes(const QStringList& keys);
    [[nodiscard]] QStringList recentShapes() const;  // most recent first
    void addRecentShape(const QString& key);
    // Palette ids whose section is expanded in the palette dock.
    [[nodiscard]] QStringList expandedPalettes() const; // default {"general"}
    void setExpandedPalettes(const QStringList& ids);

    // Absolute paths of the user libraries (.xml mxlibrary files) shown in the
    // palette; reopened on start.
    [[nodiscard]] QStringList openLibraries() const;
    void setOpenLibraries(const QStringList& paths);

    // --- styles and fonts ----------------------------------------------------
    // Set as Default Style (style/defaultVertex, style/defaultEdge as style
    // strings); empty when the draw.io defaults are in use.
    [[nodiscard]] commands::DefaultStyles defaultStyles() const;
    void setDefaultStyles(const commands::DefaultStyles& styles);
    // Font families chosen in the Format panel, most recent first, as typed
    // (a draw.io family list such as "Arial,Helvetica" stays one entry).
    static constexpr int kMaxRecentFonts = 10;
    [[nodiscard]] QStringList recentFonts() const;
    void addRecentFont(const QString& family);

    // --- window state (MainWindow saveGeometry/saveState) --------------------
    [[nodiscard]] QByteArray windowGeometry() const;
    void setWindowGeometry(const QByteArray& geometry);
    [[nodiscard]] QByteArray windowState() const;
    void setWindowState(const QByteArray& state);
};

} // namespace qraw::ui
