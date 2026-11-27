// createEdgeHandlers: the connect (hover arrows) and edge handle handlers.
// The label editor is created by
// installDefaultHandlers (interaction.cpp) from label_editor.h.
#include "connect_handler.h"
#include "edge_handles.h"

#include "qraw/ui/tool_factories.h"

namespace qraw::ui {

HandlerList createEdgeHandlers(const ToolContext& context)
{
    HandlerList handlers;
    handlers.emplace_back(std::make_unique<edgeh::EdgeHandlesHandler>(context), PriorityEdgeHandles);
    handlers.emplace_back(std::make_unique<connect::ConnectHandler>(context), PriorityConnect);
    return handlers;
}

} // namespace qraw::ui
