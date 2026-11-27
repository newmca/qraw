# Qt 6 techniques and pitfalls for a very fast 2D canvas (Qraw)

Written before the canvas work was done; the implementation is described in ARCHITECTURE.md and DECISIONS.md (D-030 to D-037), and the findings are kept as background. Scope: advice for the canvas performance design (decode-once images, exact invalidation, tiled backing store,
off-thread tile raster against an immutable snapshot, snapshot dragging, LOD, CPU compositor behind an interface).
Qt in use: 6.11.2 (Linux). Project minimum: Qt 6.5.

Conventions
- "Source:" cites qt/qtbase branch 6.11 at `https://raw.githubusercontent.com/qt/qtbase/6.11/src/<path>`. Local copies, so the
  line numbers below are checkable: the Qt 6.11 source files named (local copies were taken during the research and are not in the repo).
- "Docs:" cites doc.qt.io/qt-6.
- "MEASURED" = micro-benchmarks written for this study, Qt 6.11.2, offscreen platform, 24-core desktop, -O2. Sources:
  ad-hoc micro-benchmarks run during the research (not in the repo). Numbers are relative, not budgets.
- "UNVERIFIED" = from memory or secondary sources; verify before relying on it.
- Qt blog posts (2009 raster-engine and cache-mode posts) would not load as full text; only search snippets were seen. Treated as UNVERIFIED.

## 0. What the current code does (from reading it)

- `CanvasView : QGraphicsView`, `FullViewportUpdate`, hints AA+TextAA+SmoothPixmapTransform (`canvas_view.cpp:103-107`).
  Every mouse move/press/release/key calls `viewport()->update()` (8 call sites in canvas_view.cpp), so hover = full repaint of every visible cell.
- `DiagramScene`: `BspTreeIndex`, one `CellItem` per cell (`diagram_scene.cpp:264`). Each change does a full `PageView::build` (O(n)),
  `diffViews` (O(n)), then `refresh()` on changed items (`diagram_scene.cpp:397-400`).
- `CellItem::paint` (`cell_item.cpp:52`) builds a `PainterCanvas`, calls `paintShape` + `paintLabel` every time. Style values are
  string-keyed map lookups at paint time (`SketchSettings::fromStyle`, `style.has(...)`, `style.color("startStrokeColor")`, etc.).
- `PainterCanvas::image` (`painter_canvas.cpp:~430`) does `QImage::fromData` or `QSvgRenderer::load` per call; `ImageShape::paint`
  first calls `resolveImage` (base64 decode or file read) per call (`shapes_image.cpp:44`).
- `PainterCanvas::Impl::text` constructs a `QTextDocument`, `setHtml/setPlainText`, lays out, draws, per label per paint.
- `drawPath` does `painter.save()/restore()` around every path and rebuilds the `QPen` each time.
- Paint-time reads from `CellState` are self-contained: `state.cell` (a `core::Cell*` into the live model) is used only inside
  `PageView::build` (`page_view.cpp:381,471,569,612`), never by painters. So a `shared_ptr<const PageView>` is already a valid immutable
  snapshot for the render thread, provided `view_` stops being mutated in place (today it is replaced by move: `diagram_scene.cpp:399`).
- Registries touched while painting: `ShapeRegistry::builtin()` (function-local static const, thread-safe init),
  `StencilRegistry` (own `std::mutex`, `call_once`, `stencil_registry.cpp:83,201`), `Stylesheet::drawioDefault()` (static const),
  `thread_local currentView`, `thread_local includeDepth`. No `QPixmap`, no `QFontDatabase`, no GUI-thread-only calls in `src/render` painting code (grep);
  `export.cpp` only checks that a `QGuiApplication` exists (needed for fonts). Render code looks thread-ready.

MEASURED, image cost of a typical embedded screenshot: a 1000x600 PNG (880 KB, 1.18 MB base64) = base64 decode 6.2 ms + `QImage::fromData` 5.4 ms
(+ drawing 0.03-0.08 ms). 32 such images per full repaint is about 370 ms, which is the reported stall. Drawing from the decoded
full-resolution image is cheap (0.03 ms ARGB32PM smooth 1000x600 to 300x180); decode is the whole problem.

## 1. QGraphicsView / QGraphicsScene at scale

### BSP index
- Lazy: inserts/removes mark the index dirty and `_q_updateIndex` runs from a zero-timer before the next query/paint
  (Source: `widgets/graphicsview/qgraphicsscenebsptreeindex.cpp:88-146`). Depth is auto (`intmaxlog(itemCount)`); the tree is
  regenerated when the depth changes and the item count moved by more than 100 (`:113-121`), or when `clear()`/sceneRect changes
  set `regenerateIndex` (`:450`). Regeneration reinserts every item.
- Docs say "best for static scenes"; `NoIndex` "ideal for dynamic scenes" (Docs: QGraphicsScene::ItemIndexMethod).
  Item moves are remove+insert into every leaf the rect touches; large items touch many leaves.
- `sortCacheEnabled` is false by default (`:74`), so every `items(rect)` sorts its result by stacking order each call.
- The scene rect when unset "grows ... but never shrinks" (Docs: QGraphicsScene::sceneRect). You never call `scene->setSceneRect`
  (only `QGraphicsView::setSceneRect`), so the BSP is built over the grown items rect: after a big transient (drag far away,
  paste) it stays huge until `scene->setSceneRect` is set. Set an explicit scene rect (page area + generous margin) if the scene stays.
- Qraw's own benchmark numbers at the time (scene build 13x for 5x cells; undo-with-scene 2.1 s at 50k) are consistent with N
  `QGraphicsItem` allocations + index regeneration + per-item `prepareGeometryChange`. The only fix that scales is not having N items.

### Cache modes (Source: `qgraphicsscene.cpp:4310-4520`, `drawItemHelper`)
- Both modes store the buffer in the global `QPixmapCache`, default limit 10240 KB, GUI thread only (Docs: QPixmapCache).
  `setCacheMode` docs: "Be aware that QPixmapCache's cache limit may need to be changed". Under pressure Qt evicts and `paint()` runs
  again with no `update()` (Docs: QGraphicsItem::setCacheMode note).
- `ItemCoordinateCache` (`:4356-4439`): pixmap size = `boundingRect().toAlignedRect().size() * dpr` + 2px margin, i.e. 1 pixel
  per item unit, independent of view zoom. Zoom in = bilinear-scaled bitmap = blurry; the docs say "Rendering quality will
  typically degrade". `setCacheMode(mode, QSize)` fixes the pixel size but then you pick the resolution once. The pixmap is
  recreated whenever the bounding rect size changes. Not usable as a zoom-correct cache.
- `DeviceCoordinateCache` (`:4442-...`): buffer in device pixels = full quality, but "If the world transform is rotated we always
  recreate"; any non-translate change in `diff` (`simpleTransform`, `:4475`) discards the cache. Every zoom step therefore
  invalidates every item cache and re-renders at the new size. Memory = device bbox of the item, unbounded (a large container or
  swimlane at 400% zoom is a multi-hundred-MB pixmap; the 2009 blog reported OOM on deep zoom, UNVERIFIED snippet; Qt added
  `QGraphicsSvgItem`'s max-device-cache-size escape hatch at `:4454`).
- Net: per-item caches cost one pixmap + one `QPixmapCache` entry per cell (a thousand tiny pixmaps; GPU-less so no atlas),
  thrash on zoom, and still repaint via `drawPixmap` per item. A single tile cache you own beats them on every axis.

### Viewport update modes (Source: `qgraphicsview.cpp` `updateRect`/`updateRegion`/`processPendingUpdates`, ~`:370-700`)
- Full: sets `fullUpdatePending` and calls `viewport->update()`. Minimal (default) and Smart share the same region path.
  BoundingRect unites into one rect. Smart/Minimal switch to bounding rect when 50+ dirty rects (`QGRAPHICSVIEW_REGION_RECT_THRESHOLD`).
- Per-item `update()` pads the rect by 2 px for AA unless `DontAdjustForAntialiasing` (then 1 px) (`updateRegion`).
- Scrolling: `scrollContentsBy` (`:3625-3679`) only uses `viewport()->scroll()` (a backing-store blit) when mode is not Full and
  `accelerateScrolling` is true (false for GL viewports, `:2738`). With `FullViewportUpdate`, every scrollbar tick repaints the
  whole viewport. Your pan uses scrollbars (`scrollBy` in `canvas_view.cpp:68`).
- Direct `viewport()->update(QRect)` calls bypass all of this: they go to `QWidgetRepaintManager::markDirty`, are coalesced into
  one `UpdateRequest` per event-loop turn, and only the dirty region is repainted and flushed (Source:
  `widgets/kernel/qwidgetrepaintmanager.cpp:158-250,331-366`). This works in any view mode, which is what a tile compositor wants.

### Flags
- `DontSavePainterState`: view skips `painter.save()/restore()` around items and background (`qgraphicsview.cpp:3425,3485`,
  `qgraphicsscene.cpp:4875`). Your `PainterCanvas` already save/restores internally, but leaves `Antialiasing` and
  `SmoothPixmapTransform` hints set (`Impl` ctor), so with the flag the hints leak to the next item (harmless: all set the same).
  Saves ~one state copy per item; irrelevant once items go away.
- `DontAdjustForAntialiasing`: only shrinks the 2 px pad to 1 px. Your `CellItem` already adds 1 px slack, so it is safe but worth <1%.
- `IndirectPainting`: restores the old `drawItems()` path. Never use it (slower, per-item style option arrays, `:3512-3551`).
- `CacheBackground`: stores the background in one viewport-sized pixmap (`qgraphicsview.cpp:3449-3482`); invalidated on any
  transform (zoom) and only partially on scroll (it `QPixmap::scroll`s it, `:3659-3674`). It is a viewport-sized back buffer with
  all the problems of a tile cache and none of the benefits (no zoom buckets, GUI-thread render). Skip it; bake grid/page into your tiles.
- `QGraphicsScene::setMinimumRenderSize(1.0)` (`qgraphicsscene.cpp:4687-4699`) skips painting items whose device bbox is smaller
  than N px but keeps them hit-testable. Free LOD for the current design. Implement the equivalent in the tile painter.
- `boundingRect()` pitfalls: must call `prepareGeometryChange()` before changing it or the index goes stale (Docs); the BSP
  stores `sceneEffectiveBoundingRect` per leaf. Your `paintBounds` is "conservative" but derived from a text-extent *estimate*
  (`geometry::estimateTextExtent`). Overflowing labels (`overflow=visible`, long unwrapped text) can paint outside the
  index rect and get clipped by the exposed-rect cull. The same hole becomes visible tile seams/clips in a tile renderer. See section 6.

### Is QGraphicsView the right base?
Recommendation: keep `QGraphicsView` only as a thin shell, remove the N items. Build a tile compositor inside it.
1. Why not item-per-cell + caches: see above; also 50k `QGraphicsItem`s cost build time, memory, undo/redo teardown.
2. Why not a bare `QWidget`/`QAbstractScrollArea` right now: `label_editor.cpp:244` hosts rotated-label editing in a
   `QGraphicsProxyWidget` in the scene (a child `QWidget` cannot be rotated). Also free: scrollbars with scene-sized range, `mapToScene`,
   `viewportTransform`, focus/drag/drop plumbing you already use. Replacing them is real work with no speed gain.
3. How: zero `CellItem`s. `CanvasView::drawBackground` (scene coordinates, painter already has the view transform) does
   `painter->resetTransform()`/`setWorldTransform(QTransform())`, blits ready tiles at integer device positions, draws a coarse
   placeholder (previous-bucket tile scaled) for tiles not ready, restores the transform; `drawForeground` stays the overlay.
   Tile completion calls `viewport()->update(deviceRectOfTile)`. Keep the label proxy as the only scene item.
4. Replace `scene.items(probe)` in `cellsAt` with your own index over `CellState::paintBounds` (uniform grid keyed by cell size ~
   median bbox, or a packed R-tree). Z-order = index in `PageView::states()`, so a query is "collect indices, sort ints", which is
   cheaper than `QGraphicsScene` sorting item pointers by stacking order, and the same structure serves the tile renderer's
   "cells intersecting this tile in paint order" and the exact-invalidation lookup.
5. Later (optional): drop `QGraphicsView` for `QAbstractScrollArea` once the rotated editor is rebuilt. `QRhiWidget` (Qt 6.7+, present in
   `/usr/include/qt6/QtWidgets/qrhiwidget.h`) is the future GPU path; it does not exist in 6.5, so gate behind the compositor interface and a Qt version check.

## 2. Threading (Qt 6)

- `QPainter` on `QImage` in worker threads is supported: "can be used in a thread to paint onto QImage, QPrinter, QPicture, and
  (for most platforms) QPixmap"; "Painting onto QWidgets is not supported"; "Any number of threads can paint at any given time,
  however only one thread at a time can paint on a given paint device" (Docs: threads-modules). `QImage`: "All functions in this class are reentrant" (Docs).
- Implicit sharing: a `QImage` handed to the GUI thread by value is fine (atomic refcount), but painting on a `QImage` that still
  has another holder triggers a hidden deep copy. Give each tile one owner while rendering; after handoff never paint it again, copy first.
- `QPixmap`: GUI thread only in practice. `QPixmapCache`: "Access from other threads will be ignored and return failure" (Docs).
  Never create `QPixmap` in a worker or use `QPixmap::fromImage` there. Use `QImage` for tiles and caches; on the raster backend
  `drawImage` and `drawPixmap` hit the same blitters (UNVERIFIED for every platform, but the raster `QPlatformPixmap` is `QImage`-backed on Linux).
- `QTextDocument`/`QTextCursor`/related: reentrant; a document made on the GUI thread "may contain QPixmap image resources"; use
  `clone()` to hand a copy to another thread (Docs: threads-modules). Do not share one document across threads, even for drawing.
- `QSvgRenderer`: reentrant (Docs). `QImageReader`: all functions reentrant (Docs). `QRawFont`/`QGlyphRun` cannot move threads:
  "cannot be moved to a different thread, but will have to be recreated" (QRawFont docs, via search snippet; UNVERIFIED wording). So no pre-shaped glyph runs shared from the GUI thread.
- `QFont`, `QFontMetrics`, `QTextLayout`, `QStaticText`: not mentioned on the thread-support page; reentrant by the usual rule if each object stays in one thread. Verified in source:
  - Font engines and their glyph caches are per thread: `QFontCache::instance()` is a `QThreadStorage<QFontCache*>`
    (`gui/text/qfont.cpp:3420-3428`); `QFontPrivate::engineForScript` throws away `engineData` created on another thread
    (`qfont.cpp:248-251`). Each worker therefore has its own FreeType face open, its own glyph cache (raster glyph caches are keyed by
    context, format, matrix, colour inside the thread's engine, `qpaintengine_raster.cpp:2815-2822`).
  - A global recursive mutex guards `QFontDatabase` and `engineForScript` (`qfontdatabase.cpp:525`, `qfont.cpp:242`). It is taken per script lookup, not per glyph, so contention is short.
  - `QFontCache::autoClean` is only enabled on the GUI thread (`qfont.cpp:~3440`), worker caches are never trimmed by a timer (UNVERIFIED consequence: bounded by distinct font/size/matrix combos, but zoom buckets multiply the matrix variants).
  - `QThreadPool` threads expire after 30 s idle by default (Source: `corelib/thread/qthreadpool.cpp:417,569`); on exit the thread's `QFontCache` dies, so the next job pays font-engine reload. MEASURED: first `drawText` with a given font in a fresh thread 1.15 ms vs 0.011 ms warm.
    Fix: `pool.setExpiryTimeout(-1)` on a dedicated pool; do not use `QThreadPool::globalInstance()`.
- MEASURED scaling (bench2, 500 items per job, steady state, persistent pool): rounded-rect AA paths scale about 7x at 8 threads (1.29 M items/s vs 0.18 M);
  `QTextLayout` labels 6.2x at 8 threads (553 k/s vs 89 k/s); `QTextDocument` labels 5.4x at 8 threads (308 k/s vs 57 k/s); both degrade past 8 threads (this CPU has SMT). 16 threads is slower per item than 8.
  Plain-text `QTextDocument` costs 17.6 us/label vs `QTextLayout` 11.3 us; HTML via `setHtml` 42.8 us (bench3). Thread count: `max(1, idealThreadCount()/2 - 1)`, cap about 8.
- Platform caveat from search: old Krita workaround for X11 `drawText` off-thread exists (KDE invent snippet, UNVERIFIED, Qt 4/5 era). `QFontDatabase::supportsThreadedFontRendering()` is obsolete and always true since Qt 5 (Docs: QFontDatabase obsolete). Run the off-thread text test on the Windows and macOS CI runners before trusting it there; only Linux was tried.
- Rules for the worker job: take `shared_ptr<const PageView>` + immutable `RenderContext`, never `QApplication` calls, never touch `core::Page`. `thread_local currentView` is already thread-safe. Check `ImageResolveContext` use: file reads from a worker are fine.
- Cancellation: check a `std::atomic<uint64_t> generation` between cells (not per primitive) and abandon the tile; discard results whose generation or zoom bucket is stale on arrival. Tile hand-off by queued signal carrying `QImage` + key.

## 3. Fastest raster paths

### Formats and blits
- Docs: "Rendering is best optimized to Format_RGB32 and Format_ARGB32_Premultiplied" (QImage); QPainter docs: "For optimal performance only use ... ARGB32_Premultiplied, RGB32 or RGB16. Any other format, including ARGB32, has significantly worse performance" (Source: `gui/painting/qpainter.cpp:1336-1343`).
  `Format_RGBA8888_Premultiplied` is only listed as secondary. Use ARGB32_Premultiplied for tiles and decoded images, converted once in the cache (`convertToFormat` 0.07 ms for 1000x600, MEASURED).
  `QImage::fromData` of a PNG with alpha returns non-premultiplied `Format_ARGB32`; drawing it converts per scanline every time. MEASURED: ARGB32 smooth draw 0.08 ms vs ARGB32PM 0.03 ms.
- The widget backing store is a raster `QImage` (RGB32 or ARGB32PM; per-platform, UNVERIFIED for Win/mac). `QBackingStore::beginPaint/paintDevice` hands out that image (`gui/painting/qbackingstore.cpp:117-165`).
  `drawImage(QPointF, QImage)` hits the memcpy-like `blitImage` or the SIMD src-over fast path only when the matrix is translate-only
  and `devicePixelRatio <= 1` (`qpaintengine_raster.cpp:2093-2135`); otherwise it falls to the rect overload which has a special case only for exactly 2x source on 2x target (`:2296-2322`) and a scale path otherwise.
- MEASURED tile blit (12 tiles of 512x512 into a 1920x1080 logical target, `drawImage(QPointF, tile)`, tile dpr == target dpr): dpr 1.0 0.31 ms, 1.25 0.83 ms, 1.5 0.99 ms, 2.0 0.65 ms; ARGB32PM src-over and RGB32 Source are the same cost (memory-bound), so there is no need to make tiles opaque for speed. A full-window composite is about 0.3-1 ms, not a bottleneck on CPU. Keep tile origins at integer device pixels and render at device scale, otherwise you pay for a resample.
- Bake background, page border and grid into the tiles (opaque or not): the grid alone costs about 1 ms per 1024x768 frame (MEASURED, 178 cosmetic lines, non-AA) when drawn live per paint.

### drawImage scaling
- `SmoothPixmapTransform` off: nearest neighbour, fast but aliased on downscale. On: bilinear with dedicated SIMD downscale helpers for ARGB32PM (`gui/painting/qdrawhelper.cpp:1576`, `DownscaleTransform` at `:2107`). Bilinear fetch samples 2x2 source pixels, so a >2x downscale aliases/shimmers. That is why mip levels (area-filtered via `QImage::scaled(..., SmoothTransformation)`, 0.11 ms for 1000x600 to 300x180) matter for quality, not speed. Pick the mip with the smallest level >= device size, then bilinear.
- `drawImage`/`drawPixmap` with Smooth also flip `Antialiasing` for the edge (`qpainter.cpp:4959,5089,5250,5362`): image edges get AA, costing slightly more.
- Do not use `setCompositionMode` other than SourceOver/Source for blits; the fast path list is `canUseFastImageBlending` = SourceOver, or Source for images without alpha (`qpaintengine_raster.cpp:3683-3691`).

### RenderHints and shapes (MEASURED, 1000 AA rounded rects 26x16 in 1024x768)
- AA on/off barely matters for stroked round rects (3.1 vs 3.1 ms); fill-only 1.0 vs 0.8 ms; dashed strokes are not dramatically slower here (1.4-1.7 ms) but the Qt raster blog warns dashing is among the slowest operations (snippet, UNVERIFIED for 6.x).
- `save()/restore()` around each path + pen/brush reset: 3.10 ms vs 2.68 ms without (about 15%). Rebuilding the `QPainterPath` each time: +0.15 ms per 1000. So path caching is a minor win (5%); the cost is rasterization. Do not build elaborate path caches; spend effort on not painting at all (tiles).
- Cosmetic pens are not faster here (1.63 vs 1.63 ms). They matter for zoom-independent hairlines (selection handles, grid). Keep `isCosmetic=false` for diagram strokes (they must scale).
- `QPicture` as a display list: replay was slower than direct drawing (3.9 ms vs 3.3 ms for 1000 round rects, 358 KB for 1000 paths, MEASURED). It records and replays QPainter calls; it saves nothing on rasterization and re-encodes text as text items. Do not use it. If you want a display list, record your own `Canvas` ops (`RecordingCanvas`), see section 6.
- Clip: setting a clip rect per cell inside a tile is fine; complex clip paths force the slow span-clip path. Prefer to clip once per tile via the target image bounds.

### Text (MEASURED, 200 labels, plain 12 px, 90 px wide)
| approach | per label |
|---|---|
| fresh `QTextDocument` + setPlainText + layout + draw | 17 us |
| fresh `QTextDocument` + `setHtml` (small HTML) | 43 us |
| fresh `QTextLayout` + layout + draw | 11 us |
| pre-laid-out `QTextLayout::draw` | 7.4 us |
| kept laid-out `QTextDocument` draw only | 19 us |
| `QStaticText` (ModerateCaching) | 4 us |
| zoom 1.37: static text / prelaid layout | 4.3 / 8.0 us |
- Layout caching saves only 35-60%; glyph rasterization and compositing dominate the rest. The decisive fix is rendering text into cached tiles once per zoom bucket, not caching `QTextDocument`s. Cache layout only for the label editor and for autosize measure.
- `QStaticText` is the cheapest but "altering any part of the painter's transformation or font will cause the layout to be recalculated" (Docs: QStaticText); here the 1.37 scale did not hurt, but it is plain text only (`Qt::RichText` supported, no `QTextDocument` features like line height). Not worth the fidelity risk for draw.io HTML labels.
- Qt keeps the final glyph bitmaps in a per-thread, per-matrix glyph cache (`qpaintengine_raster.cpp:2815`), so each distinct zoom value creates a new cache; zoom buckets (few distinct scales) keep that bounded. Another reason to quantise scales.
- Sub-pixel/LCD text: for `QImage` targets the FreeType engine requests `Format_A8` glyphs (grayscale), only `QInternal::Widget` devices get `Format_None` and may use LCD (`qpaintengine_raster.cpp:2768-2773`). Tile-rendered text will be grayscale-AA even where direct widget painting is currently LCD. Expect a visible, deliberate difference; screenshots/goldens must be re-baselined. `QFont::NoSubpixelAntialias` exists (Docs/source `qfont.cpp:1476`) if you want identical behaviour everywhere.
- Text layout invariance: `configureTextDocument` sets integer `pixelSize = round(fontSize * canvasScale)`; with zoom applied by the painter transform, layout (wrapping) is zoom-independent, which matches draw.io. Keep that. Do not "improve" crispness by scaling pixelSize with zoom in tiles or labels will reflow between buckets.

### devicePixelRatio
- Render tiles as `QImage` at device pixel size with `devicePixelRatio = 1` and an explicit painter transform `translate(-tileOriginDevice) * scale(zoom * dpr)`, then `setDevicePixelRatio(dpr)` on the finished image so the widget blit is 1:1. Avoid mixing dpr on the paint target (the 2x-on-2x fast path is the only dpr special case, `:2315`).
- Fractional dpr (Wayland 1.25/1.5): device-pixel blit positions are `devicePx / dpr` in logical coordinates; Qt maps back and rounds. MEASURED: 1.25 and 1.5 blits stay under about 1 ms for a 1080p-sized area, so even the slow path is cheap, but confirm no blur by test (compare pixels after blit; `tests` can use `QT_SCALE_FACTOR=1.25` offscreen).
- Wayland: Qt 6 uses a shared-memory buffer and damages the region passed to `flush`; partial `update(QRect)` therefore reduces compositor work. X11 (xcb) uses SHM `xcb_put_image` per region. Windows/macOS: DIB/CoreGraphics image blit of the region (UNVERIFIED details; rely on the contract "only the dirty region is repainted and flushed", verified in `qwidgetrepaintmanager.cpp:700-1050`).

### Partial updates and scroll
- `QWidget::update(QRegion)` is coalesced per event-loop turn into one `UpdateRequest` (Source: `qwidgetrepaintmanager.cpp:331-366`; low priority event). Mouse-move storms already collapse.
- `QWidget::scroll(dx, dy)` copies within the backing store ("accelerateScroll", `:468-520`) only if the widget is opaque (`isOpaque`, set via `WA_OpaquePaintEvent` or an opaque background) and not overlapped; env `QT_NO_FAST_SCROLL` disables it. This is the cheapest pan: `scroll` + render only the revealed strips from tiles. With `QGraphicsView`, `viewport()->scroll` is what `scrollContentsBy` calls in non-Full modes (`qgraphicsview.cpp:3648`). Set `viewport()->setAttribute(Qt::WA_OpaquePaintEvent)` and paint everything (no system background fill).
- Because overlays (selection handles, guides, rubber band) are painted in `drawForeground` on every paint and are in scene space, a scroll-blit moves them with the content and is correct; just invalidate the overlay's old/new rects around gesture changes (`update(rect)` with +2 px) rather than `viewport()->update()`.

## 4. Image decoding

- `QImageReader::setScaledSize` is honoured only by handlers that support `ImageOption::ScaledSize`. The PNG handler does not (`gui/image/qpnghandler.cpp:1076-1084`: supports Gamma, Description, ImageFormat, Quality, CompressionRatio, Size).
  `QImageReader::read` then does a full decode followed by `scaled(..., SmoothTransformation)` (`qimagereader.cpp:1202-1236`). MEASURED: reader+scaledSize on PNG 5.7 ms vs plain decode 5.4 ms. So no decode-time shrinking for PNG; peak memory is the full image anyway.
  JPEG supports DCT scaling via libjpeg `scale_denom` (powers of two 1/2, 1/4, 1/8; `plugins/imageformats/jpeg/qjpeghandler.cpp:266-290`), so for JPEG a `setScaledSize` decode of a thumbnail is genuinely cheaper. UNVERIFIED for WebP/GIF/BMP.
- Header-only metadata: `QImageReader::size()` / `imageFormat()` read the header without decoding (Docs; MEASURED 0.6 ms including buffer setup, mostly constant). Use it to compute decoded bytes (w*h*4) before decoding and to refuse or downscale oversize images.
- Allocation limit: default `QImageReader::allocationLimit()` is 256 MB (`qimagereader.cpp:435`), overridable with `QImageReader::setAllocationLimit(mb)` or env `QT_IMAGEIO_MAXALLOC`; 0 disables. Computed at minimum 32 bpp. A 16384x16384 PNG (1 GB) is rejected and `read()` returns a null image: treat null as "broken image placeholder" and cache the failure (negative cache) so it is not retried every paint.
- Off-thread: `QImageReader`/`QImage::fromData` are reentrant. Decode in a pool job, key by content hash of the source string (compute once; the string itself is up to multi-MB, see below), convert to ARGB32PM in the job, hand back a `shared_ptr<const DecodedImage>`.
- Mip chain: level k = `scaled(w>>k, h>>k, Smooth)` generated lazily on the render thread that first needs it, under a per-image mutex or via `std::call_once` per level. Cost of mips is +33% memory.
  Level choice uses `deviceWidth = cellWidth * zoom * dpr`; use level `floor(log2(srcW / deviceWidth))`. Do not bilinear from a level more than 2x larger than the target.
- Budget: count bytes (`QImage::sizeInBytes()`), LRU eviction, pin images used by visible tiles, evict lowest mips first (they are cheap to rebuild from level 0 if kept; keep level 0 only if its cost to re-decode is high; else keep level 0 compressed bytes plus the mip levels in use). For a document with 32 such images: 32 x 2.4 MB = 77 MB decoded, fine under a 256 MB budget; do not retain both `QByteArray` base64 and the decoded image longer than needed.
- SVG: cache a `QSvgRenderer` per source and re-`render` into each tile; render is reentrant but one renderer instance must not be used from two threads at once, so one renderer per (source, thread) or a mutex. MEASURED: load+render 10 us vs render-only 4 us for a trivial icon; big SVGs are far worse, so caching the parse matters.
- Embedded images live as base64 in style strings. Intern image payloads at `ResolvedStyle` construction (`shared_ptr<const std::string>` or a hash id) so `CellState` copies, undo snapshots and the Format panel never copy them. Compute the content hash once at intern time.

## 5. Measuring without perf/valgrind

- Qt already emits: `QT_LOGGING_RULES="qt.widgets.painting=true"` (category defined in `widgets/kernel/qwidget.cpp:89`, default level Warning; logs `Marking ... dirty`, `Syncing`, `Painting and flushing dirty`, `Flushing` regions at Info/Debug, in `qwidgetrepaintmanager.cpp`). `QT_DEBUG_FPS=1` prints flushed frames per 5 s (`:1033`). `QT_NO_FAST_SCROLL=1` and `QT_NO_FAST_MOVE=1` disable the blit paths for A/B (`:390,468`). `QT_QPA_UPDATE_IDLE_TIME` tunes update batching (`gui/kernel/qwindow.cpp:2851` docs). The old `QT_FLUSH_PAINT` env is not in 6.11's `qwidget.cpp`/repaint manager (searched), do not rely on it.
- Frame timing: wrap `CanvasView::paintEvent` with `QElapsedTimer`; also record the gap between `update()` and the paint (input latency) using a timestamp stored at the input event. Report p50/p95/p99/max over a window, not averages. Keep `QElapsedTimer` to monotonic ns.
- Ad-hoc profiler: a thread-local `PerfScope("shape"|"label"|"image"|"tile.raster")` accumulating ns and counts into a static table, enabled by env var; dump as JSON. Use `clock_gettime(CLOCK_THREAD_CPUTIME_ID)` (or `QThread::currentThread()` CPU time via `getrusage(RUSAGE_THREAD)`) next to wall time to separate waiting from computing in workers.
- Reliable offscreen paint benchmark (for `qraw-bench --paint`):
  1. No window system: render straight to a `QImage` (`Format_ARGB32_Premultiplied`, fixed size, dpr 1) via the same tile compositor entry point; avoid `QGraphicsView::render()`/`QWidget::grab()` unless you want widget overhead. For end-to-end include one run with `QT_QPA_PLATFORM=offscreen` and a shown `CanvasView`, calling `repaint()` (synchronous) not `update()`.
  2. Warm-up pass first (font caches, glyph caches, image cache), then N >= 20 timed iterations, report min/median/p95. Fresh process per scenario for cold-start numbers (decode, first glyphs).
  3. Fix environment: `QT_QPA_PLATFORM=offscreen`, `QT_SCALE_FACTOR` pinned, one font family installed explicitly (`QFontDatabase::addApplicationFont`) so the font fallback does not vary by machine, `QT_FONT_DPI=96`. CPU governor noise: median not mean; report on multi-thread runs the thread count.
  4. Scenarios: first paint (cold), full repaint (warm), pan 1 viewport (tiles ready), zoom step (bucket change), hover (overlay only), drag 1 cell and 100 cells, bulk edit. Assert tile cache hit rate and "cells painted per frame" counters, which are deterministic, in CI; use wall-time budgets only as smoke thresholds with 3x slack (CI runners are noisy).
  5. QtTest `QBENCHMARK` gives iteration control and `-iterations`/`-median` (Docs: Qt Test Overview, benchmark section); fine for micro-benchmarks. CPU-instruction counters (`-callgrind`, `-perf`) need tools you do not have.
- Counters worth exposing in a debug HUD (Ctrl+Alt+F): tiles painted per second, tile queue depth, stale tiles dropped, bytes in image cache, cells culled by LOD.

## 6. Cheap big wins not (fully) covered by the plan

1. Decode-once alone removes the stall (about 12 ms per image per paint, MEASURED). Do this first, behind a small interface, before any tile work; it ships independently. The cache key must be a hash of the *resolved* source (data URI payload or file path+mtime), including the `ImageResolveContext` base directory, and must also cache negative results.
2. `RecordingCanvas`/display list: `Canvas` is already an interface (`PainterCanvas`, `SvgCanvas`, `SketchCanvas`). Add a recorder that stores resolved ops (path elements, pen/brush values, image handles, `TextBlock`) once per `CellState` change and a replayer onto `QPainter`. The recording replaces repeated string-keyed style resolution, stencil interpretation, `SketchCanvas` randomness setup (`SketchSettings::fromStyle`) and path building. It is immutable, trivially thread-safe, and gives a per-cell cache of "what to draw" independent of zoom (replay applies the zoom transform). Cost today per cell is dominated by rasterization (MEASURED), so this is the second-order win, but it also gives you exact per-cell invalidation keys (the display list hash) and cheap tile re-render after a zoom-bucket change.
3. Interned image payloads. Without it, every `CellState` copy in a snapshot copies megabytes of base64 (`ResolvedStyle::map()` owns strings). Make snapshots share `std::shared_ptr<const PageView>` with no copy, and intern payloads.
4. Hit testing and drawing decouple from `QGraphicsItem`: own spatial index (see section 1).
5. `paintBounds` accuracy for tile correctness: overflow labels and shadows. Add per-tile safety margin: when selecting cells for a tile, query the index with the tile rect inflated by `max(label overflow estimate, 4 px)`; or compute exact label bounds from the real `QTextLayout` once at snapshot-build time (worker thread) and store them back into the immutable state. Without this you will see clipped text at tile edges and stale ghosts after moves, which is the classic tile-cache bug.
6. Exact invalidation: invalidate tiles for `old.paintBounds ∪ new.paintBounds` per changed cell (+ edge-route changes of connected edges, which `diffViews` already catches since it diffs `points`). Table ancestors already queued by `diffViews`. Page-property changes (grid, background, page size) invalidate everything of that layer only; keep background+grid as a separate layer so a cell edit never re-rasters them.
7. Make `PageView::build` incremental or at least skip it during drags: a drag of one cell triggers a full `build` + `diffViews` per mouse move today (`diagram_scene.cpp:397-400`, O(n)). Snapshot dragging (planned) avoids that during the gesture; commit once on release. Check that `DocumentSession::documentChanged` is not emitted per mouse move during gestures.
8. Replace `viewport()->update()` after every mouse event (8 sites) with rect updates: overlay rect old/new union, rubber band rect, hover feedback rect. With tiles and FullViewportUpdate gone this is what makes hover O(overlay), not O(viewport).
9. Quantise zoom to buckets for tile scales (e.g. 2^(k/4)); the live view shows the nearest bucket scaled by a `QTransform` during wheel/pinch, then swaps. Qt side: glyph caches multiply by distinct matrices (section 3), so a continuous zoom without buckets will leak glyph cache memory in every worker thread.
10. Lower-cost interim (if the work lands in phases): `setViewportUpdateMode(MinimalViewportUpdate)` + `setOptimizationFlags(DontSavePainterState | DontAdjustForAntialiasing)` + `scene->setMinimumRenderSize(1)` + `PainterCanvas` hoisting `save/restore` out of `drawPath` give an hour-scale improvement for hover and scroll today, and `setItemIndexMethod(NoIndex)` removes BSP regeneration cost on 50k-cell loads. All of it is deleted once tiles land, so only do it if tiles are more than a milestone away.
11. Drag overlay: paint dragged cells' snapshot (one cached `QImage`, ARGB32PM, bounding rect of the selection at current bucket) at a float translate with `SmoothPixmapTransform` off and a pure translate (hits the blit path); edges attached to dragged vertices need live re-routing, so give those a vector overlay.
12. Memory budget guardrails in the compositor: tile edge 256 or 512 device px (256 KB or 1 MB ARGB32PM; 512 is a good default, MEASURED blit cost per tile is trivial), budget about 4x viewport pixels per bucket plus one neighbour bucket; evict least recently displayed.

## 7. Recommended changes to the plan (ranked)

1. Ship decode-once (decoded `QImage` ARGB32PM, hash-keyed, byte-budget LRU, negative cache, lazy mips) first and on its own. It removes about 12 ms per image per repaint today. PNG cannot decode scaled (no `ScaledSize` support in the PNG handler); do not plan "decode at reduced size" for PNG, only for JPEG.
2. Keep `QGraphicsView` as a shell but delete the N `CellItem`s: tile compositor in `drawBackground`, overlay in `drawForeground`, rotated-label proxy stays the only scene item, own spatial index replaces `scene.items(probe)`. Do not build on `ItemCoordinateCache`/`DeviceCoordinateCache`/`CacheBackground`: global 10 MB `QPixmapCache`, GUI-thread only, blur or full re-render on every zoom.
3. Tiles are `QImage` ARGB32_Premultiplied, 512 device px, origins at integer device pixels, painter transform `translate(-origin)*scale(zoom*dpr)`, `setDevicePixelRatio(dpr)` after rendering, blit with `drawImage(QPointF)` translate-only at dpr-matched images. MEASURED compositing cost is 0.3-1 ms for a full 1080p, so CPU compositing is not a bottleneck; keep the interface (`TileSource`/`Compositor`) thin and put the future `QRhiWidget` (Qt 6.7+ only) behind a version check.
4. Worker pool: dedicated `QThreadPool`, `setExpiryTimeout(-1)`, 4-8 threads, jobs take `shared_ptr<const PageView>` + generation counter, one owner per tile `QImage`. QPainter/QTextDocument/QTextLayout/QSvgRenderer/QImageReader are all fine per-thread (docs + MEASURED near-linear scaling to 8 threads for paths, 5-6x for text). Font engines and glyph caches are per thread (`qfont.cpp:3420`), so first use per thread costs about 1 ms and thread expiry throws them away.
5. Quantise zoom to buckets (bounded per-thread glyph caches, bounded tile sets) and show the old bucket scaled while the new one renders. Re-baseline goldens: tile text is grayscale AA, not LCD (`qpaintengine_raster.cpp:2768`).
6. Exact invalidation from `ChangeSet` + `diffViews`: invalidate tiles on `old ∪ new paintBounds` plus a label-overflow margin; make `paintBounds` text extent real (layout in the worker) or inflate by a margin, otherwise text clips at tile edges.
7. Replace `viewport()->update()` after every input event with rect updates; enable `WA_OpaquePaintEvent` and use `viewport()->scroll` + edge-strip tile paint for panning (accelerated only for opaque, non-overlapped widgets; `qwidgetrepaintmanager.cpp:468`).
8. Add a recording `Canvas` (display list of resolved ops) and intern image payloads in `ResolvedStyle`; this is what makes the snapshot cheap and the replay independent of style-map lookups. Do not use `QPicture` (slower than direct drawing, MEASURED) and do not rely on `QStaticText`/cached layouts for fidelity (saves 35-60% of text cost, not the dominant cost).
9. LOD in the tile painter (`setMinimumRenderSize` analogue): skip cells with device bbox < 1 px, replace them with a 1 px rect under ~3 px, skip labels under ~4 px font; kill the AA grid when `step*zoom < 4` (already done).
10. Benchmark scaffolding before tuning: `qraw-bench --paint` rendering to `QImage`, warm-up, median/p95, deterministic counters (cells painted, tiles rastered) asserted in CI, wall-time only with 3x slack; `qt.widgets.painting` logging and `QT_DEBUG_FPS` for the live widget.
