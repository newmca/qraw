#include "link_badge.h"

#include "qraw/ui/canvas_view.h"
#include "qraw/ui/data_actions.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"

#include <QPainter>

#include <algorithm>

namespace qraw::ui {

namespace {

constexpr double kBadgePixels = 16;   // Graph.createLinkOverlay: a 16 x 16 image
constexpr double kBadgeInsetPixels = 4;
// A cell has to be this many badges wide and two high to carry one, so that it does not hide the cell's label.
constexpr double kMinWidthInBadges = 3;
constexpr double kMinHeightInBadges = 2;

const core::Cell* cellOf(const DocumentSession& session, const std::string& id)
{
    const core::Page* page = session.currentPage();
    return page != nullptr ? page->findCell(id) : nullptr;
}

bool hasLink(const core::Cell& cell)
{
    const core::CellWrapper* wrapper = cell.wrapper();
    if (wrapper == nullptr)
        return false;
    const std::string* link = wrapper->attributes.find("link");
    return link != nullptr && !link->empty();
}

} // namespace

QRectF linkBadgeRect(const render::CellState& state, double scale)
{
    const double size = kBadgePixels / scale;
    if (state.kind != render::CellState::Kind::Vertex || state.bounds.width < kMinWidthInBadges * size ||
        state.bounds.height < kMinHeightInBadges * size)
        return {};
    const double inset = kBadgeInsetPixels / scale;
    return {state.bounds.x + state.bounds.width - inset - size, state.bounds.y + inset, size, size};
}

std::string LinkBadgeHandler::linkedCellAt(QPointF scenePos, double tolerance) const
{
    if (ctx_.scene == nullptr || ctx_.session == nullptr || ctx_.session->currentPage() == nullptr)
        return {};
    const std::string hit = ctx_.scene->cellAt(scenePos, tolerance);
    if (hit.empty())
        return {};
    // Graph.getClickableLinkForCell: the link of the cell or of the nearest ancestor that has one.
    const core::Page& page = *ctx_.session->currentPage();
    const core::Cell* cell = page.findCell(hit);
    for (int steps = 0; cell != nullptr && steps < 64; ++steps) {
        if (hasLink(*cell))
            return cell->isVertex() ? cell->id() : std::string();
        if (cell->parent().empty() || cell->parent() == cell->id())
            break;
        cell = page.findCell(cell->parent());
    }
    return {};
}

std::vector<LinkBadgeHandler::Badge> LinkBadgeHandler::badges(double scale) const
{
    std::vector<Badge> result;
    if (ctx_.scene == nullptr || ctx_.session == nullptr)
        return result;
    std::vector<std::string> ids = ctx_.session->selection().cells();
    if (!hovered_.empty() && std::find(ids.begin(), ids.end(), hovered_) == ids.end())
        ids.push_back(hovered_);
    for (const std::string& id : ids) {
        const core::Cell* cell = cellOf(*ctx_.session, id);
        const render::CellState* state = ctx_.scene->state(id);
        if (cell == nullptr || state == nullptr || !hasLink(*cell))
            continue;
        if (const QRectF rect = linkBadgeRect(*state, scale); !rect.isNull())
            result.push_back({id, rect});
    }
    return result;
}

const LinkBadgeHandler::Badge* LinkBadgeHandler::badgeAt(const std::vector<Badge>& all, QPointF scenePos) const
{
    for (auto it = all.rbegin(); it != all.rend(); ++it) {
        if (it->rect.contains(scenePos))
            return &*it;
    }
    return nullptr;
}

std::optional<Qt::CursorShape> LinkBadgeHandler::hover(const CanvasEvent& event)
{
    const std::string linked = linkedCellAt(event.scenePos, event.pixels(4));
    hovered_ = linked;
    // The icon claims the pointer only on itself, everything else of the cell stays as it is.
    return badgeAt(badges(event.scale), event.scenePos) != nullptr ? std::optional<Qt::CursorShape>(Qt::PointingHandCursor)
                                                                   : std::nullopt;
}

// A click on the icon: follows the link on release when the pointer is still on it.
class LinkClickGesture : public Gesture {
public:
    LinkClickGesture(LinkBadgeHandler& owner, std::string id) : owner_(owner), id_(std::move(id)) {}
    void move(const CanvasEvent& event) override { (void)event; }
    void release(const CanvasEvent& event) override
    {
        const std::vector<LinkBadgeHandler::Badge> all = owner_.badges(event.scale);
        const LinkBadgeHandler::Badge* badge = owner_.badgeAt(all, event.scenePos);
        if (badge == nullptr || badge->id != id_)
            return;
        if (const core::Cell* cell = cellOf(*owner_.ctx_.session, id_)) {
            if (const core::CellWrapper* wrapper = cell->wrapper())
                actions::followLink(*owner_.ctx_.session, wrapper->attributes.value("link"));
        }
    }
    void cancel() override {}
    [[nodiscard]] Qt::CursorShape cursor() const override { return Qt::PointingHandCursor; }

private:
    LinkBadgeHandler& owner_;
    std::string id_;
};

std::unique_ptr<Gesture> LinkBadgeHandler::press(const CanvasEvent& event)
{
    if (event.button != Qt::LeftButton)
        return nullptr;
    const std::vector<Badge> all = badges(event.scale);
    const Badge* badge = badgeAt(all, event.scenePos);
    return badge != nullptr ? std::make_unique<LinkClickGesture>(*this, badge->id) : nullptr;
}

OverlayBounds LinkBadgeHandler::overlayBounds(const OverlayContext& context) const
{
    QRectF united;
    for (const Badge& badge : badges(context.scale)) {
        const double pad = context.pixels(2);
        const QRectF padded = badge.rect.adjusted(-pad, -pad, pad, pad);
        united = united.isNull() ? padded : united.united(padded);
    }
    return united;
}

void LinkBadgeHandler::paintOverlay(QPainter& painter, const OverlayContext& context) const
{
    painter.save();
    painter.setRenderHint(QPainter::Antialiasing, true);
    for (const Badge& badge : badges(context.scale)) {
        if (!context.exposed.isNull() && !context.exposed.intersects(badge.rect))
            continue;
        // Drawn in view pixels, centred on the badge.
        painter.save();
        painter.translate(badge.rect.center());
        painter.scale(1 / context.scale, 1 / context.scale);
        const double r = kBadgePixels / 2;
        QPen outline(overlay::kHandleStroke, 1);
        outline.setCosmetic(true);
        painter.setPen(outline);
        painter.setBrush(overlay::kHandleFill);
        painter.drawEllipse(QPointF(0, 0), r, r);
        // Two links of a chain, turned 45 degrees.
        QPen glyph(Qt::white, 1.6);
        glyph.setCapStyle(Qt::RoundCap);
        painter.setPen(glyph);
        painter.setBrush(Qt::NoBrush);
        painter.rotate(-45);
        painter.drawRoundedRect(QRectF(-5, -2.2, 6.2, 4.4), 2.2, 2.2);
        painter.drawRoundedRect(QRectF(-1.2, -2.2, 6.2, 4.4), 2.2, 2.2);
        painter.restore();
    }
    painter.restore();
}

} // namespace qraw::ui
