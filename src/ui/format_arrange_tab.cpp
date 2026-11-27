// The Arrange tab of the Format panel: geometry, rotation, z-order, grouping,
// alignment and locking (draw.io's ArrangePanel, Format.js:2240; addGeometry
// :3324, addAngle :3155, addAlign :2816, addDistribute :3098, addGroupOps
// :2734).
#include "format_controls.h"
#include "format_edit.h"
#include "format_tab.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_structure.h"
#include "qraw/commands/transaction.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/edit_actions.h"

#include <QCheckBox>
#include <QDoubleSpinBox>
#include <QFormLayout>
#include <QGroupBox>
#include <QHBoxLayout>
#include <QSignalBlocker>
#include <QSpinBox>
#include <QToolButton>
#include <QVBoxLayout>

#include <functional>
#include <initializer_list>

namespace qraw::ui {
namespace {

// Moves every selected vertex so that its x (or y) becomes `target`. Each
// vertex is read again before its move, so a moved group does not shift the
// offsets of cells that were selected with it.
void setVertexPosition(DocumentSession& docSession, const QString& name, bool horizontal, double target,
                       const std::string& mergeKey)
{
    const std::vector<std::string> ids = selectedCells(docSession, CellFilter::Vertices);
    if (ids.empty())
        return;
    editCurrentPage(
        docSession, name,
        [&docSession, &ids, horizontal, target](commands::Transaction& tx, std::size_t index) {
            for (const std::string& id : ids) {
                const core::Cell* cell = docSession.document().page(index)->findCell(id);
                if (cell == nullptr || cell->geometry() == nullptr)
                    continue;
                const core::Geometry& geometry = *cell->geometry();
                const double dx = horizontal ? target - geometry.x() : 0;
                const double dy = horizontal ? 0 : target - geometry.y();
                if (dx == 0 && dy == 0)
                    continue;
                const commands::Result result = commands::ops::moveCells(tx, index, {id}, dx, dy);
                if (!result)
                    return result;
            }
            return commands::Result::success();
        },
        mergeKey);
}

// Sets the width (or height) of every selected vertex to `value`.
void setVertexSize(DocumentSession& docSession, const QString& name, bool width, double value,
                   const std::string& mergeKey)
{
    const std::vector<std::string> ids = selectedCells(docSession, CellFilter::Vertices);
    if (ids.empty())
        return;
    editCurrentPage(
        docSession, name,
        [&docSession, &ids, width, value](commands::Transaction& tx, std::size_t index) {
            for (const std::string& id : ids) {
                const core::Cell* cell = docSession.document().page(index)->findCell(id);
                if (cell == nullptr || cell->geometry() == nullptr)
                    continue;
                core::Rect bounds = cell->geometry()->bounds();
                if (width)
                    bounds.width = value;
                else
                    bounds.height = value;
                const commands::Result result = commands::ops::resizeCells(tx, index, {id}, {bounds});
                if (!result)
                    return result;
            }
            return commands::Result::success();
        },
        mergeKey);
}

// A tool button that runs `action` on click.
QToolButton* makeActionButton(const char* objectName, const QString& text, std::function<void()> action)
{
    auto* button = new QToolButton;
    button->setObjectName(QString::fromLatin1(objectName));
    button->setText(text);
    onClicked(button, [action](bool) { action(); });
    return button;
}

// Puts buttons side by side.
QWidget* buttonRow(std::initializer_list<QToolButton*> buttons)
{
    auto* row = new QWidget;
    auto* layout = new QHBoxLayout(row);
    layout->setContentsMargins(0, 0, 0, 0);
    for (QToolButton* button : buttons)
        layout->addWidget(button);
    layout->addStretch();
    return row;
}

class ArrangeTab final : public FormatTab {
public:
    ArrangeTab(DocumentSession& docSession, const QList<QAction*>& dataActions);

    void refresh(const StyleSummary& summary) override;

private:
    void buildGeometry(QVBoxLayout* layout);
    void buildOrder(QVBoxLayout* layout);
    void buildAlign(QVBoxLayout* layout);
    void buildTable(QVBoxLayout* layout);

    QGroupBox* geometryGroup_ = nullptr;
    QGroupBox* tableGroup_ = nullptr;
    QToolButton* mergeCells_ = nullptr;
    QToolButton* unmergeCells_ = nullptr;
    LengthSpinBox* x_ = nullptr;
    LengthSpinBox* y_ = nullptr;
    LengthSpinBox* width_ = nullptr;
    LengthSpinBox* height_ = nullptr;
    QSpinBox* angle_ = nullptr;
    QToolButton* group_ = nullptr;
    QToolButton* ungroup_ = nullptr;
    QToolButton* distributeHorizontal_ = nullptr;
    QToolButton* distributeVertical_ = nullptr;
    QCheckBox* lock_ = nullptr;
    QToolButton* turn_ = nullptr;
    QToolButton* autosize_ = nullptr;
};

ArrangeTab::ArrangeTab(DocumentSession& docSession, const QList<QAction*>& dataActions) : FormatTab(docSession)
{
    buildGeometry(contentLayout());
    buildTable(contentLayout());
    buildOrder(contentLayout());
    buildAlign(contentLayout());
    // Format.js:7249 ArrangePanel.addEditOps: Edit Data, Edit Tooltip, Edit Link, Edit Image.
    if (!dataActions.isEmpty()) {
        QFormLayout* form = addFormGroup(contentLayout(), formatTr("Data"));
        form->addRow(createActionButtonGrid(dataActions, QStringLiteral("format.dataActions")));
    }
    contentLayout()->addStretch();
}

void ArrangeTab::buildGeometry(QVBoxLayout* layout)
{
    QFormLayout* form = addFormGroup(layout, formatTr("Geometry"), &geometryGroup_);
    DocumentSession* docSession = &this->session();

    x_ = new LengthSpinBox(-100000, 100000);
    x_->setObjectName(QStringLiteral("format.x"));
    form->addRow(formatTr("Left"), x_);
    onLengthEdited(x_, *docSession, [docSession](double value) {
        setVertexPosition(*docSession, formatTr("Set Position"), true, value, "format:x:");
    });

    y_ = new LengthSpinBox(-100000, 100000);
    y_->setObjectName(QStringLiteral("format.y"));
    form->addRow(formatTr("Top"), y_);
    onLengthEdited(y_, *docSession, [docSession](double value) {
        setVertexPosition(*docSession, formatTr("Set Position"), false, value, "format:y:");
    });

    width_ = new LengthSpinBox(1, 100000);
    width_->setObjectName(QStringLiteral("format.width"));
    form->addRow(formatTr("Width"), width_);
    onLengthEdited(width_, *docSession, [docSession](double value) {
        setVertexSize(*docSession, formatTr("Set Size"), true, value, "format:width:");
    });

    height_ = new LengthSpinBox(1, 100000);
    height_->setObjectName(QStringLiteral("format.height"));
    form->addRow(formatTr("Height"), height_);
    onLengthEdited(height_, *docSession, [docSession](double value) {
        setVertexSize(*docSession, formatTr("Set Size"), false, value, "format:height:");
    });

    angle_ = createIntSpin(nullptr, 0, 359);
    angle_->setObjectName(QStringLiteral("format.angle"));
    form->addRow(formatTr("Angle"), angle_);
    onSpinEdited(angle_, *docSession, [docSession](double value) {
        const std::vector<std::string> ids = selectedCells(*docSession);
        applyStyle(*docSession, formatTr("Rotate"), ids, "rotation", std::to_string(static_cast<int>(value)),
                   formatMergeKey("rotation", ids));
    });

    // draw.io's ArrangePanel.addAngle: the turn button sits under the angle, the autosize button by the size.
    turn_ = makeActionButton("format.turn", formatTr("Rotate 90 Degrees"),
                             [docSession] { actions::turn(*docSession); });
    form->addRow(buttonRow({turn_}));

    autosize_ = makeActionButton("format.autosize", formatTr("Autosize"),
                                 [docSession] { actions::autosize(*docSession); });
    form->addRow(buttonRow({autosize_}));
}

// ArrangePanel.addTable (Format.js:2401): row and column buttons for a table, a row or a cell.
void ArrangeTab::buildTable(QVBoxLayout* layout)
{
    QFormLayout* form = addFormGroup(layout, formatTr("Table"), &tableGroup_);
    DocumentSession* docSession = &this->session();
    using actions::TableEdit;
    const auto button = [docSession](const char* name, const QString& text, TableEdit edit) {
        return makeActionButton(name, text, [docSession, edit] { actions::editTable(*docSession, edit); });
    };
    form->addRow(buttonRow({
        button("format.table.insertRowAbove", formatTr("Insert Row Above"), TableEdit::InsertRowBefore),
        button("format.table.insertRowBelow", formatTr("Insert Row Below"), TableEdit::InsertRowAfter),
        button("format.table.deleteRow", formatTr("Delete Row"), TableEdit::DeleteRow),
    }));
    form->addRow(buttonRow({
        button("format.table.insertColumnLeft", formatTr("Insert Column Left"), TableEdit::InsertColumnBefore),
        button("format.table.insertColumnRight", formatTr("Insert Column Right"), TableEdit::InsertColumnAfter),
        button("format.table.deleteColumn", formatTr("Delete Column"), TableEdit::DeleteColumn),
    }));
    // Format.js:2604: Merge Cells for a range of cells, Unmerge Cells for a merged cell.
    mergeCells_ = makeActionButton("format.table.mergeCells", formatTr("Merge Cells"),
                                   [docSession] { actions::mergeCells(*docSession); });
    unmergeCells_ = makeActionButton("format.table.unmergeCells", formatTr("Unmerge Cells"),
                                     [docSession] { actions::unmergeCells(*docSession); });
    form->addRow(buttonRow({mergeCells_, unmergeCells_}));
}

void ArrangeTab::buildOrder(QVBoxLayout* layout)
{
    QFormLayout* form = addFormGroup(layout, formatTr("Arrange"));
    DocumentSession* docSession = &this->session();

    form->addRow(buttonRow({
        makeActionButton("format.toFront", formatTr("To Front"),
                         [docSession] { actions::order(*docSession, commands::ops::ZOrder::ToFront); }),
        makeActionButton("format.toBack", formatTr("To Back"),
                         [docSession] { actions::order(*docSession, commands::ops::ZOrder::ToBack); }),
        makeActionButton("format.forward", formatTr("Forward"),
                         [docSession] { actions::order(*docSession, commands::ops::ZOrder::BringForward); }),
        makeActionButton("format.backward", formatTr("Backward"),
                         [docSession] { actions::order(*docSession, commands::ops::ZOrder::SendBackward); }),
    }));

    group_ = makeActionButton("format.group", formatTr("Group"), [docSession] { actions::group(*docSession); });
    ungroup_ = makeActionButton("format.ungroup", formatTr("Ungroup"), [docSession] { actions::ungroup(*docSession); });
    form->addRow(buttonRow({group_, ungroup_}));

    lock_ = new QCheckBox(formatTr("Lock"));
    lock_->setObjectName(QStringLiteral("format.lock"));
    form->addRow(lock_);
    onClicked(lock_, [docSession, this](bool) {
        if (!actions::toggleLock(*docSession)) {
            QSignalBlocker blocker(lock_);
            lock_->setChecked(!lock_->isChecked()); // nothing changed: show the model state again
        }
    });
}

void ArrangeTab::buildAlign(QVBoxLayout* layout)
{
    QFormLayout* form = addFormGroup(layout, formatTr("Align"));
    DocumentSession* docSession = &this->session();

    form->addRow(buttonRow({
        makeActionButton("format.alignLeft", formatTr("Left"),
                         [docSession] { actions::align(*docSession, actions::Align::Left); }),
        makeActionButton("format.alignCenter", formatTr("Center"),
                         [docSession] { actions::align(*docSession, actions::Align::Center); }),
        makeActionButton("format.alignRight", formatTr("Right"),
                         [docSession] { actions::align(*docSession, actions::Align::Right); }),
    }));
    form->addRow(buttonRow({
        makeActionButton("format.alignTop", formatTr("Top"),
                         [docSession] { actions::align(*docSession, actions::Align::Top); }),
        makeActionButton("format.alignMiddle", formatTr("Middle"),
                         [docSession] { actions::align(*docSession, actions::Align::Middle); }),
        makeActionButton("format.alignBottom", formatTr("Bottom"),
                         [docSession] { actions::align(*docSession, actions::Align::Bottom); }),
    }));

    distributeHorizontal_ = makeActionButton("format.distributeHorizontal", formatTr("Distribute horizontally"),
                                             [docSession] { actions::distribute(*docSession, true); });
    distributeVertical_ = makeActionButton("format.distributeVertical", formatTr("Distribute vertically"),
                                           [docSession] { actions::distribute(*docSession, false); });
    form->addRow(buttonRow({distributeHorizontal_, distributeVertical_}));
}

void ArrangeTab::refresh(const StyleSummary& summary)
{
    geometryGroup_->setVisible(summary.hasVertices());
    tableGroup_->setVisible(!actions::tableTarget(session()).empty());
    const bool canMerge = actions::canMergeCells(session());
    mergeCells_->setVisible(canMerge);
    unmergeCells_->setVisible(!canMerge && actions::canUnmergeCells(session()));
    const GeometryValue& geometry = summary.geometry();
    x_->setPixels(geometry.x);
    y_->setPixels(geometry.y);
    width_->setPixels(geometry.width);
    height_->setPixels(geometry.height);
    setSpinState(angle_, summary.number("rotation", 0));

    const core::Page* page = session().currentPage();
    bool anyChildren = false;
    if (page != nullptr) {
        for (const std::string& id : selectedCells(session(), CellFilter::Vertices))
            anyChildren = anyChildren || hasChildren(*page, id);
    }
    group_->setEnabled(summary.cellCount() >= 2);
    ungroup_->setEnabled(anyChildren);
    distributeHorizontal_->setEnabled(summary.vertexCount() >= 3);
    distributeVertical_->setEnabled(summary.vertexCount() >= 3);
    turn_->setEnabled(summary.cellCount() > 0);
    autosize_->setEnabled(summary.vertexCount() > 0);

    setCheckState(lock_, summary.anyLocked());
}

} // namespace

std::unique_ptr<FormatTab> makeArrangeTab(DocumentSession& session, const QList<QAction*>& dataActions)
{
    return std::make_unique<ArrangeTab>(session, dataActions);
}

} // namespace qraw::ui
