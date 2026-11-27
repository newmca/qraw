#pragma once
// Edit > Edit Image... (draw.io 'editImage'; Actions.js 'image', ImageDialog):
// the image of the selected image shapes and label shapes: a URL or file path
// that stays a reference, a file that is embedded in the diagram
// (`data:image/png,<base64>`, image_embed.h), the clipboard's image, or none (the
// image is removed). "Reset to Original Size" also sets the shapes to the size
// of the image. Nothing is fetched from the network, so the size of an image at
// an http address is not known and that option is off for it.
//
// The same dialog inserts an image (Insert > Image..., draw.io shows ImageDialog
// for both): there is no current image, no Remove Image and no size option, OK
// waits for an image, and insertion() gives the image and the size of its cell.

#include "qraw/ui/data_actions.h"
#include "qraw/ui/image_embed.h"

#include <QDialog>
#include <QSizeF>
#include <QString>

#include <optional>

class QCheckBox;
class QDialogButtonBox;
class QLabel;
class QPushButton;
class QLineEdit;

namespace qraw::ui {

class EditImageDialog : public QDialog {
    Q_OBJECT

public:
    // `baseDirectory`: where a relative file path is looked up (the document's folder).
    EditImageDialog(const actions::ImageSheet& sheet, const QString& baseDirectory, QWidget* parent = nullptr);

    // The dialog of Insert > Image...
    struct Insertion {
        std::string image; // an `image` style value
        QSizeF size;       // the cell's size: the image's, within actions::kMaxInsertedImageSize
    };
    explicit EditImageDialog(const QString& baseDirectory, QWidget* parent = nullptr);

    // What the user chose: nullopt image when nothing was changed.
    [[nodiscard]] actions::ImageEdit edit() const;
    // Insert mode: what to insert; nullopt while there is no image.
    [[nodiscard]] std::optional<Insertion> insertion() const;

    // The steps of the Choose File and Paste buttons without a file dialog: false, with
    // the reason in errorText(), when the image is refused.
    bool useFile(const QString& path);
    bool useClipboardImage();
    void removeImage();
    [[nodiscard]] QString errorText() const;

private:
    enum class Source { Keep, Typed, Embedded, Removed };
    void setEmbedded(const EmbeddedImage& image);
    void updateSizeOption();
    [[nodiscard]] std::string currentValue() const;

    actions::ImageSheet sheet_;
    QString baseDirectory_;
    Source source_ = Source::Keep;
    std::optional<EmbeddedImage> embedded_;
    bool keepIsEmbedded_ = false; // the current image is embedded data, too long to show
    qint64 keepBytes_ = 0;
    QLineEdit* url_ = nullptr;
    QLabel* error_ = nullptr;
    QCheckBox* resetSize_ = nullptr;
    QPushButton* remove_ = nullptr;
    QDialogButtonBox* buttons_ = nullptr;
    bool insertMode_ = false;
};

} // namespace qraw::ui
