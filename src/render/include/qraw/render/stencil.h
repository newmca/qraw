#pragma once

// mxStencil: XML-described shapes (mxgraph/src/shape/mxStencil.js), the
// vendored draw.io stencil sets and inline shape=stencil(...) styles.

#include "qraw/core/diagnostic.h"
#include "qraw/core/xml_node.h"
#include "qraw/render/shape.h"

#include <QByteArrayView>

#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace qraw::render {

// One <shape> element of a stencil set, interpreted on every paint exactly
// like mxStencil.drawShape / drawNode (grammar: docs/research/
// drawio-rendering-data.md section 2).
class Stencil final : public VertexShape {
public:
    // Builds a stencil from a <shape> element. Returns nullopt (with a
    // diagnostic) when the element is not <shape>. Missing w/h default to
    // 100 (upstream), aspect "fixed" is the only non-variable value,
    // strokewidth "inherit" or a number (default "1").
    [[nodiscard]] static std::optional<Stencil> fromXml(const core::XmlNode& shapeElement,
                                                        std::vector<core::Diagnostic>* diagnostics = nullptr);

    [[nodiscard]] const std::string& name() const { return name_; }
    [[nodiscard]] double width() const { return w0_; }
    [[nodiscard]] double height() const { return h0_; }
    [[nodiscard]] bool fixedAspect() const { return fixedAspect_; }
    // The <shape> element this stencil was built from (the XML Edit Shape shows).
    [[nodiscard]] const core::XmlNode& shapeElement() const { return shape_; }

    // mxStencil.drawShape(canvas, shape, x, y, w, h): computeAspect (fixed
    // aspect and direction), background then foreground, with the style's
    // stroke width per `strokewidth`. Colors named in fillcolor/strokecolor
    // ops resolve through the style (see the grammar).
    void paint(Canvas& canvas, const ShapePaintContext& context, double x, double y, double w,
               double h) const override;
    [[nodiscard]] bool handlesDirection() const override { return true; }
    // <labelBounds> (draw.io extension, js/grapheditor/Shapes.js:8-60) when
    // present, else the default.
    [[nodiscard]] Rect labelBounds(const ResolvedStyle& style, const Rect& rect) const override;
    // <connections><constraint .../></connections>.
    [[nodiscard]] std::vector<geometry::ConnectionConstraint>
    connectionConstraints(const ResolvedStyle& style, double w, double h) const override;

private:
    std::string name_;
    double w0_ = 100;
    double h0_ = 100;
    bool fixedAspect_ = false;
    std::string strokeWidth_ = "1"; // "inherit" or a number
    core::XmlNode shape_;           // the <shape> element, interpreted on paint
};

// Decodes one packed stencil set: the raw-inflated bytes of an
// `f['<path>.xml'] = '<base64>'` entry of upstream js/stencils.min.js
// (20-byte header of 5 big-endian uint32 stream lengths, then the op,
// x-delta, y-delta, numeric-delta and string streams; see
// docs/research/drawio-rendering-data.md section 1 and
// tools/drawio-extract/decode_stencils.py). Returns the <shapes> element,
// or nullopt on malformed input (never reads out of bounds).
[[nodiscard]] std::optional<core::XmlNode> decodePackedStencilSet(QByteArrayView inflated);

// Registry key of a stencil: lower(shapes@name) + "." +
// lower(shape@name with ' ' -> '_') (Graph.js:33833 parseStencilSet),
// e.g. "mxgraph.basic.4_point_star".
[[nodiscard]] std::string stencilKey(std::string_view setName, std::string_view shapeName);

class StencilRegistry {
public:
    // The vendored draw.io stencil sets (:/qraw/stencils/stencils.bin and
    // :/qraw/stencils/index.tsv, see docs/design/render.md). A set is
    // decoded on first use. Thread-safe.
    [[nodiscard]] static StencilRegistry& builtin();

    StencilRegistry();
    ~StencilRegistry();

    // A stencil by `shape` style value, case-insensitive
    // ("mxgraph.flowchart.process"). nullptr when unknown.
    [[nodiscard]] const Stencil* find(std::string_view shapeName);
    // shape=stencil(<base64 of raw-deflated <shape> XML>), the format
    // draw.io writes for custom shapes (Graph.compress of the XML; plain
    // URI-encoded or uncompressed XML is accepted too). Cached by value.
    // nullptr when it does not decode.
    [[nodiscard]] const Stencil* findInline(std::string_view shapeValue);
    // Whether `name` is in the index (no decoding).
    [[nodiscard]] bool contains(std::string_view shapeName) const;
    [[nodiscard]] std::size_t indexedCount() const;

    // Adds every <shape> of a <shapes name="..."> element (tests, custom
    // libraries). Existing keys are replaced.
    void addSet(const core::XmlNode& shapesElement);

private:
    struct Impl;
    std::unique_ptr<Impl> d;
};

} // namespace qraw::render
