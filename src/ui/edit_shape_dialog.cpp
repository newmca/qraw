#include "qraw/ui/edit_shape_dialog.h"

#include "palette_thumbnail.h"
#include "qraw/core/style.h"
#include "qraw/format/compression.h"
#include "qraw/format/xml_io.h"
#include "qraw/render/stencil.h"

#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFontDatabase>
#include <QHBoxLayout>
#include <QLabel>
#include <QPixmap>
#include <QPlainTextEdit>
#include <QPushButton>
#include <QTimer>
#include <QVBoxLayout>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::EditShapeDialog)
};

constexpr int kPreviewSize = 160;
constexpr int kPreviewDelayMs = 250;
constexpr std::string_view kInlinePrefix = "stencil(";

// The <shape> element of a stencil document: the root itself, or the first
// <shape> of a <shapes> set.
const core::XmlNode* shapeOf(const core::XmlNode& root)
{
    if (root.isElement("shape"))
        return &root;
    if (root.isElement("shapes"))
        return root.firstChild("shape");
    return nullptr;
}

} // namespace

std::optional<std::string> stencilXmlOf(const core::Cell& cell)
{
    if (!cell.isVertex())
        return std::nullopt;
    const std::string shape = cell.style().value("shape", "");
    if (shape.empty())
        return std::nullopt;
    render::StencilRegistry& registry = render::StencilRegistry::builtin();
    const render::Stencil* stencil = shape.starts_with(kInlinePrefix) ? registry.findInline(shape) : registry.find(shape);
    if (stencil == nullptr)
        return std::nullopt;
    return format::serializeElement(stencil->shapeElement());
}

std::optional<std::string> stencilShapeValue(const QString& xml, QString* error)
{
    const auto fail = [&](const QString& message) -> std::optional<std::string> {
        if (error != nullptr)
            *error = message;
        return std::nullopt;
    };
    const QByteArray bytes = xml.toUtf8();
    const format::XmlParseResult parsed = format::parseXml(bytes);
    if (!parsed.document) {
        return fail(Text::tr("Line %1, column %2: %3")
                        .arg(parsed.error.line)
                        .arg(parsed.error.column)
                        .arg(QString::fromStdString(parsed.error.message)));
    }
    const core::XmlNode* shape = shapeOf(parsed.document->root);
    if (shape == nullptr)
        return fail(Text::tr("The root element must be <shape>."));
    std::vector<core::Diagnostic> diagnostics;
    if (!render::Stencil::fromXml(*shape, &diagnostics))
        return fail(Text::tr("The XML is not a valid stencil."));
    // A <shapes> set stores only its first shape; a <shape> is kept as typed.
    const std::string text = shape == &parsed.document->root ? std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()))
                                                              : format::serializeElement(*shape);
    return "stencil(" + format::compressDiagram(text) + ")";
}

EditShapeDialog::EditShapeDialog(const core::Cell& cell, QWidget* parent)
    : QDialog(parent), style_(cell.styleText()), width_(cell.geometry() != nullptr ? cell.geometry()->width() : 0),
      height_(cell.geometry() != nullptr ? cell.geometry()->height() : 0)
{
    setObjectName(QStringLiteral("EditShapeDialog"));
    setWindowTitle(tr("Edit Shape"));

    editor_ = new QPlainTextEdit(this);
    editor_->setObjectName(QStringLiteral("stencilXml"));
    editor_->setLineWrapMode(QPlainTextEdit::NoWrap);
    editor_->setFont(QFontDatabase::systemFont(QFontDatabase::FixedFont));
    editor_->setPlainText(QString::fromStdString(stencilXmlOf(cell).value_or(std::string())));
    editor_->setMinimumSize(420, 280);

    previewLabel_ = new QLabel(this);
    previewLabel_->setObjectName(QStringLiteral("stencilPreview"));
    previewLabel_->setFixedSize(kPreviewSize, kPreviewSize);
    previewLabel_->setFrameShape(QFrame::StyledPanel);
    previewLabel_->setAlignment(Qt::AlignCenter);

    errorLabel_ = new QLabel(this);
    errorLabel_->setObjectName(QStringLiteral("stencilError"));
    errorLabel_->setWordWrap(true);
    errorLabel_->setStyleSheet(QStringLiteral("color: #c00;"));

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    QPushButton* previewButton = buttons->addButton(tr("Preview"), QDialogButtonBox::ActionRole);
    connect(buttons, &QDialogButtonBox::accepted, this, &EditShapeDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &EditShapeDialog::reject);
    connect(previewButton, &QPushButton::clicked, this, &EditShapeDialog::updatePreview);

    auto* top = new QHBoxLayout;
    top->addWidget(editor_, 1);
    top->addWidget(previewLabel_, 0, Qt::AlignTop);
    auto* layout = new QVBoxLayout(this);
    layout->addLayout(top, 1);
    layout->addWidget(errorLabel_);
    layout->addWidget(buttons);

    timer_ = new QTimer(this);
    timer_->setSingleShot(true);
    timer_->setInterval(kPreviewDelayMs);
    connect(timer_, &QTimer::timeout, this, &EditShapeDialog::updatePreview);
    connect(editor_, &QPlainTextEdit::textChanged, timer_, qOverload<>(&QTimer::start));
    updatePreview();
}

std::optional<std::string> EditShapeDialog::shapeValue() const
{
    return stencilShapeValue(editor_->toPlainText());
}

QString EditShapeDialog::errorText() const
{
    return errorLabel_->text();
}

void EditShapeDialog::updatePreview()
{
    timer_->stop();
    QString error;
    const std::optional<std::string> value = stencilShapeValue(editor_->toPlainText(), &error);
    errorLabel_->setText(value ? QString() : error);
    if (!value) {
        preview_ = QImage();
        previewLabel_->clear();
        return;
    }
    core::Style style(style_);
    style.set("shape", *value);
    PaletteEntry entry;
    entry.kind = PaletteEntry::Kind::Vertex;
    entry.style = QString::fromStdString(style.toString());
    entry.width = width_ > 0 ? width_ : 100;
    entry.height = height_ > 0 ? height_ : 100;
    entry.showLabel = false;
    const qreal dpr = devicePixelRatioF();
    preview_ = renderPaletteThumbnail(entry, QSize(kPreviewSize, kPreviewSize), dpr, 8, Qt::white);
    previewLabel_->setPixmap(QPixmap::fromImage(preview_));
}

void EditShapeDialog::accept()
{
    updatePreview();
    if (!errorLabel_->text().isEmpty())
        return;
    QDialog::accept();
}

} // namespace qraw::ui
