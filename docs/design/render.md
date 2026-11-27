# Render engine and export

This is the design of the render engine: style resolution, native shape painters, the stencil interpreter, edges, labels, scene projection, export to SVG, PNG, JPEG and PDF with editable embedding, and `qraw-cli --export`. The decisions behind it are DECISIONS.md D-014 (globbing) and D-020 to D-024. Upstream references point at `$DRAWIO_SRC/src/main/webapp` (draw.io commit `c72c9a98`, Apache-2.0), which is a read-only reference and is never executed. Background research is in `docs/research/drawio-rendering-data.md`.

**Acceptance:** the CLI exports the whole corpus without errors, golden SVG tests pass on a small fixture set, and embedded XML re-imports.

## 1. Architecture

```
core::Page ──PageView::build──▶ CellState[] (paint order, absolute geometry, routes, label boxes)
                 │   uses: Stylesheet::resolve      (style → ResolvedStyle)
                 │         geometry::routeEdge       (terminals, perimeters, edge styles)
                 │         geometry::layoutVertex/EdgeLabel, makeRichText
                 │         ShapeRegistry / StencilRegistry (painter per vertex)
                 ▼
SceneRenderer::paintPage / paintCell ──▶ Canvas (mxAbstractCanvas2D mirror)
                 │   vertices: paintVertex(shape)    edges: configureCanvas + polyline + markers
                 ▼
      ┌──────────┴───────────┐
 PainterCanvas (QPainter)   SvgCanvas (own writer)
  PNG / JPEG / PDF / editor  SVG (+ content attr)
                 ▼
export.h: exportPage / exportPdf ──▶ bytes (+ format::embedInPng / embedInPdf)
cli: qraw-cli --export
```

| Layer | Target / dir | Qt? | Notes |
|---|---|---|---|
| Geometry | `qraw_geometry`, `src/render/geometry/` | **no** (forbid_qt) | paths, perimeters, routing, polyline helpers, label layout |
| Render | `qraw_render`, `src/render/` | QtGui, QtSvg | styles, canvases, text, shapes, stencils, markers, scene, export |
| Embedding | `qraw_format`, `src/format/embed.*` | QtCore + zlib | PNG/SVG/PDF read and write of the embedded mxfile |
| CLI | `qraw_cli`, `src/cli/` | QtGui for export | `--export` |
| Data | `resources/` → `:/qraw/...` | n/a | `styles/default.xml`, `stencils/stencils.bin`, `stencils/index.tsv` |

**The editor seam.** The editor builds a `PageView` per page and records each `CellState` through `SceneRenderer::paintCell` onto a recording canvas (ARCHITECTURE.md, "Canvas rendering"); `paintBounds` gives each cell's extent. When a `ChangeSet` arrives it patches or rebuilds the view and diffs states by id. Hit testing uses the Qt-free geometry (`distanceSquaredToPolyline`, the rotated bounds).

## 2. Public headers and implementation files

| Header | Implementation files |
|---|---|
| `geometry/path.h` (inline builders) | `src/render/geometry/path.cpp` |
| `geometry/polyline.h` | `src/render/geometry/polyline.cpp` |
| `geometry/label_layout.h` | `src/render/geometry/label_layout.cpp` |
| `render/markers.h` | `src/render/markers.cpp` |
| `geometry/perimeter.h` | `src/render/geometry/perimeter.cpp` |
| `geometry/routing.h` | `src/render/geometry/routing.cpp`, `src/render/geometry/routing_*.cpp` |
| `render/stylesheet.h`, `render/resolved_style.h` (inline) | `src/render/stylesheet.cpp` |
| `format/embed.h` | `src/format/embed.cpp`, `src/format/embed_*.cpp` |
| `render/text.h` | `src/render/rich_text.cpp`, `src/render/rich_text_*.cpp` |
| `render/canvas.h` (interface only) | — |
| `render/painter_canvas.h` | `src/render/painter_canvas.cpp`, `src/render/painter_*.cpp` |
| `render/svg_canvas.h` | `src/render/svg_canvas.cpp`, `src/render/svg_*.cpp` |
| `render/shape.h`, `render/image_source.h` | `src/render/shape_pipeline.cpp`, `src/render/shape_registry.cpp`, `src/render/shapes_*.cpp`, `src/render/image_source.cpp` |
| `render/stencil.h` | `src/render/stencil.cpp`, `src/render/stencil_pack.cpp`, `src/render/stencil_registry.cpp`, `resources/stencils/**`, `tools/stencils/**` |
| `render/page_view.h`, `render/scene_renderer.h`, `render/export.h` | `src/render/page_view.cpp`, `src/render/scene_renderer.cpp`, `src/render/scene_*.cpp`, `src/render/export.cpp`, `src/render/export_*.cpp`, `src/cli/export_command.cpp`, `src/cli/cli.cpp` (help text only) |

Tests live in `tests/render/tst_<name>.cpp` and `tests/format/tst_<name>.cpp` with data under `tests/render/data/<subdir>/`. `tests/render/support/recording_canvas.h` is the canvas that records calls for painter tests; `tests/render/tst_design_contract.cpp` checks the rules of this document (for example that tile code uses no GUI-thread-only Qt class).

## 3. Conventions

- **Coordinates.** Page units at scale 1, y pointing down, angles in degrees and clockwise on screen. A `CellState` is absolute: parent origins are added. Zoom and export scale are canvas transforms (`Canvas::scale`) and never touch model numbers.
- **Fidelity.** Port upstream behavior line by line where the code cites it, including upstream quirks (stencil `fillalpha` sets the global alpha, `Number(null) == 0`, and so on). Where we deviate, say so in a code comment.
- **Robustness.** Painting never throws and never fails. Bad numbers (NaN, inf, negative sizes) paint nothing for that element. Unknown shapes use the fallback painter, unknown markers paint nothing, and unknown edge styles route straight. Problems go into diagnostics (`PageView::diagnostics`, `ExportResult::diagnostics`), not to stderr.
- **No network.** Images come only from `data:` URIs and local files (`image_source.h`). HTML is sanitized (`sanitizeHtml`).
- **Determinism.** The same document and options produce the same SVG bytes on every machine: there are no random ids, no timestamps and no font metrics in SVG (D-023), and SVG numbers have at most two decimals. Raster output may differ by font.
- **Threads.** `Stylesheet::drawioDefault()`, `ShapeRegistry::builtin()` and `StencilRegistry::builtin()` are safe to call from any thread. Everything else is single-threaded per object.
- **Style.** C++20 and Qt 6.5+. Warnings are errors in `dev`. Code is readable and split into functions named after their upstream counterparts (`paintSwimlane`, `getJettySize`). Ported files carry `// Port of <upstream path> (draw.io, Apache-2.0), modified.` (licensing: docs/research/drawio-rendering-data.md §8).

## 4. Style resolution

See `stylesheet.h`. The order is the stylesheet base (`defaultVertex` or `defaultEdge`), then the tokens in order (bare names merge named styles, `key=none` erases), then inherited label styles, then `default` colors from the Theme, then `light-dark()` selection. The page `mxGraphModel@style` (theme name) is ignored, and the default stylesheet is always used (KNOWN_ISSUES). `adaptiveColors` and dark export are deferred, so export uses `Theme{}` (light).

## 5. Canvas and text

`canvas.h` documents the shared semantics. Text follows `mxAbstractCanvas2D.text(x, y, w, h, str, align, valign, wrap, format, overflow, clip, rotation)`. `(x, y)` is the alignment anchor. `w`/`h` are the box size (0 = unbounded). Rotation turns around the anchor. `SvgCanvas::text` writes draw.io's structure (`mxSvgCanvas2D.prototype.text`, `createDiv`, `updateTextNodes` in `mxgraph/src/util/mxSvgCanvas2D.js`): `<g><switch><foreignObject ...><div xmlns="http://www.w3.org/1999/xhtml" style="display: flex; align-items: unsafe <v>; justify-content: unsafe <h>; width: <w>px; height: 1px; padding-top: <y>px; margin-left: <x>px;"><div style="box-sizing: border-box; font-size: 0px; text-align: <align>;"><div style="display: inline-block; font-size: <n>px; font-family: <f>; color: <c>; line-height: 1.2; pointer-events: all; white-space: normal|nowrap; word-wrap: normal;">HTML</div></div></div></foreignObject><text ...>fallback</text></switch></g>`.

## 6. Shapes

**Native shapes** (the `shape` style value, upstream source):

| Name(s) | Upstream |
|---|---|
| `rectangle`, `label` (label adds image + indicator) | `mxgraph/src/shape/mxRectangleShape.js`, `mxLabel.js` |
| `ellipse`, `doubleEllipse` | `mxEllipse.js`, `mxDoubleEllipse.js` (+ Shapes.js:3292 overrides) |
| `rhombus` | `mxRhombus.js` (+ Shapes.js:3196) |
| `triangle`, `hexagon` | `mxTriangle.js`; Shapes.js:3140 HexagonShape |
| `cylinder`, `cylinder3` | `mxCylinder.js`; Shapes.js:1645 |
| `actor`, `cloud`, `line` | `mxActor.js`, `mxCloud.js`, `mxLine.js` |
| `image` | `mxImageShape.js` |
| `swimlane` (horizontal/vertical, startSize, swimlaneFillColor, swimlaneLine, rounded) | `mxSwimlane.js` + Shapes.js:1018 |
| `table`, `tableRow`, `partialRectangle` | Shapes.js:207, :940 area, :5787 |
| `process`, `document`, `parallelogram`, `trapezoid`, `step`, `card`, `tape`, `note`, `internalStorage`, `manualInput`, `cross`, `plus`, `callout` | Shapes.js (the source lines are cited in `src/render/shapes_*.cpp`) |
| `text` | there is no such shape: `text` is a named style. `shape=text` paints nothing but the label. |

Anything else, after native shapes and then stencils (exact key, case-insensitive), falls back to `ShapeRegistry::fallback()`. Unknown `mxgraph.*` JS-only shapes are expected to fall back until later milestones port them.

## 7. Stencils

`tools/stencils/vendor_stencils.py <upstream webapp dir>` reads `js/stencils.min.js` as text. It never executes it. It extracts the 204 `f['<path>'] = '<base64>';` entries and writes:

- `resources/stencils/stencils.bin`: magic `QDSTPK01`, `u32be count`, then per set, sorted by path: `u16be pathLen`, path (UTF-8), `u32be blobLen`, blob (the base64-decoded upstream value, i.e. raw DEFLATE of the packed DOM).
- `resources/stencils/index.tsv`: one line `key<TAB>path` per stencil, sorted by key (`stencilKey`). The first line is a `# source jgraph/drawio <commit>` comment.
- `resources/stencils/LICENSE` (copy of upstream `stencils/LICENSE`) and `resources/stencils/README.md` (provenance, regeneration command).

At runtime `StencilRegistry::builtin()` loads the index once. `find()` locates the set, inflates and decodes it on first use (`format::inflateBytes(blob, -15)`, then `decodePackedStencilSet`), and caches every `<shape>` of that set.

## 8. Scene and export

- **States.** `page_view.h` describes them. Vertex geometry: absolute for children of layers. Children of vertices add the parent origin. With `relative=1` under a vertex parent, x/y are fractions of the parent size plus `offset`. With `relative=1` under an edge parent, the origin (top-left) is `relativePointOnPolyline(parent.points, x, y, offset)`. Relative children of a rotated vertex parent are rotated around the parent center. Both follow `mxGraphView.updateCellState` (`mxgraph/src/view/mxGraphView.js:2636`) and `updateVertexState` (`:2737`). A non-relative vertex's geometry `offset` moves only its label (`LabelParams::offset`). Edge waypoints and source/target points are in parent coordinates.
- **Edges.** Edges are routed with `geometry::routeEdge`. The terminals come from the source/target states (rotation, perimeter from style, constraints from the edge style, port constraints). Rounded edges use `polylinePath(points, true, arcSize / 2)` and `curved=1` uses `curvedPath`. Markers come from `createMarker`, painted after the line.
- **Labels.** Each state gets a `layoutVertexLabel` (with the shape's `labelBounds` hook) or a `layoutEdgeLabel`, plus `makeRichText(value, html)`. Wrapped cells (`<object label=...>`) use their label. Placeholders (`%name%`, `placeholders=1`) are substituted from the wrapper attributes. Edge labels with `labelBackgroundColor` get a background.
- **Export sizing.** Image size is draw.io's `getSvg`: `w = max(1, ceil(bounds.width * scale) + 2 * border)` (same for `h`), translated so that `bounds.x` maps to `border`. SVG uses `viewBox="-0.5 -0.5 w h"`. PNG goes through `QImage::Format_ARGB32_Premultiplied` and PainterCanvas, with background fill unless transparent, then `QImageWriter("png")`, then `format::embedInPng` with `--embed`. JPEG is the same on a white background. PDF uses `QPdfWriter` at resolution 96 with a custom page size in points (`w * 0.75`), one PDF page per diagram page, then `format::embedInPdf` with `--embed`.
- **CLI.** `qraw-cli --export IN --format svg|png|jpg|pdf --output OUT [--page N | --all-pages] [--scale S] [--transparent] [--border N] [--embed]`. `IN` may be `.drawio`/`.xml` or a `.png`/`.svg`/`.pdf` with an embedded diagram (`format::readAnyFile`). `--page` is 1-based and defaults to 1. With `--all-pages`, PDF writes one multi-page file, and other formats write `OUT` with `-<n>` (1-based) inserted before the extension. The format is inferred from the `OUT` extension when `--format` is absent. Exit codes are `cli::ExitCode`.

## 9. Testing

- Unit tests per area. Painters are tested with `test::RecordingCanvas`.
- **Golden SVGs**: `tests/render/data/golden/<name>.drawio` gets exported with default options and `embed=true`, then compared with `tests/render/data/golden/<name>.svg`. Line endings are normalized and the comparison is otherwise byte-exact. Set `QRAW_UPDATE_GOLDENS=1` to (re)write the `.svg` files instead of comparing. A missing golden is a `QSKIP` with a hint, not a failure.
- **Embed round trips:** PNG (`tests/format/tst_embed.cpp` on bytes, and export → `readAnyDocument` → `semanticEqual`), plus the same for SVG and PDF.
- **Corpus smoke**: every corpus file (`tests/corpus/**`, `tests/compat/fixtures/**`) has every page exported to SVG and PNG (`maxPixelSize = 2048`). The SVG must parse with `format::parseXml` and the PNG must decode with `QImage`. The test fails on any error diagnostic, and it has a total budget of 300 s with the per-file time printed for the slowest 5.

## 10. Deliberate gaps

`glass=1`, the `mxGraphModel@style` theme, `isometricEdgeStyle`, libavoid routing, line jumps on `curved=1` edges, math typesetting (`math=1` labels render as text; not planned), `rounded=1;roundedPerimeter=1`, the callout/lifeline/cloud perimeters (approximated by rectangle), JS-only `mxgraph.*` shapes (fallback painter), the composite AWS4/Kubernetes icons, `img/lib/**` images (not vendored), remote images (never fetched), and PDF EmbeddedFile writing.

Sketch and comic (`render/sketch_canvas.*`): `SceneRenderer::paintShape` wraps the canvas in a `SketchCanvas` for cells with `sketch=1` or `comic=1`, for vertices and edges alike; labels are painted on the plain canvas. Comic ports HandJiggle (straight segments only). Rough is clean-room (no rough.js): the path is recorded and, on fill/stroke, flattened and redrawn as doubled bowed Béziers (end points fixed, as `preserveVertices`), with hachure/cross-hatch/dashed fills in the fill colour (solid for white or gradient fills). The PRNG is seeded from the cell id (D-027); `paintBounds` grows by `SketchSettings::paintMargin`.
