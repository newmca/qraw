# Compatibility matrix

Levels: **supported** (read, modeled, written back), **partial** (works with stated limits), **fallback-rendered** (shown with a generic placeholder, kept intact), **preserved-not-rendered** (kept byte-for-byte or semantically, not interpreted).

Sections: [File formats](#file-formats), [Model constructs](#model-constructs), [Rendering and export](#rendering-and-export), [Editor behaviour](#editor-behaviour), [Platforms](#platforms), [Hardening](#hardening). A row says **supported** only when code and at least one test back it; the tests are named in the section intros. Anything not listed is not supported.

Verified against the corpus: 363 of 363 files (155 upstream templates, 200 generated, 8 hand-written fixtures; 410 pages) load, save semantically equal, reload equal and save a second time byte-identical (`tst_compat_corpus`). The same 363 files export every page to SVG and PNG without errors (`tst_render_corpus_export`, about 6 s in a release build).

## File formats

| Format | Level | Notes |
|---|---|---|
| `.drawio` `<mxfile>`, uncompressed pages | supported | encoding reproduced per page |
| `.drawio` compressed pages, `base64(deflateRaw(encodeURIComponent(xml)))` | supported | re-encoded on save (D-005) |
| legacy compressed pages without URI encoding | supported (read) | written back in the current encoding |
| zlib- or gzip-wrapped page payloads | supported (read) | written back as raw deflate |
| bare `<mxGraphModel>` `.xml` | supported | written back as a bare model |
| undecodable `<diagram>` payload | preserved-not-rendered | page kept verbatim (D-006), warning |
| XML declaration, DOCTYPE, comments, PIs around the root, and around the model inside a compressed payload | preserved-not-rendered | declaration encoding normalized to UTF-8; written inline, the payload's comments/PIs stay around the model and its declaration/DOCTYPE are dropped |
| comments/PIs next to a compressed payload in `<diagram>` | preserved-not-rendered | a page with a second `<mxGraphModel>` or stray text next to it is never compressed (it would read back differently) |
| entity references the reader cannot expand (undeclared or from an external DTD) | preserved (text), warning (attributes) | kept as `&name;` in element content; in attribute values they are indistinguishable from literal text, so a warning is issued when a DOCTYPE is present |
| whitespace in mixed content, `xml:space="preserve"` | preserved | only indentation (whitespace with a line break between elements) is regenerated |
| element nesting deeper than 512 levels | rejected | clean error instead of a stack overflow |
| `<mxlibrary>` shape libraries (`.xml`) | supported | read (plain and compressed entries, image entries) and written (uncompressed, `JSON.stringify(.., null, 2)` like draw.io) by `format::parseLibrary` / `serializeLibrary`; opened from File > Open Library, File > Open or a file drop, and shown in the palette. The diagram reader still refuses an `<mxlibrary>` file with a distinct error (D-008). Not supported: the web app's per-browser shape clipboard library, library `title` / `tags` attributes (ignored on read, not written), entry `style` |
| `.svg` / `.png` / `.pdf` with embedded diagram | supported | read (`--export IN`) and written (`--embed`); PDF stores the diagram in `/Subject` like draw.io, EmbeddedFile streams are read only |
| export to SVG, PNG, JPEG, PDF | supported | `qraw-cli --export` |
| `.vsdx` | not supported | no import code and no menu entry; planned in [ROADMAP.md](ROADMAP.md) |

## Model constructs

| Construct | Level | Notes |
|---|---|---|
| `mxfile` attributes (host, modified, agent, etag, version, type, compressed, unknown) | supported | order kept |
| unknown `mxfile` children | preserved-not-rendered | kept in position between pages |
| `diagram` id/name/unknown attributes | supported | |
| `mxGraphModel` attributes (dx, dy, grid, gridSize, guides, tooltips, connect, arrows, fold, page, pageScale, pageWidth, pageHeight, background, math, shadow, adaptiveColors, unknown) | supported | typed accessors with draw.io defaults |
| unknown `mxGraphModel` children / `root` attributes | preserved-not-rendered | |
| `mxCell` id, value, style, vertex, edge, connectable, visible, collapsed, parent, source, target | supported | canonical order for new attributes |
| `object` / `UserObject` / custom-tag wrappers and their attributes, placeholders, links, tooltips | supported | custom attributes preserved; `%name%`, `%id%`, `%width%`, `%height%` and the global variables evaluated when rendering (`placeholders="1"` on the wrapper) |
| unknown entries between cells in `root` | preserved-not-rendered | z-order position kept |
| `mxGeometry` x/y/width/height/relative, `offset`, `sourcePoint`, `targetPoint`, `Array as="points"` | supported | |
| other geometry children (e.g. `mxRectangle as="alternateBounds"`) | preserved-not-rendered | |
| style strings (names, key=value, duplicates, empty tokens) | supported | byte-identical when untouched |
| HTML labels, entities, newlines/tabs in attribute values | supported | stable escaping (D-004) |
| namespaced attributes/elements (`xmlns:x`, `x:attr`) | preserved-not-rendered | prefixes kept verbatim |
| CDATA sections | preserved-not-rendered | |
| dangling parent/source/target, duplicate ids, parent cycles | partial | loaded and reported as warnings, never repaired |

## Rendering and export

Checked by `tst_render_corpus_export` (every page of all 363 corpus files exports to SVG and PNG without errors), eight golden SVG fixtures (regression guards, not comparisons with draw.io) and the unit tests in `tests/render/`. "Rendered" means the output looks like draw.io's for the covered styles; it has not been compared pixel by pixel.

| Feature | Level | Notes |
|---|---|---|
| Style resolution, named styles, default stylesheet | supported | `resources/styles/default.xml` |
| Core shapes (rectangle, ellipse, rhombus, triangle, hexagon, cylinder, cloud, actor, parallelogram, trapezoid, step, ... 68 painters, among them the Shapes.js shapes cube, folder, umlLifeline, umlActor, umlFrame, module, component, message, start/endState, single/doubleArrow, datastore, offPageConnector, note2, curlyBracket, crossbar, lineEllipse, ext, waypoint, dimension), rounded corners, shadow, gradients, dashes, opacity, rotation, flip | supported | connection constraints of the JavaScript shape classes are not ported; `lifelineMirror` draws the foot box but not the copy of the label |
| `shape=curvedText` (Misc, "Curved Text"): the value along a round or quadratic curve (`arcStartY`, `arcMidY`, `arcEndY`, `curveType`, `align`, `verticalAlign`, font styles) | supported | `tst_render_shapes_js`, `tst_render_painter_canvas`, golden `curved-text` | arc handles and the autosize rule are not ported; SVG output uses `<textPath>`, which QtSvg does not draw |
| Flowchart, ER, UML, BPMN, network, mockup and other `mxgraph.*` stencil shapes | supported | drawn by the stencil interpreter from the vendored sets. JavaScript-only families with native painters: `mxgraph.bpmn.{task2,gateway2,event,shape}` (markers through stencils), `mxgraph.basic.rect`, `mxgraph.arrows2.{arrow,twoWayArrow}`, `mxgraph.floorplan.{wall,wallCorner,wallU,window,dimension,doorLeft,doorRight,doorDouble}`. Other JavaScript-only shapes are fallback-rendered (KNOWN_ISSUES); a shape name that draw.io has no painter for either (`shape=rect`) is the plain rectangle, as in draw.io |
| AWS4 composite icons (`resourceIcon`, `productIcon`, `group`), Kubernetes `icon` | fallback-rendered | background shape drawn, the icon glyph and group header are missing |
| Swimlane, containers, tables and rows | supported | rendering and editing: insert/delete rows and columns, resize rows and columns, connect to rows; merged cells (`rowspan`, `colspan`) are laid out and drawn, `tableRender=collapsed` is painted (striping colours and the title's glass effect are not) |
| `childLayout=stackLayout` | supported | executed on user edits (never on open or save); `horizontalStack`, `stackSpacing`, `stackBorder`, `margin*`, `resizeParent`, `resizeParentMax`, `resizeLast`, `allowGaps`, `stackUnitSize`, swimlane `startSize`/`footerSize` |
| `childLayout=tableLayout` | supported | executed on user edits (never on open or save); `fixedRows`, `resizeLast`, `resizeLastRow`, `startSize` of the table and of its rows, `horizontal=0` rows; `rowspan` and `colspan` of a cell (the covering cell grows, the covered ones are hidden) |
| `childLayout=treeLayout` | supported (approximated) | executed on user edits (never on open or save); `horizontalTree` (default 1), `treeLevelDistance` (default 30), `parentPadding` (default 20), `resizeParent`; our own tidy tree, not draw.io's ELK `mrtree` replacement, so positions differ; edges keep their style and lose their waypoints; the container only grows |
| `childLayout=circleLayout` | supported | executed on user edits; mxCircleLayout with the Graph.js wrapper (circle 20 px inside the container and below a `startSize` strip, container fitted to the circle, `transparentBounds` anchored in place); edges get `noEdgeStyle=1` and lose their waypoints |
| `childLayout=flowLayout` | supported (approximated) | executed on user edits; draw.io's ELK `layered` replacement of mxHierarchicalLayout: `flowOrientation` (default `east`, which flows right to left as Graph.getElkDirection does; `north` down, `south` up, `west` right), `intraCellSpacing` (30), `interRankCellSpacing` (100), `interHierarchySpacing` (60), `parallelEdgeSpacing` (10), `parentPadding`, `resizeParent`; our own layered layout, so positions differ; edges get `orthogonalEdgeStyle` and routes; the container only grows |
| Other `childLayout` values (`organicLayout`, `rack`) | preserved | stored and saved unchanged, not executed |
| `collapsed`, `alternateBounds`, `collapsible` | supported | the +/- control folds and unfolds, swapping the geometry with the alternate bounds |
| Edge styles: straight, elbow, orthogonal, entity relation, side-to-side, top-to-bottom, loop | supported | |
| `isometricEdgeStyle`, libavoid routing | fallback-rendered | drawn as straight or the base style |
| Line jumps (`jumpStyle` arc, gap, sharp, line; `jumpSize`, `noJump`, `jumpLayers`) | supported | on straight and rounded edges; `curved=1` edges draw no jumps and are not jumped over |
| Edge arrowheads | supported | all upstream marker names |
| `perimeter` rectangle, ellipse, rhombus, triangle, hexagon, parallelogram, trapezoid, step, ... | supported | callout, lifeline and cloud perimeters fall back to the rectangle |
| HTML labels | partial | the draw.io subset; arbitrary HTML/CSS is sanitized, not laid out like a browser. Tables: width and height of 100 % in an `overflow=fill` box, `cellpadding`, `border`, `border-collapse`, row and cell backgrounds, centred cells; `<hr>` colour and position, list indent and inner `text-align` differ by a few pixels (docs/FIDELITY.md) |
| Placeholders `%name%`, `%id%`, `%width%`, `%height%`, `%page%`, `%pagenumber%`, `%pagecount%`, `%filename%`, `%date%`, `%time%`, `%timestamp%` | supported | active only when the value's wrapper has `placeholders="1"` (a style flag alone does not count); `%page%`, `%pagenumber%` and `%pagecount%` are set by the canvas, `%filename%` is unset in the app and in export (the token stays literal), so export leaves the page variables literal; date and time use the system short formats; `date{fmt}`, unit suffixes and `%length%` stay literal |
| `math=1` | preserved, rendered as plain text | math typesetting is not planned (not planned); the flag round-trips and the formula text is shown as written |
| `sketch=1`, `comic=1`, `sketchStyle`, `jiggle`, `bowing`, `curveFitting`, `fillStyle`, `fillWeight`, `hachureAngle`, `hachureGap`, `dashGap`, `dashOffset`, `disableMultiStroke`, `disableMultiStrokeFill` | partial | drawn by a clean-room hand-drawn renderer, not rough.js: doubled bowed strokes and hachure, cross-hatch and dashed fills (solid for white or gradient fills); `zigzag`, `zigzag-line` and `dots` fills are hatched. Deterministic per cell id, so it never changes between renders. The sketch font is not substituted |
| `adaptiveColors` (`auto`, `simple`, `none`), `light-dark(a, b)` colour values, dark mode | supported | see "Dark diagram mode" below; `tst_render_adaptive_colors`, `tst_ui_canvas_tiles` |
| `glass=1`, graph theme | not supported | ignored |
| Images from `data:` URIs | supported | PNG, JPEG, SVG |
| Images from `img/lib/**` and remote URLs | not supported | not vendored and never fetched; a gray frame is drawn |

## Editor behaviour

The editor is checked by the UI tests in `tests/ui/` (offscreen platform, synthetic mouse and key events) and by `tst_ui_e2e_editor`, which drives open, select, move, resize, connect, label edit, format, undo/redo, layer, page, palette drop, PNG export, save and reopen. Every edit is one or more `commands::` operations; open and save never run a layout. The limits in the last column are detailed in [KNOWN_ISSUES.md](KNOWN_ISSUES.md).

| Feature | Level | Tests | Limits |
|---|---|---|---|
| Selection: click, shift/ctrl toggle, rubber band, select all, group drill-down | supported | `tst_ui_select_selection`, `tst_ui_canvas_projection` | the rubber band selects cells entirely inside it, including horizontal and vertical edges and zero-size vertices; a rotated vertex counts with its rotated bounding box (`mxGraph.getCells`) |
| Move with grid snapping, guides, Ctrl+drag duplicate, nudge | supported | `tst_ui_select_move`, `tst_ui_select_snapper`, `tst_ui_interaction_layer` | the move is previewed live: the cells themselves follow the pointer (vector-sharp, edges attached to them re-routed), with the dashed outline and the guides on top; draw.io shows dashed outlines only. While dragging, the moving cells are drawn above everything else (draw.io's preview is an overlay too); the stacking order is restored on release. |
| Resize and rotate (single vertex, union of several cells) | partial | `tst_ui_select_resize`, `tst_ui_select_multiresize`, `tst_ui_interaction_layer` | a single vertex is resized or rotated live (the cell is redrawn for the previewed geometry, its children stay until the release, attached edges follow; draw.io shows a dashed outline); multi-selection preview outlines the cells only, no live children or layouts; grid snapping skipped for rotated vertices |
| Connect: hover arrows, connection points, drag to connect, clone, dangling edge | supported | `tst_ui_connect_edit` | |
| Edge handles: ends, waypoints, virtual handles, segment handles with routed preview | partial | `tst_ui_connect_edit` | loops and entity-relation edges keep plain waypoint and virtual handles |
| Label editing (plain and HTML), autosize on commit | partial | `tst_ui_connect_label`, `tst_ui_editor_behaviour` | markup beyond b/i/u/s/sub/sup/a/font/span/lists/div/br is flattened; autosize measures with Qt text layout, so sizes differ slightly from draw.io |
| Format panel (Diagram, Style, Text, Arrange) with mixed state | supported | `tst_ui_format_panel`, `tst_ui_format_summary` | the font family is a searchable combo of draw.io's default fonts, recent fonts and the installed families; a comma list is stored verbatim; installed fonts are listed by family name only (no `fontSource` web fonts) |
| Copy Style, Paste Style (Edit menu, context menu, Style tab) | partial | `tst_session_edit`, `tst_ui_style_defaults` | Paste Style replaces the whole style text of the target; draw.io pastes only `Graph.cellStyles` keys |
| Set as Default Style / Clear Default Style (`currentVertexStyle`, `currentEdgeStyle`) | partial | `tst_commands_default_style`, `tst_ui_style_defaults` | see "Default styles" below |
| Containers and swimlanes: drop into, drag out, grow, collapse/expand | supported | `tst_ui_select_containers`, `tst_commands_ops_layout` | drop target is the topmost cell under the pointer, not `mxGraph.getSwimlaneAt`; no keyboard shortcut for fold |
| Stack layout (`childLayout=stackLayout`) | supported | `tst_commands_ops_layout`, `tst_ui_select_containers` | ignores `wrap` and `borderCollapse=false` |
| Tree and circle child layouts (`childLayout=treeLayout`, `circleLayout`) | supported | `tst_commands_layout_ops` | the tree is our own tidy tree (see the layouts row) |
| Tree, circle and flow child layouts (`childLayout=treeLayout`, `circleLayout`, `flowLayout`) | supported | `tst_commands_layout_ops` | the tree and flow layouts are our own (see the layouts row) |
| Other `childLayout` values (`organicLayout`, `rack`) | preserved-not-rendered | | stored and saved, never executed |
| Tables: `tableLayout`, insert/delete row and column, row and column resize, merge and unmerge cells, collapsed borders, connect to rows | partial | `tst_ui_tables`, `tst_commands_ops_tables`, `tst_render_tables_collapsed`, golden `tables-merged` | no drag-to-move of rows or columns, no Ctrl border drags, no striping colours; handles hidden on rotated tables (KNOWN_ISSUES.md, "Tables") |
| Table grid lines on the canvas | supported | `tst_ui_canvas_table_lines`, `tst_render_scene_renderer` | |
| Find / replace (labels, custom attributes, shape types; plain text or regex, whole word, all pages) | partial | `tst_ui_docks_find` | Replace never edits styles; a replacement inside an HTML label works within one text node only |
| Align, distribute, z-order, lock, group/ungroup | supported | `tst_ui_shell_actions`, `tst_ui_session_edit` | Align Center/Middle use the first selected cell's centre (as draw.io) |
| Arrange > Layout: Horizontal Flow, Vertical Flow, Horizontal Tree, Vertical Tree, Radial Tree, Org Chart, Organic, Circle | partial | `tst_commands_layout_tree`, `tst_commands_layout_radial_circle`, `tst_commands_layout_flow`, `tst_commands_layout_ops`, `tst_ui_arrange_actions` | draw.io runs these through ELK (JavaScript: `mrtree`, `radial`, its own org chart library) and the classic mxGraph layouts are not in the upstream clone, so the algorithms are our own (D-038) and the positions differ from draw.io's, as do the spacings (30 between levels, 20 between neighbours; org chart 40 / 20; radial 40 between rings). Org Chart is a vertical tree with elbow edges: draw.io's own chart library (stacked leaves, its dialog) is approximated. Circle is a port of mxCircleLayout. Radial rings grow where nodes would touch. Rank aligned levels. Trees follow outgoing edges and order siblings by z-order. The entries have no dialog; Balloon Tree, Mindmap and Parallels are not built. Flow (ELK `layered`) and Organic (ELK `force`) are also our own (D-039): flow uses longest-path ranks, barycenter ordering and least-squares positions, with orthogonal waypoints through the long-edge dummies and 30 / 100 spacings; organic is Fruchterman-Reingold with a force constant of 50 between node borders, deterministic and independent of the current positions (draw.io's keeps them) |
| Insert > Text, Image..., Link..., Horizontal Line, Vertical Line, Table | partial | `tst_ui_insert_actions` | styles and sizes of `Actions.js` (`insertLink`, `image`), `Sidebar.js` (lines) and `Graph.createTable` (60 x 40 cells, collapsed borders, no title); images embedded as `data:image/png,<base64>`, scaled to fit 1200 x 1200 (`EditorUi.maxImageSize`), `aspect=fixed`; no Title and Container options in the table picker, no Alt for a free insert point, no single-key shortcuts (A, L); Insert > Advanced and the other draw.io Insert items are implemented or not planned (KNOWN_ISSUES.md) |
| Insert > Advanced: From Text..., CSV..., Mermaid... | partial | `tst_commands_import_text`, `tst_commands_import_csv`, `tst_commands_import_mermaid`, `tst_ui_import_dialogs` | CSV is a port of `doImportCsv`; Mermaid is a documented subset (flowchart and sequenceDiagram) read by our own parser, not the mermaid library; layouts are ours (D-038, D-039); no PlantUML, no update of existing cells by `identity`, no `container` or `list`; see "Insert > Advanced" below |
| Edit Data (Ctrl+M), Edit Link, Edit Tooltip, Edit Image, Open Link | partial | `tst_ui_data_actions`, `tst_commands_ops_properties` | wrapping as draw.io (`<object>` from Edit Data, `<UserObject>` from link and tooltip), `tags` in its own field, `linkTarget=_blank`, `data:page/id,` links, `data:image/png,<base64>` embedding; no WYSIWYG tooltip editor, no `label` row for layers, no `placeholder` quirk, no crop, no Edit Note (KNOWN_ISSUES.md) |
| Flip horizontal/vertical, Rotate 90 Degrees (Ctrl+R), Autosize (Ctrl+Shift+Y), Edit Geometry (Ctrl+Shift+M) | partial | `tst_ui_arrange_actions`, `tst_commands_ops_transform`, `tst_ui_shell_actions` | the reverse turn (draw.io turns the other way with Shift) is not in the UI; the direction style is stored even without a view state; Autosize skips groups, tables, swimlanes and relative geometries; Edit Geometry has no Relative or Offset fields; see KNOWN_ISSUES.md "Arrange" |
| Extras > Edit Diagram... | supported | `tst_commands_replace_page_model`, `tst_ui_extras_actions` | the page's `<mxGraphModel>` as text (the dialog of `EditDiagramDialog`, Dialogs.js); upstream's extra "Select..." menu (copy to clipboard, open in a new window, extract text, anonymize) is not offered; text that uses an entity declared in the file's DOCTYPE cannot be parsed once edited |
| Units (View > Units, Diagram tab) | supported | `tst_ui_extras_actions` | points, inches, millimeters, centimeters, meters with `Editor.toUnit` / `fromUnit` (3.937 px per mm, 100 px per inch). **Stored in the application settings, not in the file**, as draw.io does (`mxSettings.setUnit`); the model is always in pixels. Points show two decimals and are not rounded to 0.1 px. The rulers, the unit of the drag hint and the unit-sized arrow-key step of draw.io are not implemented |
| Edit > Copy as Image (Ctrl+Alt+X) | supported | `tst_ui_extras_actions` | PNG at the screen's pixel ratio, selection or page; the `image/png` flavour carries the selected cells (or the page) as an embedded diagram. Pasting it into Qraw inserts it as an image; "Copy as SVG" is not offered |
| Page tabs: drag to reorder, Move Left / Right | supported | `tst_ui_extras_actions`, `tst_ui_docks_pages` | one undo step per drag; the collapsible tab strip is not implemented |
| Copy, paste, duplicate, copy/paste style | partial | `tst_ui_session_transfer`, `tst_ui_session_edit` | duplicating edges across parents drops their terminals; pasting part of a table does not re-lay it out |
| Shape palette, search, favorites, recent | supported | `tst_ui_palette_dock`, `tst_ui_palette_library` | Azure entries need `img/lib` images that are not shipped (their palette sections are titled "icons not included") |
| User libraries (`.xml` mxlibrary) | partial | `tst_ui_palette_userlib`, `tst_format_library` | entries are not searchable and cannot be favorites; `title`/`tags` ignored |
| Templates gallery, welcome dialog | supported | `tst_ui_dialogs_welcome` | the upstream templates of the corpus |
| Keyboard navigation between cells: Tab, Shift+Tab, Alt+Tab, Alt+Shift+Tab, arrows | partial | `tst_ui_keyboard_navigation` | see "Keyboard navigation" below |
| Accessibility: names on all controls, the canvas as "Diagram", View > Cells | partial | `tst_ui_accessibility`, `tst_ui_docks_cells` | draw.io has no equivalent of the Cells panel; no accessible child per cell; no run with a real screen reader |
| Menu texts and shortcuts (KDE HIG) | partial | `tst_ui_menu_conventions`, `tst_ui_shell_actions` | see "Menus and shortcuts" below |
| Pages, layers, outline | supported | `tst_ui_docks_pages`, `tst_ui_docks_layers`, `tst_ui_docks_outline` | |
| Themes (light, dark, high contrast), preferences, shortcuts, translations | supported | `tst_ui_dialogs_settings`, `tst_ui_shell_window` | right-to-left layout untested |
| Files: new, open, save, revert, recent, drag and drop | supported | `tst_ui_session_files` | |
| Autosave and crash recovery | supported | `tst_ui_session_files` | |
| Export dialog (PNG, JPEG, SVG, PDF, "Include a copy of my diagram") | supported | `tst_ui_dialogs_export`, `tst_format_embed` | `--embed` is ignored for JPEG, without a warning |
| Print and print preview | partial | `tst_ui_dialogs_print` (prints to PDF) | the interactive print dialog and the preview window are not covered by tests |
| Edit Shape (stencil) dialog | supported | `tst_ui_edit_shape` | |
| `.vsdx` import | not supported | | |

Interactive behaviour on a real desktop (X11, Wayland, high DPI, input methods) has only been exercised through synthetic events.

## Keyboard navigation

draw.io (`EditorUi.onKeyDown`, `Graph.selectCell`, `Graph.selectParentOrPreviousCell`, `mxGraph.selectChildCell`) and Qraw:

| Key | draw.io | Qraw |
|---|---|---|
| Tab | next cell: vertices and edges that are shown and selectable, in document order, containers before their children; at the last cell the selection is cleared and the browser moves the focus on | the same order; **wraps** from the last cell to the first; the keyboard focus never leaves the canvas while there is a cell; on a page with nothing to select the focus moves on |
| Shift+Tab | the container of the selected cell, else the previous cell (newer builds) | the previous cell in the same order (wraps); the container is Alt+Shift+Tab |
| Alt+Tab | the parent cell | the first child of the selected cell (draw.io's `selectChildCell`) |
| Alt+Shift+Tab | | the container of the selected cell; nothing for a cell that sits on a layer or at the top of the entered group |
| Ctrl+Tab | the browser's | leaves the canvas for the next control (Ctrl+Shift+Tab: the previous) |
| Arrows, Shift+Arrows | move the selection by 1 unit, by the grid size | the same; the keys of one sequence are one undo step (merge key) |
| Enter, F2 | edit the label | the same |

Differences: with an entered group Tab visits only that group's cells (draw.io ignores `currentRoot` there); the selected cell stays in the order even when it is no longer selectable; the view scrolls minimally to show the cell (40 px margin). Alt+Tab and Alt+Shift+Tab are used by the window switcher on most desktops and may never reach the canvas (KNOWN_ISSUES.md). Tab while a label is being edited is the label editor's (it commits the label, unchanged by this work; draw.io indents).

## Menus and shortcuts

Texts follow the KDE HIG (D-050): Title Case, "…" (U+2026) only on commands that always open a dialog that asks for input, "Full Screen". The wording of the entries is draw.io's; only the ellipsis character and where it is written differ. Shortcuts are draw.io's wherever that does not clash with KDE: F1 (Keyboard Shortcuts) and F2 (Edit) are KDE's Help and Rename; F11 is Full Screen; the other bare keys are draw.io's (Delete, Backspace, Home, X for Freehand). The platform's standard shortcuts are used where Qt has one (`QKeySequence::StandardKey`); on Plasma Qt maps Full Screen to Ctrl+Shift+F, which is draw.io's To Front, so Full Screen is F11 and To Front keeps Ctrl+Shift+F. No shortcut was moved: no bare function key outside KDE's set was in use.

## Platforms

| Platform | Built and tested by CI | Qt in CI | Notes |
|---|---|---|---|
| Linux (Ubuntu 24.04) | release preset, full `ctest` | 6.8 | the platform the program is developed and used on (Fedora 44, GCC 16, Qt 6.11 by hand) |
| macOS (latest, arm64) | release preset, full `ctest` | 6.10 | much less used than Linux |
| Windows (2022, MSVC) | release preset, full `ctest` | 6.8 | much less used than Linux |

The whole suite (about 160 CTest entries, including `fuzz_short`, `fuzz_regressions` and `bench_smoke`) is meant to pass on all three platforms, and passes in the `dev`, `release` and `asan` presets on Fedora 44. macOS needs Qt 6.10 (Qt 6.8 links the removed AGL framework), a shortcut filter that also matches portable key text, and platform-stable arc splitting in `arcToCurves`; Windows needs fonts for the offscreen platform (`QT_QPA_FONTDIR`, set by the test environment). GUI tests run offscreen; no interactive desktop session is exercised in CI.

Minimum versions: Qt 6.5 (CMake `find_package(Qt6 6.5)`), C++20 compiler (GCC 11, Clang 14, MSVC 2022). Qt 6.6 or later is needed to keep a `standalone` attribute in the XML declaration. Qt below 6.9 links the AGL framework on macOS, which newer SDKs no longer ship: use 6.9 or later there. Per-platform install commands are in the [README](BUILDING.md#dependencies).

## Hardening

- **Fuzzing.** `tests/fuzz/qraw-fuzz` mutates corpus seeds against five parsers (file codec, compression, style, stencil, HTML labels) and checks round-trip and idempotence invariants; a short run and the regression fixtures are CTest entries (`fuzz_short`, `fuzz_regressions`). Long runs (120 s per target, two seeds, release) found three `sanitizeHtml` bugs, all fixed with fixtures; nothing in the other four targets. See [BUILDING.md](BUILDING.md#fuzzing) and D-028.
- **Sanitizers.** The full test suite and a 120 s fuzz run per target are clean under AddressSanitizer and UBSan (`ctest --preset asan`); no suppressions. There is no sanitizer job in CI. See [BUILDING.md](BUILDING.md#sanitizers).
- **Performance.** 1k, 10k and 50k cell diagrams are timed by `tests/bench/qraw-bench`; results are in [docs/BENCHMARKS.md](docs/BENCHMARKS.md).
- **Not claimed.** Rendering matches draw.io's closely but not pixel for pixel ([docs/FIDELITY.md](docs/FIDELITY.md)), and the editor has had little use on real desktop sessions outside its author's.

## Default styles (`currentVertexStyle`, `currentEdgeStyle`)

Set as Default Style copies the keys of draw.io's `Graph.cellStyles` (text, edge and shape-appearance keys such as `fillColor`, `strokeColor`, `fontSize`, `rounded`, `endArrow`, `edgeStyle`) from the selected cell, but not `shape` for vertices and not a transparent stroke (`strokeColor=none`), as `Graph.copyCellStyles` with `force`. New cells take them as `Graph.pasteCellStyles` does without `force`: a key the palette entry already defines stays (and so do the other keys of its group: fill, gradient and gradient direction; start and end arrow; sizes; perimeter spacings; rounded; opacity), text cells only take text styles unless the default was set from a text cell, keys of edges never reach vertices, and an edge label takes the text styles of the edge default. A new connector takes `edgeStyle`, `curved`, `rounded`, `shape` and the sketch keys from the edge default alone (`Graph.createCurrentEdgeStyle`), so a default edge without `edgeStyle` makes straight connectors.

Differences: (1) only Set as Default Style changes the defaults. draw.io also lets `currentVertexStyle` follow the last style edit made in the Format panel (`styleChanged`), so a shape inserted after changing a fill takes that fill; Qraw does not. (2) The keys are read from the cell's own style, not the resolved style (a named style from a custom stylesheet is not expanded). (3) The defaults are application settings (`style/defaultVertex`, `style/defaultEdge`, `style/defaultVertexFromText`), shared by all windows and kept after a restart, not part of the document; draw.io keeps only the edge choice in its settings. (4) A drop with Shift held applies the defaults (draw.io skips them). (5) There is no theme default style (`defaultVertexStyle` / `defaultEdgeStyle` of the configuration); "clear" means no default.

## Insert > Advanced (From Text, CSV, Mermaid)

The importers (`commands/import_diagram.h`) read text into detached cells, arrange them with the layouts of Arrange > Layout and insert them as one undo step at the insert point. The same text always gives the same cells. Imports above 10,000 shapes are an error, and a force-directed (organic) layout above 500 shapes becomes a layered one (it compares every pair of shapes). Errors name the line and, where known, the column, and nothing is inserted.

**From Text.** *List*: each non-empty line is a vertex, a tab counts as 4 spaces, a line is the child of the nearest line above with less indentation (`EditorUi.parseOutline`), `""` is an empty label; laid out as a vertical, horizontal or radial tree. *Diagram*: `A -> B`, `A -> B -> C`, `A <- B`, `A <-> B` and `A -- B` (no arrowheads), `: label` at the end labels every connection of the line, a name in double quotes may hold `->` and `:`, a line with one name is a vertex, `//` starts a comment line; the same name is the same vertex; laid out as a vertical or horizontal flow. draw.io's ParseDialog (the other text types: tables, PlantUML, ...) is not in the upstream clone; its outline types besides the trees (balloon tree, mindmap) are not offered.

**CSV.** Header lines start with `#` (`##` is a comment, a trailing `\` continues the directive on the next `#` line); the first other line holds the column names, then one cell per line, fields as RFC 4180 (`"a, b"`, `""` for a quote; a quote that is not closed is an error, which draw.io accepts). Each row becomes a vertex with a `UserObject` value: `label` (the template, `placeholders=1`), one property per column (the column name as an XML attribute name) and `link` for the `link` column.

| Directive | Support |
|---|---|
| `label`, `labelname`, `labels` | supported (`%name%` placeholders stay in the label and are replaced when drawn) |
| `style`, `stylename`, `styles`, `vars`, `parentstyle`, `unknownStyle` | supported; placeholders in styles are replaced once at the import (`%id%` only with an identity column); `stylePlaceholders=1` is not supported (warning) |
| `identity`, `namespace`, `parent` | supported inside the import (a parent is an earlier row, by identity or by row number; the parent takes `parentstyle` and, with automatic sizes, the bounds of its children); rows are never matched with cells already in the diagram, so a re-import adds cells instead of updating them, and the cell ids are new ones |
| `width`, `height`, `padding`, `left`, `top`, `collapsed` | supported (`auto` is the size of the label from the raster canvas, `@column` reads a column, `height: width`) |
| `ignore`, `link` | supported |
| `connect` | supported: `from`, `to`, `invert`, `label`, `fromlabel`, `tolabel`, `sourcelabel`, `targetlabel`, `style`, `placeholders`, `labels` (extra label cells) and `data`; the target column may list values separated by commas |
| `nodespacing`, `levelspacing`, `edgespacing` | supported (tree and flow spacings, parallel edge spacing) |
| `layout` | `auto`, `none`, `verticaltree`, `horizontaltree`, `verticalflow`, `horizontalflow`, `organic`, `circle`, `orgchart`, `elkRadial` (radial tree), `elkOrganic` and `elkStress` (organic); `libavoid`, `parallels`, JSON layouts and unknown names give a warning and leave the cells in a column |
| `container`, `list` | not supported (warning) |
| any other key | warning, ignored |

`auto` follows doImportCsv: one root and as many connections as a tree has gives a vertical tree, one root a vertical flow, several roots with connections the organic layout, no connections a column.

**Mermaid.** Comments (lines starting with `%%`), front matter and `%%{...}%%` directives are skipped (the last two with a warning); `;` ends a statement. Text may be quoted (`"..."`), contain `<br>` and the entity codes `#quot;` and `#35;`; other HTML is shown as text.

*Flowchart* (`graph` or `flowchart`, then TD, TB, BT, LR or RL):

| Construct | Maps to |
|---|---|
| `A[text]`, `A(text)`, `A([text])`, `A[[text]]`, `A[(text)]`, `A((text))`, `A(((text)))` | rectangle, rounded, stadium (`arcSize=50`), subroutine (`process`), database (`cylinder3`), circle, double circle (`doubleEllipse`) |
| `A>text]`, `A{text}`, `A{{text}}` | flag (`step`, notch on the left, not Mermaid's exact shape), rhombus, hexagon |
| `A[/text/]`, `A[\text\]`, `A[/text\]`, `A[\text/]` | parallelogram, parallelogram flipped, trapezoid, trapezoid flipped |
| `-->`, `---`, `-.->`, `-.-`, `==>`, `===`, `--x`, `--o`, `<-->`, `x--x`, `o--o`, `~~~` (more dashes make no difference) | edges with `endArrow` / `startArrow` (classic, none, cross, oval), `dashed=1`, `strokeWidth=3`; `~~~` is drawn invisible (`strokeColor=none`) |
| `\|text\|`, `-- text -->`, `-. text .->`, `== text ==>` | the edge label |
| `A --> B --> C`, `A & B --> C & D` | chains and groups: one edge per pair |
| `subgraph id [Title]` ... `end` (nested; `direction` inside) | a container (`container=1`) laid out on its own, its nodes are its children; an edge may end at a subgraph |
| `classDef a,b fill:#f9f,stroke:#333,stroke-width:4px,color:#fff,font-size:14px,font-weight:bold,font-style:italic,stroke-dasharray: 5 5,opacity:0.5`, `class A,B name`, `A:::name`, `style A fill:#f9f`, `linkStyle 0,1 stroke:#f00` / `linkStyle default ...` | `fillColor`, `strokeColor`, `strokeWidth`, `fontColor`, `fontSize`, `fontStyle`, `dashed` + `dashPattern`, `opacity`; `rgb(r, g, b)` becomes hex; `classDef default` applies to every node; other properties give one warning each |
| `click`, `callback`, `href`, `accTitle`, `accDescr`, `title` | ignored with a warning |

Not supported (an error): `A@{ shape: ... }` and edge ids (`e1@-->`), markdown formatting other than the surrounding backticks, icons and images, `flowchart` `ELK` renderer options. The layout is the layered layout (D-039) in the declared direction, per level; edges that cross a subgraph border are routed by the renderer, not by the layout.

*Sequence diagram* (`sequenceDiagram`):

| Construct | Maps to |
|---|---|
| `participant A`, `participant A as Alice`, `actor B as Bob`, `create participant X` | `umlLifeline` boxes in order of declaration or first use; an actor is `participant=umlActor`; `destroy` is ignored with a warning |
| `->>`, `-->>`, `->`, `-->`, `-x`, `--x`, `-)`, `--)`, `<<->>`, `<<-->>` and `A->>A` | edges between the lifelines at the height of their row (`exitDy` / `entryDy`, `exitPerimeter=0`): block, none, cross and open arrowheads, dashed for the double-dash forms, a self message loops to the right |
| `+` and `-` after the arrow, `activate A`, `deactivate A` | activation bars (10 wide, nested ones shifted by 5), messages meet the edge of the bar |
| `Note left of A: text`, `Note right of A: text`, `Note over A: text`, `Note over A,B: text` | `shape=note` cells (`<br>` breaks lines) |
| `loop`, `alt` / `else`, `opt`, `par` / `and`, `critical` / `option`, `break` (nested) | `umlFrame` over the lifelines it uses, a text cell `[text]`, dashed dividers with their text |
| `autonumber` | messages are prefixed `1. `, `2. ` ... |
| `rect`, `box` | the block is skipped with a warning (its lines are read as usual) |
| `title`, `link`, `links`, `properties`, `details` | ignored with a warning |

The columns are spaced so that every message text and note fits; row heights follow the text. Messages are connected to the lifelines but draw.io's `sequenceEdgeStyle` is not used: they are straight edges attached at fixed points, so moving a lifeline moves the ends of its messages but not their height.

Any other diagram type (class, state, ER, Gantt, pie, journey, git graph, mindmap, timeline, ...) is an error that names the supported types. PlantUML is not implemented.

## Dark diagram mode (`adaptiveColors`)

**File.** `mxGraphModel@adaptiveColors` is read, kept and written where draw.io writes it (after `shadow`); the Diagram tab's box sets `auto`, `simple` or `none` and removes the attribute for Default (Automatic), like Editor.js. A value that is none of these is kept in the file and treated as `auto`. **Mapping** (`mxUtils.getInverseColor`, `Graph.js` and `mxSvgCanvas2D.getLightDarkColor`; D-043): in the dark scheme a `default` fill (and background) colour is `#121212` and every other `default` colour (stroke, text, border, gradient) is white; `light-dark(a, b)` shows `b`; with `auto` every other explicit colour of a colour key (`fillColor`, `strokeColor`, `fontColor`, `gradientColor`, `labelBackgroundColor`, `labelBorderColor`, `swimlaneFillColor`, `imageBackground`, `imageBorder` and keys ending in `Color`, `Color2` to `Color4`) and the inline colours of labels are inverted; `simple` leaves them; `none` keeps the page in the light scheme. The page background is inverted as a colour (`#121212` without one). **Whether the scheme is dark** is a user preference (View > Dark Diagram, stored in the application settings, not in the file) and an option of an export, because draw.io takes it from the application's appearance; Qraw's own theme stays independent.

Differences: (1) colours that do not come from the style are not mapped (the fixed black and white of a few painters, explicit colours inside stencils, images), so a stencil with a hard-coded colour keeps it on the dark page. (2) `Editor.systemColors` (`Canvas`, `CanvasText`) and `var(--...)` colours are not supported: they are no colour here. (3) The colour pickers do not offer a dark colour per swatch (`light-dark` entries are read, not written). (4) The label editor draws the text in the colour the diagram shows but loads the label as stored, so an inline colour is the stored one while it is edited.

## Freehand

draw.io's freehand source (`mxFreehand`) is not in the vendored sources, so the stored form could not be compared with a draw.io file. A stroke is a vertex with `shape=stencil(<compressed shape XML>)` (the form of Edit Shape and of every custom shape), one open path of quadratic segments, `fillColor=none`, `strokeWidth` from the default style or 2: draw.io renders and edits it as any custom shape, and it scales with the cell. If draw.io itself stores freehand strokes some other way, files drawn there open as whatever shape they are (an edge or another shape, drawn by the existing painters), and files drawn here open in draw.io as custom shapes. The brush options of draw.io (size and smoothing window, Close Path, pressure) are not offered (D-044).

