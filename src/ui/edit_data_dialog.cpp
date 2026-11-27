#include "qraw/ui/edit_data_dialog.h"

#include "qraw/commands/ops_properties.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFormLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QScrollArea>
#include <QShortcut>
#include <QStyle>
#include <QToolButton>
#include <QVBoxLayout>

#include <algorithm>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::EditDataDialog)
};

constexpr int kValueLines = 3; // the height of a value field, in text lines

QString toQ(const std::string& text)
{
    return QString::fromStdString(text);
}

// An icon-only button needs a name for screen readers and a tooltip.
QToolButton* iconButton(const QString& objectName, QStyle::StandardPixmap icon, const QString& text, QWidget* parent)
{
    auto* button = new QToolButton(parent);
    button->setObjectName(objectName);
    button->setIcon(parent->style()->standardIcon(icon));
    button->setToolTip(text);
    button->setAccessibleName(text);
    return button;
}

} // namespace

EditDataDialog::EditDataDialog(const actions::DataSheet& sheet, QWidget* parent) : QDialog(parent), sheet_(sheet)
{
    setObjectName(QStringLiteral("EditDataDialog"));
    setWindowTitle(sheet.isDiagram ? Text::tr("Edit Diagram Data") : Text::tr("Edit Data"));
    auto* layout = new QVBoxLayout(this);

    auto* form = new QFormLayout;
    if (!sheet.id.empty()) {
        // Selectable text, so that it can be copied by hand too.
        auto* idRow = new QWidget(this);
        auto* idLayout = new QHBoxLayout(idRow);
        idLayout->setContentsMargins(0, 0, 0, 0);
        auto* id = new QLineEdit(toQ(sheet.id), idRow);
        id->setObjectName(QStringLiteral("data.id"));
        id->setReadOnly(true);
        id->setAccessibleName(Text::tr("ID"));
        // The theme's copy icon; a text button where the theme has none.
        auto* copy = new QToolButton(idRow);
        copy->setObjectName(QStringLiteral("data.copyId"));
        copy->setToolTip(Text::tr("Copy ID"));
        copy->setAccessibleName(Text::tr("Copy ID"));
        const QIcon copyIcon = QIcon::fromTheme(QStringLiteral("edit-copy"));
        if (copyIcon.isNull()) {
            copy->setText(Text::tr("Copy"));
        } else {
            copy->setIcon(copyIcon);
        }
        connect(copy, &QToolButton::clicked, this, [id] { QApplication::clipboard()->setText(id->text()); });
        idLayout->addWidget(id, 1);
        idLayout->addWidget(copy);
        form->addRow(Text::tr("ID"), idRow);
    }
    if (!sheet.isDiagram) {
        tags_ = new QLineEdit(toQ(sheet.tags), this);
        tags_->setObjectName(QStringLiteral("data.tags"));
        tags_->setPlaceholderText(sheet.tagsMixed ? Text::tr("Multiple Values") : Text::tr("Tags separated by spaces"));
        tags_->setToolTip(Text::tr("Tags are words separated by spaces; they are stored in the cell's tags property."));
        form->addRow(Text::tr("Tags"), tags_);
    }
    layout->addLayout(form);

    // The properties: a scrolling column of rows.
    auto* scroll = new QScrollArea(this);
    scroll->setObjectName(QStringLiteral("data.properties"));
    scroll->setWidgetResizable(true);
    scroll->setFrameShape(QFrame::StyledPanel);
    auto* content = new QWidget(scroll);
    rowsLayout_ = new QVBoxLayout(content);
    emptyLabel_ = new QLabel(Text::tr("No properties. Add one below."), content);
    emptyLabel_->setObjectName(QStringLiteral("data.empty"));
    emptyLabel_->setEnabled(false); // the palette's disabled text
    rowsLayout_->addWidget(emptyLabel_);
    rowsLayout_->addStretch(1);
    scroll->setWidget(content);
    layout->addWidget(scroll, 1);

    for (const actions::DataRow& row : sheet.rows)
        addRow(row, false);

    auto* addLayout = new QHBoxLayout;
    newName_ = new QLineEdit(this);
    newName_->setObjectName(QStringLiteral("data.newName"));
    newName_->setPlaceholderText(Text::tr("Property Name"));
    newName_->setAccessibleName(Text::tr("Property Name"));
    addButton_ = new QPushButton(Text::tr("Add Property"), this);
    addButton_->setObjectName(QStringLiteral("data.addProperty"));
    addButton_->setAutoDefault(false);
    addButton_->setEnabled(false);
    addLayout->addWidget(newName_, 1);
    addLayout->addWidget(addButton_);
    layout->addLayout(addLayout);
    connect(newName_, &QLineEdit::textChanged, this, [this](const QString& text) {
        addButton_->setEnabled(!text.isEmpty());
        error_->clear();
    });
    const auto add = [this] {
        if (addProperty(newName_->text()))
            newName_->clear();
    };
    connect(addButton_, &QPushButton::clicked, this, add);
    connect(newName_, &QLineEdit::returnPressed, this, add);

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("data.error"));
    error_->setWordWrap(true);
    error_->setAccessibleName(Text::tr("Error"));
    layout->addWidget(error_);

    if (sheet.placeholdersAvailable) {
        placeholders_ = new QCheckBox(Text::tr("Placeholders"), this);
        placeholders_->setObjectName(QStringLiteral("data.placeholders"));
        placeholders_->setToolTip(
            Text::tr("Replace %name% in the label with the value of the property of that name."));
        if (sheet.placeholders == Qt::PartiallyChecked) {
            placeholders_->setTristate(true);
            placeholders_->setCheckState(Qt::PartiallyChecked);
        } else {
            placeholders_->setChecked(sheet.placeholders == Qt::Checked);
        }
        connect(placeholders_, &QCheckBox::clicked, this, [this] {
            placeholdersTouched_ = true;
            placeholders_->setTristate(false); // a mixed state is only shown, never chosen
        });
        layout->addWidget(placeholders_);
    }

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);
    // draw.io applies on Ctrl+Enter; plain Enter belongs to the multi-line values.
    auto* apply = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(apply, &QShortcut::activated, this, &QDialog::accept);

    resize(480, 460);
    if (!rows_.empty())
        rows_.front().edit->setFocus();
    else
        newName_->setFocus();
}

void EditDataDialog::addRow(const actions::DataRow& property, bool isNew)
{
    Row row;
    row.name = property.name;
    row.original = property.value;
    row.mixed = property.mixed;
    row.isNew = isNew;

    auto* widget = new QWidget(rowsLayout_->parentWidget());
    widget->setObjectName(QStringLiteral("data.row.%1").arg(toQ(property.name)));
    auto* rowLayout = new QHBoxLayout(widget);
    rowLayout->setContentsMargins(0, 0, 0, 0);
    auto* name = new QLabel(toQ(property.name) + QLatin1Char(':'), widget);
    name->setMinimumWidth(90);
    name->setMaximumWidth(140);
    name->setWordWrap(true);
    name->setTextInteractionFlags(Qt::TextSelectableByMouse);
    name->setAlignment(Qt::AlignRight | Qt::AlignTop);
    auto* edit = new QPlainTextEdit(toQ(property.value), widget);
    edit->setObjectName(QStringLiteral("data.value.%1").arg(toQ(property.name)));
    edit->setAccessibleName(toQ(property.name));
    edit->setTabChangesFocus(true);
    edit->setFixedHeight(edit->fontMetrics().lineSpacing() * kValueLines + 12);
    if (property.mixed)
        edit->setPlaceholderText(Text::tr("Multiple Values"));
    auto* remove = iconButton(QStringLiteral("data.delete.%1").arg(toQ(property.name)), QStyle::SP_TrashIcon,
                              Text::tr("Delete Property %1").arg(toQ(property.name)), widget);
    remove->setIcon(QIcon::fromTheme(QStringLiteral("edit-delete"), remove->icon()));
    rowLayout->addWidget(name, 0, Qt::AlignTop);
    rowLayout->addWidget(edit, 1);
    rowLayout->addWidget(remove, 0, Qt::AlignTop);

    row.widget = widget;
    row.edit = edit;
    rows_.push_back(row);
    const std::size_t index = rows_.size() - 1;
    connect(remove, &QToolButton::clicked, this, [this, index] { removeRow(index); });
    // Before the stretch at the end.
    rowsLayout_->insertWidget(rowsLayout_->count() - 1, widget);
    emptyLabel_->hide();
}

void EditDataDialog::removeRow(std::size_t index)
{
    Row& row = rows_[index];
    row.removed = true;
    row.widget->hide();
    const bool anyLeft = std::any_of(rows_.begin(), rows_.end(), [](const Row& r) { return !r.removed; });
    emptyLabel_->setVisible(!anyLeft);
}

bool EditDataDialog::addProperty(const QString& text)
{
    const std::string name = text.toStdString();
    error_->clear();
    if (name == "tags" && tags_ != nullptr) {
        tags_->setFocus(); // tags have a field of their own
        return true;
    }
    if (!commands::ops::isValidDataName(name)) {
        QString message = Text::tr("The name is not valid. Use letters, digits, '_', '-' and '.', and start with a letter or '_'.");
        if (commands::ops::isReservedDataName(name))
            message += QLatin1Char(' ') + Text::tr("These names are reserved: label, id, placeholders, and names with ':'.");
        error_->setText(message);
        return false;
    }
    for (const Row& row : rows_) {
        if (!row.removed && row.name == name) {
            row.edit->setFocus();
            return true;
        }
    }
    addRow({name, std::string(), false}, true);
    rows_.back().edit->setFocus();
    return true;
}

QString EditDataDialog::errorText() const
{
    return error_->text();
}

actions::DataChange EditDataDialog::change() const
{
    actions::DataChange change;
    for (const Row& row : rows_) {
        if (row.removed) {
            // A property added and deleted again in the dialog never existed.
            if (!row.isNew)
                change.removed.push_back(row.name);
            continue;
        }
        const std::string value = row.edit->toPlainText().toStdString();
        if (row.isNew)
            change.written.push_back({row.name, value, false});
        else if (row.mixed ? !value.empty() : value != row.original)
            change.written.push_back({row.name, value, false});
    }
    if (placeholders_ != nullptr && placeholdersTouched_)
        change.placeholders = placeholders_->checkState() == Qt::Checked;
    if (tags_ != nullptr) {
        const std::string tags = actions::normalizeTags(tags_->text().toStdString());
        const bool changed = sheet_.tagsMixed ? !tags.empty() : tags != actions::normalizeTags(sheet_.tags);
        if (changed)
            change.tags = tags;
    }
    return change;
}

} // namespace qraw::ui
