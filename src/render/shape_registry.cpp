// The registry of native shapes and the fallback painter for unknown ones.

#include "qraw/render/shape.h"

#include "shapes_common.h"

#include <algorithm>
#include <array>

namespace qraw::render {

namespace {

// Unknown shapes (JS-only mxgraph.* shapes and the like): the plain
// rectangle with the cell's fill and stroke, plus an 8x8 folded corner
// outline at the top right in half the stroke alpha, so that the shape stays
// visible and recognizable as unsupported.
class FallbackShape final : public VertexShape {
public:
    void paint(Canvas& c, const ShapePaintContext&, double x, double y, double w, double h) const override
    {
        c.rect(x, y, w, h);
        c.fillAndStroke();

        constexpr double kFold = 8;
        const double s = std::min({kFold, w, h});
        c.setShadow(false);
        c.setStrokeAlpha(0.5);
        c.begin();
        c.moveTo(x + w - s, y);
        c.lineTo(x + w - s, y + s);
        c.lineTo(x + w, y + s);
        c.stroke();
    }
};

// Vertex shape names that draw.io registers (mxCellRenderer.registerShape in
// Shapes.js, mxShapeBasic*, the UML and mermaid shapes) but Qraw has no
// painter for yet. Everything else that is not a native shape or a stencil is
// a name draw.io itself does not know, and it draws the default rectangle
// (mxCellRenderer.getShapeConstructor -> defaultVertexShape). Keeping this
// list explicit lets the two cases be told apart: a gap of ours is shown with
// the placeholder, a name without a painter in draw.io is a plain rectangle.
// tst_shapes checks that none of these is registered (delete the name when the
// painter is added).
constexpr std::array<std::string_view, 53> kUpstreamOnlyShapes = {
    "associativeEntity", "collate", "corner", "customShape", "cylinder2", "dataStorage", "delay",
    "gitCherryPick", "gitMergeCommit", "gitTag", "ishikawaHead", "isoCube", "isoCube2", "isoRectangle",
    "lollipop", "loopLimit", "mermaidBlockArrow", "mermaidOdd", "mermaidPerson", "mermaidSankeyLink",
    "mermaidUsecaseActor", "mindmapBang", "or", "orEllipse", "parallelMarker", "pipe",
    "providedRequiredInterface", "requiredInterface", "requires", "sample", "seqActorStick", "seqBoundary",
    "seqCollections", "seqControl", "seqDatabase", "seqEntity", "seqQueue", "smileyFace", "sortShape",
    "sumEllipse", "switch", "tableLine", "tapeData", "tee", "tie", "transparent", "umlBoundary", "umlControl",
    "umlDestroy", "umlEntity", "umlState", "wedgeCallout", "xor",
};

ShapeRegistry makeBuiltin()
{
    ShapeRegistry registry;
    detail::registerBasicShapes(registry);
    detail::registerFlowchartShapes(registry);
    detail::registerContainerShapes(registry);
    detail::registerImageShapes(registry);
    detail::registerUmlShapes(registry);
    detail::registerRect2Shapes(registry);
    detail::registerBpmnShapes(registry);
    detail::registerFloorplanShapes(registry);
    detail::registerArrowShapes(registry);
    detail::registerCurvedTextShape(registry);
    return registry;
}

} // namespace

const ShapeRegistry& ShapeRegistry::builtin()
{
    static const ShapeRegistry registry = makeBuiltin(); // thread-safe initialization
    return registry;
}

ShapeRegistry::ShapeRegistry() : fallback_(std::make_unique<FallbackShape>()) {}
ShapeRegistry::~ShapeRegistry() = default;
ShapeRegistry::ShapeRegistry(ShapeRegistry&&) noexcept = default;
ShapeRegistry& ShapeRegistry::operator=(ShapeRegistry&&) noexcept = default;

const VertexShape* ShapeRegistry::find(std::string_view name) const
{
    // Case-sensitive: draw.io shape names are.
    const auto it = shapes_.find(std::string(name));
    return it == shapes_.end() ? nullptr : it->second.get();
}

const VertexShape& ShapeRegistry::fallback() const { return *fallback_; }

bool ShapeRegistry::isUpstreamOnly(std::string_view name)
{
    // Stencil names (mxgraph.*) come from data Qraw may not ship.
    return name.starts_with("mxgraph.") ||
           std::find(kUpstreamOnlyShapes.begin(), kUpstreamOnlyShapes.end(), name) != kUpstreamOnlyShapes.end();
}

void ShapeRegistry::add(std::string name, std::unique_ptr<VertexShape> shape)
{
    shapes_[std::move(name)] = std::move(shape);
}

std::vector<std::string> ShapeRegistry::names() const
{
    std::vector<std::string> names;
    names.reserve(shapes_.size());
    for (const auto& entry : shapes_)
        names.push_back(entry.first);
    std::sort(names.begin(), names.end());
    return names;
}

} // namespace qraw::render
