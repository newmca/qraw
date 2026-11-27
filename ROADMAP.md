# Status and roadmap

Qraw 0.1.0 is the first public release. This file says what is done, how far it has been verified, and what comes next. [COMPATIBILITY.md](COMPATIBILITY.md) is the feature matrix, [KNOWN_ISSUES.md](KNOWN_ISSUES.md) lists every known gap in detail, [FEATURES.md](FEATURES.md) describes what the program does, and [CHANGELOG.md](CHANGELOG.md) the release.

## Status

| Area | State |
|---|---|
| Lossless model and file codec (`.drawio`, bare `<mxGraphModel>` `.xml`, compressed and legacy pages) | done; 363 corpus files (155 are draw.io's own templates) load, save and reload semantically equal, and the second save is byte-stable |
| Commands and undo | done; every operation is checked to undo and redo to identical bytes on the corpus |
| Rendering and export (SVG, PNG, JPEG, PDF, editable embedding) | done for the shapes below; 363 of 363 corpus files export without errors; fidelity against draw.io is measured, not perfect ([docs/FIDELITY.md](docs/FIDELITY.md)) |
| Editor | done: selection, move, resize, rotate, connect, inline label editing, Format panel, pages, layers, containers, tables (including merged cells), layouts, find and replace, clipboard, libraries, templates, Insert > Advanced (text, CSV, Mermaid subset), freehand, dark diagram mode, autosave and crash recovery |
| Shape palette | 18 palettes, about 3,500 shapes, search, favorites, recent shapes; vendor icon images (AWS4 and Kubernetes composites, Azure `img/lib`) are not bundled |
| Canvas performance | tiles rasterised on worker threads, retained display lists, spatial index; every interaction takes a few milliseconds or less on image-heavy and 10k-cell documents ([docs/BENCHMARKS.md](docs/BENCHMARKS.md)) |
| Accessibility | accessible names on every control (enforced by a test), keyboard navigation between cells, a Cells panel; not tried with a real screen reader |
| Packaging | RPM, DEB and Flatpak bundle built from `packaging/`; Windows installer and ZIP and a macOS disk image built in CI; none is signed |

### How far it is verified

- **Linux** is the platform the program is developed and used on. About 160 CTest entries (unit, golden, corpus, GUI end-to-end driven by synthetic events, fuzz, HiDPI variants, crash handler, benchmark gates) pass in the `dev` and `release` presets, and the `asan` preset (AddressSanitizer and UBSan) is clean.
- **macOS and Windows** are built and tested by CI, but have had far less use than Linux. The installer, the ZIP and the disk image are new and lightly tested.
- **GUI tests run on the `offscreen` platform.** Interactive behaviour on real desktops (X11, Wayland, fractional scaling, input methods, right-to-left text, screen readers) has been exercised only through synthetic events.
- **Fidelity.** On a 123-file sample, 98 files are within 10 % of draw.io's own export on layout. Text is rendered by a different engine, so pixels never match exactly.
- **Not done:** `.vsdx` import and export, PlantUML, math typesetting (the flag is preserved, the formula is shown as text), the `glass` effect, export as HTML, a number of JavaScript-only shapes, vendor icon sets. See below.

## Roadmap

Ordered by value to someone replacing draw.io Desktop for local work. The order is a judgment, not a commitment.

1. **Verify macOS and Windows** on real machines: the packages, file association, fonts, HiDPI and the crash handler (the Windows handler is compiled but no test runs it). An ASan job in CI.
2. **Real-desktop testing on Linux:** X11 and Wayland sessions, fractional scaling, a window moved between screens of different ratios, input methods, right-to-left layouts, Orca.
3. **Fidelity work** from the measured classes in [docs/FIDELITY.md](docs/FIDELITY.md): edge shapes and the isometric edge style, pattern and hachure fills, label background padding, `<hr>` rendering, list indentation.
4. **Remaining JavaScript-only shapes** (fallback painter today): `mxgraph.mockup.*` forms, misc, markup, navigation and buttons; `mxgraph.bpmn.{data2,conversation2,swimlane,task}`; `mxgraph.basic.{patternFillRect,polygon,arc}`; `arrows2` beyond arrow and twoWayArrow; floorplan stairs and other doors and windows; the `Shapes.js` shapes `or`, `xor`, `delay`, `display`, `sumEllipse`, `storage`, `dataStorage`, `loopLimit`, `tee`; callout, lifeline and cloud perimeters. Each is a port of a JavaScript painter.
5. **Table gestures:** moving a row or column by dragging, the Ctrl and Ctrl+Shift border drags, striping colours, the Collapsed Borders switch of the Format panel.
6. **Containers:** `childLayout=organicLayout` and `rack` are kept in the file but not executed; a swimlane-aware drop target.
7. **Data and editing polish:** tag filtering (the Tags window and tag colours), a WYSIWYG tooltip editor, keyboard shortcuts for fold and flip, segment handles for loop and entity-relation edges, group previews during multi-selection resize, find and replace across inline HTML tags, user-library search and favorites, the collapsible page-tab strip, page shadow.
8. **Glass** (`glass=1`) and the `zigzag` and `dots` sketch fill styles.
9. **`.vsdx` import.** Unzip with Qt's private zip support or a small bundled reader, map Visio masters to draw.io shape styles using the mapping tables in draw.io's importer as a reference, import pages as new pages. There is no menu entry or code yet.
10. **Vendor icon add-on libraries** (lowest priority). AWS, Azure, GCP and Kubernetes icon sets and the `img/lib/**` images are not bundled. They would ship as optional, separately installable libraries built on the mxlibrary import mechanism, so each can carry its own licence and update on its own.
11. **Distribution:** signing and notarisation for Windows and macOS (needs certificates and an Apple developer account), a Flathub submission (needs a tagged release and the name question, [DECISIONS.md](DECISIONS.md) D-047), per-user Windows installer.
12. **Performance leftovers** (details in [KNOWN_ISSUES.md](KNOWN_ISSUES.md)): lazy image decoding at open, drag previews for multi-selection resize and edge handles, a GPU compositor behind the existing `Compositor` seam (D-031), faster large PNG export.

### Not planned

- Cloud storage and sync, accounts, plugins, online shape or image search, AI features, real-time collaboration and a web build (D-000).
- **Math typesetting** (`math=1`): the flag is preserved in files and the formula text is rendered as plain text.
- **PlantUML:** draw.io renders it on a server, or needs Java and Graphviz locally; neither belongs in a local-first native editor. It could return with a bundled renderer or an offline converter.
- **Export as HTML:** the page embeds draw.io's JavaScript viewer, which Qraw does not ship. Export as XML is Save As > XML diagram.
- **AppImage:** the RPM, DEB and Flatpak cover the Linux distributions, and an AppImage would mean bundling Qt and a second tool chain (D-052).
