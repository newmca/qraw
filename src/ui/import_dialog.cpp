#include "qraw/ui/import_dialog.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QFormLayout>
#include <QLabel>
#include <QListWidget>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QComboBox>
#include <QShortcut>
#include <QTextBlock>
#include <QTextCursor>
#include <QVBoxLayout>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::ImportDialog)
};

namespace imports = commands::imports;

// The example of From Text: not translated, it is the data the user edits.
constexpr char kListExample[] = "Fruit\n  Apple\n  Banana\nVegetables\n  Carrot\n  Leek\n    Baby leek\n";
constexpr char kDiagramExample[] = "Start -> Decide\nDecide -> Ship: yes\nDecide -> Fix: no\nFix -> Decide\n// Names in \"quotes\" may hold -> and :\n";

QString utf8(std::string_view text)
{
    return QString::fromUtf8(text.data(), static_cast<qsizetype>(text.size()));
}

} // namespace

ImportDialog::ImportDialog(Kind kind, imports::LabelMeasure measure, QWidget* parent)
    : QDialog(parent), kind_(kind), measure_(std::move(measure))
{
    const char* objectName = kind == Kind::FromText ? "ImportTextDialog" : kind == Kind::Csv ? "ImportCsvDialog" : "ImportMermaidDialog";
    setObjectName(QLatin1String(objectName));
    setWindowTitle(kind == Kind::FromText ? Text::tr("Insert From Text")
                   : kind == Kind::Csv    ? Text::tr("Insert CSV")
                                          : Text::tr("Insert Mermaid Diagram"));
    resize(640, 520);

    auto* intro = new QLabel(this);
    intro->setObjectName(QStringLiteral("import.intro"));
    intro->setWordWrap(true);
    if (kind == Kind::FromText) {
        intro->setText(Text::tr("A list: indent a line to make it a child of the line above. A diagram: write connections such as A -> B."));
    } else if (kind == Kind::Csv) {
        intro->setText(Text::tr("Lines that start with # configure the import. The first other line holds the column names, then one line per shape."));
    } else {
        intro->setText(Text::tr("Flowcharts (graph) and sequence diagrams are supported. Other Mermaid diagram types are not."));
    }

    auto* form = new QFormLayout;
    if (kind == Kind::FromText) {
        type_ = new QComboBox(this);
        type_->setObjectName(QStringLiteral("import.type"));
        type_->setAccessibleName(Text::tr("Type"));
        type_->addItem(Text::tr("List"), static_cast<int>(imports::TextType::List));
        type_->addItem(Text::tr("Diagram"), static_cast<int>(imports::TextType::Diagram));
        layout_ = new QComboBox(this);
        layout_->setObjectName(QStringLiteral("import.layout"));
        layout_->setAccessibleName(Text::tr("Layout"));
        form->addRow(Text::tr("&Type:"), type_);
        form->addRow(Text::tr("&Layout:"), layout_);
        connect(type_, &QComboBox::currentIndexChanged, this, [this] {
            fillLayouts();
            edit_->setPlaceholderText(example());
        });
    }

    edit_ = new QPlainTextEdit(this);
    edit_->setObjectName(QStringLiteral("import.text"));
    edit_->setAccessibleName(Text::tr("Text to import"));
    edit_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    edit_->setLineWrapMode(QPlainTextEdit::NoWrap);
    edit_->setTabChangesFocus(false);
    edit_->setMinimumSize(380, 200);

    errors_ = new QListWidget(this);
    errors_->setObjectName(QStringLiteral("import.errors"));
    errors_->setAccessibleName(Text::tr("Errors"));
    errors_->setMaximumHeight(110);
    errors_->hide();
    connect(errors_, &QListWidget::currentRowChanged, this, [this](int row) {
        if (const QListWidgetItem* item = errors_->item(row))
            goToLine(item->data(Qt::UserRole).toInt(), item->data(Qt::UserRole + 1).toInt());
    });

    auto* buttons = new QDialogButtonBox(this);
    QPushButton* insert = buttons->addButton(Text::tr("Insert"), QDialogButtonBox::AcceptRole);
    insert->setObjectName(QStringLiteral("import.insert"));
    insert->setDefault(true);
    // The Insert button does not accept by itself: it imports first and may find errors.
    connect(insert, &QPushButton::clicked, this, &ImportDialog::tryInsert);
    QPushButton* sample = buttons->addButton(Text::tr("Load Example"), QDialogButtonBox::ActionRole);
    sample->setObjectName(QStringLiteral("import.example"));
    sample->setToolTip(Text::tr("Replace the text with the example"));
    connect(sample, &QPushButton::clicked, this, [this] {
        edit_->setPlainText(example());
        edit_->setFocus();
    });
    buttons->addButton(QDialogButtonBox::Cancel);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    auto* apply = new QShortcut(QKeySequence(Qt::CTRL | Qt::Key_Return), this);
    connect(apply, &QShortcut::activated, this, &ImportDialog::tryInsert);

    auto* layout = new QVBoxLayout(this);
    layout->addWidget(intro);
    layout->addLayout(form);
    layout->addWidget(edit_, 1);
    layout->addWidget(errors_);
    layout->addWidget(buttons);

    fillLayouts();
    edit_->setPlaceholderText(example());
    edit_->setFocus();
}

QString ImportDialog::example() const
{
    switch (kind_) {
    case Kind::Csv: return utf8(imports::csvExample());
    case Kind::Mermaid: return utf8(imports::mermaidExample());
    case Kind::FromText: return QLatin1String(textType() == imports::TextType::List ? kListExample : kDiagramExample);
    }
    return {};
}

QString ImportDialog::text() const
{
    return edit_->toPlainText();
}

void ImportDialog::setText(const QString& text)
{
    edit_->setPlainText(text);
}

imports::TextType ImportDialog::textType() const
{
    return type_ != nullptr ? static_cast<imports::TextType>(type_->currentData().toInt()) : imports::TextType::List;
}

imports::TextLayout ImportDialog::textLayout() const
{
    return layout_ != nullptr ? static_cast<imports::TextLayout>(layout_->currentData().toInt()) : imports::TextLayout::VerticalTree;
}

void ImportDialog::setTextLayout(imports::TextLayout layout)
{
    if (type_ == nullptr)
        return;
    type_->setCurrentIndex(type_->findData(static_cast<int>(imports::textTypeOf(layout))));
    layout_->setCurrentIndex(layout_->findData(static_cast<int>(layout)));
}

// The arrangements of the chosen type.
void ImportDialog::fillLayouts()
{
    if (layout_ == nullptr)
        return;
    layout_->clear();
    if (textType() == imports::TextType::List) {
        layout_->addItem(Text::tr("Vertical Tree"), static_cast<int>(imports::TextLayout::VerticalTree));
        layout_->addItem(Text::tr("Horizontal Tree"), static_cast<int>(imports::TextLayout::HorizontalTree));
        layout_->addItem(Text::tr("Radial Tree"), static_cast<int>(imports::TextLayout::RadialTree));
    } else {
        layout_->addItem(Text::tr("Vertical Flow"), static_cast<int>(imports::TextLayout::VerticalFlow));
        layout_->addItem(Text::tr("Horizontal Flow"), static_cast<int>(imports::TextLayout::HorizontalFlow));
    }
}

void ImportDialog::tryInsert()
{
    const std::string text = edit_->toPlainText().toStdString();
    imports::Options options;
    options.measure = measure_;
    imports::Result result;
    switch (kind_) {
    case Kind::FromText: result = imports::importText(text, textType(), textLayout(), options); break;
    case Kind::Csv: result = imports::importCsv(text, options); break;
    case Kind::Mermaid: result = imports::importMermaid(text, options); break;
    }
    if (!result.ok()) {
        if (result.errors.empty())
            result.errors.push_back({0, 0, Text::tr("There is nothing to insert.").toStdString()});
        showErrors(result.errors);
        return;
    }
    result_ = std::move(result);
    accept();
}

void ImportDialog::showErrors(const std::vector<imports::Diagnostic>& errors)
{
    errors_->blockSignals(true);
    errors_->clear();
    for (const imports::Diagnostic& error : errors) {
        auto* item = new QListWidgetItem(utf8(imports::describe(error)), errors_);
        item->setData(Qt::UserRole, error.line);
        item->setData(Qt::UserRole + 1, error.column);
    }
    errors_->blockSignals(false);
    errors_->show();
    errors_->setCurrentRow(0); // goes to the line of the first error, keeps the text as it is
}

void ImportDialog::goToLine(int line, int column)
{
    if (line <= 0)
        return;
    const QTextBlock block = edit_->document()->findBlockByNumber(line - 1);
    if (!block.isValid())
        return;
    QTextCursor cursor(block);
    cursor.movePosition(QTextCursor::Right, QTextCursor::MoveAnchor, std::max(0, std::min(column - 1, block.length() - 1)));
    edit_->setTextCursor(cursor);
    edit_->centerCursor();
    edit_->setFocus();
}

} // namespace qraw::ui
