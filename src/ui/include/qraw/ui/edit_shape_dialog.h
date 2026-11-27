#pragma once
// Edit > Edit Shape...: the XML of a stencil shape with a live preview
// (draw.io action 'editShape', enabled for a cell whose shape is a stencil,
// js/diagramly/EditorUi.js:39346). OK stores the edited stencil inline as
// shape=stencil(<Graph.compress'ed XML>).
// (src/ui/edit_shape_dialog.cpp).

#include "qraw/core/cell.h"

#include <QDialog>
#include <QImage>
#include <QString>

#include <optional>
#include <string>

class QLabel;
class QPlainTextEdit;
class QTimer;

namespace qraw::ui {

// The XML of the stencil behind a vertex's `shape` style: the decoded XML of
// an inline shape=stencil(...), or the registered stencil of a named shape
// (mxgraph.flowchart.process). nullopt when the cell is not a vertex or its
// shape is not a stencil.
[[nodiscard]] std::optional<std::string> stencilXmlOf(const core::Cell& cell);

// Checks that `xml` is a <shape> (or a <shapes> set, whose first shape is
// used) that the renderer accepts, and returns the style value to store:
// "stencil(" + compressDiagram(xml) + ")". nullopt with a translated reason
// in `error` otherwise (XML parse errors carry line and column).
[[nodiscard]] std::optional<std::string> stencilShapeValue(const QString& xml, QString* error = nullptr);

class EditShapeDialog : public QDialog {
    Q_OBJECT

public:
    explicit EditShapeDialog(const core::Cell& cell, QWidget* parent = nullptr);

    [[nodiscard]] QPlainTextEdit* editor() const { return editor_; }
    // The `shape` style value of the edited XML; nullopt while it is invalid.
    [[nodiscard]] std::optional<std::string> shapeValue() const;
    // The cell drawn with the edited stencil (null while the XML is invalid).
    [[nodiscard]] QImage previewImage() const { return preview_; }
    // Re-validates and redraws the preview now (otherwise after a short pause).
    void updatePreview();
    // The message shown under the editor, empty when the XML is valid.
    [[nodiscard]] QString errorText() const;

    // An invalid stencil shows its error and keeps the dialog open.
    void accept() override;

private:
    std::string style_; // the cell's style
    double width_;
    double height_;
    QPlainTextEdit* editor_;
    QLabel* previewLabel_;
    QLabel* errorLabel_;
    QTimer* timer_;
    QImage preview_;
};

} // namespace qraw::ui
