#pragma once
// Extras > Preferences.

#include <QDialog>

#include <memory>

namespace qraw::ui {

class AppSettings;

// Controls bound to AppSettings: theme (System, Light, Dark, High Contrast),
// language (System + every compiled-in translation found as
// :/qraw/i18n/qraw_<lang>.qm, shown by QLocale native language name;
// "a restart is required" note), autosave interval (Off, 10 s, 30 s, 1 min,
// 5 min), default grid size for new diagrams (1..100), guides on/off, show
// the welcome dialog at startup. OK writes the settings and emits
// settingsApplied(); Cancel writes nothing.
class PreferencesDialog : public QDialog {
    Q_OBJECT

public:
    explicit PreferencesDialog(AppSettings& settings, QWidget* parent = nullptr);
    ~PreferencesDialog() override;

    // Writes the controls to AppSettings (what OK does) and emits.
    void apply();

signals:
    // Theme/autosave/guides may have changed: MainWindow re-applies them.
    void settingsApplied();

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
