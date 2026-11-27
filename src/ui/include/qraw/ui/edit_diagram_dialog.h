#pragma once
// Extras > Edit Diagram... (draw.io 'editDiagram', EditDiagramDialog in
// js/grapheditor/Dialogs.js): the current page's <mxGraphModel> as indented
// XML in a monospace editor. OK parses the text; a parse error is shown with
// its line and column and the dialog stays open, so the text is never lost.
// Cancel changes nothing. The caller applies the parsed model as one edit
// (actions::replacePageModel).
//
// The text can be as large as the diagram (images are embedded as base64 in
// style values, tens of megabytes), so it is loaded once, as plain text, with
// no highlighting; see KNOWN_ISSUES for the limits.

#include "qraw/core/page.h"

#include <QDialog>

#include <optional>
#include <string>

class QLabel;
class QPlainTextEdit;

namespace qraw::ui {

class EditDiagramDialog : public QDialog {
    Q_OBJECT

public:
    // `text`: pageModelText() of the current page.
    explicit EditDiagramDialog(const std::string& text, QWidget* parent = nullptr);
    ~EditDiagramDialog() override;

    // The model parsed from the text when the dialog was accepted; empty when the
    // text was not changed (it is not parsed then).
    [[nodiscard]] const std::optional<core::Page>& model() const { return model_; }
    // Moves the model out (the parsed page can be tens of megabytes).
    [[nodiscard]] std::optional<core::Page> takeModel() { return std::move(model_); }
    // The text as it is now (UTF-8).
    [[nodiscard]] std::string text() const;

    // Texts above this size (bytes) get a warning that editing may be slow.
    static constexpr qsizetype kLargeText = 8 * 1024 * 1024;

public slots:
    void accept() override;

private:
    void showError(const QString& message, int line, int column);

    QPlainTextEdit* edit_ = nullptr;
    QLabel* error_ = nullptr;
    QString initial_;
    std::optional<core::Page> model_;
};

} // namespace qraw::ui
