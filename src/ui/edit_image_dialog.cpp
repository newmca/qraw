#include "qraw/ui/action_registry.h"
#include "qraw/ui/edit_image_dialog.h"

#include "qraw/ui/insert_actions.h"

#include <QApplication>
#include <QClipboard>
#include <QCheckBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QFileDialog>
#include <QHBoxLayout>
#include <QLabel>
#include <QLineEdit>
#include <QLocale>
#include <QPushButton>
#include <QVBoxLayout>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::EditImageDialog)
};

// A value longer than this is shown as a summary, not in the field.
constexpr std::size_t kMaxShownValue = 2048;

} // namespace

EditImageDialog::EditImageDialog(const actions::ImageSheet& sheet, const QString& baseDirectory, QWidget* parent)
    : QDialog(parent), sheet_(sheet), baseDirectory_(baseDirectory)
{
    setObjectName(QStringLiteral("EditImageDialog"));
    setWindowTitle(Text::tr("Edit Image"));

    auto* label = new QLabel(Text::tr("Image URL or File:"), this);
    url_ = new QLineEdit(this);
    url_->setObjectName(QStringLiteral("image.url"));
    url_->setClearButtonEnabled(true);
    url_->setAccessibleName(Text::tr("Image URL or File"));
    label->setBuddy(url_);
    if (sheet.mixed) {
        url_->setPlaceholderText(Text::tr("Multiple Values"));
    } else if (sheet.image.size() > kMaxShownValue) {
        keepIsEmbedded_ = true;
        keepBytes_ = static_cast<qint64>(sheet.image.size());
        url_->setPlaceholderText(Text::tr("Embedded image (%1)").arg(QLocale().formattedDataSize(keepBytes_)));
    } else {
        url_->setText(QString::fromStdString(sheet.image));
        url_->setPlaceholderText(Text::tr("No image"));
    }

    auto* chooseFile = new QPushButton(withEllipsis(Text::tr("Choose File")), this);
    chooseFile->setObjectName(QStringLiteral("image.chooseFile"));
    chooseFile->setAutoDefault(false);
    auto* paste = new QPushButton(Text::tr("Paste Image from Clipboard"), this);
    paste->setObjectName(QStringLiteral("image.paste"));
    paste->setAutoDefault(false);
    auto* remove = remove_ = new QPushButton(Text::tr("Remove Image"), this);
    remove->setObjectName(QStringLiteral("image.remove"));
    remove->setAutoDefault(false);

    resetSize_ = new QCheckBox(Text::tr("Reset to Original Size"), this);
    resetSize_->setObjectName(QStringLiteral("image.resetSize"));
    resetSize_->setToolTip(Text::tr("Sets the shapes to the size of the image. Not available for an image at a web address, "
                                    "which is never downloaded."));

    error_ = new QLabel(this);
    error_->setObjectName(QStringLiteral("image.error"));
    error_->setWordWrap(true);
    error_->setAccessibleName(Text::tr("Error"));

    auto* buttons = buttons_ = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);

    auto* row = new QHBoxLayout;
    row->addWidget(chooseFile);
    row->addWidget(paste);
    row->addWidget(remove);
    row->addStretch(1);
    auto* layout = new QVBoxLayout(this);
    layout->addWidget(label);
    layout->addWidget(url_);
    layout->addLayout(row);
    layout->addWidget(resetSize_);
    layout->addWidget(error_);
    layout->addWidget(buttons);

    connect(chooseFile, &QPushButton::clicked, this, [this] {
        const QString path = QFileDialog::getOpenFileName(
            this, Text::tr("Choose Image"), baseDirectory_,
            Text::tr("Images (*.png *.jpg *.jpeg *.gif *.bmp *.webp *.svg);;All Files (*)"));
        if (!path.isEmpty())
            useFile(path);
    });
    connect(paste, &QPushButton::clicked, this, [this] { useClipboardImage(); });
    connect(remove, &QPushButton::clicked, this, [this] { removeImage(); });
    connect(url_, &QLineEdit::textEdited, this, [this] {
        source_ = Source::Typed;
        embedded_.reset();
        error_->clear();
        updateSizeOption();
    });
    // The clear button empties the field without an edit: that removes the image.
    connect(url_, &QLineEdit::textChanged, this, [this](const QString& text) {
        if (text.isEmpty() && source_ == Source::Typed)
            updateSizeOption();
    });
    updateSizeOption();
    resize(480, sizeHint().height());
    url_->setFocus();
}

EditImageDialog::EditImageDialog(const QString& baseDirectory, QWidget* parent)
    : EditImageDialog(actions::ImageSheet{}, baseDirectory, parent)
{
    insertMode_ = true;
    setWindowTitle(Text::tr("Insert Image"));
    remove_->hide();
    resetSize_->hide();
    updateSizeOption(); // OK waits for an image
}

void EditImageDialog::setEmbedded(const EmbeddedImage& image)
{
    embedded_ = image;
    source_ = Source::Embedded;
    keepIsEmbedded_ = false;
    error_->clear();
    url_->clear(); // a long data URI is not shown
    url_->setPlaceholderText(Text::tr("Embedded image (%1)").arg(QLocale().formattedDataSize(image.bytes)));
    updateSizeOption();
}

bool EditImageDialog::useFile(const QString& path)
{
    QString message;
    const std::optional<EmbeddedImage> image = embedImageFile(path, &message);
    if (!image) {
        error_->setText(message);
        return false;
    }
    setEmbedded(*image);
    return true;
}

bool EditImageDialog::useClipboardImage()
{
    QString message;
    const std::optional<EmbeddedImage> image = embedQImage(QApplication::clipboard()->image(), &message);
    if (!image) {
        error_->setText(message);
        return false;
    }
    setEmbedded(*image);
    return true;
}

void EditImageDialog::removeImage()
{
    source_ = Source::Removed;
    embedded_.reset();
    keepIsEmbedded_ = false;
    error_->clear();
    url_->clear();
    url_->setPlaceholderText(Text::tr("No image"));
    updateSizeOption();
}

QString EditImageDialog::errorText() const
{
    return error_->text();
}

std::string EditImageDialog::currentValue() const
{
    switch (source_) {
    case Source::Embedded: return embedded_ ? embedded_->value : std::string();
    case Source::Typed: return url_->text().trimmed().toStdString();
    case Source::Removed: return {};
    case Source::Keep: break;
    }
    return sheet_.image;
}

// The size of the new image is known for embedded data and for local files and data URIs that were typed.
void EditImageDialog::updateSizeOption()
{
    QSize size;
    if (source_ == Source::Embedded && embedded_)
        size = embedded_->size;
    else if (source_ == Source::Typed || source_ == Source::Keep)
        size = (source_ == Source::Keep && keepIsEmbedded_) ? QSize() : naturalImageSize(currentValue(), baseDirectory_);
    resetSize_->setEnabled(size.isValid());
    if (!size.isValid())
        resetSize_->setChecked(false);
    if (insertMode_)
        buttons_->button(QDialogButtonBox::Ok)->setEnabled(!currentValue().empty());
}

std::optional<EditImageDialog::Insertion> EditImageDialog::insertion() const
{
    const std::string image = currentValue();
    if (!insertMode_ || image.empty())
        return std::nullopt;
    const QSize natural = source_ == Source::Embedded && embedded_ ? embedded_->size
                                                                    : naturalImageSize(image, baseDirectory_);
    return Insertion{image, actions::insertedImageSize(natural)};
}

actions::ImageEdit EditImageDialog::edit() const
{
    actions::ImageEdit edit;
    if (source_ != Source::Keep)
        edit.image = currentValue();
    if (resetSize_->isChecked() && resetSize_->isEnabled()) {
        const QSize size = source_ == Source::Embedded && embedded_ ? embedded_->size
                                                                    : naturalImageSize(currentValue(), baseDirectory_);
        if (size.isValid())
            edit.size = std::make_pair(static_cast<double>(size.width()), static_cast<double>(size.height()));
    }
    return edit;
}

} // namespace qraw::ui
