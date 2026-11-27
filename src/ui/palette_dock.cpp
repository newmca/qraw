// The shape palette dock: search field, Favorites / Recently Used, one
// collapsible section per palette with a flow grid of thumbnails
// (docs/design/ui.md section 8; draw.io's Sidebar, js/grapheditor/Sidebar.js).
#include "qraw/ui/action_registry.h"
#include "qraw/ui/palette_dock.h"

#include "drop_target.h"

#include "palette_flow_layout.h"
#include "palette_thumbnail.h"
#include "qraw/ui/app_settings.h"
#include "qraw/ui/cell_transfer.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/edit_actions.h"
#include "qraw/ui/palette_library.h"
#include "user_library.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QHBoxLayout>
#include <QInputDialog>
#include <QApplication>
#include <QCoreApplication>
#include <QDrag>
#include <QHash>
#include <QIcon>
#include <QLabel>
#include <QLineEdit>
#include <QMenu>
#include <QMimeData>
#include <QMouseEvent>
#include <QPalette>
#include <QPixmap>
#include <QSaveFile>
#include <QPointer>
#include <QScrollArea>
#include <QTimer>
#include <QToolButton>
#include <QVBoxLayout>

#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace qraw::ui {

namespace {

constexpr int kThumbSize = 32;  // Sidebar.thumbWidth/thumbHeight (Sidebar.js:273)
constexpr int kButtonSize = 36; // thumbnail + padding 2 * 2
constexpr int kSearchDebounceMs = 150;
constexpr int kMaxSearchHits = 300;
constexpr int kThumbsPerTick = 8; // thumbnails rendered per event-loop pass, keeps the UI responsive

constexpr char kTranslationContext[] = "qraw::ui::PaletteDock";

QString translated(const QString& text)
{
    return QCoreApplication::translate(kTranslationContext, text.toUtf8().constData());
}

// One thumbnail: a palette entry with the key it is stored under.
struct Item {
    QString key;
    const PaletteEntry* entry = nullptr;
    std::shared_ptr<const Palette> keepAlive = nullptr; // set for user libraries: entry points into it
};

// A flat button that starts a drag after the press moved a few pixels.
class ItemButton : public QToolButton {
public:
    ItemButton(Item item, QWidget* parent) : QToolButton(parent), item_(std::move(item))
    {
        setFixedSize(kButtonSize, kButtonSize);
        setIconSize(QSize(kThumbSize, kThumbSize));
        setAutoRaise(true);
        setFocusPolicy(Qt::NoFocus);
        setContextMenuPolicy(Qt::CustomContextMenu);
    }

    [[nodiscard]] const QString& key() const { return item_.key; }
    [[nodiscard]] const Item& item() const { return item_; }
    std::function<void(ItemButton&)> onDragStart;

protected:
    void mousePressEvent(QMouseEvent* event) override
    {
        if (event->button() == Qt::LeftButton)
            pressPos_ = event->position().toPoint();
        QToolButton::mousePressEvent(event);
    }

    void mouseMoveEvent(QMouseEvent* event) override
    {
        if ((event->buttons() & Qt::LeftButton) != 0 && onDragStart
            && (event->position().toPoint() - pressPos_).manhattanLength() >= QApplication::startDragDistance()) {
            setDown(false);
            onDragStart(*this);
            return;
        }
        QToolButton::mouseMoveEvent(event);
    }

private:
    Item item_;
    QPoint pressPos_;
};

// A header button over a body holding a FlowLayout of item buttons. The
// buttons are created when the section is first shown expanded.
class PaletteSection : public QWidget {
public:
    using ButtonFactory = std::function<QWidget*(const Item&, QWidget* parent)>;

    PaletteSection(QString id, QString title, bool collapsible, bool expanded, ButtonFactory factory, QWidget* parent)
        : QWidget(parent), id_(std::move(id)), factory_(std::move(factory))
    {
        auto* layout = new QVBoxLayout(this);
        layout->setContentsMargins(0, 0, 0, 0);
        layout->setSpacing(0);

        header_ = new QToolButton(this);
        header_->setText(title);
        header_->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
        header_->setSizePolicy(QSizePolicy::Expanding, QSizePolicy::Fixed);
        header_->setAutoRaise(true);
        header_->setFocusPolicy(Qt::NoFocus);
        QFont font = header_->font();
        font.setBold(true);
        header_->setFont(font);
        headerRow_ = new QHBoxLayout;
        headerRow_->setContentsMargins(0, 0, 0, 0);
        headerRow_->setSpacing(0);
        headerRow_->addWidget(header_, 1);
        layout->addLayout(headerRow_);

        body_ = new QWidget(this);
        grid_ = new FlowLayout(body_, 4, 2);
        layout->addWidget(body_);

        if (collapsible) {
            header_->setCheckable(true);
            header_->setChecked(expanded);
            connect(header_, &QToolButton::toggled, this, [this](bool on) { setExpanded(on); });
        }
        applyExpanded(collapsible ? expanded : true);
        header_->setArrowType(collapsible ? (expanded ? Qt::DownArrow : Qt::RightArrow) : Qt::NoArrow);
    }

    [[nodiscard]] const QString& id() const { return id_; }
    [[nodiscard]] bool isExpanded() const { return body_->isVisibleTo(this); }
    [[nodiscard]] QToolButton* header() const { return header_; }
    [[nodiscard]] const std::vector<Item>& items() const { return items_; }
    [[nodiscard]] QWidget* body() const { return body_; }
    [[nodiscard]] int populatedCount() const { return populated_ ? grid_->count() : 0; }
    void setTitle(const QString& title) { header_->setText(title); }
    // A small command button right of the title (user library sections).
    QToolButton* addHeaderButton(const QString& text, const QString& toolTip)
    {
        auto* button = new QToolButton(this);
        button->setText(text);
        button->setToolTip(toolTip);
        button->setAccessibleName(toolTip);
        button->setAutoRaise(true);
        button->setFocusPolicy(Qt::NoFocus);
        headerRow_->addWidget(button);
        return button;
    }

    // Called after the expanded state changed by the user.
    std::function<void(PaletteSection&)> onExpandedChanged;

    void setItems(std::vector<Item> items)
    {
        items_ = std::move(items);
        populated_ = false;
        if (isExpanded())
            populate();
    }

    void setExpanded(bool on)
    {
        applyExpanded(on);
        header_->setArrowType(on ? Qt::DownArrow : Qt::RightArrow);
        if (header_->isChecked() != on)
            header_->setChecked(on);
        if (onExpandedChanged)
            onExpandedChanged(*this);
    }

private:
    void applyExpanded(bool on)
    {
        body_->setVisible(on);
        if (on && !populated_)
            populate();
    }

    void populate()
    {
        while (QLayoutItem* item = grid_->takeAt(0)) {
            // A button can be replaced from inside its own clicked() handler.
            if (QWidget* widget = item->widget()) {
                widget->hide();
                widget->deleteLater();
            }
            delete item;
        }
        for (const Item& item : items_) {
            if (QWidget* button = factory_(item, body_))
                grid_->addWidget(button);
        }
        populated_ = true;
        body_->updateGeometry();
    }

    QString id_;
    ButtonFactory factory_;
    QToolButton* header_ = nullptr;
    QHBoxLayout* headerRow_ = nullptr;
    QWidget* body_ = nullptr;
    FlowLayout* grid_ = nullptr;
    std::vector<Item> items_;
    bool populated_ = false;
};

} // namespace

struct PaletteDock::Impl {
    Impl(PaletteDock& owner, const PaletteLibrary& lib, DocumentSession& sess, CanvasView& view, AppSettings& prefs)
        : dock(owner), library(lib), session(sess), canvas(&view), settings(prefs)
    {
    }

    PaletteDock& dock;
    const PaletteLibrary& library;
    DocumentSession& session;
    QPointer<CanvasView> canvas;
    AppSettings& settings;

    QLineEdit* search = nullptr;
    QTimer* searchTimer = nullptr;
    QTimer* thumbTimer = nullptr;
    QVBoxLayout* sectionLayout = nullptr;
    PaletteSection* favorites = nullptr;
    PaletteSection* recent = nullptr;
    PaletteSection* results = nullptr;
    std::vector<PaletteSection*> paletteSections;

    // User libraries (.xml mxlibrary files), shown between "Recently Used"
    // and the built-in palettes. Changes are saved to the file at once.
    struct UserSection {
        std::unique_ptr<UserLibrary> library;
        PaletteSection* section = nullptr;
    };
    std::vector<UserSection> userSections;

    QHash<QString, QPixmap> thumbnails;
    struct PendingThumb {
        QPointer<QToolButton> button;
        Item item;
    };
    std::deque<PendingThumb> pending;

    // --- construction ---------------------------------------------------------
    void build()
    {
        auto* root = new QVBoxLayout(&dock);
        root->setContentsMargins(0, 0, 0, 0);
        root->setSpacing(0);

        search = new QLineEdit(&dock);
        search->setPlaceholderText(PaletteDock::tr("Search Shapes"));
        search->setAccessibleName(PaletteDock::tr("Search Shapes"));
        search->setClearButtonEnabled(true);
        root->addWidget(search);

        auto* scroll = new QScrollArea(&dock);
        scroll->setWidgetResizable(true);
        scroll->setFrameShape(QFrame::NoFrame);
        scroll->setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
        auto* content = new QWidget(scroll);
        sectionLayout = new QVBoxLayout(content);
        sectionLayout->setContentsMargins(0, 0, 0, 0);
        sectionLayout->setSpacing(0);
        scroll->setWidget(content);
        root->addWidget(scroll, 1);

        searchTimer = new QTimer(&dock);
        searchTimer->setSingleShot(true);
        searchTimer->setInterval(kSearchDebounceMs);
        QObject::connect(searchTimer, &QTimer::timeout, &dock, [this] { applySearch(); });
        QObject::connect(search, &QLineEdit::textChanged, &dock, [this] { searchTimer->start(); });

        thumbTimer = new QTimer(&dock);
        thumbTimer->setSingleShot(true);
        thumbTimer->setInterval(0);
        QObject::connect(thumbTimer, &QTimer::timeout, &dock, [this] { renderPendingThumbnails(); });

        favorites = addSection(QStringLiteral("favorites"), PaletteDock::tr("Favorites"), false, true, content);
        recent = addSection(QStringLiteral("recent"), PaletteDock::tr("Recently Used"), false, true, content);
        const QStringList expanded = settings.expandedPalettes();
        for (const Palette& palette : library.palettes()) {
            // Azure entries are image shapes whose img/lib icons are not shipped: label the section
            // instead of letting its shapes insert as empty frames without notice.
            QString title = translated(palette.title);
            if (palette.library == QLatin1String("azure2"))
                title += PaletteDock::tr(" (icons not included)");
            PaletteSection* section = addSection(palette.id, title, true, expanded.contains(palette.id), content);
            std::vector<Item> items;
            items.reserve(palette.entries.size());
            for (const PaletteEntry& entry : palette.entries)
                items.push_back({entry.key(palette.id), &entry});
            section->setItems(std::move(items));
            section->onExpandedChanged = [this](PaletteSection&) { saveExpandedPalettes(); };
            paletteSections.push_back(section);
        }
        results = addSection(QStringLiteral("results"), PaletteDock::tr("Search Results"), false, true, content);
        results->hide();
        sectionLayout->addStretch(1);
        refreshUserSections();
        restoreLibraries();
    }

    PaletteSection* addSection(const QString& id, const QString& title, bool collapsible, bool expanded, QWidget* parent)
    {
        auto* section = new PaletteSection(
            id, title, collapsible, expanded,
            [this](const Item& item, QWidget* body) { return makeButton(item, body); }, parent);
        sectionLayout->addWidget(section);
        return section;
    }

    // --- thumbnail buttons -------------------------------------------------------
    QWidget* makeButton(const Item& item, QWidget* parent)
    {
        auto* button = new ItemButton(item, parent);
        const PaletteEntry& entry = *item.entry;
        button->setToolTip(QStringLiteral("%1 (%2 x %3)")
                               .arg(entry.title.isEmpty() ? item.key : entry.title)
                               .arg(entry.width, 0, 'g', 6)
                               .arg(entry.height, 0, 'g', 6));
        button->setAccessibleName(entry.title);
        const auto cached = thumbnails.constFind(item.key);
        if (cached != thumbnails.constEnd())
            button->setIcon(QIcon(*cached));
        else
            enqueueThumbnail(button, item);
        QObject::connect(button, &QToolButton::clicked, &dock, [this, key = item.key] { insertEntry(key); });
        QObject::connect(button, &QToolButton::customContextMenuRequested, &dock,
                         [this, button, key = item.key](const QPoint& pos) { showItemMenu(key, button->mapToGlobal(pos)); });
        button->onDragStart = [this](ItemButton& source) { startDrag(source); };
        return button;
    }

    QPixmap thumbnailFor(const Item& item)
    {
        // Light tile under dark themes; the cache is keyed per theme so a live
        // theme switch re-renders instead of reusing the other variant.
        const bool darkUi = dock.palette().color(QPalette::Window).lightness() < 128;
        const QString cacheKey = darkUi ? QStringLiteral("dark:") + item.key : item.key;
        const auto cached = thumbnails.constFind(cacheKey);
        if (cached != thumbnails.constEnd())
            return *cached;
        const qreal dpr = dock.devicePixelRatioF();
        const QColor tile = darkUi ? QColor(0xf2, 0xf2, 0xf2) : QColor(Qt::transparent);
        const QImage image = renderPaletteThumbnail(*item.entry, QSize(kThumbSize, kThumbSize), dpr, 1, tile);
        QPixmap pixmap = QPixmap::fromImage(image);
        pixmap.setDevicePixelRatio(dpr);
        thumbnails.insert(cacheKey, pixmap);
        return pixmap;
    }

    // The window moved to a screen with another device pixel ratio: the pixmaps were rendered for
    // the old one (blurry when it grew, needlessly large when it shrank). Every button gets one
    // for the new ratio, a few per event-loop pass like the first time.
    void refreshThumbnails()
    {
        thumbnails.clear();
        pending.clear();
        for (QToolButton* tool : dock.findChildren<QToolButton*>()) {
            if (const auto* button = dynamic_cast<ItemButton*>(tool))
                enqueueThumbnail(tool, button->item());
        }
    }

    void enqueueThumbnail(QToolButton* button, const Item& item)
    {
        pending.push_back({button, item});
        thumbTimer->start();
    }

    void renderPendingThumbnails()
    {
        for (int i = 0; i < kThumbsPerTick && !pending.empty(); ++i) {
            PendingThumb job = std::move(pending.front());
            pending.pop_front();
            if (job.button)
                job.button->setIcon(QIcon(thumbnailFor(job.item)));
        }
        if (!pending.empty())
            thumbTimer->start();
    }

    // --- lookup ----------------------------------------------------------------------
    // Built-in entries by "<palette>/<entry>", user library entries by
    // "user:<path>/e<position>".
    const PaletteEntry* findEntry(const QString& key) const
    {
        if (const PaletteEntry* entry = library.entry(key))
            return entry;
        for (const UserSection& user : userSections) {
            const QString prefix = user.library->paletteId() + QLatin1Char('/');
            if (!key.startsWith(prefix))
                continue;
            for (const PaletteEntry& entry : user.library->palette().entries) {
                if (entry.key(user.library->paletteId()) == key)
                    return &entry;
            }
        }
        return nullptr;
    }

    static bool isUserKey(const QString& key) { return key.startsWith(QLatin1String("user:")); }

    // --- favorites, recent ---------------------------------------------------------
    std::vector<Item> itemsForKeys(const QStringList& keys) const
    {
        std::vector<Item> items;
        for (const QString& key : keys) {
            if (const PaletteEntry* entry = library.entry(key))
                items.push_back({key, entry});
        }
        return items;
    }

    void refreshUserSections()
    {
        favorites->setItems(itemsForKeys(settings.favoriteShapes()));
        recent->setItems(itemsForKeys(settings.recentShapes()));
        updateSectionVisibility();
    }

    void updateSectionVisibility()
    {
        const bool searching = !search->text().trimmed().isEmpty();
        favorites->setVisible(!searching && !favorites->items().empty());
        recent->setVisible(!searching && !recent->items().empty());
        for (PaletteSection* section : paletteSections)
            section->setVisible(!searching);
        for (const UserSection& user : userSections)
            user.section->setVisible(!searching);
        results->setVisible(searching);
    }

    void saveExpandedPalettes()
    {
        QStringList ids;
        for (const PaletteSection* section : paletteSections) {
            if (section->isExpanded())
                ids.push_back(section->id());
        }
        for (const UserSection& user : userSections) {
            if (user.section->isExpanded())
                ids.push_back(user.section->id());
        }
        settings.setExpandedPalettes(ids);
    }

    void toggleFavorite(const QString& key)
    {
        QStringList favoriteKeys = settings.favoriteShapes();
        if (!favoriteKeys.removeOne(key))
            favoriteKeys.push_back(key);
        settings.setFavoriteShapes(favoriteKeys);
        refreshUserSections();
    }

    void showItemMenu(const QString& key, const QPoint& globalPos)
    {
        if (isUserKey(key)) {
            showUserItemMenu(key, globalPos);
            return;
        }
        QMenu menu;
        const bool isFavorite = settings.favoriteShapes().contains(key);
        QAction* toggle = menu.addAction(isFavorite ? PaletteDock::tr("Remove from Favorites") : PaletteDock::tr("Add to Favorites"));
        if (menu.exec(globalPos) == toggle)
            toggleFavorite(key);
    }

    // --- user libraries ----------------------------------------------------------------
    int libraryIndex(const QString& path) const
    {
        for (std::size_t i = 0; i < userSections.size(); ++i) {
            if (userSections[i].library->path() == path)
                return static_cast<int>(i);
        }
        return -1;
    }

    void saveLibraryList()
    {
        QStringList paths;
        for (const UserSection& user : userSections)
            paths.push_back(user.library->path());
        settings.setOpenLibraries(paths);
    }

    void reportError(const QString& message)
    {
        emit dock.statusMessage(message);
    }

    std::vector<Item> libraryItems(const UserLibrary& lib) const
    {
        std::vector<Item> items;
        for (const PaletteEntry& entry : lib.palette().entries)
            items.push_back({entry.key(lib.paletteId()), &entry, lib.palettePtr()});
        return items;
    }

    // Shows a library that changed: its cached thumbnails are dropped.
    void refreshLibrary(UserSection& user)
    {
        const QString prefix = user.library->paletteId() + QLatin1Char('/');
        for (auto it = thumbnails.begin(); it != thumbnails.end();)
            it = it.key().contains(prefix) ? thumbnails.erase(it) : std::next(it);
        user.section->setItems(libraryItems(*user.library));
    }

    // Adds the section of an opened or created library.
    void addLibrarySection(std::unique_ptr<UserLibrary> lib, bool expanded)
    {
        const QString path = lib->path();
        auto* section = new PaletteSection(
            lib->paletteId(), lib->title(), true, expanded,
            [this](const Item& item, QWidget* body) { return makeButton(item, body); },
            sectionLayout->parentWidget());
        section->header()->setToolTip(path);
        connect(section->addHeaderButton(QStringLiteral("+"), PaletteDock::tr("Add Selection to Library")),
                [this, path] { promptAddSelection(path); });
        connect(section->addHeaderButton(PaletteDock::tr("Export"), withEllipsis(PaletteDock::tr("Export Library"))),
                [this, path] { promptExport(path); });
        connect(section->addHeaderButton(QStringLiteral("x"), PaletteDock::tr("Close Library")),
                [this, path] { closeLibrary(path); });
        sectionLayout->insertWidget(sectionLayout->indexOf(recent) + 1 + static_cast<int>(userSections.size()), section);
        userSections.push_back({std::move(lib), section});
        section->onExpandedChanged = [this](PaletteSection&) { saveExpandedPalettes(); };
        section->setItems(libraryItems(*userSections.back().library));
        updateSectionVisibility();
    }

    void connect(QToolButton* button, std::function<void()> action)
    {
        QObject::connect(button, &QToolButton::clicked, &dock, [action = std::move(action)] { action(); });
    }

    // Reopens the libraries of the last session; files that are gone are dropped.
    void restoreLibraries()
    {
        const QStringList expanded = settings.expandedPalettes();
        QStringList missing;
        for (const QString& path : settings.openLibraries()) {
            QString error;
            std::optional<UserLibrary> lib = UserLibrary::open(path, &error);
            if (!lib) {
                missing.push_back(QFileInfo(path).fileName());
                continue;
            }
            const bool open = expanded.contains(lib->paletteId());
            addLibrarySection(std::make_unique<UserLibrary>(std::move(*lib)), open);
        }
        if (missing.isEmpty())
            return;
        saveLibraryList();
        const QString message = PaletteDock::tr("Library not found, removed from the palette: %1").arg(missing.join(QStringLiteral(", ")));
        QMetaObject::invokeMethod(&dock, [this, message] { reportError(message); }, Qt::QueuedConnection);
    }

    bool adoptLibrary(std::optional<UserLibrary> lib, const QString& error, QString* errorOut)
    {
        if (!lib) {
            if (errorOut != nullptr)
                *errorOut = error;
            return false;
        }
        const int existing = libraryIndex(lib->path());
        if (existing >= 0) {
            userSections[static_cast<std::size_t>(existing)].section->setExpanded(true);
            return true;
        }
        addLibrarySection(std::make_unique<UserLibrary>(std::move(*lib)), true);
        saveLibraryList();
        saveExpandedPalettes();
        return true;
    }

    // The section's entries at `index` of `path`, from a key "user:<path>/e<n>".
    bool locateEntry(const QString& key, int* sectionOut, std::size_t* index) const
    {
        for (std::size_t i = 0; i < userSections.size(); ++i) {
            const UserLibrary& lib = *userSections[i].library;
            const QString prefix = lib.paletteId() + QLatin1Char('/');
            if (!key.startsWith(prefix))
                continue;
            const auto& entries = lib.palette().entries;
            for (std::size_t n = 0; n < entries.size(); ++n) {
                if (entries[n].key(lib.paletteId()) == key) {
                    *sectionOut = static_cast<int>(i);
                    *index = n;
                    return true;
                }
            }
        }
        return false;
    }

    void showUserItemMenu(const QString& key, const QPoint& globalPos)
    {
        int lib = -1;
        std::size_t index = 0;
        if (!locateEntry(key, &lib, &index))
            return;
        const QString path = userSections[static_cast<std::size_t>(lib)].library->path();
        QMenu menu;
        QAction* rename = menu.addAction(withEllipsis(PaletteDock::tr("Rename")));
        QAction* remove = menu.addAction(PaletteDock::tr("Remove"));
        QAction* chosen = menu.exec(globalPos);
        if (chosen == rename) {
            const QString current = QString::fromStdString(
                userSections[static_cast<std::size_t>(lib)].library->library().entries[index].title);
            bool accepted = false;
            const QString title = QInputDialog::getText(&dock, PaletteDock::tr("Rename"), PaletteDock::tr("Title:"),
                                                        QLineEdit::Normal, current, &accepted);
            if (accepted)
                renameEntry(path, index, title);
        } else if (chosen == remove) {
            removeEntry(path, index);
        }
    }

    void promptAddSelection(const QString& path)
    {
        bool accepted = false;
        const QString title = QInputDialog::getText(&dock, PaletteDock::tr("Add Selection to Library"),
                                                    PaletteDock::tr("Title:"), QLineEdit::Normal, QString(), &accepted);
        QString error;
        if (accepted && !addSelection(path, title, &error))
            reportError(error);
    }

    // Library > Export: a copy of the library under another name.
    void promptExport(const QString& path)
    {
        const QString target = QFileDialog::getSaveFileName(&dock, PaletteDock::tr("Export Library"), path,
                                                            PaletteDock::tr("Libraries (*.xml)"));
        if (target.isEmpty())
            return;
        QString error;
        if (!exportLibrary(path, target, &error))
            reportError(error);
    }

    bool addSelection(const QString& path, const QString& title, QString* error)
    {
        const int index = libraryIndex(path);
        const core::Page* page = session.currentPage();
        const std::vector<std::string> ids = session.selection().cells();
        if (index < 0 || page == nullptr || ids.empty()) {
            if (error != nullptr)
                *error = PaletteDock::tr("Select the cells to add to the library first.");
            return false;
        }
        const std::string xml = encodeCells(*page, ids);
        const std::optional<CellBundle> bundle = decodeCells(xml);
        const std::optional<core::Rect> bounds = bundle ? bundle->bounds() : std::nullopt;
        if (!bounds) {
            if (error != nullptr)
                *error = PaletteDock::tr("The selection has no size and cannot be added.");
            return false;
        }
        UserSection& user = userSections[static_cast<std::size_t>(index)];
        QString saveError;
        if (!user.library->addCells(xml, bounds->width, bounds->height, title, &saveError)) {
            if (error != nullptr)
                *error = saveError;
            return false;
        }
        refreshLibrary(user);
        return true;
    }

    bool renameEntry(const QString& path, std::size_t entryIndex, const QString& title)
    {
        const int index = libraryIndex(path);
        if (index < 0)
            return false;
        UserSection& user = userSections[static_cast<std::size_t>(index)];
        QString error;
        if (!user.library->renameEntry(entryIndex, title, &error)) {
            reportError(error);
            return false;
        }
        refreshLibrary(user);
        return true;
    }

    bool removeEntry(const QString& path, std::size_t entryIndex)
    {
        const int index = libraryIndex(path);
        if (index < 0)
            return false;
        UserSection& user = userSections[static_cast<std::size_t>(index)];
        QString error;
        if (!user.library->removeEntry(entryIndex, &error)) {
            reportError(error);
            return false;
        }
        refreshLibrary(user);
        return true;
    }

    bool exportLibrary(const QString& path, const QString& target, QString* error)
    {
        const int index = libraryIndex(path);
        if (index < 0)
            return false;
        const std::string bytes = format::serializeLibrary(userSections[static_cast<std::size_t>(index)].library->library());
        QSaveFile file(target);
        if (!file.open(QIODevice::WriteOnly) || file.write(bytes.data(), static_cast<qint64>(bytes.size())) < 0
            || !file.commit()) {
            if (error != nullptr)
                *error = PaletteDock::tr("The library could not be exported: %1").arg(file.errorString());
            return false;
        }
        return true;
    }

    void closeLibrary(const QString& path)
    {
        const int index = libraryIndex(path);
        if (index < 0)
            return;
        UserSection user = std::move(userSections[static_cast<std::size_t>(index)]);
        userSections.erase(userSections.begin() + index);
        const QString prefix = user.library->paletteId() + QLatin1Char('/');
        for (auto it = thumbnails.begin(); it != thumbnails.end();)
            it = it.key().contains(prefix) ? thumbnails.erase(it) : std::next(it);
        user.section->hide();
        user.section->deleteLater();
        saveLibraryList();
        saveExpandedPalettes();
    }

    // --- search ---------------------------------------------------------------------
    void applySearch()
    {
        const QString query = search->text().trimmed();
        if (!query.isEmpty()) {
            std::vector<Item> items;
            const auto hits = library.search(query, kMaxSearchHits);
            items.reserve(hits.size());
            for (const auto& hit : hits)
                items.push_back({hit.entry->key(hit.palette->id), hit.entry});
            results->setTitle(QStringLiteral("%1 (%2)").arg(PaletteDock::tr("Search Results")).arg(items.size()));
            results->setItems(std::move(items));
        }
        updateSectionVisibility();
    }

    // --- insertion ---------------------------------------------------------------------
    bool insertEntry(const QString& key)
    {
        const PaletteEntry* entry = findEntry(key);
        if (entry == nullptr || canvas == nullptr)
            return false;
        const std::optional<CellBundle> bundle = decodeCells(entry->cellsXml());
        if (!bundle || !actions::insertBundle(session, *bundle, canvas->centerInsertPoint(), PaletteDock::tr("Insert")))
            return false;
        noteRecent(key);
        return true;
    }

    void noteRecent(const QString& key)
    {
        if (isUserKey(key)) // user entry keys are positional: not stable enough to remember
            return;
        settings.addRecentShape(key);
        refreshUserSections();
    }

    // Sidebar.createDragSource (Sidebar.js:5871): the payload is the cells XML.
    void startDrag(ItemButton& source)
    {
        const PaletteEntry* entry = findEntry(source.key());
        if (entry == nullptr)
            return;
        const QByteArray xml = QByteArray::fromStdString(entry->cellsXml());
        auto* mime = new QMimeData;
        mime->setData(QString::fromLatin1(kCellsMimeType), xml);
        mime->setText(QString::fromUtf8(xml));

        const QPixmap pixmap = thumbnailFor({source.key(), entry});
        QDrag drag(&source);
        drag.setMimeData(mime);
        drag.setPixmap(pixmap);
        drag.setHotSpot(QPoint(kThumbSize / 2, kThumbSize / 2));
        if (drag.exec(Qt::CopyAction) != Qt::IgnoreAction)
            noteRecent(source.key());
    }
};

PaletteDock::PaletteDock(const PaletteLibrary& library, DocumentSession& session, CanvasView& canvas,
                         AppSettings& settings, QWidget* parent)
    : QWidget(parent), d(std::make_unique<Impl>(*this, library, session, canvas, settings))
{
    setObjectName(QStringLiteral("PaletteDock"));
    d->build();
    canvas.addDropHandler(this);
}

bool PaletteDock::event(QEvent* event)
{
    // QEvent::DevicePixelRatioChange exists from Qt 6.6; before that, a move to another screen is
    // ScreenChangeInternal (which later versions also send, when nothing changes for us: the
    // thumbnails are then rendered once more at the same ratio, a few per event-loop pass).
    bool ratioChanged = event->type() == QEvent::ScreenChangeInternal;
#if QT_VERSION >= QT_VERSION_CHECK(6, 6, 0)
    ratioChanged = ratioChanged || event->type() == QEvent::DevicePixelRatioChange;
#endif
    const bool handled = QWidget::event(event);
    if (ratioChanged)
        d->refreshThumbnails();
    return handled;
}

PaletteDock::~PaletteDock()
{
    if (d->canvas != nullptr)
        d->canvas->removeDropHandler(this);
}

QLineEdit* PaletteDock::searchField() const
{
    return d->search;
}

void PaletteDock::setSearchText(const QString& text)
{
    d->search->setText(text);
    d->searchTimer->stop();
    d->applySearch();
}

bool PaletteDock::insertEntry(const QString& key)
{
    return d->insertEntry(key);
}

bool PaletteDock::openLibrary(const QString& path, QString* error)
{
    QString message;
    std::optional<UserLibrary> lib = UserLibrary::open(path, &message);
    return d->adoptLibrary(std::move(lib), message, error);
}

bool PaletteDock::newLibrary(const QString& path, QString* error)
{
    QString message;
    std::optional<UserLibrary> lib = UserLibrary::create(path, &message);
    return d->adoptLibrary(std::move(lib), message, error);
}

void PaletteDock::closeLibrary(const QString& path)
{
    d->closeLibrary(QFileInfo(path).absoluteFilePath());
}

QStringList PaletteDock::libraryPaths() const
{
    QStringList paths;
    for (const Impl::UserSection& user : d->userSections)
        paths.push_back(user.library->path());
    return paths;
}

bool PaletteDock::addSelectionToLibrary(const QString& path, const QString& title, QString* error)
{
    return d->addSelection(QFileInfo(path).absoluteFilePath(), title, error);
}

bool PaletteDock::renameLibraryEntry(const QString& path, int index, const QString& title)
{
    return index >= 0 && d->renameEntry(QFileInfo(path).absoluteFilePath(), static_cast<std::size_t>(index), title);
}

bool PaletteDock::removeLibraryEntry(const QString& path, int index)
{
    return index >= 0 && d->removeEntry(QFileInfo(path).absoluteFilePath(), static_cast<std::size_t>(index));
}

bool PaletteDock::exportLibrary(const QString& path, const QString& target, QString* error)
{
    return d->exportLibrary(QFileInfo(path).absoluteFilePath(), target, error);
}

bool PaletteDock::canDrop(const QMimeData& mime) const
{
    return mime.hasFormat(QString::fromLatin1(kCellsMimeType));
}

bool PaletteDock::drop(const QMimeData& mime, QPointF scenePos)
{
    const std::optional<CellBundle> bundle = cellsFromMimeData(mime);
    if (!bundle)
        return false;
    // A drop onto a container goes into it; a click keeps the insertion parent.
    const std::string container =
        d->canvas != nullptr ? dropTarget(d->session, d->canvas->diagramScene(), {}, scenePos) : std::string();
    return actions::insertBundle(d->session, *bundle, scenePos, PaletteDock::tr("Insert"), container);
}

} // namespace qraw::ui
