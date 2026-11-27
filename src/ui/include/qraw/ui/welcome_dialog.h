#pragma once
// Start dialog: new blank diagram, recent files, templates. Implemented in
// src/ui/welcome_dialog.cpp. Modelled on draw.io's
// NewDialog/TemplatesDialog (js/diagramly/Dialogs.js) without online parts.

#include <QDialog>
#include <QString>

#include <memory>

namespace qraw::ui {

class AppSettings;

// Left: "Blank Diagram", "Open Existing Diagram...", the recent files list
// (AppSettings::recentFiles; missing files greyed out). Right: template
// categories (TemplateCatalog) and a grid of template thumbnails
// (rendered with render::exportPage(png, scale fitting 160x120) on first
// display, cached per path). Double click or "Create" picks; a
// "Show at startup" checkbox writes AppSettings::setShowWelcomeOnStart.
// The dialog performs no file I/O itself besides reading the templates for
// thumbnails; the caller acts on choice().
class WelcomeDialog : public QDialog {
    Q_OBJECT

public:
    enum class Choice { None, Blank, OpenDialog, OpenRecent, Template };

    explicit WelcomeDialog(AppSettings& settings, QWidget* parent = nullptr);
    ~WelcomeDialog() override;

    [[nodiscard]] Choice choice() const;
    // The recent file or template path for OpenRecent / Template.
    [[nodiscard]] QString path() const;

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::ui
