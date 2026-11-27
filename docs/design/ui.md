# Editor UI

This is the design of the editor: the window, canvas, interaction, panels, file handling, autosave and recovery, export and print dialogs, preferences and themes, the shape palette (data extraction and the palette dock) and the template-based Welcome dialog. The public headers are in `src/ui/include/qraw/ui/`. Upstream references point at `$DRAWIO_SRC/src/main/webapp` (a local clone of the draw.io repository at commit `c72c9a98`, Apache-2.0), a read-only reference that is never executed.

**Acceptance:** the app launches, edits and saves a corpus file (smoke tests via Qt Test); the palette lists at least the core families and drag-to-create works.

## 1. Architecture

```
                         ┌──────────── MainWindow  ─────────────┐
 ActionRegistry ◀────────┤ menus, toolbar, status bar, docks, prompts      │
 (ids, shortcuts)        └─┬──────────┬───────────┬──────────┬────────────┘
                           │          │           │          │
             DocumentController   CanvasView   FormatPanel  PaletteDock, LayersPanel,
             (open/save/autosave)  (canvas)    (format)     PageTabs, Outline, Find
                     │               │  owns                     │
         FileService/RecoveryStore   ├── DiagramScene ── CellItem per render::CellState
                     │               └── InteractionController ── handlers ── Gestures
                     ▼                                │
                DocumentSession ◀─────────────────────┘ (edit / selection / page)
                  │  owns commands::Editor (model + UndoStack), SelectionModel
                  │  observer → documentChanged(ChangeSet, origin) Qt signal
                  ▼
            core::Document (never mutated outside commands)
```

| Layer | Files |
|---|---|
| Session  | `document_session.h`, `selection_model.h`, `cell_queries.h`, `app_settings.h` |
| Interaction framework  | `interaction.h`, `tool_factories.h` |
| Action registry  | `action_registry.h`  |
| Canvas | `diagram_scene.h`, `cell_item.h`, `canvas_view.h` |
| Files, clipboard, edit actions | `file_service.h`, `document_controller.h`, `cell_transfer.h`, `edit_actions.h` |
| Selection tools | `snapping.h`, `createSelectionHandlers` |
| Edge tools, inline editing | `createEdgeHandlers`, `label_editor.h` |
| Format panel | `format_panel.h`, `style_summary.h` |
| Docks | `page_tabs.h`, `layers_panel.h`, `outline_view.h`, `find_panel.h` |
| Palette | `palette_library.h`, `palette_dock.h`, `tools/palette/`, `resources/palettes/` |
| Dialogs | `welcome_dialog.h`, `template_catalog.h`, `export_dialog.h`, `print_support.h`, `preferences_dialog.h`, `shortcuts_dialog.h` |
| Shell | `main_window.h`, `app_setup.h`, `src/app/main.cpp` |

Dependency rules: `qraw_ui` may use every lower module. Widgets talk to the model only through `DocumentSession::edit` (which runs `commands::ops::*` inside one `Transaction`) and read it through `DocumentSession::document()`. No widget, handler or gesture does file I/O: that is `FileService`, `RecoveryStore`, `runExport` and `printDocument`, called from MainWindow/DocumentController. Every user-visible string goes through `tr()` (or `QCoreApplication::translate` in non-QObject code).

## 2. Session and data flow

`DocumentSession` owns the `commands::Editor` of one document plus the UI state that belongs to it: current page (index), `SelectionModel` (ordered ids on the current page), current layer and entered group (draw.io's `currentRoot`). It adapts `commands::DocumentObserver` to Qt signals:

1. A widget calls `session.edit(tr("Move"), [&](Transaction& tx) { return ops::moveCells(tx, page, ids, dx, dy); })`.
2. The Editor commits and calls the observer; the session clamps the current page, prunes the selection (removed cells), selects the changed cells on undo/redo (draw.io's undo listener), then emits `documentChanged(ChangeSet, origin)` and `undoStateChanged`/`modifiedChanged`.
3. On success `edit` applies the `Result`: `Result.cells` becomes the selection, `Result.page` the current page. Failures emit `statusMessage` and change nothing.

Coalescing: callers that repeat edits (spin boxes, arrow-key nudge, sliders) pass a merge key and call `session.endMerge()` when the interaction ends (D-019). Pointer gestures do **not** coalesce: they preview in the overlay and commit once on release (§4).

The dirty flag is the undo stack's clean index (`Editor::isModified`); `DocumentController` calls `markSaved()` after a successful save.

## 3. Canvas projection

Scene coordinates are page units (absolute model coordinates). Zoom is only the view transform. The scene has no per-cell items: cells are painted from tiles, hit tested through a spatial grid, and an edit that keeps the tree patches the view (`PageView::update`). ARCHITECTURE.md, "Canvas rendering", describes the current pipeline; the rest of this section describes what is drawn and how the view behaves.

`DiagramScene::drawBackground` paints: the area outside pages `#f0f0f0` (draw.io's `geDiagramBackdrop` uses a light grey), the page tiles from `Graph.getPageLayout` (Graph.js:15483) filled with the page background (white default) with a soft shadow when `pageVisible`, otherwise an unbounded white background; the grid when `gridEnabled` (minor lines every `gridSize`, major every 4, colours `#e0e0e0`/`#d0d0d0`, skipped while minor lines are < 4 px apart on screen). Locked and hidden layers: `PageView` already skips hidden layers; locked layers are drawn normally and their cells are not hit-testable (`isCellSelectable`).

`CanvasView`: zoom on draw.io's steps (`Graph.getZoomSteps`, Graph.js:21672: factor 1.2 rounded to whole percents, range 1 %-16000 %; `zoomIn`/`zoomOut` pick the next step, `Graph.getZoomStep` :21763), Ctrl+wheel zoom around the cursor, wheel scroll, Shift+wheel horizontal, pan with middle drag, Space+left drag and right drag (a right click without movement emits `contextMenuRequested`), fit page/window/selection, `centerInsertPoint` snapped to the grid, scene rect = page area plus one viewport of margin on every side, cosmetic overlay pens for high-DPI. Drops go to registered `CanvasDropHandler`s.

## 4. Interaction

`CanvasView` converts mouse events to `CanvasEvent` (scene + view position, buttons, modifiers, zoom) after its own pan/zoom handling and forwards them to its `InteractionController`. The controller asks handlers in priority order (`HandlerPriority`, draw.io's mxGraph dispatch order):

| Priority | Handler | Upstream |
|---|---|---|
| 0 | `LabelEditor` (inline editing; when editing it sees presses first) | `mxCellEditor` |
| 50 | fold control: the +/- box on foldable containers (screen only) | `mxCellRenderer.createControl`, `mxGraph.foldCells` |
| 100 | edge handles: ends, waypoints, virtual midpoints, segment handles | `mxEdgeHandler`, `mxElbowEdgeHandler`, `mxEdgeSegmentHandler` |
| 200 | vertex handles: 8 sizers + rotation, selection outlines | `mxVertexHandler` |
| 300 | connect: hover arrows and connection points | `HoverIcons` (Graph.js:23620), `mxConnectionHandler`, `mxConstraintHandler` |
| 400 | selection: click, toggle, drill-down, move, rubber band, keyboard nudge, enter/exit group | `mxGraphHandler`, `mxRubberband`, `EditorUi.createKeyHandler` |

**State machine.** `Idle` → (hover: first handler returning a cursor owns the hover feedback) → press: first handler returning a `Gesture` owns the drag → `Dragging` (every move goes to the gesture; keys are swallowed; Escape → `cancel()`) → release → `Idle`. A gesture **previews in the overlay and changes the model exactly once on release** through one `DocumentSession::edit` (one undo entry, no merge key); `cancel()` leaves the model untouched. Gestures never change the selection before release (the rubber band shows its rectangle and selects on release). Double clicks are offered to handlers in order; keys too (after window shortcuts, §6).

**Selection rules** (`Graph.selectCellForEvent` Graph.js:16725 simplified; table rows and cells are parts of their table: the first click selects the table, the next the cell, a click on another part of the table switches to it): a click hits `scene.cellsAt(p, kHitTolerance px)`; the hit is mapped to `ancestorInScope(hit, enteredGroup)` unless that ancestor is the only selected cell, in which case the hit itself is selected (click a group child twice to drill down). Shift/Ctrl/Meta click toggles. Click on blank canvas clears the selection and starts a rubber band (selects `cellsInRect(rect, enteredGroup)` on release; with toggle modifiers adds). Locked cells (`isCellLocked`) and cells with `movable=0` select but do not move; `resizable=0` hides sizers; `rotatable=0` hides the rotation handle; `deletable=0`/`editable=0`/`connectable=0` are honoured by the respective actions (Ctrl+L, Actions.js:534, writes all of them).

**Containers** (src/ui/drop_target.*, fold_controls.*; src/commands/ops_layout.*). `dropTarget(session, scene, cells, pos)` is `Graph.getDropTarget`: the topmost vertex under the pointer that is not (inside) a moving cell, or its nearest ancestor that is a valid target (container, or an expanded cell with children; not locked, `dropTarget=0`, a table, a collapsed cell, a lane for a lane). `MoveGesture` highlights it and commits one edit: `moveCells`, `reparentCells(target)` for the cells not in it yet, `extendParents`, then `moveInLayout` so that a stack layout picks the position among its children; dragging the first cell's pointer out of its vertex parent reparents to `insertionParent()` (`mxGraphHandler.shouldRemoveCellsFromParent`). Palette drops and paste-at-point use the same target. Alt disables dropping. Layouts run in `DocumentSession::edit` after the caller's function, inside the same transaction (`ops::applyLayouts` over `tx.changes()`; `ChangeSet::childrenChanged` names parents that lost a child); loading and saving never run them. The fold control is drawn by `FoldControlHandler::paintOverlay` and folds with `ops::foldCells`.

**Overlay** look comes from `interaction.h` `overlay::` constants (Graph.js:33903-33913): selection `#00a8ff` dashed outline, 9 px handles filled `#29b6f2`, guides `#0088cf`, rotation handle 16 px off the top-right corner.

## 5. Panels

- **Format**: tabs per selection (Format.js `refresh`): Diagram (no selection), Style/Text/Arrange (vertices and/or edges; edge style group replaces fill/shadow for edges). Values come from `StyleSummary` (resolved styles via `render::Stylesheet`, mixed state). Every control writes through `DocumentSession::edit` with `ops::setStyles` (or `setPageProperties`, `resizeCells`, `moveCells`). Spin boxes use merge keys `"format:<key>:<ids>"` and `endMerge()` on `editingFinished`. A refresh never writes (block signals while filling controls).
- **Pages**: `PageTabs` under the canvas, ops::addPage/duplicatePage/removePage/renamePage/movePage.
- **Layers**, **Outline**, **Find/Replace**: right-side docks, hidden by default.
- **Palette**: left dock, §8.

## 6. Actions, shortcuts, focus

All commands are `QAction`s registered in `ActionRegistry` under the ids in `action_registry.h` (draw.io action names). `createStandardActions`  sets texts and shortcuts; MainWindow connects behaviour and updates enabled/checked state on `selectionChanged`, `undoStateChanged`, `currentPageChanged`, `documentChanged`. Shortcut context is `Qt::WindowShortcut`.

| Id | Shortcut(s) | Upstream |
|---|---|---|
| new / open / save / saveAs | Ctrl+N / Ctrl+O / Ctrl+S / Ctrl+Shift+S | Actions.js:132-133, EditorUi.js:11315-11316 |
| print / printPreview | Ctrl+P / — | Actions.js:151 |
| exportAs, pageSetup, revert, close, quit | — / — / — / Ctrl+W / Ctrl+Q | (desktop conventions) |
| undo / redo | Ctrl+Z / Ctrl+Shift+Z, Ctrl+Y | Actions.js:155-156, EditorUi.js:11330-11332 |
| cut / copy / paste | Ctrl+X / Ctrl+C / Ctrl+V | Actions.js:157-211 |
| delete / deleteAll | Delete, Backspace / Ctrl+Delete, Ctrl+Backspace | EditorUi.js:11302-11306 |
| duplicate | Ctrl+D | EditorUi.js:11328 |
| edit (label) | F2 (Enter handled by the canvas) | Actions.js:748, EditorUi.js:11352-11353 |
| editStyle | Ctrl+E | EditorUi.js:11324 |
| copyStyle / pasteStyle | Alt+Shift+Q / Alt+Shift+W | diagramly/EditorUi.js:23525-23526 |
| selectVertices / selectEdges / selectAll / selectNone | Ctrl+Shift+I / Ctrl+Shift+E / Ctrl+A / Ctrl+Shift+A | Actions.js:525-528 |
| lockUnlock | Ctrl+L | Actions.js:534 |
| findReplace | Ctrl+F | diagramly/EditorUi.js:23512 |
| format / outline / layers / toggleShapes | Ctrl+Shift+P / Ctrl+Shift+O / Ctrl+Shift+L / Ctrl+Shift+K | Actions.js:2259-2310, diagramly/EditorUi.js:23523 |
| pageView, guides | — | Menus.js 'view' |
| grid | Ctrl+Shift+G | Actions.js:1326 |
| resetView / zoomIn / zoomOut | Home / Ctrl++ (Ctrl+=) / Ctrl+- | Actions.js:1205-1222, EditorUi.js:11283-11289 |
| fitWindow / fitPage / fitSelection / actualSize | Ctrl+Shift+H / Ctrl+J / — / Ctrl+0 | Actions.js:1233-1276 (draw.io's Ctrl+0 is customZoom; Qraw resets to 100 %) |
| fullscreen | F11 | (desktop) |
| toFront / toBack / bringForward / sendBackward | Ctrl+Shift+F / Ctrl+Shift+B / Ctrl+Alt+Shift+F / Ctrl+Alt+Shift+B | Actions.js:582-594 |
| group / ungroup / removeFromGroup | Ctrl+G / Ctrl+Shift+U / — | Actions.js:657, :678 |
| enterGroup / exitGroup / home | Ctrl+Shift+End / Ctrl+Shift+Home / Shift+Home | Actions.js:575-577 |
| align*, distribute* | — | Menus.js 'align', 'distribute'; Graph.js:40214, :40430 |
| clearWaypoints | Alt+Shift+R | Actions.js:1918 |
| insertPage / duplicatePage / renamePage / deletePage | — | diagramly page menu |
| previousPage / nextPage | Ctrl+PgUp / Ctrl+PgDown | (desktop tabs convention) |
| preferences / keyboardShortcuts / about | — / F1 / — | |

Menus (Menus.js:389-480, desktop layout): **File** new, open, Open Recent ▸, save, saveAs, revert, —, exportAs, —, pageSetup, print, printPreview, —, close, quit. **Edit** undo, redo, —, cut, copy, paste, delete, deleteAll, —, duplicate, —, edit, editStyle, copyStyle, pasteStyle, —, selectVertices, selectEdges, selectAll, selectNone, —, lockUnlock, —, findReplace. **View** format, outline, layers, toggleShapes, —, pageView, grid, guides, —, resetView, zoomIn, zoomOut, fitWindow, fitPage, fitSelection, actualSize, —, fullscreen. **Arrange** toFront, toBack, bringForward, sendBackward, —, Align ▸, Distribute ▸, —, Navigation ▸ (home, exitGroup, enterGroup), —, group, ungroup, removeFromGroup, —, clearWaypoints, —, Pages ▸ (insertPage, duplicatePage, renamePage, deletePage, previousPage, nextPage). **Extras** preferences. **Help** keyboardShortcuts, about.

**Canvas-only keys** (`ActionRegistry::canvasKeys()`, handled in handler `keyPress`, so they only act when the canvas has focus): arrows nudge 1 px, Shift+arrows nudge by gridSize (EditorUi.js:10925 `nudge`; Alt+Shift+arrow resize is out of scope), Enter starts label editing, Escape cancels a gesture/editing or clears the selection.

**Focus rule.** Window shortcuts fire unless the focus widget accepts the `ShortcutOverride` event. Qt's text widgets (`QLineEdit`, `QTextEdit`, spin boxes) accept it for the keys they use (copy, paste, undo, select all, Delete, Backspace), so typing in the Format panel, the palette search or the inline label editor never triggers diagram actions. The inline editor additionally accepts ShortcutOverride for Ctrl+B/I/U and Ctrl+Enter. Shortcuts that would clash with text entry (single letters, plain Enter) are never window shortcuts.

## 7. Files, autosave, recovery

- Open: `qraw [file]` and File > Open, Open Recent, the Welcome dialog and drops of files onto the window all end in `DocumentController::openPath` → `FileService::load` (`format::readAnyFile`: `.drawio`, `.xml`, `.png`/`.svg`/`.pdf` with an embedded diagram) → `DocumentSession::resetDocument`. Loader warnings go to `DocumentPrompts::showWarnings`.
- Save keeps the file's kind (`FileKind`): `.drawio`/`.xml` via `format::writeFile` (Preserve encoding, atomic), `.drawio.png`/`.drawio.svg` re-exported with the diagram embedded; a PDF opens untitled-like (Save asks for a `.drawio` path).
- Dirty state = undo clean index; title `"<name>[*] - Qraw"` with `setWindowModified`.
- Autosave: every `AppSettings::autosaveIntervalSeconds` (default 30 s) while modified, `RecoveryStore::write(sessionId, document, path)` into `QStandardPaths::AppLocalDataLocation/recovery/` with a `QLockFile` per session. Clean save, revert, open, new and normal close remove the snapshot. On start, orphaned snapshots (lock not held) are offered: Restore (modified document, original path kept), Discard, Later.

## 8. Shape palette

`tools/palette/extract_palettes.py <webapp dir> <out dir>` (Python 3 stdlib, run with `-I`, never executes JavaScript) builds on `tools/drawio-extract/` (tokenizer `sidebar_scan.py`, evaluator `sidebar_eval.py`) and writes one JSON file per family into `resources/palettes/` plus `resources/palettes/REPORT.md` (per palette: entries written, entries skipped because they need JS evaluation, with file:line and reason), `README.md` (provenance, commit, regeneration command) and `LICENSE` notes (Apache-2.0 for the sidebar code; the stencil field-of-use terms of `resources/stencils/LICENSE` apply to the referenced shapes). Families: General, Misc, Advanced, Basic, Arrows (arrows + arrows2), Flowchart, UML, Entity Relation, BPMN, Network, AWS (aws4), Azure (azure2), GCP (gcp2), Kubernetes, Mockups, Floorplan, plus two derived palettes, Tables (entries of General/Misc/Advanced whose style has `shape=table`, `childLayout=tableLayout` or `shape=tableRow`) and Containers (`container=1` or `swimlane`).

Schema (version 1):

```json
{
  "schema": 1,
  "source": {"repo": "jgraph/drawio", "commit": "c72c9a98", "files": ["js/grapheditor/Sidebar.js"], "license": "Apache-2.0"},
  "palettes": [
    {"id": "general", "title": "General", "library": "", "order": 0,
     "entries": [
       {"id": "rectangle", "title": "Rectangle", "kind": "vertex", "style": "rounded=0;whiteSpace=wrap;html=1;",
        "w": 120, "h": 60, "value": "", "tags": ["rect", "rectangle", "box"], "showLabel": true},
       {"id": "line", "title": "Line", "kind": "edge", "style": "endArrow=none;html=1;", "w": 50, "h": 50, "value": "", "tags": ["line"]},
       {"id": "list", "title": "List", "kind": "cells", "xml": "<mxGraphModel><root>...</root></mxGraphModel>", "w": 160, "h": 110, "tags": ["list"]}
     ]}
  ]
}
```

`id` is unique per palette (slug of the title, `-2`, `-3` for duplicates) and stable across regenerations as long as titles are; favorites and recent shapes store `"<paletteId>/<id>"`. `kind: "cells"` holds draw.io's `addDataEntry` payloads decoded (`Graph.decompress`: base64 → raw inflate → `decodeURIComponent`) or multi-cell builders; its XML uses absolute geometry with top-level parent `"1"`. `PaletteEntry::cellsXml()` normalises vertex/edge entries to the same XML, so insertion has one path (`decodeCells` → `actions::insertBundle`).

## 9. Testing

- **QTest, offscreen.** Every `tests/ui/tst_*.cpp` becomes `tst_ui_<name>` (D-014), linked against `qraw_ui`, run with `QT_QPA_PLATFORM=offscreen`. Fixtures live in `tests/ui/data/`, helpers in `tests/ui/support/`. Do not name headers `ui_*.h` (AUTOUIC treats them as generated forms).
- **Shared helpers** (`tests/ui/support/test_support.h`, design-owned): `simpleDocument()` (a fixed one-page diagram with vertices `a`, `b`, edge `e`, group `g` with `c1`, `c2`), `makeCanvas()`, synthetic mouse input in scene coordinates (`press`, `move`, `release`, `click`, `doubleClick`, `drag`), `savedBytes()` for undo identity, `grabToPng()` (writes `$QRAW_SCREENSHOT_DIR/<name>.png` when set).
- **Undo identity.** Every test that edits through the UI checks that undo restores the saved bytes (`savedBytes` before == after undo): the undo guarantee of the command layer must survive the UI.
- **Screenshot harness** : `qraw --screenshot OUT.png [FILE] [--panel NAME]... [--size WxH] [--select ID]...` renders the full main window offscreen (`app_setup.h`). Used for visual review and by `tst_shell_smoke` (the image is non-blank and the canvas area differs from the background).
- **Smoke acceptance** (`tst_shell_smoke.cpp`): open a corpus file through `DocumentController::openPath`, select a vertex, move it by `actions`/session edit, save to a temp file, reload and compare; launch-and-close MainWindow without prompts.

## 10. Deliberate gaps

What the editor does not do is listed in [KNOWN_ISSUES.md](../../KNOWN_ISSUES.md) and the plans in [ROADMAP.md](../../ROADMAP.md). Cloud and online features are out of scope (D-000).
