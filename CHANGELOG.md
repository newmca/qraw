# Changelog

All notable changes are listed here. The format follows [Keep a Changelog](https://keepachangelog.com/en/1.1.0/) and the project follows [Semantic Versioning](https://semver.org/).

## [Unreleased]

### Changed
- The diagram canvas follows the application theme, as in draw.io: a dark theme (and High Contrast) shows the dark scheme, a light theme the light one, and System follows the OS colour scheme, also when it changes while Qraw runs. View > Diagram Appearance (Follow Theme, Light, Dark; also in Preferences) pins the canvas to one scheme. The old View > Dark Diagram switch is replaced by it: a stored "on" becomes Dark, "off" becomes Follow Theme. The Export As dialog's "Dark appearance" starts from the scheme the canvas shows. `qraw --screenshot` has `--light-diagram` next to `--dark-diagram`, and `--theme` now also decides the canvas.
- While a shape is dragged into a container, the container is outlined in green on a dark canvas and in blue on a light one, as draw.io does.
- The System theme gives the application palette back to the platform instead of keeping a copy of the startup palette, so the widgets follow the OS colour scheme too.

## [0.1.0] - 2026-11-26

The first release. Qraw is a native Qt 6 / C++20 editor for draw.io files; see [README.md](README.md) for what it is and [ROADMAP.md](ROADMAP.md) for what is not done yet. It is licensed under the Apache License 2.0 ([LICENSE](LICENSE), [NOTICE](NOTICE)); the draw.io code and data it derives from are Apache-2.0 too ([THIRD_PARTY.md](THIRD_PARTY.md)).

### Added

#### File format and model
- A lossless document model and codec for `.drawio` (`<mxfile>`) and bare `<mxGraphModel>` `.xml` files. Unknown elements, attributes, comments, CDATA, processing instructions, namespaces and style keys survive a load and save in their original order; an untouched style or number re-serializes byte-identically.
- Compressed pages (`base64(deflateRaw(encodeURIComponent(xml)))`), the older payload forms (no URI encoding, zlib-wrapped) and undecodable pages (kept verbatim). Each page keeps its encoding; one encoding can be forced for all pages.
- Deterministic, atomic saving, and `format::semanticEqual` for comparing documents. Malformed XML is reported with line and column; model problems (dangling references, duplicate ids, parent cycles) are reported as warnings.
- Shape libraries (`<mxlibrary>` files), read and written.
- A regression corpus of 363 files (155 of them draw.io's own templates) that is checked for semantic round trips, byte-stable second saves and export without errors.

#### Command line
- `qraw-cli` (also available as flags of `qraw`): `--validate`, `--info`, `--convert` (optionally changing the page encoding) and `--export` to SVG, PNG, JPEG and PDF, one page or all pages, with scale, border, transparency, dark appearance and `--embed`.

#### Rendering and export
- Style resolution with draw.io's default stylesheet, 68 native shape painters (basic, flowchart, swimlane and table containers, images, UML, BPMN 2, arrows, floorplan and the JavaScript-only shapes of draw.io's `Shapes.js`), a stencil interpreter with 204 vendored draw.io stencil sets (9,089 shapes) and a labelled fallback for shapes it does not know.
- Edges: straight, elbow, orthogonal, entity-relation, side-to-side, top-to-bottom, loop and curved routing, perimeters, ports, waypoints, edge-to-edge links, all draw.io arrowheads, edge labels and line jumps.
- HTML labels (bold, italic, lists, links, colours, wrapping, tables) and `%placeholders%`.
- Hand-drawn styles (`sketch=1`, `comic=1`), curved text, dark diagram mode (`adaptiveColors`, `light-dark()`).
- Export to SVG (own writer), PNG, JPEG and PDF. SVG, PNG and PDF can embed the editable diagram, and Qraw can open them again.
- Diagram text does not depend on the screen: glyph hinting is off and font sizes are pixels, so a PNG export has the same pixels at every font DPI.

#### Editor
- Main window with menus, toolbar, shape palette, Format panel, page tabs, and Layers, Outline, Find/Replace and Cells docks. Light, dark and high-contrast themes; all strings go through `tr()`.
- Canvas: grid, page view, pan, zoom (draw.io's steps, fit page, fit window, fit selection), selection (click, shift, ctrl, rubber band, drill into groups), move with grid snapping and alignment guides, resize and rotate handles (also on the union box of several cells), connection by hover arrows and connection points, edge end, waypoint and segment handles, inline label editing.
- Format panel for the diagram and for selected cells (fill, line, effects, text, arrange), with mixed values shown as such; copy and paste style, Set as Default Style, a searchable font list.
- Edit actions: cut, copy, paste, duplicate, delete, group, ungroup, align, distribute, z-order, lock, flip, rotate 90 degrees, autosize, Edit Geometry, Edit Style, Edit Shape (stencil XML with live preview), find and replace across pages and by shape type, Edit Diagram (the page's raw XML).
- Cell data: Edit Data, Edit Link, Edit Tooltip, Edit Image, Open Link.
- Containers and tables: dropping into swimlanes and containers, collapse and expand, `stackLayout`, `tableLayout`, row and column insert, delete and resize, merged cells.
- Layouts: horizontal and vertical tree, radial tree, org chart, circle, horizontal and vertical flow, organic, and the matching `childLayout` containers.
- Insert menu: text, image (file or clipboard, embedded), link, lines, table with a size picker, freehand drawing, and Insert > Advanced with From Text, CSV and Mermaid (flowcharts and sequence diagrams).
- Shape palette with 18 palettes (about 3,500 shapes), search, favorites and recently used shapes; user libraries; a template gallery and a welcome dialog.
- Pages and layers (add, duplicate, rename, move, delete, hide, lock), units (points, inches, millimeters, centimeters, meters), print and print preview, preferences, keyboard shortcut list.
- Autosave and crash recovery: after a crash or kill, the next start offers the autosaved documents (Recover, Discard, Later) and a link to the crash reports.
- Accessibility: every control has an accessible name, the canvas is an accessible "Diagram", Tab and Shift+Tab move the selection between cells, and a Cells panel lists them as a tree. Menus follow the KDE Human Interface Guidelines.
- Scaled displays: tiles, overlays and thumbnails are drawn in device pixels; verified at scale factors 1.25, 1.5 and 2 on the offscreen platform.

#### Performance
- The canvas paints from retained per-cell display lists rasterised into tiles on worker threads, with zoom buckets, a spatial index for hit testing, interaction snapshots for live move and resize previews, a level of detail for far zooms, shared buffers for large style values (embedded images) and decode-once image and label caches. Bulk changes (group, ungroup, delete, reorder) are a fixed number of primitives. Figures are in [docs/BENCHMARKS.md](docs/BENCHMARKS.md).

#### Diagnostics
- A crash handler that writes a text report (version, commit, Qt, system, stack trace, no document content) to the data folder, a log file with logging categories (`QT_LOGGING_RULES`), and session markers that detect an unclean end.

#### Packaging
- Linux: RPM and source RPM (Fedora), DEB (Debian 13, Ubuntu 25.10 and newer) and a Flatpak bundle, built in containers by `packaging/build-all.sh` and smoke-tested by `packaging/test-install.sh`. Desktop file, AppStream metainfo, MIME type (`application/vnd.jgraph.mxfile`) and icons for the application id `io.github.newmca.qraw`.
- Windows installer and portable ZIP, and a macOS disk image with `Qraw.app`, built by CI. They are unsigned.
- Original application icon and a `.drawio` file association on all platforms.

#### Quality
- About 160 CTest entries: unit tests per module, golden SVG tests, corpus round trips, GUI end-to-end tests on the offscreen platform, a deterministic mutation fuzzer for the XML codec, page decompression, style parser, stencil parser and HTML sanitizer, benchmark gates on work counters, accessibility and menu-convention tests, and HiDPI variants (`ctest -L hidpi`).
- A fidelity tool that exports a sample of files with draw.io Desktop and with Qraw and scores the pairs (`tools/fidelity/`, [docs/FIDELITY.md](docs/FIDELITY.md)).
- CI on Linux, macOS and Windows, with `asan` and `dev` presets for local sanitizer and warning-as-error builds.

### Known limitations

See [KNOWN_ISSUES.md](KNOWN_ISSUES.md) and [COMPATIBILITY.md](COMPATIBILITY.md). The main ones: no `.vsdx`, PlantUML or math typesetting; some JavaScript-only shapes and the vendor icon sets are not drawn; fidelity against draw.io is close but not pixel-exact; macOS and Windows have had much less testing than Linux; the packages are unsigned.
