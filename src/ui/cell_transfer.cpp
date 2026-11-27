// Cells as draw.io-compatible XML: clipboard, palette drag and templates.
#include "qraw/ui/cell_transfer.h"

#include "qraw/commands/ops_cells.h"
#include "qraw/commands/queries.h"
#include "qraw/commands/transaction.h"
#include "qraw/format/codec.h"
#include "qraw/core/geometry.h"
#include "qraw/core/number.h"
#include "qraw/ui/cell_queries.h"

#include <QMimeData>
#include <QString>
#include <QUrl>

#include <algorithm>
#include <cmath>
#include <unordered_map>
#include <unordered_set>

namespace qraw::ui {

namespace {

using core::Cell;
using core::Point;

constexpr char kEmptyModel[] =
    R"(<mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/></root></mxGraphModel>)";
constexpr char kExportLayer[] = "1";

// The absolute centre of `terminalId`'s geometry, or of its nearest ancestor
// with a usable geometry: the view-less terminal fallback of
// mxGraph.cellsRemoved (mxGraph.js:5279-5350), as in ops::removeCells.
std::optional<Point> absoluteTerminalCenter(const core::Page& page, const std::string& terminalId)
{
    for (const Cell* cell = page.findCell(terminalId); cell != nullptr; cell = page.findCell(cell->parent())) {
        const core::Geometry* geometry = cell->geometry();
        if (geometry == nullptr || geometry->relative() || cell->isEdge())
            continue;
        const Point origin = commands::geometryOrigin(page, cell->id());
        return Point{origin.x + geometry->x() + geometry->width() / 2, origin.y + geometry->y() + geometry->height() / 2};
    }
    return std::nullopt;
}

// Detaches the terminal of `cell` that is not part of the copied set and
// leaves a terminal point in the edge's own parent coordinates.
void detachTerminalOutside(const core::Page& page, Cell& cell, bool source, const std::unordered_set<std::string>& copied)
{
    const std::string terminal = source ? cell.source() : cell.target();
    if (terminal.empty() || copied.count(terminal) != 0)
        return;
    const std::optional<Point> center = absoluteTerminalCenter(page, terminal);
    if (center) {
        const Point edgeOrigin = commands::geometryOrigin(page, cell.id());
        const Point point{center->x - edgeOrigin.x, center->y - edgeOrigin.y};
        if (source)
            cell.ensureGeometry().setSourcePoint(point);
        else
            cell.ensureGeometry().setTargetPoint(point);
    }
    if (source)
        cell.setSource({});
    else
        cell.setTarget({});
}

std::vector<std::string> copiedTopLevelIds(const core::Page& page, const std::vector<std::string>& ids)
{
    std::vector<std::string> top = commands::sortByZOrder(page, commands::topmostCells(page, ids));
    std::erase_if(top, [&](const std::string& id) {
        const Cell* cell = page.findCell(id);
        return cell == nullptr || cell->parent().empty() || isLayer(page, id);
    });
    return top;
}

void extendBounds(std::optional<core::Rect>& bounds, const core::Rect& rect)
{
    if (!bounds) {
        bounds = rect;
        return;
    }
    const double right = std::max(bounds->x + bounds->width, rect.x + rect.width);
    const double bottom = std::max(bounds->y + bounds->height, rect.y + rect.height);
    bounds->x = std::min(bounds->x, rect.x);
    bounds->y = std::min(bounds->y, rect.y);
    bounds->width = right - bounds->x;
    bounds->height = bottom - bounds->y;
}

} // namespace

std::optional<core::Rect> CellBundle::bounds() const
{
    std::optional<core::Rect> result;
    for (const Cell& cell : cells) {
        const core::Geometry* geometry = cell.geometry();
        if (!cell.parent().empty() || geometry == nullptr)
            continue;
        if (cell.isEdge()) {
            std::vector<Point> points = geometry->points();
            if (const auto p = geometry->sourcePoint())
                points.push_back(*p);
            if (const auto p = geometry->targetPoint())
                points.push_back(*p);
            for (const Point& p : points)
                extendBounds(result, {p.x, p.y, 0, 0});
        } else if (cell.isVertex() && !geometry->relative()) {
            extendBounds(result, geometry->bounds());
        }
    }
    return result;
}

std::string encodeCells(const core::Page& page, const std::vector<std::string>& ids)
{
    const std::vector<std::string> top = copiedTopLevelIds(page, ids);

    // The copied set: the top-level cells and every descendant.
    std::unordered_set<std::string> copied;
    for (const std::string& id : top) {
        copied.insert(id);
        for (const Cell* descendant : page.descendants(id))
            copied.insert(descendant->id());
    }

    format::ReadResult empty = format::readDocument(kEmptyModel);
    core::Document document = std::move(*empty.document);
    core::Page& out = *document.page(0);

    for (const std::string& id : top) {
        std::vector<const Cell*> subtree{page.findCell(id)};
        for (const Cell* descendant : page.descendants(id))
            subtree.push_back(descendant);
        const Point origin = commands::origin(page, page.findCell(id)->parent());
        for (const Cell* original : subtree) {
            Cell cell = *original;
            detachTerminalOutside(page, cell, true, copied);
            detachTerminalOutside(page, cell, false, copied);
            if (original->id() == id) {
                cell.setParent(kExportLayer);
                if (core::Geometry* geometry = cell.geometry())
                    commands::translateGeometry(*geometry, origin.x, origin.y, cell.isEdge());
            }
            out.addCell(std::move(cell));
        }
    }
    return format::writeDocument(document).toStdString();
}

namespace {

// Clipboard, palette and template payloads are untrusted: a coordinate such as
// "nan" or "-inf" is replaced by 0 when pasted. File loading keeps the text as
// written (lossless), so only decodeCells calls this.
bool isFiniteNumber(std::string_view text)
{
    const std::optional<double> value = core::parseNumber(text);
    return value.has_value() && std::isfinite(*value);
}

// Sets an attribute that is present and not finite to "0"; the attribute keeps
// its place in the element.
void sanitizeCoordinate(core::Attributes& attributes, std::string_view name)
{
    if (attributes.has(name) && !isFiniteNumber(attributes.value(name)))
        attributes.set(name, "0");
}

void sanitizePoint(core::XmlNode& node)
{
    sanitizeCoordinate(node.attributes, "x");
    sanitizeCoordinate(node.attributes, "y");
}

// The points of a geometry: sourcePoint, targetPoint and offset are direct
// <mxPoint> children, the waypoints sit in <Array as="points">.
void sanitizeGeometry(core::Geometry& geometry)
{
    for (const char* name : {"x", "y", "width", "height"})
        sanitizeCoordinate(geometry.attributes(), name);
    for (core::XmlNode& child : geometry.children()) {
        if (child.isElement("mxPoint"))
            sanitizePoint(child);
        else if (child.isElement("Array")) {
            for (core::XmlNode& point : child.children) {
                if (point.isElement("mxPoint"))
                    sanitizePoint(point);
            }
        }
    }
}

} // namespace

std::optional<CellBundle> decodeCells(std::string_view text)
{
    QByteArray bytes(text.data(), static_cast<qsizetype>(text.size()));
    bytes = bytes.trimmed();
    if (bytes.startsWith("%3C") || bytes.startsWith("%3c"))
        bytes = QUrl::fromPercentEncoding(bytes).toUtf8().trimmed(); // draw.io's legacy copy format
    if (!bytes.startsWith('<'))
        return std::nullopt;

    format::ReadResult read = format::readDocument(bytes);
    if (!read.ok() || read.document->pageCount() == 0)
        return std::nullopt;
    const core::Page& page = *read.document->page(0);
    if (page.rootCell() == nullptr)
        return std::nullopt;

    CellBundle bundle;
    for (const Cell* cell : page.cells()) {
        if (cell->parent().empty() || isLayer(page, cell->id()))
            continue; // the root and the layers are not part of a selection
        Cell copy = *cell;
        if (isLayer(page, copy.parent()))
            copy.setParent({});
        if (core::Geometry* geometry = copy.geometry())
            sanitizeGeometry(*geometry);
        bundle.cells.push_back(std::move(copy));
    }
    return bundle;
}

commands::Result insertBundle(commands::Transaction& tx, std::size_t page, const CellBundle& bundle,
                              const std::string& parent, double dx, double dy)
{
    const core::Page* target = tx.page(page);
    if (target == nullptr)
        return commands::Result::failure("Unknown page");

    // Fresh ids first, so that references can be remapped in one pass.
    std::unordered_map<std::string, std::size_t> position; // old id -> index in the bundle
    std::unordered_map<std::string, std::string> renamed;
    for (std::size_t i = 0; i < bundle.cells.size(); ++i) {
        const std::string& id = bundle.cells[i].id();
        position.emplace(id, i);
        renamed.emplace(id, tx.newCellId(page));
    }

    const Point origin = commands::origin(*target, parent);
    std::vector<Cell> cells;
    std::vector<std::string> topLevel;
    struct DeferredTerminal {
        std::string edge, terminal;
        bool source;
    };
    std::vector<DeferredTerminal> deferred; // terminals that come later in the bundle than their edge

    for (std::size_t i = 0; i < bundle.cells.size(); ++i) {
        Cell cell = bundle.cells[i];
        const bool isTop = renamed.count(cell.parent()) == 0;
        cell.setId(renamed[cell.id()]);
        cell.setParent(isTop ? parent : renamed[cell.parent()]);
        for (const bool source : {true, false}) {
            const std::string old = source ? cell.source() : cell.target();
            if (old.empty())
                continue;
            const auto it = renamed.find(old);
            if (it != renamed.end() && position[old] > i)
                deferred.push_back({cell.id(), it->second, source});
            const std::string replacement = it != renamed.end() && position[old] <= i ? it->second : std::string();
            if (source)
                cell.setSource(replacement);
            else
                cell.setTarget(replacement);
        }
        if (isTop) {
            topLevel.push_back(cell.id());
            if (core::Geometry* geometry = cell.geometry())
                commands::translateGeometry(*geometry, dx - origin.x, dy - origin.y, cell.isEdge());
        }
        cells.push_back(std::move(cell));
    }

    commands::Result added = commands::ops::addCells(tx, page, std::move(cells));
    if (!added)
        return added;
    for (const DeferredTerminal& d : deferred) {
        const commands::Result connected = commands::ops::connectEdge(tx, page, d.edge, d.terminal, d.source);
        if (!connected)
            return connected;
    }
    return commands::Result::success(std::move(topLevel));
}

void setCellsMimeData(QMimeData& mime, const std::string& xml)
{
    const QByteArray bytes(xml.data(), static_cast<qsizetype>(xml.size()));
    mime.setData(QString::fromLatin1(kCellsMimeType), bytes);
    mime.setText(QString::fromUtf8(bytes));
}

std::optional<CellBundle> cellsFromMimeData(const QMimeData& mime)
{
    const QString type = QString::fromLatin1(kCellsMimeType);
    if (mime.hasFormat(type)) {
        const QByteArray bytes = mime.data(type);
        if (auto bundle = decodeCells(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size()))))
            return bundle;
    }
    if (mime.hasText()) {
        const QByteArray bytes = mime.text().toUtf8();
        return decodeCells(std::string_view(bytes.constData(), static_cast<std::size_t>(bytes.size())));
    }
    return std::nullopt;
}

} // namespace qraw::ui
