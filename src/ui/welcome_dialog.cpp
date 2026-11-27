#include "qraw/ui/action_registry.h"
#include "qraw/ui/welcome_dialog.h"

#include "qraw/format/codec.h"
#include "qraw/render/export.h"
#include "qraw/render/page_view.h"
#include "qraw/ui/app_settings.h"
#include "qraw/ui/template_catalog.h"

#include <QCheckBox>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QHash>
#include <QIcon>
#include <QImage>
#include <QLabel>
#include <QListWidget>
#include <QPixmap>
#include <QPushButton>
#include <QVBoxLayout>

#include <algorithm>

namespace qraw::ui {
namespace {

constexpr int kThumbnailWidth = 160;
constexpr int kThumbnailHeight = 120;
constexpr int kThumbnailBorder = 4;
constexpr int kPathRole = Qt::UserRole;

// Scale that fits the first page's content into the thumbnail box.
double thumbnailScale(const core::Document& document)
{
    const render::PageView view = render::PageView::build(*document.page(0), render::RenderContext::defaults());
    const auto bounds = view.bounds();
    const double usableWidth = kThumbnailWidth - 2 * kThumbnailBorder;
    const double usableHeight = kThumbnailHeight - 2 * kThumbnailBorder;
    return std::min(usableWidth / std::max(bounds.width, 1.0), usableHeight / std::max(bounds.height, 1.0));
}

// Renders the first page of a template as a PNG thumbnail of kThumbnailWidth x kThumbnailHeight
// logical pixels, with `devicePixelRatio` pixels to each so that it is sharp on a scaled screen;
// a null icon when the template does not load.
QIcon renderThumbnail(const QString& templatePath, qreal devicePixelRatio)
{
    const format::ReadResult read = format::readFile(templatePath);
    if (!read.ok() || read.document->pageCount() == 0)
        return {};
    const core::Document& document = *read.document;

    render::ExportOptions options;
    options.format = render::ExportFormat::Png;
    options.scale = thumbnailScale(document) * devicePixelRatio;
    options.border = kThumbnailBorder * devicePixelRatio;
    const render::ExportResult png = render::exportPage(document, 0, options);
    if (!png.ok())
        return {};
    QPixmap pixmap = QPixmap::fromImage(QImage::fromData(png.data, "PNG"));
    pixmap.setDevicePixelRatio(devicePixelRatio);
    return QIcon(pixmap);
}

// Recent files in the left column; a missing file is listed but cannot be
// picked.
void fillRecent(QListWidget& list, const AppSettings& settings)
{
    for (const QString& file : settings.recentFiles()) {
        auto* item = new QListWidgetItem(QFileInfo(file).fileName(), &list);
        item->setData(kPathRole, file);
        item->setToolTip(file);
        if (!QFileInfo::exists(file))
            item->setFlags(item->flags() & ~Qt::ItemIsEnabled);
    }
}

bool isPickable(const QListWidgetItem* item)
{
    return item != nullptr && item->flags().testFlag(Qt::ItemIsEnabled);
}

} // namespace

// The dialog's behaviour. Widgets are children of the dialog, which outlives
// this struct's use.
struct WelcomeDialog::Impl {
    Impl(WelcomeDialog& owner, AppSettings& appSettings) : dialog(owner), settings(appSettings) {}

    WelcomeDialog& dialog;
    AppSettings& settings;
    Choice choice = Choice::None;
    QString path;
    std::vector<TemplateInfo> templates;
    QHash<QString, QIcon> thumbnails; // by template path, rendered on first display

    QListWidget* recent = nullptr;
    QListWidget* categories = nullptr;
    QListWidget* templateGrid = nullptr;
    QPushButton* create = nullptr;

    void build()
    {
        dialog.setWindowTitle(dialog.tr("Welcome to Qraw"));
        dialog.resize(900, 600);
        auto* root = new QVBoxLayout(&dialog);
        auto* columns = new QHBoxLayout;
        root->addLayout(columns, 1);
        columns->addLayout(buildLeftColumn(), 1);
        columns->addLayout(buildRightColumn(), 3);
        root->addLayout(buildBottomRow());
        connectSignals();

        if (categories->count() > 0)
            categories->setCurrentRow(0);
        updateCreateButton();
    }

    QVBoxLayout* buildLeftColumn()
    {
        auto* left = new QVBoxLayout;
        auto* blank = new QPushButton(dialog.tr("Blank Diagram"), &dialog);
        blank->setObjectName(QStringLiteral("welcome.blank"));
        auto* open = new QPushButton(withEllipsis(dialog.tr("Open Existing Diagram")), &dialog);
        open->setObjectName(QStringLiteral("welcome.open"));
        left->addWidget(blank);
        left->addWidget(open);
        left->addWidget(new QLabel(dialog.tr("Recent"), &dialog));
        recent = new QListWidget(&dialog);
        recent->setObjectName(QStringLiteral("welcome.recent"));
        recent->setAccessibleName(dialog.tr("Recent Diagrams"));
        fillRecent(*recent, settings);
        left->addWidget(recent, 1);
        QObject::connect(blank, &QPushButton::clicked, &dialog, [this] { pick(Choice::Blank, {}); });
        QObject::connect(open, &QPushButton::clicked, &dialog, [this] { pick(Choice::OpenDialog, {}); });
        return left;
    }

    QHBoxLayout* buildRightColumn()
    {
        templates = TemplateCatalog::scan();
        auto* right = new QHBoxLayout;
        categories = new QListWidget(&dialog);
        categories->setObjectName(QStringLiteral("welcome.categories"));
        categories->setAccessibleName(dialog.tr("Template Categories"));
        for (const QString& category : TemplateCatalog::categories(templates))
            categories->addItem(category);
        right->addWidget(categories, 1);

        templateGrid = new QListWidget(&dialog);
        templateGrid->setObjectName(QStringLiteral("welcome.templates"));
        templateGrid->setAccessibleName(dialog.tr("Templates"));
        templateGrid->setViewMode(QListView::IconMode);
        templateGrid->setIconSize(QSize(kThumbnailWidth, kThumbnailHeight));
        templateGrid->setGridSize(QSize(kThumbnailWidth + 20, kThumbnailHeight + 40));
        templateGrid->setResizeMode(QListView::Adjust);
        templateGrid->setMovement(QListView::Static);
        right->addWidget(templateGrid, 3);
        return right;
    }

    QHBoxLayout* buildBottomRow()
    {
        auto* bottom = new QHBoxLayout;
        auto* showAtStartup = new QCheckBox(dialog.tr("Show at startup"), &dialog);
        showAtStartup->setObjectName(QStringLiteral("welcome.showAtStartup"));
        showAtStartup->setChecked(settings.showWelcomeOnStart());
        QObject::connect(showAtStartup, &QCheckBox::toggled, &dialog,
                         [this](bool on) { settings.setShowWelcomeOnStart(on); });
        bottom->addWidget(showAtStartup);
        bottom->addStretch(1);

        create = new QPushButton(dialog.tr("Create"), &dialog);
        create->setObjectName(QStringLiteral("welcome.create"));
        auto* cancel = new QPushButton(dialog.tr("Cancel"), &dialog);
        cancel->setObjectName(QStringLiteral("welcome.cancel"));
        QObject::connect(create, &QPushButton::clicked, &dialog, [this] { createSelected(); });
        QObject::connect(cancel, &QPushButton::clicked, &dialog, &QDialog::reject);
        bottom->addWidget(create);
        bottom->addWidget(cancel);
        return bottom;
    }

    void connectSignals()
    {
        QObject::connect(recent, &QListWidget::itemDoubleClicked, &dialog, [this](QListWidgetItem* item) {
            if (isPickable(item))
                pick(Choice::OpenRecent, item->data(kPathRole).toString());
        });
        QObject::connect(templateGrid, &QListWidget::itemDoubleClicked, &dialog,
                         [this](QListWidgetItem* item) { pick(Choice::Template, item->data(kPathRole).toString()); });
        QObject::connect(categories, &QListWidget::currentRowChanged, &dialog,
                         [this](int row) { showCategory(row); });
        // Only one of the two lists holds a selection at a time.
        QObject::connect(recent, &QListWidget::currentItemChanged, &dialog, [this](QListWidgetItem* item) {
            if (item != nullptr)
                templateGrid->clearSelection();
            updateCreateButton();
        });
        QObject::connect(templateGrid, &QListWidget::currentItemChanged, &dialog, [this](QListWidgetItem* item) {
            if (item != nullptr)
                recent->clearSelection();
            updateCreateButton();
        });
    }

    void pick(Choice picked, const QString& pickedPath)
    {
        choice = picked;
        path = pickedPath;
        dialog.accept();
    }

    void createSelected()
    {
        if (isPickable(templateGrid->currentItem()))
            pick(Choice::Template, templateGrid->currentItem()->data(kPathRole).toString());
        else if (isPickable(recent->currentItem()))
            pick(Choice::OpenRecent, recent->currentItem()->data(kPathRole).toString());
    }

    void updateCreateButton()
    {
        create->setEnabled(isPickable(templateGrid->currentItem()) || isPickable(recent->currentItem()));
    }

    // Fills the grid for the category in `row`; thumbnails are rendered on
    // first display and cached per template path.
    void showCategory(int row)
    {
        templateGrid->clear();
        if (row < 0 || row >= categories->count())
            return;
        const QString category = categories->item(row)->text();
        for (const TemplateInfo& info : templates) {
            if (info.category != category)
                continue;
            auto* item = new QListWidgetItem(info.title, templateGrid);
            item->setData(kPathRole, info.path);
            item->setToolTip(info.path);
            if (!thumbnails.contains(info.path))
                thumbnails.insert(info.path, renderThumbnail(info.path, dialog.devicePixelRatioF()));
            item->setIcon(thumbnails.value(info.path));
        }
        updateCreateButton();
    }
};

WelcomeDialog::WelcomeDialog(AppSettings& settings, QWidget* parent)
    : QDialog(parent), d(std::make_unique<Impl>(*this, settings))
{
    d->build();
}

WelcomeDialog::~WelcomeDialog() = default;

WelcomeDialog::Choice WelcomeDialog::choice() const
{
    return d->choice;
}

QString WelcomeDialog::path() const
{
    return d->path;
}

} // namespace qraw::ui
