#pragma once
// Edit > Edit Data... (draw.io 'editData', Ctrl+M; EditDataDialog,
// js/grapheditor/Dialogs.js:3834): the cell's id, its tags, its custom properties
// (a name and a multi-line value each, with Delete), Add Property, and the
// Placeholders check box. Several cells show the union of their properties; a
// property whose values differ shows "Multiple Values" and is written only when
// the user types a value. OK applies nothing itself: change() describes what the
// user changed, for actions::editData (one undo step).

#include "qraw/ui/data_actions.h"

#include <QDialog>

#include <vector>

class QCheckBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;
class QVBoxLayout;
class QWidget;

namespace qraw::ui {

class EditDataDialog : public QDialog {
    Q_OBJECT

public:
    explicit EditDataDialog(const actions::DataSheet& sheet, QWidget* parent = nullptr);

    // The user's changes against the sheet the dialog was opened with.
    [[nodiscard]] actions::DataChange change() const;

    // Adds a property row as the Add Property button does: false, with the
    // reason shown under the field, for a reserved or invalid name; an existing
    // name only moves the focus to its value.
    bool addProperty(const QString& name);
    // The reason the last Add Property was refused ("" when it was not).
    [[nodiscard]] QString errorText() const;

private:
    struct Row {
        std::string name;
        std::string original; // the value shown first
        bool mixed = false;
        bool isNew = false;   // added in this dialog
        bool removed = false;
        QWidget* widget = nullptr;
        QPlainTextEdit* edit = nullptr;
    };
    void addRow(const actions::DataRow& property, bool isNew);
    void removeRow(std::size_t index);

    actions::DataSheet sheet_;
    std::vector<Row> rows_;
    QVBoxLayout* rowsLayout_ = nullptr;
    QLabel* emptyLabel_ = nullptr;
    QLineEdit* tags_ = nullptr;
    QLineEdit* newName_ = nullptr;
    QPushButton* addButton_ = nullptr;
    QLabel* error_ = nullptr;
    QCheckBox* placeholders_ = nullptr;
    bool placeholdersTouched_ = false;
};

} // namespace qraw::ui
