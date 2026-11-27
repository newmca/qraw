#pragma once
// Shared helpers for the UI tests (owned by the design; packages add
// support/<package>_*.h files instead of editing this one).

#include "qraw/format/codec.h"
#include "qraw/ui/action_registry.h"
#include "qraw/ui/app_settings.h"
#include "qraw/ui/canvas_view.h"
#include "qraw/ui/cell_transfer.h"
#include "qraw/ui/diagram_scene.h"
#include "qraw/ui/document_session.h"
#include "qraw/ui/edit_actions.h"
#include "qraw/ui/file_service.h"
#include "qraw/ui/label_editor.h"
#include "qraw/ui/palette_library.h"
#include "qraw/ui/style_summary.h"
#include "qraw/ui/tool_factories.h"
#include "canvas_input.h"
#include "test_profile.h"

#include <QApplication>
#include <QDir>
#include <QMouseEvent>
#include <QStandardPaths>
#include <QWidget>
#include <QtTest>

#include <stdexcept>
#include <string>

namespace qraw::ui::test {

// A one-page document: layer "1"; vertices "a" (40,40 120x60) and "b"
// (280,40 120x60, html=1, label "B"); edge "e" a->b; a group "g"
// (40,200 200x100, style group) with children "c1" (0,0 80x40) and
// "c2" (120,60 80x40) in group coordinates.
inline constexpr char kSimpleDiagram[] = R"(<mxfile><diagram id="p1" name="Page-1"><mxGraphModel dx="800" dy="600" grid="1" gridSize="10" guides="1" tooltips="1" connect="1" arrows="1" fold="1" page="1" pageScale="1" pageWidth="827" pageHeight="1169" math="0" shadow="0"><root><mxCell id="0"/><mxCell id="1" parent="0"/><mxCell id="a" value="A" style="rounded=0;whiteSpace=wrap;" vertex="1" parent="1"><mxGeometry x="40" y="40" width="120" height="60" as="geometry"/></mxCell><mxCell id="b" value="B" style="ellipse;whiteSpace=wrap;html=1;" vertex="1" parent="1"><mxGeometry x="280" y="40" width="120" height="60" as="geometry"/></mxCell><mxCell id="e" style="edgeStyle=orthogonalEdgeStyle;" edge="1" parent="1" source="a" target="b"><mxGeometry relative="1" as="geometry"/></mxCell><mxCell id="g" value="" style="group" vertex="1" connectable="0" parent="1"><mxGeometry x="40" y="200" width="200" height="100" as="geometry"/></mxCell><mxCell id="c1" value="C1" style="rounded=1;" vertex="1" parent="g"><mxGeometry width="80" height="40" as="geometry"/></mxCell><mxCell id="c2" value="C2" style="" vertex="1" parent="g"><mxGeometry x="120" y="60" width="80" height="40" as="geometry"/></mxCell></root></mxGraphModel></diagram></mxfile>)";

inline core::Document documentFromXml(std::string_view xml)
{
    format::ReadResult result = format::readDocument(QByteArrayView(xml.data(), static_cast<qsizetype>(xml.size())));
    if (!result.ok())
        throw std::runtime_error("test document does not parse");
    return std::move(*result.document);
}

inline core::Document simpleDocument()
{
    return documentFromXml(kSimpleDiagram);
}

// The saved bytes of the session's document (for undo/redo identity checks).
inline QByteArray savedBytes(const DocumentSession& session)
{
    return format::writeDocument(session.document());
}

// Writes `widget` to $QRAW_SCREENSHOT_DIR/<name>.png when the variable is
// set (manual review of UI tests); otherwise does nothing. Returns true when
// a file was written.
inline bool grabToPng(QWidget& widget, const QString& name)
{
    const QString dir = qEnvironmentVariable("QRAW_SCREENSHOT_DIR");
    if (dir.isEmpty())
        return false;
    QDir().mkpath(dir);
    return widget.grab().save(dir + QLatin1Char('/') + name + QStringLiteral(".png"));
}

// A CanvasView of a fixed size showing the session at 100 %, shown (so
// mapping and painting work on the offscreen platform).
inline std::unique_ptr<CanvasView> makeCanvas(DocumentSession& session, QSize size = {800, 600})
{
    auto view = std::make_unique<CanvasView>(session);
    // Deterministic pixels and counters: tiles are rasterised by the paint that
    // needs them. The tests of the worker-thread path (tst_ui_canvas_tiles_threaded)
    // switch it off.
    view->setSynchronousRasterization(true);
    view->resize(size);
    view->show();
    view->setZoom(1);
    view->centerOn(QRectF(0, 0, size.width(), size.height()));
    return view;
}

// --- availability probes ------------------------------------------------------
// A test that needs the behaviour of another component calls the probe
// first and QSKIPs when it is false. A test never skips on the probe of the
// component it tests.

inline bool sessionFilesAvailable() // FileService
{
    return FileService::blankDocument().pageCount() > 0;
}

inline bool transferAvailable() // cell_transfer
{
    return decodeCells(R"(<mxGraphModel><root><mxCell id="0"/><mxCell id="1" parent="0"/><mxCell id="2" vertex="1" parent="1"><mxGeometry width="10" height="10" as="geometry"/></mxCell></root></mxGraphModel>)")
        .has_value();
}

inline bool editActionsAvailable() // edit_actions
{
    DocumentSession session(simpleDocument());
    session.selection().set({"a"});
    return actions::deleteSelection(session, false);
}

inline bool selectionHandlersAvailable(const ToolContext& context)
{
    return !createSelectionHandlers(context).empty();
}

inline bool edgeHandlersAvailable(const ToolContext& context)
{
    return !createEdgeHandlers(context).empty();
}

inline bool labelEditorAvailable(CanvasView& view)
{
    LabelEditor* editor = view.interaction().labelEditor();
    if (editor == nullptr || view.session().currentPage() == nullptr)
        return false;
    const auto states = view.diagramScene().pageView().states();
    if (states.empty() || !editor->startEditing(states.front().id))
        return false;
    editor->stopEditing(true);
    return true;
}

inline bool styleSummaryAvailable()
{
    const core::Document document = simpleDocument();
    return !StyleSummary::of(*document.page(0), {"a"}, render::Stylesheet::drawioDefault()).empty();
}

inline bool paletteAvailable()
{
    return PaletteLibrary::builtin().entryCount() > 0;
}

inline bool standardActionsAvailable()
{
    ActionRegistry registry;
    createStandardActions(registry);
    return !registry.actions().empty();
}

} // namespace qraw::ui::test
