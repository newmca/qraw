#pragma once
// Edit > Edit Link... (draw.io 'editLink', Alt+Shift+L; LinkDialog): a web
// address (or any URL; only http, https and mailto are ever opened), or a page
// of this document (`data:page/id,<page id>`), and "Open in New Window"
// (linkTarget="_blank"). With several cells that have different links the
// address field is blank, and stays unchanged until the user types or clears it.
// An empty address removes the link.

#include "qraw/ui/data_actions.h"

#include <QDialog>
#include <QString>

#include <string>
#include <vector>

class QCheckBox;
class QComboBox;
class QLineEdit;
class QRadioButton;

namespace qraw::ui {

class EditLinkDialog : public QDialog {
    Q_OBJECT

public:
    struct PageChoice {
        std::string id;
        QString name;
    };
    EditLinkDialog(const actions::LinkSheet& sheet, std::vector<PageChoice> pages, QWidget* parent = nullptr);

    [[nodiscard]] actions::LinkEdit edit() const;

private:
    void updateEnabled();

    actions::LinkSheet sheet_;
    std::vector<PageChoice> pages_;
    QRadioButton* urlChoice_ = nullptr;
    QRadioButton* pageChoice_ = nullptr;
    QLineEdit* url_ = nullptr;
    QComboBox* page_ = nullptr;
    QCheckBox* newWindow_ = nullptr;
    bool linkTouched_ = false;
    bool newWindowTouched_ = false;
};

} // namespace qraw::ui
