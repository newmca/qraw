// The model side of the Edit and Arrange menus (docs/design/ui.md section 6).
// Upstream references are relative to the draw.io webapp directory.
#include "qraw/ui/edit_actions.h"

#include "default_styles.h"
#include "drop_target.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_layout.h"
#include "qraw/commands/ops_pages.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/commands/ops_tables.h"
#include "qraw/commands/queries.h"
#include "qraw/commands/ops_transform.h"
#include "qraw/format/codec.h"
#include "qraw/format/embed.h"
#include "qraw/render/export.h"
#include "qraw/render/stylesheet.h"
#include "qraw/ui/app_settings.h"
#include "qraw/ui/label_autosize.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/document_session.h"

#include <QClipboard>
#include <QCoreApplication>
#include <QImage>
#include <QMimeData>

#include <algorithm>
#include <cmath>
#include <functional>
#include <unordered_map>

namespace qraw::ui::actions {

namespace {

using commands::Result;
using commands::Transaction;
using core::Cell;
using core::Point;

// --- helpers ------------------------------------------------------------------

// Runs `fn` as one named edit; true when it succeeded and changed the model.
bool runEdit(DocumentSession& session, const char* name, const std::function<Result(Transaction&)>& fn)
{
    bool changed = false;
    const Result result = session.edit(QCoreApplication::translate("qraw::ui::actions", name),
                                       [&](Transaction& tx) {
                                           Result r = fn(tx);
                                           changed = r.ok && !tx.empty();
                                           return r;
                                       });
    return result.ok && changed;
}

// Cells just inserted at `point` into a container: it grows to hold them and
// a stack layout picks their position among its children (cellsAdded
// extendParent, mxLayoutManager.cellsMoved).
Result settleInserted(Transaction& tx, std::size_t pageIndex, Result inserted, QPointF point)
{
    if (!inserted)
        return inserted;
    if (Result r = commands::ops::extendParents(tx, pageIndex, inserted.cells); !r)
        return r;
    if (Result r = commands::ops::moveInLayout(tx, pageIndex, inserted.cells, {point.x(), point.y()}); !r)
        return r;
    return inserted;
}

bool styleFlagIsOff(const Cell& cell, const char* key)
{
    return core::Style(cell.styleText()).value(key, "1") == "0";
}

// Graph.getMovableCells / getDeletableCells / getEditableCells: locked cells
// (own or ancestor `locked=1`) and cells whose own style forbids the action
// are excluded.
std::vector<std::string> selectedWhere(const DocumentSession& session, const char* forbiddenKey)
{
    std::vector<std::string> ids;
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return ids;
    for (const std::string& id : session.selection().cells()) {
        const Cell* cell = page->findCell(id);
        if (cell == nullptr || isCellLocked(*page, id))
            continue;
        if (forbiddenKey != nullptr && styleFlagIsOff(*cell, forbiddenKey))
            continue;
        ids.push_back(id);
    }
    return ids;
}

std::vector<std::string> unlockedSelection(const DocumentSession& session)
{
    return selectedWhere(session, nullptr);
}

Point absoluteOrigin(const core::Page& page, const Cell& cell)
{
    return commands::origin(page, cell.parent());
}

double snapToGrid(const core::Page& page, double value)
{
    const double grid = page.gridSize();
    return page.gridEnabled() && grid > 0 ? std::round(value / grid) * grid : value;
}

// --- paste bookkeeping (EditorUi.pasteXml, EditorUi.js:27817) ------------------

// graph.lastPasteXml / graph.pasteCounter, per session.
struct PasteState {
    std::optional<std::string> lastXml;
    int counter = 0;
};

PasteState& pasteState(const DocumentSession& session)
{
    static std::unordered_map<const DocumentSession*, PasteState> states;
    const auto [it, inserted] = states.try_emplace(&session);
    if (inserted) {
        const DocumentSession* key = &session;
        QObject::connect(&session, &QObject::destroyed, [key] { states.erase(key); });
    }
    return it->second;
}

std::string clipboardKey(const QMimeData& mime)
{
    const QString type = QString::fromLatin1(kCellsMimeType);
    return (mime.hasFormat(type) ? QString::fromUtf8(mime.data(type)) : mime.text()).toStdString();
}

// The pasted label of a plain-text paste: html=1 labels hold escaped HTML.
std::string htmlLabel(const QString& text)
{
    return text.toHtmlEscaped().replace(QLatin1Char('\n'), QStringLiteral("<br>")).toStdString();
}

Result addTextVertex(Transaction& tx, std::size_t pageIndex, const std::string& parent, const std::string& label,
                     const Point& absolute)
{
    const core::Page* page = tx.page(pageIndex);
    const Point origin = commands::origin(*page, parent);
    Cell cell = Cell::makeVertex(tx.newCellId(pageIndex), parent, label, "text;html=1;whiteSpace=wrap;",
                                 {absolute.x - origin.x, absolute.y - origin.y, 120, 40});
    return commands::ops::addCells(tx, pageIndex, {std::move(cell)});
}

bool deleteCells(DocumentSession& session, bool includeEdges, const char* name)
{
    const std::vector<std::string> ids = selectedWhere(session, "deletable");
    if (ids.empty())
        return false;
    const std::size_t page = session.currentPageIndex();
    return runEdit(session, name, [&](Transaction& tx) {
        return commands::ops::removeCells(tx, page, ids, {.includeEdges = includeEdges, .respectDeletable = true});
    });
}

// --- arrange ------------------------------------------------------------------

struct AbsoluteBox {
    std::string id;
    double x, y, width, height;
};

// The vertices among `ids` that have a plain (non-relative) geometry, with
// their absolute bounds (view-less stand-in for the cell states upstream uses).
std::vector<AbsoluteBox> alignableBoxes(const core::Page& page, const std::vector<std::string>& ids)
{
    std::vector<AbsoluteBox> boxes;
    for (const std::string& id : ids) {
        const Cell* cell = page.findCell(id);
        const core::Geometry* geometry = cell != nullptr ? cell->geometry() : nullptr;
        if (geometry == nullptr || cell->isEdge() || geometry->relative())
            continue;
        const Point origin = absoluteOrigin(page, *cell);
        boxes.push_back({id, origin.x + geometry->x(), origin.y + geometry->y(), geometry->width(), geometry->height()});
    }
    return boxes;
}

// mxGraph.alignCells (mxgraph/src/view/mxGraph.js:3741): the coordinate comes
// from the cells (min left / max right / min top / max bottom; the first
// cell's centre for Center and Middle).
double alignCoordinate(const std::vector<AbsoluteBox>& boxes, Align how)
{
    double param = 0;
    bool first = true;
    for (const AbsoluteBox& b : boxes) {
        double value = 0;
        switch (how) {
        case Align::Left: value = b.x; break;
        case Align::Center: return b.x + b.width / 2;
        case Align::Right: value = b.x + b.width; break;
        case Align::Top: value = b.y; break;
        case Align::Middle: return b.y + b.height / 2;
        case Align::Bottom: value = b.y + b.height; break;
        }
        const bool takeMax = how == Align::Right || how == Align::Bottom;
        param = first ? value : (takeMax ? std::max(param, value) : std::min(param, value));
        first = false;
    }
    return param;
}

double alignedPosition(const AbsoluteBox& b, Align how, double param)
{
    switch (how) {
    case Align::Left: return param;
    case Align::Center: return param - b.width / 2;
    case Align::Right: return param - b.width;
    case Align::Top: return param;
    case Align::Middle: return param - b.height / 2;
    case Align::Bottom: return param - b.height;
    }
    return param;
}

bool isHorizontal(Align how)
{
    return how == Align::Left || how == Align::Center || how == Align::Right;
}

// Moves one cell without disconnecting its edges (setGeometry upstream).
Result moveKeepingEdges(Transaction& tx, std::size_t page, const std::string& id, double dx, double dy)
{
    if (std::abs(dx) < 1e-9 && std::abs(dy) < 1e-9)
        return Result::success();
    return commands::ops::moveCells(tx, page, {id}, dx, dy, {.disconnect = false});
}

} // namespace

// --- clipboard ------------------------------------------------------------------

bool copySelection(DocumentSession& session, QClipboard& clipboard)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr || session.selection().isEmpty())
        return false;
    const std::string xml = encodeCells(*page, session.selection().cells());
    const auto bundle = decodeCells(xml);
    if (!bundle || bundle->empty())
        return false;
    auto* mime = new QMimeData;
    setCellsMimeData(*mime, xml);
    clipboard.setMimeData(mime);
    PasteState& state = pasteState(session);
    state.lastXml = xml;
    state.counter = 0;
    return true;
}

bool canCopyAsImage(const DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    return page != nullptr && std::any_of(page->rootEntries().begin(), page->rootEntries().end(), [](const auto& entry) {
        const Cell* cell = std::get_if<Cell>(&entry);
        return cell != nullptr && (cell->isVertex() || cell->isEdge());
    });
}

bool copyAsImage(DocumentSession& session, QClipboard& clipboard, qreal devicePixelRatio)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr || !canCopyAsImage(session))
        return false;
    const std::vector<std::string> selection = session.selection().cells();

    render::ExportOptions options;
    options.format = render::ExportFormat::Png;
    options.scale = std::max<qreal>(1, devicePixelRatio);
    options.transparent = page->background().empty(); // the page color shows when there is one
    options.cellIds = selection;
    options.maxPixelSize = 16384;
    options.context = session.renderContext();
    const render::ExportResult rendered = render::exportPage(session.document(), session.currentPageIndex(), options);
    if (!rendered.ok()) {
        emit session.statusMessage(QCoreApplication::translate("qraw::ui::actions", "The image could not be rendered."));
        return false;
    }

    // The editable diagram goes with the image: the selected cells, else this page.
    QByteArray png = rendered.data;
    const std::string xml = selection.empty() ? format::pageModelText(*page) : encodeCells(*page, selection);
    if (const std::optional<QByteArray> embedded = format::embedInPng(png, xml))
        png = *embedded;

    QImage image = QImage::fromData(png, "PNG");
    if (image.isNull())
        return false;
    image.setDevicePixelRatio(options.scale);
    auto* mime = new QMimeData;
    mime->setImageData(image);
    mime->setData(QStringLiteral("image/png"), png);
    clipboard.setMimeData(mime);
    return true;
}

bool cutSelection(DocumentSession& session, QClipboard& clipboard)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return false;
    // EditorUi.copyXml(removeCells): only what can actually be removed is copied.
    const std::vector<std::string> cuttable = selectedWhere(session, "deletable");
    if (cuttable.empty())
        return false;
    const std::string xml = encodeCells(*page, cuttable);
    if (!deleteCells(session, false, QT_TRANSLATE_NOOP("qraw::ui::actions", "Cut")))
        return false;
    auto* mime = new QMimeData;
    setCellsMimeData(*mime, xml);
    clipboard.setMimeData(mime);
    pasteState(session).lastXml.reset(); // a paste after cut lands in place
    return true;
}

bool paste(DocumentSession& session, const QMimeData& mime, std::optional<QPointF> at)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return false;
    const std::string key = clipboardKey(mime);
    PasteState& state = pasteState(session);
    const bool repeated = state.lastXml && *state.lastXml == key;
    if (repeated) {
        if (!at)
            ++state.counter;
    } else {
        state.lastXml = key;
        state.counter = 0;
    }
    const double grid = page->gridSize();
    const double offset = state.counter * grid;
    const std::size_t pageIndex = session.currentPageIndex();
    std::string parent = session.insertionParent();
    if (parent.empty())
        return false;
    if (at) {
        // Pasting at a point drops into the container under it, as a drop does.
        const render::PageView view = render::PageView::build(*page, session.pageRenderContext());
        if (std::string target = dropTarget(session, view, {}, *at); !target.empty())
            parent = std::move(target);
    }

    if (const std::optional<CellBundle> bundle = cellsFromMimeData(mime)) {
        if (bundle->empty())
            return false;
        double dx = offset;
        double dy = offset;
        if (at) {
            // EditorUi.pasteXml with a target point: the bounds' top-left lands on the snapped point.
            const core::Rect bounds = bundle->bounds().value_or(core::Rect{});
            dx = std::round(snapToGrid(*page, at->x())) - bounds.x;
            dy = std::round(snapToGrid(*page, at->y())) - bounds.y;
        }
        return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Paste"), [&](Transaction& tx) {
            Result inserted = ui::insertBundle(tx, pageIndex, *bundle, parent, dx, dy);
            return at ? settleInserted(tx, pageIndex, std::move(inserted), *at) : inserted;
        });
    }

    const QString text = mime.text();
    if (text.trimmed().isEmpty())
        return false;
    const Point where = at ? Point{at->x(), at->y()} : Point{grid + offset, grid + offset};
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Paste"), [&](Transaction& tx) {
        return addTextVertex(tx, pageIndex, parent, htmlLabel(text), where);
    });
}

// --- delete / duplicate ------------------------------------------------------------

bool deleteSelection(DocumentSession& session, bool includeEdges)
{
    return deleteCells(session, includeEdges, QT_TRANSLATE_NOOP("qraw::ui::actions", "Delete"));
}

bool duplicateSelection(DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr || session.selection().isEmpty())
        return false;
    // Graph.duplicateCells: every clone goes next to its original, into the
    // same parent, shifted by the grid size.
    std::vector<std::pair<std::string, std::vector<std::string>>> byParent;
    for (const std::string& id : commands::sortByZOrder(*page, commands::topmostCells(*page, session.selection().cells()))) {
        const Cell* cell = page->findCell(id);
        if (cell == nullptr || cell->parent().empty() || isLayer(*page, id))
            continue;
        auto it = std::find_if(byParent.begin(), byParent.end(), [&](const auto& g) { return g.first == cell->parent(); });
        if (it == byParent.end())
            it = byParent.insert(byParent.end(), {cell->parent(), {}});
        it->second.push_back(id);
    }
    std::vector<std::pair<std::string, CellBundle>> bundles;
    for (const auto& [parent, ids] : byParent) {
        if (auto bundle = decodeCells(encodeCells(*page, ids)))
            bundles.emplace_back(parent, std::move(*bundle));
    }
    if (bundles.empty())
        return false;

    const double grid = page->gridSize();
    const std::size_t pageIndex = session.currentPageIndex();
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Duplicate"), [&](Transaction& tx) {
        std::vector<std::string> clones;
        for (const auto& [parent, bundle] : bundles) {
            Result inserted = ui::insertBundle(tx, pageIndex, bundle, parent, grid, grid);
            if (!inserted)
                return inserted;
            clones.insert(clones.end(), inserted.cells.begin(), inserted.cells.end());
        }
        return Result::success(std::move(clones));
    });
}

// --- selection -----------------------------------------------------------------

void selectAll(DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    const std::string scope = session.insertionParent();
    if (page == nullptr || scope.empty())
        return;
    std::vector<std::string> ids;
    for (const Cell* cell : page->children(scope)) {
        if (cell->isVisible() && isCellSelectable(*page, cell->id()))
            ids.push_back(cell->id());
    }
    session.selection().set(std::move(ids));
}

namespace {
void selectDescendants(DocumentSession& session, bool vertices)
{
    const core::Page* page = session.currentPage();
    const std::string scope = session.insertionParent();
    if (page == nullptr || scope.empty())
        return;
    std::vector<std::string> ids;
    for (const Cell* cell : page->descendants(scope)) {
        if ((vertices ? cell->isVertex() : cell->isEdge()) && cell->isVisible() && isCellSelectable(*page, cell->id()))
            ids.push_back(cell->id());
    }
    session.selection().set(std::move(ids));
}
} // namespace

void selectVertices(DocumentSession& session)
{
    selectDescendants(session, true);
}

void selectEdges(DocumentSession& session)
{
    selectDescendants(session, false);
}

void selectNone(DocumentSession& session)
{
    session.selection().clear();
}

// --- arrange --------------------------------------------------------------------

bool group(DocumentSession& session)
{
    const std::vector<std::string> ids = unlockedSelection(session);
    const std::size_t page = session.currentPageIndex();
    return !ids.empty() && runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Group"), [&](Transaction& tx) {
        return commands::ops::groupCells(tx, page, ids);
    });
}

bool ungroup(DocumentSession& session)
{
    const std::vector<std::string> ids = unlockedSelection(session);
    const std::size_t page = session.currentPageIndex();
    return !ids.empty() && runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Ungroup"), [&](Transaction& tx) {
        return commands::ops::ungroupCells(tx, page, ids);
    });
}

bool removeFromGroup(DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return false;
    // Cells that already sit in the layer would only be moved to the front.
    const std::string layer = session.currentLayer();
    std::vector<std::string> ids = unlockedSelection(session);
    std::erase_if(ids, [&](const std::string& id) { return page->findCell(id)->parent() == layer; });
    const std::size_t pageIndex = session.currentPageIndex();
    return !ids.empty() && runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Remove from Group"),
                                   [&](Transaction& tx) {
                                       return commands::ops::removeFromGroup(tx, pageIndex, ids, layer);
                                   });
}

bool order(DocumentSession& session, commands::ops::ZOrder how)
{
    const std::vector<std::string> ids = session.selection().cells();
    const std::size_t page = session.currentPageIndex();
    if (ids.empty())
        return false;
    const char* name = nullptr;
    switch (how) {
    case commands::ops::ZOrder::ToFront: name = QT_TRANSLATE_NOOP("qraw::ui::actions", "To Front"); break;
    case commands::ops::ZOrder::ToBack: name = QT_TRANSLATE_NOOP("qraw::ui::actions", "To Back"); break;
    case commands::ops::ZOrder::BringForward: name = QT_TRANSLATE_NOOP("qraw::ui::actions", "Bring Forward"); break;
    case commands::ops::ZOrder::SendBackward: name = QT_TRANSLATE_NOOP("qraw::ui::actions", "Send Backward"); break;
    }
    return runEdit(session, name, [&](Transaction& tx) { return commands::ops::orderCells(tx, page, ids, how); });
}

bool align(DocumentSession& session, Align how)
{
    const core::Page* page = session.currentPage();
    const std::vector<std::string> ids = selectedWhere(session, "movable");
    if (page == nullptr || ids.size() < 2)
        return false;
    const std::vector<AbsoluteBox> boxes = alignableBoxes(*page, ids);
    if (boxes.empty())
        return false;
    const double param = alignCoordinate(boxes, how);
    const std::size_t pageIndex = session.currentPageIndex();
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Align"), [&](Transaction& tx) {
        // Parents first: a moved parent changes where its children are (Graph.alignCells).
        for (const std::string& id : commands::sortByZOrder(*tx.page(pageIndex), ids)) {
            const core::Page& current = *tx.page(pageIndex);
            const auto box = alignableBoxes(current, {id});
            if (box.empty())
                continue;
            const double delta = alignedPosition(box.front(), how, param) - (isHorizontal(how) ? box.front().x : box.front().y);
            const Result moved = moveKeepingEdges(tx, pageIndex, id, isHorizontal(how) ? delta : 0, isHorizontal(how) ? 0 : delta);
            if (!moved)
                return moved;
        }
        return Result::success();
    });
}

bool distribute(DocumentSession& session, bool horizontal)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return false;
    std::vector<std::string> vertices;
    for (const std::string& id : selectedWhere(session, "movable")) {
        const Cell* cell = page->findCell(id);
        if (cell != nullptr && cell->isVertex())
            vertices.push_back(id);
    }
    std::vector<AbsoluteBox> boxes = alignableBoxes(*page, vertices);
    if (boxes.size() < 3)
        return false;
    // Graph.distributeCells (Graph.js:40430): equal steps between the centres;
    // the outermost cells stay.
    auto centre = [&](const AbsoluteBox& b) { return horizontal ? b.x + b.width / 2 : b.y + b.height / 2; };
    std::stable_sort(boxes.begin(), boxes.end(), [&](const AbsoluteBox& a, const AbsoluteBox& b) {
        return horizontal ? a.x < b.x : a.y < b.y;
    });
    double min = centre(boxes.front());
    double max = min;
    for (const AbsoluteBox& b : boxes) {
        min = std::min(min, centre(b));
        max = std::max(max, centre(b));
    }
    const double step = (max - min) / static_cast<double>(boxes.size() - 1);
    const std::size_t pageIndex = session.currentPageIndex();
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Distribute"), [&](Transaction& tx) {
        double target = min;
        for (std::size_t i = 1; i + 1 < boxes.size(); ++i) {
            target += step;
            const AbsoluteBox& b = boxes[i];
            const double size = horizontal ? b.width : b.height;
            const double current = horizontal ? b.x : b.y;
            const double delta = std::round(target - size / 2) - current;
            const Result moved = moveKeepingEdges(tx, pageIndex, b.id, horizontal ? delta : 0, horizontal ? 0 : delta);
            if (!moved)
                return moved;
        }
        return Result::success();
    });
}

bool toggleLock(DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    const std::vector<std::string> ids = session.selection().cells();
    if (page == nullptr || ids.empty())
        return false;
    const Cell* first = page->findCell(ids.front());
    if (first == nullptr)
        return false;
    // Actions.js:534: the first cell decides; editable=1 (the default) locks.
    const bool lock = !styleFlagIsOff(*first, "editable");
    const std::string on = lock ? "0" : "1"; // movable, resizable, ...: 0 when locking
    const std::string locked = lock ? "1" : "0";
    const std::size_t pageIndex = session.currentPageIndex();
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Lock/Unlock"), [&](Transaction& tx) {
        return commands::ops::setStyles(tx, pageIndex, ids,
                                        {{"movable", on}, {"resizable", on}, {"rotatable", on}, {"deletable", on},
                                         {"editable", on}, {"locked", locked}, {"connectable", on}});
    });
}

bool clearWaypoints(DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return false;
    // addAllEdges: the edges of selected vertices are cleared too.
    std::vector<std::string> ids = unlockedSelection(session);
    for (const std::string& edge : commands::allEdges(*page, ids))
        ids.push_back(edge);
    const std::size_t pageIndex = session.currentPageIndex();
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Clear Waypoints"), [&](Transaction& tx) {
        std::vector<std::string> done;
        for (const std::string& id : ids) {
            const Cell* cell = tx.page(pageIndex)->findCell(id);
            if (cell == nullptr || !cell->isEdge() || cell->geometry() == nullptr || isCellLocked(*tx.page(pageIndex), id) ||
                std::find(done.begin(), done.end(), id) != done.end())
                continue;
            done.push_back(id);
            const Result cleared = commands::ops::setWaypoints(tx, pageIndex, id, {});
            if (!cleared)
                return cleared;
        }
        return Result::success();
    });
}

// --- style ------------------------------------------------------------------------

bool copyStyle(const DocumentSession& session, std::string& buffer)
{
    const core::Page* page = session.currentPage();
    const Cell* cell = page != nullptr ? page->findCell(session.selection().first()) : nullptr;
    if (cell == nullptr)
        return false;
    buffer = cell->styleText();
    return true;
}

bool canSetDefaultStyle(const DocumentSession& session)
{
    return session.selection().size() == 1 && session.currentPage() != nullptr &&
           session.currentPage()->findCell(session.selection().first()) != nullptr;
}

bool setAsDefaultStyle(const DocumentSession& session)
{
    if (!canSetDefaultStyle(session))
        return false;
    const core::Page& page = *session.currentPage();
    const Cell& cell = *page.findCell(session.selection().first());
    AppSettings settings;
    commands::DefaultStyles defaults = settings.defaultStyles();
    commands::setDefaultFrom(defaults, cell.style(), defaultStyleTarget(page, cell));
    settings.setDefaultStyles(defaults);
    return true;
}

void clearDefaultStyle()
{
    AppSettings().setDefaultStyles({});
}

bool setShape(DocumentSession& session, const std::string& shapeValue)
{
    const std::vector<std::string> ids = unlockedSelection(session);
    const std::size_t page = session.currentPageIndex();
    return ids.size() == 1 && runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Edit Shape"), [&](Transaction& tx) {
        return commands::ops::setStyle(tx, page, ids, "shape", shapeValue);
    });
}

bool pasteStyle(DocumentSession& session, const std::string& buffer)
{
    const std::vector<std::string> ids = unlockedSelection(session);
    const std::size_t page = session.currentPageIndex();
    return !buffer.empty() && !ids.empty() && runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Paste Style"), [&](Transaction& tx) {
        return commands::ops::setStyleText(tx, page, ids, buffer);
    });
}

// --- tables -----------------------------------------------------------------------

std::string tableTarget(const DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr || session.selection().isEmpty())
        return {};
    // ArrangePanel.addTable: ss.vertices[0], lifted to the row or table with several cells selected.
    std::string id;
    for (const std::string& selected : session.selection().cells()) {
        const Cell* cell = page->findCell(selected);
        if (cell != nullptr && cell->isVertex()) {
            id = selected;
            break;
        }
    }
    if (id.empty() || commands::ops::tableOf(*page, id).empty() || isCellLocked(*page, id))
        return {};
    if (session.selection().size() > 1) {
        if (commands::ops::isTableCell(*page, *page->findCell(id)))
            id = page->findCell(id)->parent();
        if (commands::ops::isTableRow(*page, *page->findCell(id)))
            id = page->findCell(id)->parent();
    }
    return id;
}

bool editTable(DocumentSession& session, TableEdit edit)
{
    const std::string target = tableTarget(session);
    const std::size_t page = session.currentPageIndex();
    if (target.empty())
        return false;
    switch (edit) {
    case TableEdit::InsertRowBefore:
        return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Insert Row Above"),
                       [&](Transaction& tx) { return commands::ops::insertTableRow(tx, page, target, true); });
    case TableEdit::InsertRowAfter:
        return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Insert Row Below"),
                       [&](Transaction& tx) { return commands::ops::insertTableRow(tx, page, target, false); });
    case TableEdit::InsertColumnBefore:
        return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Insert Column Left"),
                       [&](Transaction& tx) { return commands::ops::insertTableColumn(tx, page, target, true); });
    case TableEdit::InsertColumnAfter:
        return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Insert Column Right"),
                       [&](Transaction& tx) { return commands::ops::insertTableColumn(tx, page, target, false); });
    case TableEdit::DeleteRow:
        return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Delete Row"),
                       [&](Transaction& tx) { return commands::ops::deleteTableRow(tx, page, target); });
    case TableEdit::DeleteColumn:
        return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Delete Column"),
                       [&](Transaction& tx) { return commands::ops::deleteTableColumn(tx, page, target); });
    }
    return false;
}

// --- merged cells -----------------------------------------------------------------

bool canMergeCells(const DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    return page != nullptr && commands::ops::mergeRange(*page, unlockedSelection(session)).has_value();
}

bool canUnmergeCells(const DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return false;
    const std::vector<std::string> ids = unlockedSelection(session);
    return std::ranges::any_of(ids, [&](const std::string& id) {
        return commands::ops::isMergedCell(*page, *page->findCell(id));
    });
}

bool mergeCells(DocumentSession& session)
{
    const std::vector<std::string> ids = unlockedSelection(session);
    const std::size_t page = session.currentPageIndex();
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Merge Cells"),
                   [&](Transaction& tx) { return commands::ops::mergeTableCells(tx, page, ids); });
}

bool unmergeCells(DocumentSession& session)
{
    const std::vector<std::string> ids = unlockedSelection(session);
    const std::size_t page = session.currentPageIndex();
    return !ids.empty() && runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Unmerge Cells"),
                                   [&](Transaction& tx) { return commands::ops::unmergeTableCells(tx, page, ids); });
}

// --- insertion --------------------------------------------------------------------

bool insertBundle(DocumentSession& session, const CellBundle& bundle, QPointF center, const QString& name,
                  const std::string& container)
{
    const core::Page* page = session.currentPage();
    const std::string parent = container.empty() ? session.insertionParent() : container;
    if (page == nullptr || bundle.empty() || parent.empty())
        return false;
    // Sidebar.itemClicked / createDropHandler: the new cells' bounds are centred
    // on the point and the top-left snaps to the grid.
    const core::Rect bounds = bundle.bounds().value_or(core::Rect{});
    const double dx = std::round(snapToGrid(*page, center.x() - bounds.width / 2)) - bounds.x;
    const double dy = std::round(snapToGrid(*page, center.y() - bounds.height / 2)) - bounds.y;
    const std::size_t pageIndex = session.currentPageIndex();
    // New cells take the default style (Sidebar.js cellsInserted -> pasteCellStyles).
    CellBundle styled = bundle;
    applyStoredDefaults(styled);
    bool changed = false;
    const Result result = session.edit(name, [&](Transaction& tx) {
        Result r = ui::insertBundle(tx, pageIndex, styled, parent, dx, dy);
        if (!container.empty())
            r = settleInserted(tx, pageIndex, std::move(r), center);
        changed = r.ok && !tx.empty();
        return r;
    });
    return result.ok && changed;
}


// --- direction, size and geometry ----------------------------------------------

namespace {

std::vector<std::string> selectedVertices(const DocumentSession& session, bool unlockedOnly)
{
    const core::Page* page = session.currentPage();
    std::vector<std::string> ids = unlockedOnly ? unlockedSelection(session) : session.selection().cells();
    if (page == nullptr)
        return {};
    std::erase_if(ids, [&](const std::string& id) {
        const Cell* cell = page->findCell(id);
        return cell == nullptr || !cell->isVertex() || cell->geometry() == nullptr || cell->geometry()->relative();
    });
    return ids;
}

} // namespace

bool flip(DocumentSession& session, Flip axis)
{
    const core::Page* page = session.currentPage();
    const std::vector<std::string> ids = unlockedSelection(session);
    const Cell* first = page != nullptr && !ids.empty() ? page->findCell(ids.front()) : nullptr;
    if (first == nullptr)
        return false;
    // Menus.toggleStyle: getCurrentCellStyle of the first cell decides the new value.
    const bool horizontal = axis == Flip::Horizontal;
    const char* key = horizontal ? "flipH" : "flipV";
    const std::string value = first->style().value(key, "0") == "1" ? "0" : "1";
    const std::size_t pageIndex = session.currentPageIndex();
    return runEdit(session,
                   horizontal ? QT_TRANSLATE_NOOP("qraw::ui::actions", "Flip Horizontal")
                              : QT_TRANSLATE_NOOP("qraw::ui::actions", "Flip Vertical"),
                   [&](Transaction& tx) { return commands::ops::setStyle(tx, pageIndex, ids, key, value); });
}

bool turn(DocumentSession& session, bool backwards)
{
    const std::vector<std::string> ids = unlockedSelection(session);
    const std::size_t pageIndex = session.currentPageIndex();
    return !ids.empty() &&
           runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Rotate 90 Degrees"), [&](Transaction& tx) {
               return commands::ops::turnCells(tx, pageIndex, ids, backwards);
           });
}

bool autosize(DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    const std::vector<std::string> ids = selectedVertices(session, false);
    if (page == nullptr || ids.empty())
        return false;
    // The label editor's resolution: the stylesheet and theme of the render context.
    const render::RenderContext& context = session.renderContext();
    const render::Stylesheet& sheet =
        context.stylesheet != nullptr ? *context.stylesheet : render::Stylesheet::drawioDefault();
    render::ResolveOptions options;
    options.theme = context.theme;
    const label::GridSnap grid{page->gridEnabled(), page->gridSize()};
    const std::size_t pageIndex = session.currentPageIndex();
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Autosize"), [&](Transaction& tx) {
        for (const std::string& id : ids) {
            const core::Page* current = tx.page(pageIndex);
            const Cell* cell = current->findCell(id);
            if (cell == nullptr || cell->geometry() == nullptr || cell->geometry()->relative())
                continue;
            // Groups, tables and containers keep their size: their bounds come from their children.
            if (!current->children(id).empty())
                continue;
            const render::ResolvedStyle style = sheet.resolve(cell->style(), render::CellKind::Vertex, options);
            if (style.shape() == "swimlane")
                continue;
            const auto bounds = label::preferredBounds(style, cell->geometry()->bounds(), cell->value(), grid);
            if (!bounds)
                continue;
            if (commands::Result r = commands::ops::resizeCells(tx, pageIndex, {id}, {*bounds}); !r)
                return r;
        }
        return Result::success();
    });
}

bool canEditGeometry(const DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    const std::vector<std::string> ids = selectedVertices(session, false);
    if (page == nullptr || ids.empty())
        return false;
    return std::none_of(ids.begin(), ids.end(), [&](const std::string& id) {
        return page->findCell(id)->style().value("transparentBounds", "0") == "1";
    });
}

std::vector<core::Rect> selectedVertexBounds(const DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    std::vector<core::Rect> bounds;
    if (page == nullptr)
        return bounds;
    for (const std::string& id : selectedVertices(session, false))
        bounds.push_back(page->findCell(id)->geometry()->bounds());
    return bounds;
}

bool editGeometry(DocumentSession& session, const GeometryEdit& edit)
{
    const std::vector<std::string> ids = selectedVertices(session, true);
    const std::size_t pageIndex = session.currentPageIndex();
    if (ids.empty())
        return false;
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Edit Geometry"), [&](Transaction& tx) {
        for (const std::string& id : ids) {
            const Cell* cell = tx.page(pageIndex)->findCell(id);
            if (cell == nullptr || cell->geometry() == nullptr)
                continue;
            core::Rect bounds = cell->geometry()->bounds();
            if (edit.x)
                bounds.x = *edit.x;
            if (edit.y)
                bounds.y = *edit.y;
            if (edit.width)
                bounds.width = *edit.width;
            if (edit.height)
                bounds.height = *edit.height;
            if (bounds == cell->geometry()->bounds())
                continue;
            if (commands::Result r = commands::ops::resizeCells(tx, pageIndex, {id}, {bounds}); !r)
                return r;
        }
        return Result::success();
    });
}

bool replacePageModel(DocumentSession& session, core::Page model)
{
    if (session.currentPage() == nullptr)
        return false;
    const std::size_t pageIndex = session.currentPageIndex();
    return runEdit(session, QT_TRANSLATE_NOOP("qraw::ui::actions", "Edit Diagram"), [&](Transaction& tx) {
        return commands::ops::replacePageModel(tx, pageIndex, std::move(model));
    });
}

// --- layouts ----------------------------------------------------------------------

namespace {

using commands::ops::LayoutKind;

const char* layoutEditName(LayoutKind kind)
{
    switch (kind) {
    case LayoutKind::VerticalTree: return QT_TRANSLATE_NOOP("qraw::ui::actions", "Vertical Tree Layout");
    case LayoutKind::HorizontalTree: return QT_TRANSLATE_NOOP("qraw::ui::actions", "Horizontal Tree Layout");
    case LayoutKind::OrgChart: return QT_TRANSLATE_NOOP("qraw::ui::actions", "Org Chart Layout");
    case LayoutKind::RadialTree: return QT_TRANSLATE_NOOP("qraw::ui::actions", "Radial Tree Layout");
    case LayoutKind::Circle: return QT_TRANSLATE_NOOP("qraw::ui::actions", "Circle Layout");
    case LayoutKind::HorizontalFlow: return QT_TRANSLATE_NOOP("qraw::ui::actions", "Horizontal Flow Layout");
    case LayoutKind::VerticalFlow: return QT_TRANSLATE_NOOP("qraw::ui::actions", "Vertical Flow Layout");
    case LayoutKind::Organic: return QT_TRANSLATE_NOOP("qraw::ui::actions", "Organic Layout");
    }
    return "";
}

// Why a layout has nothing to do (commands::ops::canArrangeLayout is false).
QString layoutUnavailableMessage(LayoutKind kind)
{
    switch (kind) {
    case LayoutKind::Circle:
        return QCoreApplication::translate("qraw::ui::actions", "Select at least two cells to arrange in a circle.");
    case LayoutKind::HorizontalFlow:
    case LayoutKind::VerticalFlow:
    case LayoutKind::Organic:
        return QCoreApplication::translate("qraw::ui::actions", "Select at least two cells to arrange.");
    case LayoutKind::VerticalTree:
    case LayoutKind::HorizontalTree:
    case LayoutKind::OrgChart:
    case LayoutKind::RadialTree: break;
    }
    return QCoreApplication::translate("qraw::ui::actions",
                                       "No tree found: connect the cells into a tree, or select its root.");
}

} // namespace

bool canArrangeLayout(const DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return false;
    const std::vector<std::string>& ids = session.selection().cells();
    return ids.empty() || std::any_of(ids.begin(), ids.end(), [&](const std::string& id) {
               const Cell* cell = page->findCell(id);
               return cell != nullptr && cell->isVertex();
           });
}

bool arrangeLayout(DocumentSession& session, LayoutKind kind)
{
    const core::Page* page = session.currentPage();
    if (page == nullptr)
        return false;
    const commands::ops::LayoutScope scope =
        commands::ops::layoutScopeForSelection(*page, session.selection().cells(), session.insertionParent());
    if (!commands::ops::canArrangeLayout(*page, kind, scope)) {
        emit session.statusMessage(layoutUnavailableMessage(kind));
        return false;
    }
    const std::size_t pageIndex = session.currentPageIndex();
    return runEdit(session, layoutEditName(kind),
                   [&](Transaction& tx) { return commands::ops::arrangeLayout(tx, pageIndex, kind, scope); });
}


} // namespace qraw::ui::actions
