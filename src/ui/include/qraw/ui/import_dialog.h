#pragma once
// Insert > Advanced > From Text..., CSV... and Mermaid... (draw.io's ParseDialog, the
// CSV dialog and the Mermaid dialog): one text area in a monospace font, an
// example in its placeholder, Insert and Cancel. Insert runs the importer of
// commands/import_diagram.h; when the text has errors they are listed with their
// line numbers (a click goes to the line) and the dialog stays open with the
// text as it was. Warnings (input that was understood but not used) do not stop
// the import; the caller shows them.

#include "qraw/commands/import_diagram.h"

#include <QDialog>

class QComboBox;
class QListWidget;
class QPlainTextEdit;

namespace qraw::ui {

class ImportDialog : public QDialog {
    Q_OBJECT

public:
    enum class Kind { FromText, Csv, Mermaid };

    // `measure` sizes the labels of the new shapes (actions::importLabelMeasure).
    ImportDialog(Kind kind, commands::imports::LabelMeasure measure, QWidget* parent = nullptr);

    [[nodiscard]] Kind kind() const { return kind_; }
    [[nodiscard]] QString text() const;
    void setText(const QString& text);

    // From Text: the type (a list or a diagram) and how it is arranged.
    [[nodiscard]] commands::imports::TextType textType() const;
    [[nodiscard]] commands::imports::TextLayout textLayout() const;
    void setTextLayout(commands::imports::TextLayout layout);

    // What Insert made: valid after the dialog was accepted.
    [[nodiscard]] const commands::imports::Result& result() const { return result_; }

    // The example shown in the text area while it is empty.
    [[nodiscard]] QString example() const;

public slots:
    // Insert: imports the text; accepts the dialog when there is something to insert, else shows the errors.
    void tryInsert();

private:
    void fillLayouts();
    void showErrors(const std::vector<commands::imports::Diagnostic>& errors);
    void goToLine(int line, int column);

    Kind kind_;
    commands::imports::LabelMeasure measure_;
    QPlainTextEdit* edit_ = nullptr;
    QListWidget* errors_ = nullptr;
    QComboBox* type_ = nullptr;
    QComboBox* layout_ = nullptr;
    commands::imports::Result result_;
};

} // namespace qraw::ui
