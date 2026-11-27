#include "qraw/ui/edit_link_dialog.h"

#include <QCheckBox>
#include <QComboBox>
#include <QCoreApplication>
#include <QDialogButtonBox>
#include <QGridLayout>
#include <QLineEdit>
#include <QRadioButton>
#include <QVBoxLayout>

namespace qraw::ui {

namespace {

struct Text {
    Q_DECLARE_TR_FUNCTIONS(qraw::ui::EditLinkDialog)
};

} // namespace

EditLinkDialog::EditLinkDialog(const actions::LinkSheet& sheet, std::vector<PageChoice> pages, QWidget* parent)
    : QDialog(parent), sheet_(sheet), pages_(std::move(pages))
{
    setObjectName(QStringLiteral("EditLinkDialog"));
    setWindowTitle(Text::tr("Edit Link"));

    urlChoice_ = new QRadioButton(Text::tr("Web Address"), this);
    urlChoice_->setObjectName(QStringLiteral("link.urlChoice"));
    pageChoice_ = new QRadioButton(Text::tr("Page in This Document"), this);
    pageChoice_->setObjectName(QStringLiteral("link.pageChoice"));

    url_ = new QLineEdit(this);
    url_->setObjectName(QStringLiteral("link.url"));
    url_->setClearButtonEnabled(true);
    url_->setAccessibleName(Text::tr("Link Address"));
    url_->setPlaceholderText(sheet.linkMixed ? Text::tr("Multiple Values") : Text::tr("https://www.example.com/"));

    page_ = new QComboBox(this);
    page_->setObjectName(QStringLiteral("link.page"));
    page_->setAccessibleName(Text::tr("Linked Page"));
    for (const PageChoice& choice : pages_)
        page_->addItem(choice.name, QString::fromStdString(choice.id));

    newWindow_ = new QCheckBox(Text::tr("Open in New Window"), this);
    newWindow_->setObjectName(QStringLiteral("link.newWindow"));
    newWindow_->setToolTip(Text::tr("Writes linkTarget=_blank, which draw.io reads as: open the link in a new window."));
    if (sheet.newWindowMixed) {
        newWindow_->setTristate(true);
        newWindow_->setCheckState(Qt::PartiallyChecked);
    } else {
        newWindow_->setChecked(sheet.newWindow);
    }

    // The current link: a link to one of this document's pages picks the page.
    bool pageSelected = false;
    if (const std::optional<std::string> pageId = actions::pageIdOfLink(sheet.link); pageId && !sheet.linkMixed) {
        const int index = page_->findData(QString::fromStdString(*pageId));
        if (index >= 0) {
            page_->setCurrentIndex(index);
            pageSelected = true;
        }
    }
    if (!pageSelected)
        url_->setText(QString::fromStdString(sheet.link));
    (pageSelected ? pageChoice_ : urlChoice_)->setChecked(true);
    pageChoice_->setEnabled(page_->count() > 0);

    auto* layout = new QVBoxLayout(this);
    auto* grid = new QGridLayout;
    grid->addWidget(urlChoice_, 0, 0);
    grid->addWidget(url_, 0, 1);
    grid->addWidget(pageChoice_, 1, 0);
    grid->addWidget(page_, 1, 1);
    grid->setColumnStretch(1, 1);
    layout->addLayout(grid);
    layout->addWidget(newWindow_);

    auto* buttons = new QDialogButtonBox(QDialogButtonBox::Ok | QDialogButtonBox::Cancel, this);
    connect(buttons, &QDialogButtonBox::accepted, this, &QDialog::accept);
    connect(buttons, &QDialogButtonBox::rejected, this, &QDialog::reject);
    layout->addWidget(buttons);

    connect(url_, &QLineEdit::textEdited, this, [this] { linkTouched_ = true; });
    connect(url_, &QLineEdit::textChanged, this, [this] {
        // The clear button changes the text without editing it.
        if (url_->text().isEmpty())
            linkTouched_ = true;
    });
    connect(page_, &QComboBox::activated, this, [this] { linkTouched_ = true; });
    connect(urlChoice_, &QRadioButton::toggled, this, [this](bool) {
        linkTouched_ = true;
        updateEnabled();
    });
    connect(newWindow_, &QCheckBox::clicked, this, [this] {
        newWindowTouched_ = true;
        newWindow_->setTristate(false);
    });
    updateEnabled();
    resize(460, sizeHint().height());
    (pageSelected ? static_cast<QWidget*>(page_) : static_cast<QWidget*>(url_))->setFocus();
}

void EditLinkDialog::updateEnabled()
{
    const bool pageLink = pageChoice_->isChecked();
    url_->setEnabled(!pageLink);
    page_->setEnabled(pageLink);
    // A link to a page opens in this window; the option is for web addresses.
    newWindow_->setEnabled(!pageLink);
}

actions::LinkEdit EditLinkDialog::edit() const
{
    actions::LinkEdit edit;
    if (pageChoice_->isChecked()) {
        edit.setLink = true;
        edit.link = actions::pageLink(page_->currentData().toString().toStdString());
    } else {
        const std::string link = url_->text().trimmed().toStdString();
        // A mixed link that was neither typed over nor cleared keeps each cell's own.
        edit.setLink = !sheet_.linkMixed || linkTouched_ || !link.empty();
        edit.link = link;
    }
    edit.setNewWindow = newWindowTouched_;
    edit.newWindow = newWindow_->checkState() == Qt::Checked;
    return edit;
}

} // namespace qraw::ui
