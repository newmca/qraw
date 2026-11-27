// Inline label editing (mxCellEditor, mxgraph/src/view/mxCellEditor.js).
//
// A LabelTextEdit is placed over the label box of the edited cell and kept in
// place on zoom and scroll. The editor is transparent and the cell's own
// label is not painted while it is edited (DiagramScene::setHiddenLabel), as
// in draw.io. The model is touched once, on commit, through
// DocumentSession::edit: ops::setLabel plus the autosize of the cell.
//
// Two ways to host the widget, chosen when editing starts:
//  * unrotated labels: a child widget of the canvas viewport, in screen
//    pixels (font scaled with the zoom, re-laid out on zoom and scroll);
//  * rotated labels (cell rotation, horizontal=0): a QGraphicsProxyWidget in
//    the scene, in page units, turned about the label anchor like the
//    painted label. A QWidget child cannot be rotated; a QGraphicsTextItem
//    would need its own copy of the HTML subset, key handling and focus
//    rules that LabelTextEdit already has. The view forwards mouse and key
//    events to the scene while such an editor is open (CanvasView).
//
// A double click on blank canvas edits a pending text cell that is not in the
// model; it is inserted on commit, as one "Insert Text" edit.
#include "qraw/ui/label_editor.h"

#include "edge_handles.h"
#include "qraw/ui/label_autosize.h"
#include "label_html.h"
#include "label_text_edit.h"

#include "qraw/commands/default_style.h"
#include "qraw/commands/ops_cells.h"
#include "qraw/commands/ops_properties.h"
#include "qraw/commands/queries.h"
#include "qraw/render/stylesheet.h"
#include "qraw/ui/app_settings.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/snapping.h"

#include <QAbstractTextDocumentLayout>
#include <QGraphicsProxyWidget>
#include <QKeyEvent>
#include <QPalette>
#include <QPointer>
#include <QScrollBar>
#include <QTextDocument>
#include <QTextFrame>
#include <QTextOption>

#include <algorithm>
#include <cmath>
#include <optional>

namespace qraw::ui {

namespace {

// Graph.insertTextForEvent (js/grapheditor/Graph.js:39167).
constexpr char kTextStyle[] =
    "text;html=1;align=center;verticalAlign=middle;resizable=0;points=[];autosize=1;strokeColor=none;fillColor=none;";
constexpr char kTextValue[] = "Text";
constexpr double kTextWidth = 40;
constexpr double kTextHeight = 20;

// Edge labels have no box of their own: the editor is at least this big
// (screen pixels) around the label anchor.
constexpr double kMinEdgeEditorWidth = 60;
constexpr double kMinEdgeEditorHeight = 20;
constexpr double kMinEditorWidth = 24;
constexpr double kDocumentMargin = 2;

QFont fontFor(const render::ResolvedStyle& style, double zoom)
{
    QStringList families;
    const QString list = QString::fromUtf8(style.fontFamily().data(), static_cast<qsizetype>(style.fontFamily().size()));
    for (QString family : list.split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        family = family.trimmed();
        if (family.size() >= 2 && (family.startsWith(QLatin1Char('"')) || family.startsWith(QLatin1Char('\''))))
            family = family.mid(1, family.size() - 2);
        if (!family.isEmpty())
            families << family;
    }
    QFont font;
    if (!families.isEmpty())
        font.setFamilies(families);
    font.setPixelSize(std::max(1, static_cast<int>(std::lround(style.fontSize() * zoom))));
    font.setHintingPreference(QFont::PreferNoHinting); // as the canvas text (render::diagramFont)
    const int bits = style.fontStyle();
    font.setBold(bits & render::FontBold);
    font.setItalic(bits & render::FontItalic);
    font.setUnderline(bits & render::FontUnderline);
    font.setStrikeOut(bits & render::FontStrikethrough);
    return font;
}

Qt::Alignment alignmentFor(geometry::HAlign align)
{
    switch (align) {
    case geometry::HAlign::Left: return Qt::AlignLeft;
    case geometry::HAlign::Right: return Qt::AlignRight;
    case geometry::HAlign::Center: break;
    }
    return Qt::AlignHCenter;
}

QPointF routeMiddle(const std::vector<core::Point>& route)
{
    if (route.empty())
        return {};
    if (route.size() == 1)
        return {route.front().x, route.front().y};
    const std::size_t segment = (route.size() - 1) / 2;
    return {(route[segment].x + route[segment + 1].x) / 2, (route[segment].y + route[segment + 1].y) / 2};
}

} // namespace

// What an edit session needs to know about its subject, whether it is a cell
// of the page or a text cell still to be inserted.
struct LabelEditor::Impl {
    // A text cell that exists only in the editor until it is committed.
    struct Pending {
        std::string parent;
        core::Rect bounds; // relative to `parent`
        QPointF center;    // scene
        std::string style; // the style of the new cell: kTextStyle with the default style applied
    };
    // Where the label box lies, in scene units; `rotation` turns it about `anchor`.
    struct Placement {
        QRectF box;
        QPointF anchor;
        double rotation = 0;
    };

    explicit Impl(LabelEditor& editor) : owner(editor) {}

    LabelEditor& owner;
    QPointer<LabelTextEdit> edit;
    QPointer<QGraphicsProxyWidget> proxy; // set when the editor lives in the scene (rotated label)
    std::optional<Pending> pending;
    render::ResolvedStyle style;
    std::size_t page = 0;
    bool html = false;
    bool isEdge = false;
    bool wrap = false;
    double zoom = 1; // the zoom the document is laid out at (1 in the scene)
    std::string baseline; // the text as loaded (plain), or its re-export (html)
    QColor textColor = Qt::black;
    geometry::HAlign align = geometry::HAlign::Center;
    geometry::VAlign verticalAlign = geometry::VAlign::Middle;
    std::vector<QMetaObject::Connection> connections;
    bool inLayout = false;

    bool begin(const std::string& id);
    bool beginPending(const Pending& text);
    bool open(const std::string& value, bool rotated);
    [[nodiscard]] std::optional<Placement> placement() const;
    void relayout();
    [[nodiscard]] std::string currentText() const;
    void finish(bool commit);
    void commitEdit(const std::string& id, const std::string& text);
    void commitPending(const std::string& text);
    void teardown();
    [[nodiscard]] label::GridSnap grid() const;
};

label::GridSnap LabelEditor::Impl::grid() const
{
    const core::Page* current = owner.context_.session->currentPage();
    return current != nullptr ? label::GridSnap{current->gridEnabled(), current->gridSize()} : label::GridSnap{};
}

bool LabelEditor::Impl::begin(const std::string& id)
{
    DocumentSession* session = owner.context_.session;
    DiagramScene* scene = owner.context_.scene;
    const core::Page* current = session->currentPage();
    const core::Cell* cell = current != nullptr ? current->findCell(id) : nullptr;
    const render::CellState* state = scene->state(id);
    if (cell == nullptr || state == nullptr || isCellLocked(*current, id) || !state->style.flag("editable", true))
        return false;

    pending.reset();
    style = state->style;
    isEdge = state->kind == render::CellState::Kind::Edge;
    const bool rotated = state->label && std::abs(std::remainder(state->label->rotation, 360.0)) > 1e-6;
    if (!open(cell->value(), rotated))
        return false;
    scene->setHiddenLabel(id);
    return true;
}

bool LabelEditor::Impl::beginPending(const Pending& text)
{
    DocumentSession* session = owner.context_.session;
    const render::RenderContext& context = session->renderContext();
    const render::Stylesheet& sheet = context.stylesheet != nullptr ? *context.stylesheet
                                                                     : render::Stylesheet::drawioDefault();
    render::ResolveOptions options;
    options.theme = context.theme;
    // The new text takes the default style (graph 'textInserted' -> pasteCellStyles).
    pending = text;
    pending->style = commands::applyDefaultStyle(core::Style(kTextStyle), AppSettings().defaultStyles(),
                                                 commands::StyleTarget::Vertex)
                         .toString();
    style = sheet.resolve(pending->style, render::CellKind::Vertex, options);
    isEdge = false;
    return open(kTextValue, false);
}

// Builds the widget for the subject described by the members set by begin().
bool LabelEditor::Impl::open(const std::string& value, bool rotated)
{
    DocumentSession* session = owner.context_.session;
    DiagramScene* scene = owner.context_.scene;
    CanvasView* view = owner.context_.view;

    page = session->currentPageIndex();
    html = style.html();
    wrap = style.wrap();
    zoom = rotated ? 1 : view->zoom();
    const geometry::LabelParams params = style.labelParams();
    align = params.align;
    verticalAlign = params.verticalAlign;
    textColor = style.fontColor().value_or(QColor(Qt::black));

    // The editor is transparent: the shape below stays visible (the cell's own
    // label is hidden while it is edited), the text keeps the label's colour.
    auto* widget = new LabelTextEdit(html, rotated ? nullptr : view->viewport());
    edit = widget;
    QPalette palette = widget->palette();
    palette.setColor(QPalette::Text, textColor);
    widget->setPalette(palette);
    widget->setLineWrapMode(wrap ? QTextEdit::WidgetWidth : QTextEdit::NoWrap);

    QTextDocument* document = widget->document();
    document->setDefaultFont(fontFor(style, zoom));
    document->setDocumentMargin(kDocumentMargin);
    QTextOption option = document->defaultTextOption();
    option.setAlignment(alignmentFor(align));
    document->setDefaultTextOption(option);

    if (html) {
        label::loadHtml(*document, value, zoom);
        baseline = label::exportHtml(*document, zoom, textColor);
    } else {
        widget->setPlainText(QString::fromUtf8(value.data(), static_cast<qsizetype>(value.size())));
        baseline = widget->toPlainText().toStdString();
    }

    if (rotated) {
        proxy = scene->addWidget(widget);
        proxy->setZValue(1e9);
        proxy->setFlag(QGraphicsItem::ItemIsFocusable);
    }

    connections.push_back(QObject::connect(widget, &LabelTextEdit::commitRequested, &owner, [this] { finish(true); }));
    connections.push_back(QObject::connect(widget, &LabelTextEdit::cancelRequested, &owner, [this] { finish(false); }));
    connections.push_back(QObject::connect(widget, &LabelTextEdit::focusLost, &owner, [this] { finish(true); }));
    connections.push_back(QObject::connect(document->documentLayout(), &QAbstractTextDocumentLayout::documentSizeChanged,
                                           &owner, [this] { relayout(); }));
    connections.push_back(QObject::connect(view, &CanvasView::zoomChanged, &owner, [this](double newZoom) {
        if (edit == nullptr || proxy != nullptr) // the scene editor is scaled by the view
            return;
        const double ratio = newZoom / zoom;
        zoom = newZoom;
        QFont font = edit->document()->defaultFont();
        font.setPixelSize(std::max(1, static_cast<int>(std::lround(font.pixelSize() * ratio))));
        edit->document()->setDefaultFont(font);
        label::scaleExplicitSizes(*edit->document(), ratio);
        relayout();
    }));
    for (QScrollBar* bar : {view->horizontalScrollBar(), view->verticalScrollBar()})
        connections.push_back(QObject::connect(bar, &QScrollBar::valueChanged, &owner, [this] { relayout(); }));
    connections.push_back(QObject::connect(session, &DocumentSession::pendingEditsRequested, &owner,
                                           [this] { finish(true); }));
    // A page switch commits, a replaced document cancels.
    connections.push_back(QObject::connect(session, &DocumentSession::currentPageChanged, &owner,
                                           [this] { finish(true); }));
    connections.push_back(QObject::connect(
        session, &DocumentSession::documentChanged, &owner,
        [this](const commands::ChangeSet&, commands::ChangeOrigin origin) {
            if (origin == commands::ChangeOrigin::Reset)
                finish(false);
        }));

    relayout();
    if (proxy != nullptr)
        proxy->setFocus(Qt::OtherFocusReason);
    else
        widget->show();
    widget->setFocus(Qt::OtherFocusReason);
    widget->selectAll();
    return true;
}

std::optional<LabelEditor::Impl::Placement> LabelEditor::Impl::placement() const
{
    if (pending) {
        const QRectF box(pending->center.x() - pending->bounds.width / 2, pending->center.y() - pending->bounds.height / 2,
                         pending->bounds.width, pending->bounds.height);
        return Placement{box, box.center(), 0};
    }
    const render::CellState* state = owner.context_.scene->state(owner.cellId_);
    if (state == nullptr)
        return std::nullopt;
    const double rotation = state->label ? state->label->rotation : 0;
    if (isEdge) {
        // Around the label anchor, or the middle of the edge without label;
        // the minimum size is in screen pixels.
        const QPointF anchor = state->label ? QPointF(state->label->x, state->label->y) : routeMiddle(state->points);
        const double pixel = 1 / owner.context_.view->zoom();
        const QSizeF size(kMinEdgeEditorWidth * pixel, kMinEdgeEditorHeight * pixel);
        return Placement{QRectF(anchor - QPointF(size.width() / 2, size.height() / 2), size), anchor, rotation};
    }
    core::Rect box = state->bounds;
    if (state->label && state->label->width > 0 && state->label->height > 0)
        box = state->label->rect();
    const QPointF anchor = state->label ? QPointF(state->label->x, state->label->y)
                                        : QPointF(box.x + box.width / 2, box.y + box.height / 2);
    return Placement{QRectF(box.x, box.y, box.width, box.height), anchor, rotation};
}

void LabelEditor::Impl::relayout()
{
    if (edit == nullptr || inLayout)
        return;
    const std::optional<Placement> where = placement();
    if (!where)
        return;
    inLayout = true;

    // The box in editor units: viewport pixels for the child widget, page
    // units for the scene editor (the view scales that one).
    CanvasView* view = owner.context_.view;
    const QRectF box = proxy != nullptr ? where->box : view->viewportTransform().mapRect(where->box);
    QTextDocument* document = edit->document();
    const double margin = 2 * kDocumentMargin;

    double width = std::max({box.width(), kMinEditorWidth / (proxy != nullptr ? view->zoom() : 1)});
    if (!wrap)
        width = std::max(width, document->idealWidth() + margin);
    edit->resize(static_cast<int>(std::ceil(width)), edit->height());

    // QTextEdit draws its text at the top: a top margin on the root frame
    // places it according to verticalAlign inside the label box.
    QTextFrame* root = document->rootFrame();
    QTextFrameFormat frameFormat = root->frameFormat();
    const double textHeight = std::max(1.0, document->size().height() - frameFormat.topMargin());
    double padding = 0;
    if (box.height() > textHeight) {
        if (verticalAlign == geometry::VAlign::Middle)
            padding = (box.height() - textHeight) / 2;
        else if (verticalAlign == geometry::VAlign::Bottom)
            padding = box.height() - textHeight;
    }
    if (std::abs(padding - frameFormat.topMargin()) > 0.5) {
        frameFormat.setTopMargin(padding);
        root->setFrameFormat(frameFormat);
    }
    const double height = std::max(box.height(), std::ceil(document->size().height()));
    edit->resize(static_cast<int>(std::ceil(width)), static_cast<int>(std::ceil(height)));

    // The alignment decides which point of the box stays fixed while the
    // text grows.
    double x = box.left();
    if (align == geometry::HAlign::Center)
        x = box.center().x() - width / 2;
    else if (align == geometry::HAlign::Right)
        x = box.right() - width;
    double y = box.top();
    if (verticalAlign == geometry::VAlign::Middle)
        y = box.center().y() - height / 2;
    else if (verticalAlign == geometry::VAlign::Bottom)
        y = box.bottom() - height;

    if (proxy != nullptr) {
        // Turn about the label anchor, like the painted label.
        proxy->setPos(x, y);
        proxy->setTransformOriginPoint(where->anchor - QPointF(x, y));
        proxy->setRotation(where->rotation);
    } else {
        edit->move(static_cast<int>(std::lround(x)), static_cast<int>(std::lround(y)));
    }

    inLayout = false;
}

std::string LabelEditor::Impl::currentText() const
{
    return html ? label::exportHtml(*edit->document(), zoom, textColor) : edit->toPlainText().toStdString();
}

void LabelEditor::Impl::teardown()
{
    for (const QMetaObject::Connection& connection : connections)
        QObject::disconnect(connection);
    connections.clear();
    if (owner.context_.scene != nullptr)
        owner.context_.scene->setHiddenLabel({});
    if (edit == nullptr)
        return;
    edit->blockSignals(true);
    if (proxy != nullptr) {
        // The proxy owns the widget and deletes it.
        proxy->hide();
        owner.context_.scene->removeItem(proxy);
        proxy->deleteLater();
        proxy = nullptr;
    } else {
        edit->hide();
        edit->deleteLater();
    }
    edit = nullptr;
}

// ops::setLabel and the autosize of the cell in one edit (mxGraph.cellLabelChanged
// with autoSize).
void LabelEditor::Impl::commitEdit(const std::string& id, const std::string& text)
{
    const label::GridSnap snap = grid();
    const render::ResolvedStyle editedStyle = style;
    const std::size_t editedPage = page;
    owner.context_.session->edit(owner.tr("Edit Label"), [&](commands::Transaction& tx) {
        commands::Result result = commands::ops::setLabel(tx, editedPage, id, text);
        if (!result.ok || isEdge)
            return result;
        const core::Page* current = tx.page(editedPage);
        const core::Cell* cell = current != nullptr ? current->findCell(id) : nullptr;
        const core::Geometry* geometry = cell != nullptr ? cell->geometry() : nullptr;
        if (geometry == nullptr || geometry->relative())
            return result;
        if (const auto bounds = label::autosizedBounds(editedStyle, geometry->bounds(), text, snap))
            return commands::ops::resizeCells(tx, editedPage, {id}, {*bounds});
        return result;
    });
}

// The pending text cell becomes a vertex: one "Insert Text" edit.
void LabelEditor::Impl::commitPending(const std::string& text)
{
    const Pending where = *pending;
    const label::GridSnap snap = grid();
    const render::ResolvedStyle textStyle = style;
    const std::size_t editedPage = page;
    owner.context_.session->edit(owner.tr("Insert Text"), [&](commands::Transaction& tx) {
        const core::Rect bounds = label::autosizedBounds(textStyle, where.bounds, text, snap).value_or(where.bounds);
        core::Cell cell = core::Cell::makeVertex(tx.newCellId(editedPage), where.parent, text, where.style, bounds);
        return commands::ops::addCells(tx, editedPage, {std::move(cell)});
    });
}

void LabelEditor::Impl::finish(bool commit)
{
    if (edit == nullptr)
        return;
    const std::string text = currentText();
    const std::string id = owner.cellId_;
    const bool inserted = pending.has_value();
    const bool changed = commit && (inserted ? !text.empty() : text != baseline);
    // The editor goes first: the edit below rebuilds the scene items.
    teardown();
    owner.cellId_.clear();

    if (changed) {
        if (inserted)
            commitPending(text);
        else
            commitEdit(id, text);
    }
    pending.reset();
    emit owner.editingFinished(QString::fromStdString(id), commit);
    if (owner.context_.view != nullptr)
        owner.context_.view->setFocus(Qt::OtherFocusReason);
}

LabelEditor::LabelEditor(const ToolContext& context, QObject* parent)
    : QObject(parent), context_(context), d(std::make_unique<Impl>(*this))
{
}

LabelEditor::~LabelEditor()
{
    if (d->edit != nullptr) {
        d->edit->blockSignals(true);
        if (d->proxy != nullptr)
            delete d->proxy; // owns the widget
        else
            delete d->edit;
    }
}

bool LabelEditor::startEditing(const std::string& id)
{
    stopEditing(false);
    if (context_.session == nullptr || context_.scene == nullptr || context_.view == nullptr)
        return false;
    cellId_ = id;
    if (!d->begin(id)) {
        cellId_.clear();
        return false;
    }
    emit editingStarted(QString::fromStdString(id));
    return true;
}

bool LabelEditor::insertTextAt(QPointF scenePos)
{
    stopEditing(false);
    if (context_.session == nullptr || context_.scene == nullptr || context_.view == nullptr)
        return false;
    const core::Page* current = context_.session->currentPage();
    if (current == nullptr)
        return false;
    Impl::Pending text;
    text.parent = context_.session->insertionParent();
    text.center = Snapper(*context_.scene, context_.view->guidesEnabled()).snap(scenePos);
    const core::Point origin = commands::origin(*current, text.parent);
    text.bounds = core::Rect{text.center.x() - kTextWidth / 2 - origin.x, text.center.y() - kTextHeight / 2 - origin.y,
                             kTextWidth, kTextHeight};
    cellId_.clear();
    if (!d->beginPending(text))
        return false;
    emit editingStarted(QString());
    return true;
}

void LabelEditor::stopEditing(bool cancel)
{
    d->finish(!cancel);
}

bool LabelEditor::sceneEditorAt(QPointF scenePos) const
{
    return d->proxy != nullptr && d->proxy->contains(d->proxy->mapFromScene(scenePos));
}

bool LabelEditor::isEditing() const
{
    return d->edit != nullptr;
}

std::unique_ptr<Gesture> LabelEditor::press(const CanvasEvent& event)
{
    (void)event;
    // Presses on the editor widget never get here (it is a child widget), so
    // any press that does ends the edit and then goes on to the other handlers.
    if (isEditing())
        stopEditing(false);
    return nullptr;
}

bool LabelEditor::doubleClick(const CanvasEvent& event)
{
    if (context_.session == nullptr || context_.scene == nullptr || context_.view == nullptr)
        return false;
    const core::Page* page = context_.session->currentPage();
    if (page == nullptr)
        return false;

    // A double click on a waypoint handle of the selected edge removes the
    // waypoint (edge handles, higher priority number but the same gesture).
    if (const auto handles = edgeh::EdgeHandlesHandler::selectedEdgeHandles(context_, event.pixels(1))) {
        const edgeh::Handle* handle = edgeh::hitHandle(*handles, event.scenePos, event.pixels(1));
        if (handle != nullptr && handle->kind == edgeh::HandleKind::Waypoint)
            return false;
    }

    const std::string hit = context_.scene->cellAt(event.scenePos, event.pixels(overlay::kHitTolerance));
    if (hit.empty()) {
        // Inside an entered group a double click on blank canvas leaves it
        // (select handler), outside it inserts a text vertex.
        if (!context_.session->enteredGroup().empty())
            return false;
        return insertTextAt(event.scenePos);
    }

    // Groups are entered (select handler), not edited; this also holds when
    // the first click of the double click has just selected the group.
    std::string target = ancestorInScope(*page, hit, context_.session->enteredGroup());
    const auto isGroup = [&](const std::string& id) {
        const core::Cell* cell = page->findCell(id);
        return cell != nullptr && hasChildren(*page, id) && cell->style().hasName("group");
    };
    if (!target.empty() && isGroup(target))
        return false;
    if (target.empty() || context_.session->selection().contains(target))
        target = hit;
    if (isGroup(target))
        return false;
    return startEditing(target);
}

bool LabelEditor::keyPress(QKeyEvent& event)
{
    if (isEditing()) {
        if (event.key() == Qt::Key_Escape) {
            stopEditing(true);
            return true;
        }
        return false;
    }
    const bool enter = event.key() == Qt::Key_Return || event.key() == Qt::Key_Enter;
    if (enter && (event.modifiers() & ~Qt::KeypadModifier) == Qt::NoModifier && context_.session != nullptr &&
        !context_.session->selection().isEmpty())
        return startEditing(context_.session->selection().first());
    return false;
}

void LabelEditor::reset()
{
    if (!isEditing())
        return;
    if (!d->pending && context_.scene->state(cellId_) == nullptr)
        stopEditing(true); // the cell is gone
    else
        d->relayout();
}

} // namespace qraw::ui
