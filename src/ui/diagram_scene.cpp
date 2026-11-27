#include "qraw/ui/diagram_scene.h"

#include "canvas_layout.h"
#include "qraw/geometry/polyline.h"
#include "qraw/render/perf_scope.h"
#include "qraw/render/tile_rasterizer.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/document_session.h"

#include <QColor>
#include <QLineF>
#include <QPainter>
#include <QPen>
#include <QTransform>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace qraw::ui {

namespace {

constexpr double kHitProbeMinimum = 0.5; // hit probe half-size for tolerance 0
// CellState::paintBounds is conservative but antialiased edges can still touch
// the pixel just outside it: a cell is a hit candidate within this of them.
constexpr double kAntialiasSlack = 1;

QRectF toQRect(const core::Rect& r)
{
    return {r.x, r.y, r.width, r.height};
}

// The anchor-relative box a label occupies, when it has a size.
std::optional<QRectF> labelRect(const geometry::LabelBox& box)
{
    if (box.width <= 0 || box.height <= 0)
        return std::nullopt;
    return toQRect(box.rect());
}

// Rotates `p` back by `degrees` around `center`, so a rotated box can be
// tested as an axis-aligned one.
QPointF unrotate(QPointF p, QPointF center, double degrees)
{
    if (degrees == 0)
        return p;
    QTransform t;
    t.translate(center.x(), center.y());
    t.rotate(-degrees);
    t.translate(-center.x(), -center.y());
    return t.map(p);
}

bool labelContains(const render::CellState& state, QPointF p)
{
    if (!state.label)
        return false;
    const auto rect = labelRect(*state.label);
    if (!rect)
        return false;
    return rect->contains(unrotate(p, QPointF(state.label->x, state.label->y), state.label->rotation));
}

bool vertexContains(const render::CellState& state, QPointF p, double tolerance)
{
    const QRectF bounds = toQRect(state.bounds);
    const QRectF grown = bounds.adjusted(-tolerance, -tolerance, tolerance, tolerance);
    if (grown.contains(unrotate(p, bounds.center(), state.style.rotation())))
        return true;
    return labelContains(state, p);
}

bool edgeContains(const render::CellState& state, QPointF p, double tolerance)
{
    const double d2 = geometry::distanceSquaredToPolyline(state.points, core::Point{p.x(), p.y()});
    return d2 <= tolerance * tolerance || labelContains(state, p);
}

// Whether repainting `a` as `b` would look different: the fields the
// projection diff compares (ui.md section 3, step 3). `cell`, `parent`
// and `depth` are model bookkeeping, not paint input.
bool sameVisual(const render::CellState& a, const render::CellState& b)
{
    return a.kind == b.kind && a.collapsed == b.collapsed && a.shape == b.shape && a.bounds == b.bounds
        && a.paintBounds == b.paintBounds && a.points == b.points && a.routedPoints == b.routedPoints
        && a.label == b.label && a.labelText == b.labelText && a.valueText == b.valueText && a.style == b.style;
}

// Whether `inner` lies inside `outer`, edges included. QRectF::contains()
// treats a rectangle without width or height as null and never contains it,
// which would skip every horizontal or vertical edge and zero-size vertex.
bool containsInclusive(const QRectF& outer, const QRectF& inner)
{
    return inner.left() >= outer.left() && inner.right() <= outer.right() && inner.top() >= outer.top()
        && inner.bottom() <= outer.bottom();
}

// What the rubber band tests (mxGraph.getCells): the bounding box of the
// state, of a rotated vertex including its rotation.
QRectF selectionBox(const render::CellState& state)
{
    const double rotation = state.kind == render::CellState::Kind::Vertex ? state.style.rotation() : 0;
    return toQRect(rotation != 0 ? geometry::rotatedBounds(state.bounds, rotation) : state.bounds);
}

// The candidate box of a hit test: the paint bounds with their antialiasing
// slack (what the scene's items used to report to the BSP index).
QRectF candidateBox(const render::CellState& state)
{
    return toQRect(state.paintBounds).adjusted(-kAntialiasSlack, -kAntialiasSlack, kAntialiasSlack, kAntialiasSlack);
}

// Every box a query may ask about, so that one grid serves them all: the
// candidate box of a point hit, the selection box of a rubber band and the
// unrotated bounds (the arrows around a hovered vertex, the fold control).
QRectF indexedBox(const render::CellState& state)
{
    return candidateBox(state).united(selectionBox(state)).united(toQRect(state.bounds));
}

// Closed intersection (touching counts), like the grid's.
bool intersectsClosed(const QRectF& a, const QRectF& b)
{
    return a.left() <= b.right() && a.right() >= b.left() && a.top() <= b.bottom() && a.bottom() >= b.top();
}

// --- projection diff -------------------------------------------------------

struct ProjectionDiff {
    std::vector<std::string> added;   // states that are new
    std::vector<std::string> refresh; // states whose visual changed
};

// A table paints its grid from the rows and cells below it (the children of
// ShapePaintContext), so a change to a descendant repaints every table or row
// above it.
bool isTablePart(const render::CellState& state)
{
    const std::string_view shape = state.style.shape();
    return shape == "table" || shape == "tableRow";
}

// Adds the table and row ancestors of the state at `index` of `view` that are
// still states of `alive` to `refresh`, once each (`queued` is what is in
// there already).
void queueTableAncestors(const render::PageView& view, std::size_t index, const render::PageView& alive,
                         std::unordered_set<std::string>& queued, std::vector<std::string>& refresh)
{
    for (std::size_t p = view.states()[index].parent; p != render::CellState::npos; p = view.states()[p].parent) {
        const render::CellState& ancestor = view.states()[p];
        if (isTablePart(ancestor) && alive.find(ancestor.id) != nullptr && queued.insert(ancestor.id).second)
            refresh.push_back(ancestor.id);
    }
}

// Compares two builds of the same page by cell id. Edges re-routed because a
// terminal moved are not in the ChangeSet but differ here.
ProjectionDiff diffViews(const render::PageView& before, const render::PageView& after)
{
    ProjectionDiff diff;
    for (const render::CellState& s : after.states()) {
        const render::CellState* old = before.find(s.id);
        if (old == nullptr)
            diff.added.push_back(s.id);
        else if (!sameVisual(*old, s))
            diff.refresh.push_back(s.id);
    }

    // Ancestors precede their descendants in paint order, so the queued set
    // doubles as the set of changed states to walk from.
    std::unordered_set<std::string> queued(diff.added.begin(), diff.added.end());
    queued.insert(diff.refresh.begin(), diff.refresh.end());
    const auto& afterStates = after.states();
    for (std::size_t i = 0; i < afterStates.size(); ++i) {
        if (queued.contains(afterStates[i].id))
            queueTableAncestors(after, i, after, queued, diff.refresh);
    }
    // The tables above a state that vanished.
    const auto& beforeStates = before.states();
    for (std::size_t i = 0; i < beforeStates.size(); ++i) {
        if (after.find(beforeStates[i].id) == nullptr)
            queueTableAncestors(before, i, after, queued, diff.refresh);
    }
    return diff;
}

// The diff of an in-place update (PageView::update): only the rebuilt states
// can differ, and the tree is the same.
ProjectionDiff diffUpdate(const render::PageView& view, const render::PageView::Update& update)
{
    ProjectionDiff diff;
    std::unordered_set<std::string> queued;
    std::vector<std::size_t> changedIndices;
    for (std::size_t k = 0; k < update.indices.size(); ++k) {
        const std::size_t i = update.indices[k];
        if (sameVisual(update.before[k], view.states()[i]))
            continue;
        diff.refresh.push_back(view.states()[i].id);
        queued.insert(view.states()[i].id);
        changedIndices.push_back(i);
    }
    for (const std::size_t i : changedIndices)
        queueTableAncestors(view, i, view, queued, diff.refresh);
    return diff;
}

// Recreates the renderer when the session's render context changed (the
// image base directory changes after open/save-as). True when it did.
bool syncRenderer(render::SceneRenderer& renderer, const render::RenderContext& wanted)
{
    const render::ImageResolveContext& have = renderer.context().images;
    if (have.baseDirectory == wanted.images.baseDirectory && have.allowLocalFiles == wanted.images.allowLocalFiles)
        return false;
    renderer = render::SceneRenderer(wanted);
    return true;
}

} // namespace

DiagramScene::DiagramScene(DocumentSession& session, QObject* parent)
    : QGraphicsScene(parent), session_(session), renderer_(session.renderContext())
{
    // The cells are not scene items: they are painted from the render
    // snapshot and hit tested through hitGrid_. The scene stays for the
    // coordinates and for the rotated label editor's proxy widget, so it needs
    // no index and an explicit scene rectangle (an unset one only ever grows).
    setItemIndexMethod(QGraphicsScene::NoIndex);
    connect(&session_, &DocumentSession::documentChanged, this,
            [this](const commands::ChangeSet& changes, commands::ChangeOrigin) { applyChanges(changes); });
    connect(&session_, &DocumentSession::currentPageChanged, this, [this] { rebuild(); });
    connect(&session_, &DocumentSession::renderContextChanged, this, [this] { rebuild(); });
    rebuild();
}

DiagramScene::~DiagramScene() = default;

std::vector<std::uint32_t> DiagramScene::statesIn(const QRectF& rect) const
{
    std::vector<std::uint32_t> candidates;
    hitGrid_.query(rect, candidates);
    return candidates;
}

std::vector<std::string> DiagramScene::cellsAt(QPointF p, double tolerance) const
{
    std::vector<std::string> hits;
    const core::Page* page = session_.currentPage();
    if (page == nullptr)
        return hits;
    // Candidates from the grid, topmost (last painted) first; the exact shape
    // tests follow.
    const double reach = std::max(tolerance, kHitProbeMinimum);
    const QRectF probe(p.x() - reach, p.y() - reach, 2 * reach, 2 * reach);
    const std::vector<std::uint32_t> candidates = statesIn(probe);
    const auto& states = view_.states();
    for (auto it = candidates.rbegin(); it != candidates.rend(); ++it) {
        const render::CellState& s = states[*it];
        // The grid holds a box that also serves the rubber band: a point hit
        // only considers the paint bounds.
        if (!intersectsClosed(candidateBox(s), probe))
            continue;
        const bool hit = s.kind == render::CellState::Kind::Vertex ? vertexContains(s, p, tolerance)
                                                                   : edgeContains(s, p, tolerance);
        if (hit && isCellSelectable(*page, s.id))
            hits.push_back(s.id);
    }
    return hits;
}

std::string DiagramScene::cellAt(QPointF p, double tolerance,
                                 const std::function<bool(const render::CellState&)>& filter) const
{
    for (const std::string& id : cellsAt(p, tolerance)) {
        const render::CellState* s = state(id);
        if (s != nullptr && (!filter || filter(*s)))
            return id;
    }
    return {};
}

std::vector<std::string> DiagramScene::cellsInRect(const QRectF& rect, std::string_view scope) const
{
    std::vector<std::string> result;
    const core::Page* page = session_.currentPage();
    if (page == nullptr)
        return result;
    // A cell inside `rect` has its selection box inside it, so the grid's
    // candidates (boxes that meet `rect`) include every one.
    std::vector<std::uint32_t> candidates;
    hitGrid_.query(rect, candidates);
    const auto& states = view_.states();
    for (const std::uint32_t index : candidates) {
        const render::CellState& s = states[index];
        if (!containsInclusive(rect, selectionBox(s)) || !isCellSelectable(*page, s.id))
            continue;
        if (ancestorInScope(*page, s.id, scope) != s.id)
            continue;
        result.push_back(s.id);
    }
    return result;
}

QRectF DiagramScene::pageArea() const
{
    const core::Page* page = session_.currentPage();
    if (page == nullptr)
        return {};
    std::optional<QRectF> content;
    if (!view_.states().empty())
        content = toQRect(view_.bounds());
    return canvas::pageArea(*page, content);
}

QRectF DiagramScene::boundsOf(const std::vector<std::string>& ids) const
{
    bool any = false;
    for (const std::string& id : ids)
        any = any || view_.find(id) != nullptr;
    return any ? toQRect(view_.boundsOf(ids)) : QRectF();
}

render::PageDecor DiagramScene::decor() const
{
    render::PageDecor decor;
    const core::Page* page = session_.currentPage();
    if (page == nullptr)
        return decor;
    decor.hasPage = true;
    decor.background = view_.theme().pageBackground(page->background());
    decor.dark = view_.theme().dark;
    decor.pageVisible = page->pageVisible();
    decor.pageArea = pageArea();
    decor.pageTile = canvas::pageSize(*page);
    decor.gridEnabled = page->gridEnabled();
    decor.gridSize = page->gridSize();
    return decor;
}

void DiagramScene::publishSnapshot(const std::unordered_set<std::string>* changed, bool everything)
{
    render::SnapshotInput input;
    input.view = &view_;
    input.renderer = &renderer_;
    input.decor = decor();
    input.hiddenLabel = hiddenLabel_;
    if (!everything) {
        input.previous = snapshot_.get();
        input.changed = changed;
    }
    if (everything)
        excluded_.clear(); // a page switch or rebuild: nothing is lifted from the new content
    std::shared_ptr<const render::RenderSnapshot> next = render::buildSnapshot(input);
    std::shared_ptr<const render::RenderSnapshot> shown = excluded_.empty() ? next : next->withoutRecords(excluded_);
    render::Damage damage =
        everything ? render::Damage{true, {}} : render::computeDamage(tileSnapshot_.get(), *shown);
    snapshot_ = std::move(next);
    tileSnapshot_ = std::move(shown);
    if (!damage.empty())
        emit contentDamaged(damage);
}

void DiagramScene::setExcluded(std::unordered_set<std::string> ids)
{
    if (ids == excluded_ || !snapshot_)
        return;
    excluded_ = std::move(ids);
    std::shared_ptr<const render::RenderSnapshot> shown = excluded_.empty() ? snapshot_ : snapshot_->withoutRecords(excluded_);
    const render::Damage damage = render::computeDamage(tileSnapshot_.get(), *shown);
    tileSnapshot_ = std::move(shown);
    if (!damage.empty())
        emit contentDamaged(damage);
}

void DiagramScene::rebuild()
{
    ++revision_;
    syncRenderer(renderer_, session_.renderContext());
    const core::Page* page = session_.currentPage();
    view_ = page != nullptr ? render::PageView::build(*page, session_.pageRenderContext()) : render::PageView();
    indexStates();
    publishSnapshot(nullptr, true);
    emit projectionChanged();
}

void DiagramScene::setHiddenLabel(std::string_view id)
{
    if (hiddenLabel_ == id)
        return;
    hiddenLabel_ = std::string(id);
    // Only the cell that showed its label and the one that hides it are
    // recorded again (the snapshot shares every other record).
    publishSnapshot(nullptr, false);
}

void DiagramScene::applyChanges(const commands::ChangeSet& changes)
{
    const core::Page* page = session_.currentPage();
    // Structural document changes (pages added, removed, reordered) can move
    // the current page under the index; a changed image base directory
    // invalidates every rendered image. Both rebuild.
    if (page == nullptr || !changes.pagesAdded().empty() || !changes.pagesRemoved().empty()
        || changes.pagesReorderedFlag() || syncRenderer(renderer_, session_.renderContext())) {
        rebuild();
        return;
    }
    const auto found = changes.pages().find(page->id());
    if (found == changes.pages().end())
        return; // another page changed
    const commands::ChangeSet::PageChanges& pageChanges = found->second;
    if (pageChanges.reset) {
        rebuild();
        return;
    }

    ++revision_;
    // An edit that changed only the content of cells (no cell added, removed,
    // reordered or re-parented, no page property) patches the view in place:
    // the states of the changed cells and of what depends on them. Anything
    // else builds the view again and compares it with the old one. Page
    // properties (grid, background, page size) change the decoration, which
    // the snapshot compares itself; cells are recorded again only when their
    // own state differs.
    const bool contentOnly = pageChanges.added.empty() && pageChanges.removed.empty()
        && pageChanges.reordered.empty() && pageChanges.childrenChanged.empty() && !pageChanges.pagePropertiesChanged
        && !pageChanges.changed.empty();
    std::optional<ProjectionDiff> diff;
    if (contentOnly) {
        if (auto update = view_.update(*page, session_.pageRenderContext(), pageChanges.changed)) {
            QRAW_PERF_SCOPE(DiffViews);
            diff = diffUpdate(view_, *update);
        }
    }
    if (!diff) {
        render::PageView next = render::PageView::build(*page, session_.pageRenderContext());
        QRAW_PERF_SCOPE(DiffViews);
        diff = diffViews(view_, next);
        view_ = std::move(next);
    }
    indexStates();
    std::unordered_set<std::string> changed;
    changed.insert(diff->added.begin(), diff->added.end());
    changed.insert(diff->refresh.begin(), diff->refresh.end());
    publishSnapshot(&changed, false);
    emit projectionChanged();
}

void DiagramScene::indexStates()
{
    // The hit grid of the new view: one box per state, the paint index as the
    // item, so a query comes back in z-order. Read-only until the next change.
    std::vector<QRectF> boxes;
    boxes.reserve(view_.states().size());
    for (const render::CellState& s : view_.states())
        boxes.push_back(indexedBox(s));
    hitGrid_.build(std::move(boxes));
    // The scene rectangle: the page area (the canvas adds its own margin).
    setSceneRect(pageArea());
}

void DiagramScene::render(QPainter* painter, const QRectF& target, const QRectF& source,
                          Qt::AspectRatioMode aspectRatioMode)
{
    // QGraphicsScene::render's mapping of `source` onto `target`, painting the
    // snapshot (backdrop, page, grid and the replayed cells) instead of the
    // scene's items, which paint nothing.
    if (painter == nullptr || painter->device() == nullptr || !snapshot_)
        return;
    QRectF targetRect = target;
    if (targetRect.isNull())
        targetRect = QRectF(0, 0, painter->device()->width(), painter->device()->height());
    const QRectF sourceRect = source.isNull() ? sceneRect() : source;
    if (targetRect.isEmpty() || sourceRect.isEmpty())
        return;
    double xratio = targetRect.width() / sourceRect.width();
    double yratio = targetRect.height() / sourceRect.height();
    switch (aspectRatioMode) {
    case Qt::KeepAspectRatio:
        xratio = yratio = std::min(xratio, yratio);
        break;
    case Qt::KeepAspectRatioByExpanding:
        xratio = yratio = std::max(xratio, yratio);
        break;
    case Qt::IgnoreAspectRatio:
        break;
    }
    painter->save();
    painter->setClipRect(targetRect, Qt::IntersectClip);
    painter->translate(targetRect.center());
    painter->scale(xratio, yratio);
    painter->translate(-sourceRect.center());
    render::SnapshotPaint options;
    options.gridZoom = painter->worldTransform().m11();
    options.deviceScale = std::hypot(painter->worldTransform().m11(), painter->worldTransform().m12())
        * painter->device()->devicePixelRatioF();
    const double slack = render::kAntialiasSlackPx / std::min(xratio, yratio);
    render::paintSnapshot(*painter, *snapshot_, sourceRect.adjusted(-slack, -slack, slack, slack), options);
    painter->restore();
}

} // namespace qraw::ui
