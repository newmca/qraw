// The Insert menu's model edits. Upstream references are relative to the
// draw.io webapp directory: js/grapheditor/Actions.js 'insertLink' (:901) and
// 'image' (:2117), Graph.createTable (js/grapheditor/Graph.js:24607),
// Sidebar.js (the line templates).
#include "qraw/ui/insert_actions.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/core/page.h"
#include "qraw/render/stylesheet.h"
#include "qraw/ui/cell_queries.h"
#include "qraw/ui/cell_transfer.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/edit_actions.h"
#include "qraw/ui/image_embed.h"
#include "qraw/ui/label_autosize.h"
#include "qraw/ui/data_actions.h"

#include <QCoreApplication>
#include <QImage>
#include <QMimeData>

#include <algorithm>
#include <cmath>

namespace qraw::ui::actions {

namespace {

using core::Cell;
using core::Rect;

QString name(const char* source)
{
    return QCoreApplication::translate("qraw::ui::actions", source);
}

// Ids inside a bundle only have to be unique there: insertBundle gives the cells fresh ones.
constexpr const char* kBundleParent = "";

CellBundle single(Cell cell)
{
    CellBundle bundle;
    bundle.cells.push_back(std::move(cell));
    return bundle;
}

// Sidebar.js, 'Horizontal Line' and 'Vertical Line'.
constexpr char kHorizontalLineStyle[] = "line;strokeWidth=2;html=1;";
constexpr char kVerticalLineStyle[] = "line;strokeWidth=2;direction=south;html=1;";
constexpr double kLineLength = 160;
constexpr double kLineThickness = 10;

// Actions.js 'insertLink'.
constexpr char kLinkStyle[] = "fontColor=#0000EE;fontStyle=4;rounded=1;overflow=hidden;spacing=10;";
constexpr double kLinkWidth = 100;
constexpr double kLinkHeight = 40;

// Actions.js 'image' (a new cell) and EditorUi.importFiles (a pasted image).
constexpr char kImageStyle[] = "shape=image;imageAspect=0;aspect=fixed;verticalLabelPosition=bottom;verticalAlign=top;";
constexpr char kPastedImageStyle[] =
    "shape=image;verticalLabelPosition=bottom;labelBackgroundColor=default;verticalAlign=top;aspect=fixed;imageAspect=0;";
constexpr QSize kUnknownImageSize{120, 120};

// Graph.createTable: the sizes and styles of a table without a title and with
// collapsed borders (Graph.getDefaultTableSides is empty for those).
constexpr double kCellWidth = 60;
constexpr double kCellHeight = 40;
constexpr char kTableStyle[] = "shape=table;startSize=0;container=1;collapsible=0;childLayout=tableLayout;fixedHeader=1;tableRender=collapsed;";
constexpr char kRowStyle[] = "shape=tableRow;horizontal=0;startSize=0;swimlaneHead=0;swimlaneBody=0;strokeColor=inherit;"
                             "collapsible=0;dropTarget=0;fillColor=none;points=[[0,0.5],[1,0.5]];portConstraint=eastwest;fixedHeader=1;";
constexpr char kTableCellStyle[] = "shape=partialRectangle;html=1;whiteSpace=wrap;connectable=0;strokeColor=inherit;"
                                   "overflow=hidden;fillColor=none;pointerEvents=1;";

// Graph.getLinkTitle: what follows the last '/'. An address that ends in '/' or
// has none would give an empty label, so the address itself stands in then.
QString linkTitle(const DocumentSession& session, const std::string& link)
{
    if (const std::optional<std::string> pageId = pageIdOfLink(link)) {
        const core::Document& document = session.document();
        for (std::size_t i = 0; i < document.pageCount(); ++i) {
            if (document.page(i)->id() == *pageId)
                return QString::fromStdString(document.page(i)->name());
        }
        return linkDisplayText(session, link);
    }
    const QString text = QString::fromStdString(link);
    const QString last = text.mid(text.lastIndexOf(QLatin1Char('/')) + 1);
    return last.isEmpty() ? text : last;
}

// The table of Graph.createTable as detached cells for insertBundle.
CellBundle tableBundle(int rows, int columns)
{
    CellBundle bundle;
    const double width = columns * kCellWidth;
    bundle.cells.push_back(
        Cell::makeVertex("table", kBundleParent, "", kTableStyle, {0, 0, width, rows * kCellHeight}));
    for (int r = 0; r < rows; ++r) {
        const std::string row = "row" + std::to_string(r);
        bundle.cells.push_back(Cell::makeVertex(row, "table", "", kRowStyle, {0, r * kCellHeight, width, kCellHeight}));
        for (int c = 0; c < columns; ++c) {
            bundle.cells.push_back(Cell::makeVertex(row + "c" + std::to_string(c), row, "", kTableCellStyle,
                                                    {c * kCellWidth, 0, kCellWidth, kCellHeight}));
        }
    }
    return bundle;
}

} // namespace

QSizeF insertedImageSize(QSize natural)
{
    if (!natural.isValid() || natural.isEmpty())
        natural = kUnknownImageSize;
    const double scale = std::min(1.0, std::min(kMaxInsertedImageSize / natural.width(), kMaxInsertedImageSize / natural.height()));
    return {std::max(1.0, std::round(natural.width() * scale)), std::max(1.0, std::round(natural.height() * scale))};
}

bool canInsert(const DocumentSession& session)
{
    const core::Page* page = session.currentPage();
    const std::string parent = session.insertionParent();
    return page != nullptr && !parent.empty() && !isCellLocked(*page, parent);
}

bool insertLine(DocumentSession& session, bool vertical, QPointF at)
{
    if (!canInsert(session))
        return false;
    const Rect bounds = vertical ? Rect{0, 0, kLineThickness, kLineLength} : Rect{0, 0, kLineLength, kLineThickness};
    return insertBundle(session,
                        single(Cell::makeVertex("line", kBundleParent, "", vertical ? kVerticalLineStyle : kHorizontalLineStyle, bounds)),
                        at, vertical ? name(QT_TRANSLATE_NOOP("qraw::ui::actions", "Insert Vertical Line"))
                                     : name(QT_TRANSLATE_NOOP("qraw::ui::actions", "Insert Horizontal Line")));
}

bool insertLinkCell(DocumentSession& session, const std::string& link, bool newWindow, QPointF at)
{
    const QString trimmed = QString::fromStdString(link).trimmed();
    if (trimmed.isEmpty() || !canInsert(session))
        return false;
    const std::string address = trimmed.toStdString();
    Cell cell = Cell::makeVertex("link", kBundleParent, linkTitle(session, address).toStdString(), kLinkStyle,
                                 {0, 0, kLinkWidth, kLinkHeight});
    // The label decides the size (graph.cellSizeUpdated), with the label editor's resolution.
    const render::RenderContext& context = session.renderContext();
    const render::Stylesheet& sheet =
        context.stylesheet != nullptr ? *context.stylesheet : render::Stylesheet::drawioDefault();
    render::ResolveOptions options;
    options.theme = context.theme;
    const core::Page* page = session.currentPage();
    const label::GridSnap grid{page->gridEnabled(), page->gridSize()};
    const render::ResolvedStyle style = sheet.resolve(cell.style(), render::CellKind::Vertex, options);
    if (const auto fitted = label::preferredBounds(style, cell.geometry()->bounds(), cell.value(), grid))
        cell.geometry()->setBounds(*fitted);
    cell.setData("link", address);
    if (newWindow)
        cell.setData("linkTarget", "_blank");
    return insertBundle(session, single(std::move(cell)), at, name(QT_TRANSLATE_NOOP("qraw::ui::actions", "Insert Link")));
}

bool insertImage(DocumentSession& session, const std::string& image, QSizeF size, QPointF at, bool pasted)
{
    if (image.empty() || !canInsert(session))
        return false;
    // One copy of the payload: the style text is built once and then shared by the document.
    std::string style = pasted ? kPastedImageStyle : kImageStyle;
    style += "image=";
    style += image;
    return insertBundle(session,
                        single(Cell::makeVertex("image", kBundleParent, "", style, {0, 0, size.width(), size.height()})),
                        at, name(QT_TRANSLATE_NOOP("qraw::ui::actions", "Insert Image")));
}

bool insertClipboardImage(DocumentSession& session, const QMimeData& mime, QPointF at, QString* error)
{
    if (!mime.hasImage() || !canInsert(session))
        return false;
    const std::optional<EmbeddedImage> embedded = embedQImage(qvariant_cast<QImage>(mime.imageData()), error);
    return embedded && insertImage(session, embedded->value, insertedImageSize(embedded->size), at, true);
}

bool insertTable(DocumentSession& session, int rows, int columns, QPointF at)
{
    if (rows < 1 || columns < 1 || rows > kMaxTableSize || columns > kMaxTableSize || !canInsert(session))
        return false;
    return insertBundle(session, tableBundle(rows, columns), at, name(QT_TRANSLATE_NOOP("qraw::ui::actions", "Insert Table")));
}

bool insertImported(DocumentSession& session, const commands::imports::Result& result, QPointF at, const QString& name)
{
    if (result.cells.empty() || !canInsert(session))
        return false;
    CellBundle bundle;
    bundle.cells = result.cells;
    return insertBundle(session, bundle, at, name);
}

commands::imports::LabelMeasure importLabelMeasure(const DocumentSession& session)
{
    return [&session](std::string_view styleText, std::string_view label) -> core::Point {
        const render::RenderContext& context = session.renderContext();
        const render::Stylesheet& sheet =
            context.stylesheet != nullptr ? *context.stylesheet : render::Stylesheet::drawioDefault();
        render::ResolveOptions options;
        options.theme = context.theme;
        // The natural size: a label that wraps would be measured at the width of an empty cell.
        core::Style style{styleText};
        style.remove("whiteSpace");
        const render::ResolvedStyle resolved = sheet.resolve(style, render::CellKind::Vertex, options);
        const auto fitted = label::preferredBounds(resolved, {0, 0, 0, 0}, label, label::GridSnap{});
        return fitted ? core::Point{fitted->width, fitted->height} : core::Point{0, 0};
    };
}

} // namespace qraw::ui::actions
