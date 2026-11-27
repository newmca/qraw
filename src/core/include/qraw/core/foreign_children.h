#pragma once

#include "qraw/core/xml_node.h"

#include <cstddef>
#include <vector>

namespace qraw::core {

// Children of a modeled element that the model does not understand, for
// elements that have exactly one modeled child (e.g. <mxCell> with its
// <mxGeometry>, or <mxGraphModel> with its <root>).
//
// The modeled child is written after the first `modeledSlot` foreign nodes,
// which reproduces the original child order.
struct ForeignChildren {
    std::vector<XmlNode> nodes;
    std::size_t modeledSlot = 0;

    [[nodiscard]] bool empty() const { return nodes.empty(); }
    friend bool operator==(const ForeignChildren&, const ForeignChildren&) = default;
};

} // namespace qraw::core
