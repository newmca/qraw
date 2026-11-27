#pragma once
// Arrange > Edit Geometry... (draw.io action 'editGeometry', Ctrl+Shift+M):
// Left, Top, Width and Height of the selected vertices. The fields show the
// first vertex's values, or blank where the vertices differ. OK applies only
// the fields the user changed (actions::editGeometry, one edit). The fields use
// the unit of View > Units; the edit is in pixels.

#include "qraw/core/geometry.h"
#include "qraw/ui/edit_actions.h"

#include <QDialog>

#include <vector>

namespace qraw::ui {

class LengthSpinBox;

class EditGeometryDialog : public QDialog {
public:
    // `bounds`: the selected vertices' bounds, non-empty, in selection order.
    explicit EditGeometryDialog(const std::vector<core::Rect>& bounds, QWidget* parent = nullptr);

    // The fields the user changed; the others are nullopt.
    [[nodiscard]] actions::GeometryEdit edit() const;

private:
    struct Field {
        LengthSpinBox* spin = nullptr;
        bool changed = false;
    };
    Field x_;
    Field y_;
    Field width_;
    Field height_;
};

} // namespace qraw::ui
