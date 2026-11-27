# draw.io rendering data

Upstream snapshot: `jgraph/drawio` branch `dev`, commit `c72c9a98` (2026-11-04), `VERSION` 32.6.10.
All paths are relative to `src/main/webapp/` unless they start with `/`. The counts were verified by
decoding or tokenizing the files. Nothing from upstream was executed.

---

## 1. Where the stencil XML lives now

**The `stencils/*.xml` files are no longer in the repo.** `stencils/` contains only `LICENSE` and
`clipart/Gear_128x128.png`. All 204 stencil sets are packed into one file:

| File | Size | Content |
|---|---|---|
| `js/stencils.min.js` | 3,873,040 B | lines 3–206: `f['<relpath>.xml'] = '<base64>';` (204 entries); lines 208–447: decoder plus an override of `mxStencilRegistry.loadStencil` |

`loadStencil(filename, fn)` strips `STENCIL_PATH + '/'` and looks up `f[...]`. On a miss it falls
back to the original HTTP loader, which now returns a 404. For example, `sap.xml` is listed in
`mxStencilRegistry.libraries['sap']` but exists in neither place.

**Encoding.** The payload is a custom binary DOM format, not XML text. The encoder is `Xml2Js.java`
(named in a source comment; not part of this checkout).

1. base64, then **raw DEFLATE** (`pako.inflateRaw`; in Python `zlib.decompress(b, -15)`).
2. A 20-byte header: 5 × big-endian `uint32` stream lengths, followed by five streams:
   `S0` ops (bytes), `S1` x-deltas, `S2` y-deltas, `S3` other numeric deltas (zigzag varints),
   `S4` UTF-8 strings separated by NUL.
3. The op loop (`decode()` at `js/stencils.min.js:278`):
   - `op == 255` closes the current element.
   - Otherwise `nameIdx = op >> 1`. If `nameIdx == names.length`, the name is the next string from `S4`.
     `op & 1` = leaf (no children). Leaf 0 means the element becomes the parent until the next 255.
   - The next byte is the attribute count. Each attribute is `attrId` (byte; new name from `S4` when
     `== attrNames.length`) followed by a type byte. Type `0` reads a string value from `S4`. Any
     other type reads a number:
     - For elements in {move, line, quad, curve, arc, rect, roundrect, ellipse, text, image,
       include-shape}: attrs x/x1/x2/x3 do `px += zz(S1)`, and attrs y/y1/y2/y3 do `py += zz(S2)`.
     - Everything else keeps a running sum per `"elem attr"` key: `sum += zz(S3)`.
     - The value string is `String(n/1000)`, i.e. fixed-point in thousandths.
   - `px`, `py` and the per-key sums reset at every `<shape>`.
   - `zz`: a little-endian base-128 varint `n`. Odd → `-(n+1)/2`, even → `n/2`.
4. It returns a lightweight DOM (`StencilNode`: `nodeName`, `firstChild`, `nextSibling`,
   `getAttribute`). `mxStencil` consumes it unchanged.

A Python port of this is about 60 lines, so stencils can be **decoded at build time into XML or a
compact native form**. Decoded total: 38.3 MB of XML, **9,090 `<shape>` elements**. Every root is
`<shapes name="mxgraph.<pkg>">`.

**Stencils per family** (shape count, with the number of XML files in parentheses):

| Family | Count | Family | Count | Family | Count |
|---|---|---|---|---|---|
| aws4 | 1050 | electrical | 529 (24) | gcp2 | 297 |
| aws3 | 293 | pid | 478 (24) | gcp | 66 (10) |
| aws2 | 250 (18) | rack | 487 (11) | gcp3 | 45 |
| aws | 99 (11) | office | 449 (10) | kubernetes / kubernetes2 | 40 / 39 |
| aws3d | 16 | mscae | 368 (11) | cisco | 296 (14) |
| azure | 90 | veeam | 580 (4) | cisco19 | 232 |
| alibaba_cloud | 310 | signs | 369 (11) | cisco_safe | 485 (9) |
| ibm_cloud / ibm | 110 / 8 | fluid_power | 246 | networks / networks2 | 57 / 115 |
| citrix / citrix2 | 97 / 126 | weblogos / webicons | 178 / 176 | mockup | 104 (9) |
| atlassian / atlassian2 | 26 / 109 | ios7 | 168 (2) | floorplan | 46 |
| salesforce | 96 | gmdl | 104 | bpmn | 39 |
| vvd | 94 | cabinets | 53 | flowchart | 36 |
| sitemap | 50 | android | 49 | arrows | 34 |
| eip | 36 | openstack | 18 | basic | 30 |
| lean_mapping | 13 | bootstrap | 4 | **er, uml** | **0** (JS only) |

Not stencils: **azure2** (704 SVGs), mscae (489 SVGs), ibm (188), sap (165), clip_art (164) and others
live as **image files under `img/lib/**`** (1,910 files), referenced as `shape=image;image=img/lib/...`.
GCP Icons embed SVG inline as `image=data:image/svg+xml,<base64>`.

**Name resolution** (`js/grapheditor/Graph.js:33833 parseStencilSet`, `:33729 getBasenameForStencil`):
- Registry key = `lower(shapes@name) + '.' + lower(shape@name with ' '→'_')`, e.g. `mxgraph.basic.4_point_star`.
- File lookup: `mxgraph.a.b.c` → basename `a/b` → `stencils/a/b.xml`. The basename is first checked
  against `mxStencilRegistry.libraries[...]` (`js/diagramly/Editor.js:13490–13552`), which maps
  packages to XML plus JS files. Example: `aws4` → `shapes/mxAWS4.js` + `aws4.xml`. Without a
  libraries entry, `_-_` in the basename is replaced by `_`.

## 2. Stencil XML grammar (`mxgraph/src/shape/mxStencil.js`, 1,026 lines; doc comment at lines 1–200)

```
<shapes name="mxgraph.pkg">
  <shape name w h aspect strokewidth>          w,h default 100; aspect "variable"|"fixed"; strokewidth "inherit"|number (default "1")
    <connections><constraint x y perimeter="0|1" name dx dy/>…</connections>
    <labelBounds if="styleKey" x y w h/>       draw.io extension (js/grapheditor/Shapes.js:8–60)
    <background> one path|rect|roundrect|ellipse, no fill/stroke ops </background>
    <foreground> first op = fill|stroke|fillstroke for the background, then any ops </foreground>
  </shape>
</shapes>
```

**Ops** (`drawNode`, lines 650–1026):
- State: `save`, `restore`. Paint: `fill`, `stroke`, `fillstroke`.
- Path: `path` (optional `rounded="1" arcSize`, applied only when it contains nothing but move/line),
  `move x y`, `line x y`, `quad x1 y1 x2 y2`, `curve x1 y1 x2 y2 x3 y3`,
  `arc rx ry x-axis-rotation large-arc-flag sweep-flag x y` (SVG semantics), `close`.
- Primitives: `rect x y w h`, `roundrect x y w h arcsize` (percent; `0` → `RECTANGLE_ROUNDING_FACTOR`
  ×100 = 15; radius = `min(w,h)·arcsize/100`), `ellipse x y w h`.
- `image src x y w h flipH flipV`.
- `text str x y align(left) valign(top) localized vertical rotation align-shape flip-shape`.
  Painted via `canvas.text(x, y, 0, 0, str, align, valign, false, '', null, false, rot)`.
- `include-shape name x y w h`.
- Style: `strokecolor`/`fillcolor`/`fontcolor color default`, `alpha`, `fillalpha`, `strokealpha`,
  `strokewidth width fixed`, `dashed dashed`, `dashpattern pattern` (default "3 3"),
  `linejoin join`, `linecap cap` (`flat` means butt), `miterlimit limit`,
  `fontsize size`, `fontstyle style` (bits 1 bold, 2 italic, 4 underline), `fontfamily family`.

**Coordinate semantics** (`computeAspect`):
- `sx = w/w0`, `sy = h/h0`. For `aspect="fixed"`, both become `min(sx, sy)` and the shape is centred.
- `direction=north|south` swaps the axes and offsets by `(w-h)/2`.
- Point = `(x0 + x·sx, y0 + y·sy)`. Arc `rx`/`ry` scale by `sx`/`sy`; the rotation is not scaled.
- `minScale = min(sx, sy)` scales `strokewidth` (unless `fixed="1"`), `dashpattern` and `fontsize`.
- Shape-level `strokewidth="inherit"`: the cell's `strokeWidth` is used unscaled by size.
  A numeric value means `value × minScale`.
- Constraint `x`/`y` are 0..1 fractions of the bounds. `dx`/`dy` are absolute. `perimeter="1"`
  projects the point from the centre onto the perimeter.
- JS `Number(null) === 0`, so a missing numeric attribute means 0.

**Colours** (`parseColor`/`getColorValue`):
- `stroke` / `fill` / `font` → the current style colour.
- A non-colour token (e.g. `fillColor2`, `accentColor`, `portColor`) → that cell style key, falling
  back to the `default` attribute.
- `default` → `graph.shapeBackgroundColor` for fillcolor, `shapeForegroundColor` for the others.

**Quirks in the shipped data:** `fillalpha` and `strokealpha` both call `setAlpha` (global alpha);
copy that exactly. 2,376 attribute-less `<rect/>` (zero-size, invisible). `aspect="1"` (104 shapes)
behaves as variable. 5 `<dashpattern dash=…>` use the wrong attribute and are ignored; unknown
`fillstrokecolor` (1) is ignored. Non-render attributes: `ink`, `tile`, `hexscale` (atlassian2; read by
`shapes/mxAtlassian2.js:711`). No `image`, `include-shape` or `rounded` path occurs in the shipped set.
`strokewidth="inherit"` on 8,929 of 9,090 shapes.

**Op frequency** (implementation priority): line 325k, curve 203k, move 85k, close 75k,
constraint 29k, arc 29k, path 22.5k, fillstroke 13.7k, fill 12k, fillcolor 11k, rect 6.7k,
stroke 6.2k, strokecolor 3.2k, save/restore 2.6k, strokewidth 2.5k, ellipse 2.4k, quad 1.8k,
alpha 1.25k, linejoin 1.2k, text 883, fillalpha 721, linecap 549, dashed/dashpattern ~420,
miterlimit 372, fontstyle 367, roundrect 326, fontfamily 287, strokealpha 270, fontsize 218,
fontcolor 196.

## 3. Shapes implemented in JS

| Source | Registrations | Notes |
|---|---|---|
| `mxgraph/src/view/mxCellRenderer.js:139–154` (classes in `mxgraph/src/shape/*.js`) | 16 | rectangle, ellipse, doubleEllipse, rhombus, line, image, arrow, arrowConnector, label, cylinder, swimlane (873 lines), connector, actor, cloud, triangle, hexagon |
| `js/grapheditor/Shapes.js` (11,759 lines) | ~100 | see the groups below |
| `shapes/**/*.js` (48 files) | 722 | `mxgraph.<family>.*` names, often driven by `mxShape…prototype.cst` constants |

**Shapes.js groups:**
- **Table/container:** table, tableRow, tableLine (plus core swimlane).
- **Flowchart/basic:** process, process2, document, parallelogram, trapezoid, step, hexagon (override),
  card, tape, note, note2, folder, cube, cylinder2, cylinder3, datastore, internalStorage, manualInput,
  dataStorage, delay, display, offPageConnector, loopLimit, tapeData, or, xor, orEllipse, sumEllipse,
  sortShape, collate, partialRectangle, lineEllipse, cross, plus, ext, message, corner, crossbar, tee,
  switch, curlyBracket, parallelMarker, dimension, smileyFace, transparent, isoRectangle, isoCube,
  isoCube2, tie, `mxgraph.basic.rect` (RECT2).
- **Callouts:** callout, wedgeCallout.
- **Arrows / edge shapes:** singleArrow, doubleArrow, flexArrow, taperedArrow, link, filledEdge, pipe,
  wire, waypoint, zigzag, curvedText.
- **UML:** umlActor, umlBoundary, umlEntity, umlControl, umlDestroy, umlLifeline, umlFrame, umlState,
  startState, endState, lollipop, requires, requiredInterface, providedRequiredInterface, module,
  component, associativeEntity, seqBoundary, seqControl, seqEntity, seqQueue, seqCollections,
  seqDatabase, seqActorStick.
- **Other:** gitTag, gitMergeCommit, gitCherryPick, mindmapBang, ishikawaHead, mermaidOdd,
  mermaidPerson, mermaidUsecaseActor, mermaidBlockArrow, mermaidSankeyLink.

**`shapes/` by family** (registration counts):
- 60 or more: aws3d 60, mockup 158 in total (iOS 58, misc 20, forms 19, containers 16, graphics 12,
  buttons 11, text 10, navigation 8, markup 4).
- 20–59: electrical 54, archimate3 51, sysml 44, archimate4 36, basic 33, floorplan 29, lean_mapping 29,
  ios7ui 23, infographic 22, bootstrap 22, arrows2 21, rack 20.
- 10–19: android 19, er 12, pid2 12 (3 files), gmdl 11, bpmn 11.
- Under 10: archimate 9, eip 7, dfd 6, c4 5, aws4 5, uml25 4, cabinets 4, networks 2,
  kubernetes 2, gcp2 2, and 1 each for sap, networks2, ibm, flowchart, cisco_safe, cisco19,
  atlassian, atlassian2 and emoji.

**Composites** need JS and stencils together: `mxgraph.aws4.resourceIcon`/`productIcon` draw the
stencil named in `resIcon=`; `kubernetes.icon` uses `prIcon=`; `mxgraph.bpmn.shape` is driven by
`outline=`/`symbol=`; `mxER.js` adds the ER markers.

**Native priority order** (★ = implement first; this matches the shape tokens used in
`js/grapheditor/Sidebar.js` and the frequency of sidebar styles):
1. ★ **Core:** rectangle (rounded/arcSize/absoluteArcSize), label, text (named style), ellipse,
   doubleEllipse, rhombus, triangle, hexagon (+`hexagonPerimeter2`), cylinder/cylinder3, cloud, actor,
   line, image, partialRectangle. Also the generic `mxStencil` interpreter, which unlocks 9,090 shapes.
2. ★ **Swimlane/containers/ER-tables:** swimlane (startSize, horizontal, swimlaneLine), table,
   tableRow, tableLine, stackLayout child styles, `mxgraph.er.*` (12), ER markers.
3. ★ **Flowchart:** process, document, parallelogram, trapezoid, step, card, tape, note,
   manualInput, internalStorage, dataStorage, delay, display, offPageConnector, loopLimit, or, xor,
   sortShape, collate, sumEllipse, orEllipse, datastore; `mxgraph.flowchart.*` (36 stencils
   + `document2`).
4. ★ **UML:** umlActor, umlLifeline, umlFrame, folder, module, component, lollipop,
   required/providedRequiredInterface, umlBoundary/Entity/Control/Destroy, start/endState, note2;
   `mxUML25.js` (4).
5. ★ **BPMN:** `shapes/bpmn/mxBpmnShape2.js` (task, task2, event, gateway2, data, conversation,
   swimlane, `mxgraph.bpmn.shape`) + `bpmn.xml` (39).
6. ★ **Arrows & callouts:** singleArrow, doubleArrow, flexArrow (edge), callout, wedgeCallout,
   `mxgraph.arrows2.*` (21), `mxgraph.basic.*` callouts (rectCallout, roundRectCallout).
7. **Mockup** (158 JS + 104 stencils), then the cloud-vendor composites (aws4, gcp2, kubernetes),
   then the long tail.

`sketch=1` (rough.js hand-drawn rendering) is a cross-cutting style flag. Defer it.

## 4. Sidebar palette definitions

**Files:**
- 68 × `js/diagramly/sidebar/Sidebar-*.js` (5.4 MB in total; largest SAP 604 KB, GCP2 556 KB,
  Bootstrap 347 KB).
- `js/diagramly/sidebar/Sidebar.js`: palette registry `Sidebar.prototype.configuration` (line 133) and
  `initPalettes` (line 1455).
- `js/grapheditor/Sidebar.js` (7,570 lines): General, Misc, Advanced, Basic, UML palettes and the
  helper implementations.

**Helper signatures** (`js/grapheditor/Sidebar.js`):
```
createVertexTemplateEntry(style, width, height, value, title, showLabel, showTitle, tags)   :6950
createEdgeTemplateEntry(style, width, height, value, title, showLabel, tags, ...)           :7025  edge (0,h)->(w,0)
addDataEntry(tags, width, height, title, data)       :996   data = Graph.compress(mxGraphModel XML)
addEntry(tags, fn)                                   :2121  fn builds cells in JS
createVertexTemplateFromCells(cells, w, h, title...) :7008
addPaletteFunctions(id, title, expanded, fns[])      :7064
addStencilPalette(id, title, stencilFile, style, ignore, onInit, scale, tags, customFns, groupId) :7448
addImagePalette(id, title, prefix, postfix, items, titles, tags)                                   :7400
getTagsForStencil(pkg, stencil, more) -> pkg parts[1:] + stencil + more ('_' -> ' ')               :7426
```

**Typical patterns:**
```js
var s = 'whiteSpace=wrap;html=1;shape=mxgraph.basic.';            // prefix vars, also mxConstants.STYLE_*
this.addPaletteFunctions('basic', mxResources.get('basic'), false, [
  this.createVertexTemplateEntry(s2 + 'banner', w, h * 0.5, '', 'Banner', null, null,
      this.getTagsForStencil(gn, 'banner', dt).join(' ')), ...]);
this.addAWS4ComputePalette(s, w, h, w2, gn, sb);                  // prefixes passed as params
var svc = function(color) { return 'sketch=0;...fillColor=' + color + ...; };   // closure helpers (AWS4)
this.addDataEntry(dt, 180, 160, 'Table 1', '7ZnfbpswFMaf...');    // ER, SAP, Gmdl
this.addEntry(dt + ' list', function() {                          // cells built in JS
  var cell = new mxCell('List', new mxGeometry(0,0,160,110), 'swimlane;...childLayout=stackLayout;...');
  cell.vertex = true; cell.insert(sb.cloneCell(row, 'Item 1')); ...
  return sb.createVertexTemplateFromCells([cell], 160, 110, 'List'); });
this.addStencilPalette('signs' + n, 'Signs / ' + n, dir + '/signs/' + n.toLowerCase() + '.xml', ';html=1;...');
```
`addStencilPalette` emits one entry per `<shape>`: style `'shape=' + pkg + lower(name) + style`, size
`round(parseInt(w|h, default 80) × scale)`, title `name` with `_` replaced by spaces.

**Census** (a tokenizer run over all sidebar files):

| Kind | Calls | Static? |
|---|---|---|
| createVertexTemplateEntry | 10,980 | **10,815 (98.5%) fully resolved** |
| createEdgeTemplateEntry | 231 | 231 resolved |
| addDataEntry | 689 | yes: decode `Graph.decompress` = base64 → inflateRaw → `decodeURIComponent` |
| addEntry (JS cell builders) | ~810 | **needs evaluation** (Bootstrap 192, Gmdl 100, SysML 91, UML25 72, Infographic 55, grapheditor/Sidebar.js 46, BPMN 45, Atlassian 39, GCP2 36, …) |
| addStencilPalette / addImagePalette | 8 / 7 call sites | yes, expanded from the decoded stencil XML / item arrays |

The resolver handles string/number literals, `+ - * /`, local `var`s (including ASI with a missing
`;`), parameters bound from same-file call sites, `mxConstants.*` (parsed from
`mxgraph/src/util/mxConstants.js`), `mxResources.get('k')` → `k`, one-line `return`-expression helpers
and closures, and `getTagsForStencil(...).join(' ')`. The 165 unresolved calls are inline assignments
(`'…W=' + (iw = 0.508)` in Network2) and loops over runtime arrays in `Sidebar.js`; both need only small
extensions.

**Overall: ~12,700 entries: ~87% simple (style + w/h + value + title), ~5.5% data blobs (also static),
~6.5% need JS evaluation**, concentrated in high-value palettes (General/Misc/Advanced, UML, ER lists,
BPMN, tables).

**Recommended strategy:**
1. Use a Python `tools/` extractor with a real JS tokenizer (strings, comments, regex-free) and a
   tiny expression evaluator with scopes (the approach above, about 200 lines). It emits JSON at build
   time and also decodes `stencils.min.js` and `addDataEntry` blobs.
2. For `addEntry` builders, either:
   - (a) interpret a JS subset: `new mxCell(value, new mxGeometry(x,y,w,h), style)`, `.vertex/.edge = true`,
     `insert`, `clone`, `sb.cloneCell(c, v)`, `geometry.setTerminalPoint`, `relative`, `offset`, and
     `createVertexTemplateFromCells`/`createEdgeTemplateFromCells`. Fall back to listing the rest as
     unresolved. Or
   - (b) hand-port the about 150 high-traffic builders (General, UML, ER, BPMN, tables) into data. Do
     **not** add a JS engine to the product. (Upstream itself converts builders to `addDataEntry`;
     see the commented `console.trace('xml', Graph.compress(...))` in `createVertexTemplateFromCells`.)
3. Pin to an upstream commit and diff the JSON on each update. Fail the build when the resolved
   ratio regresses.

**Output schema:**
```json
{
  "source": {"repo": "jgraph/drawio", "commit": "c72c9a98", "license": "Apache-2.0 + stencil terms"},
  "libraries": [{"id": "aws4", "title": "AWS 2025", "palettes": ["aws4Compute", "..."]}],
  "palettes": [{
    "id": "aws4Compute", "title": "AWS / Compute", "library": "aws4",
    "file": "Sidebar-AWS4.js", "order": 12,
    "entries": [
      {"kind": "vertex", "style": "sketch=0;...;shape=mxgraph.aws4.ec2;", "w": 78, "h": 78,
       "value": "", "title": "EC2", "tags": ["aws", "compute", "ec2"], "showLabel": false},
      {"kind": "edge", "style": "edgeStyle=orthogonalEdgeStyle;...", "w": 100, "h": 0, "title": "Default (left)"},
      {"kind": "cells", "xml": "<mxGraphModel><root>...</root></mxGraphModel>", "w": 180, "h": 160, "title": "Table 1"},
      {"kind": "unresolved", "reason": "addEntry builder", "file": "Sidebar-UML25.js", "line": 39}
    ]}],
  "stencilSets": [{"name": "mxgraph.aws4", "file": "aws4.xml", "shapes": 1050, "jsDeps": ["shapes/mxAWS4.js"]}]
}
```
Always normalise `kind:"vertex"`/`"edge"` to a `cells` XML at load time, so the app has a single
insert path.

## 5. Default stylesheet (`styles/default.xml`, 6,511 B)

Format: `<mxStylesheet><add as="name" [extend="other"]><add as="key" value="v"/>…</add>` (optional
`<remove as>`). Decoded by `mxgraph/src/io/mxStylesheetCodec.js` (`extend` clones the named style
first); loaded by `Graph.prototype.loadStylesheet` (`js/grapheditor/Graph.js:34149`). The diagramly
theme name `default-style2` (`js/diagramly/Editor.js:9874`) is this content; `mxGraphModel@style` may
name another theme file.

- **`defaultVertex`:** `shape=label; perimeter=rectanglePerimeter; fontSize=12; fontFamily=Helvetica;
  align=center; verticalAlign=middle; fillColor=default; strokeColor=default; fontColor=default`.
- **`defaultEdge`:** `shape=connector; labelBackgroundColor=default; endArrow=classic; fontSize=11;
  fontFamily=Helvetica; align=center; verticalAlign=middle; rounded=1; strokeColor=default; fontColor=default`.
- **Named styles:** `text` (fill/gradient/stroke none, left, top); `edgeLabel` extends text
  (labelBackgroundColor=default, fontSize=11); `label` (fontStyle=1, left, middle, spacing=2,
  spacingLeft=52, image 42×42, rounded=1); `icon` extends label (centred 48×48 image, label below);
  `swimlane` (shape=swimlane, fontSize=12, fontStyle=1, startSize=23); `group` (top, no
  fill/stroke/gradient, pointerEvents=0); `ellipse`/`rhombus`/`triangle` (shape + matching perimeter);
  `line` (shape=line, strokeWidth=4, labelBackgroundColor=default, top, spacingTop=8); `image`
  (label below); `roundImage`/`rhombusImage` (image + perimeter); `arrow` (shape=arrow, edgeStyle=none,
  fillColor=default); colour presets `fancy` (shadow+glass), gray/blue/green/turquoise/yellow/orange/
  red/pink/purple extending fancy, and `plain-*` (same colours, no fancy).

**Token mapping** (`mxStylesheet.getCellStyle`, `mxgraph/src/view/mxStylesheet.js:217`): split on `;`;
start from a clone of defaultVertex/defaultEdge (empty object if the style starts with `;`); apply
left to right: `key=value` sets (`none` deletes, numerics become floats); **a bare token is a
named-style lookup** (own properties only) merged at that position; unknown bare tokens are ignored.
So `ellipse;html=1` gets `perimeter=ellipsePerimeter`, `shape=ellipse` keeps rectanglePerimeter, and a
bare `process` does nothing.

**`default` colours** (`Graph.replaceDefaultColors`, `js/grapheditor/Graph.js:~18812`): fontColor,
strokeColor, gradientColor, imageBorder, labelBorderColor → `shapeForegroundColor`; fillColor,
imageBackground, swimlaneFillColor, labelBackgroundColor → `shapeBackgroundColor`;
`default<Key>=invert` inverts; `defaultLabelBackgroundColor=page` paints no background and cuts a gap
in the edge.

**Dark theme.** `styles/dark-default.xml` (default.xml minus the colour presets) is **referenced by no
JS (legacy)**; `default-old.xml` (pre-2020 Verdana/blue) is used when `mxGraphModel` has no `style`.
Dark mode is CSS `light-dark()`: `shapeForegroundColor = light-dark(#000000, var(--ge-light-color,
#ffffff))`, `shapeBackgroundColor = light-dark(#ffffff, var(--ge-dark-color, #121212))`
(`js/grapheditor/Editor.js:350–415`). `mxGraphModel@adaptiveColors`:
- `auto` (default): an explicit colour `c` renders as `light-dark(c, mxUtils.getInverseColor(c))`
  (`mxgraph/src/util/mxUtils.js:2452`: RGB inverted at 93%, then a CSS-style 180° hue-rotate matrix);
  a colour already written as `light-dark(a,b)` is used as given.
- `simple`: explicit colours unchanged, only `default` adapts. `none`: forces the light scheme.
- Applied in the canvas by `mxSvgCanvas2D.getLightDarkColor` (`:383`).

## 6. Perimeters, edge styles, markers

**Perimeters.** `mxgraph/src/view/mxPerimeter.js`: `rectanglePerimeter`, `ellipsePerimeter`,
`rhombusPerimeter`, `trianglePerimeter`, `hexagonPerimeter` (registered in
`mxgraph/src/view/mxStyleRegistry.js:70–74`). `js/grapheditor/Shapes.js` adds `centerPerimeter` 4142,
`lifelinePerimeter` 4149, `orthogonalPerimeter` 4180, `backbonePerimeter` 4418, `calloutPerimeter` 4495,
`parallelogramPerimeter` 4597, `trapezoidPerimeter` 4643, `stepPerimeter` 4699, `hexagonPerimeter2`
4762, `cloudPerimeter` 4813, `outlinePerimeter` 4830, `mermaidPersonPerimeter` 9425.
`Graph.roundedRectanglePerimeter` / `roundedPolygonPerimeter` (`Graph.js:42145`, `:42276`) apply with
`rounded=1;roundedPerimeter=1`. Sidebar use: none 247, ellipse 54, step 22, hexagon2 6, rhombus 5.

**Edge styles.** `mxgraph/src/view/mxEdgeStyle.js`: EntityRelation 98, Loop 235, ElbowConnector 345,
SideToSide 405, TopToBottom 497, SegmentConnector 600, OrthConnector 1141, keyed as
`entityRelationEdgeStyle`, `loopEdgeStyle`, `elbowEdgeStyle` (+`elbow=horizontal|vertical`),
`sideToSideEdgeStyle`, `topToBottomEdgeStyle`, `segmentEdgeStyle`, `orthogonalEdgeStyle`. draw.io
overrides Elbow/SideToSide/TopToBottom (`js/grapheditor/Graph.js:32200–32446`) and adds
`isometricEdgeStyle` (`Shapes.js:11036`) and `sequenceEdgeStyle` (`:4257`). `edgeStyle=none`/absent =
straight polyline through waypoints; `curved=1` and `jumpStyle` are render options; libavoid routing
(`libavoidRouting`) is optional and LGPL (§8). Sidebar use: none 93, orthogonal 57, elbow 53,
entityRelation 28, isometric 4.

**Markers** (`startArrow`/`endArrow`, `startFill`/`endFill`, `startSize`/`endSize`, `DEFAULT_MARKERSIZE` 6):
- `mxgraph/src/shape/mxMarker.js`: classic, classicThin, block, blockThin (`createArrow`; classic's back
  is notched at 3/4), open, openThin, oval, baseDash, doubleBlock, diamond, diamondThin, manyOptional.
- `js/grapheditor/Shapes.js:8280–8534`: dash, box, cross, circle, circlePlus, halfCircle, async,
  openAsync, mermaidExtension, mermaidDiamond.
- `shapes/er/mxER.js:1247–1357`: ERone, ERmandOne, ERmany, ERoneToMany, ERzeroToMany, ERzeroToOne;
  `shapes/mxSysML.js`: sysMLx, sysMLFound, sysMLLost, sysMLPackCont, sysMLProvInt, sysMLReqInt. Both are
  lazy-loaded via `mxMarker.getPackageForType` (`js/diagramly/Editor.js:13556`).
- Sidebar use: none 188, open 103, block 96, oval 31, classic 27, blockThin 24, diamondThin 17, then ER*.

## 7. Embedding the diagram in exports

**PNG.** Writer `Editor.writeGraphModelToPng(data, type, key, value)` (`js/diagramly/Editor.js:7391`)
inserts a chunk **immediately before the first IDAT** with CRC. draw.io writes **`tEXt`, keyword
`mxfile`, value `encodeURIComponent(mxfileXml)`** (`js/diagramly/EditorUi.js:7912`, `:13046`), plus an
optional `pHYs` dpi chunk. Reader `Editor.extractGraphModelFromPng` (`js/grapheditor/Editor.js:925`)
accepts `zTXt` with keyword `mxGraphModel`/`mxfile` (zlib inflate, raw-inflate fallback for old CLI
output, then `+` → space for the Java URLEncoder) and `tEXt` with either keyword; it stops at IDAT and
`decodeURIComponent`s values starting with `%`. Convention: `*.drawio.png`.

**SVG.** The root `<svg>` carries `content="<mxfile …>"`: the plain mxfile XML, attribute-escaped
(`js/diagramly/EditorUi.js:9479`, `:12654`). Readers (`js/diagramly/Editor.js:2651`, `:4880`) also
accept legacy base64 (`atob` + `unescape`, when the value starts with neither `<` nor `%`) and
URI-encoded (`%…`) values. Convention: `*.drawio.svg`.

**Inside mxfile**, `<diagram>` text may be compressed: base64 → inflateRaw → `decodeURIComponent`
(`Graph.decompress`, `js/grapheditor/Graph.js:3856`). The default is now uncompressed
(`Editor.defaultCompressed = false`, `js/diagramly/Editor.js:286`); readers must handle both.

**PDF.** Produced server-side or by Electron; the web app only sends `embedXml=1`
(`EditorUi.js:4178`, `:4263`). Reader `Editor.extractGraphModelFromPdf` (`js/diagramly/Editor.js:2791`)
tries, in order:
1. If the header is `%PDF-1.7`: an **EmbeddedFile** stream whose dictionary has
   `/Subtype /application#2Fvnd.jgraph.mxfile` before `stream`; the FlateDecode body is raw-inflated
   from `stream+9`.
2. `/Subject <hex>` (`00` bytes skipped) inside a deflated `/ObjStm` (PDF 1.4).
3. Any PDF: literal `/Subject (%3Cmxfile…%3C%2Fmxfile%3E)`.
4. `/Info n 0 R` → `/Subject m 0 R` via the xref.
5. Finally unescape `\(`, `\)`, `\\` and `decodeURIComponent`.

The most compatible native writer puts `encodeURIComponent(xml)` into Info `/Subject` as a literal
string, optionally plus a PDF 1.7 attachment with that MIME type. As far as I know QPdfWriter has no
Subject setter and cannot write a `%PDF-1.7` header (check against the Qt version in use); if so,
post-process by appending an incremental-update Info dictionary.

## 8. Licensing caveats

1. **Code** (JS shapes, sidebars, stylesheet, the grammar we port) is **Apache-2.0** (`/LICENSE`).
   There is **no `NOTICE` file** at this commit, so §4(d) has nothing to propagate. We must still ship
   the licence text, keep upstream copyright headers ("Copyright (c) 2006-20xx, JGraph Holdings Ltd /
   draw.io AG") in ported or derived files, and mark modified files (§4(b)). Add our own
   `NOTICE`/`THIRD_PARTY.md` recording the upstream commit.
2. **Stencils, shapes, images:** `stencils/LICENSE`, `shapes/LICENSE`, `img/LICENSE` and the README
   "License" section put a **field-of-use restriction** on the icon sets and stencil libraries "and any
   derivatives thereof (including conversions to other formats, traced reproductions …)": they may not
   be used in or distributed for **Atlassian products or the Atlassian marketplace/plugin ecosystem**
   without written permission; diagram output is exempt. Our decoded/converted stencils are explicitly
   derivatives, so we must carry this text and never ship them through Atlassian channels. Because
   `shapes/*.js` carries the same notice, native ports of those shapes are arguably covered too. Get
   legal review of how this interacts with Apache-2.0.
3. **Templates** (`templates/LICENSE`) are **CC-BY-4.0**: attribution, licence link, and an indication
   of changes.
4. **Third-party vendor icons.** Upstream "verified that all original licenses permit use in this
   project"; that covers draw.io, not Qraw. From general knowledge (check the current vendor pages
   before vendoring): AWS and Microsoft Azure architecture icons (azure2, mscae SVGs) allow use in
   architecture diagrams and documentation, and Azure forbids cropping/flipping/rotating; Google Cloud
   icons follow Google's brand guidelines; Cisco, IBM, SAP, Salesforce, Citrix, Veeam, Alibaba,
   Atlassian and Kubernetes (CC-BY-4.0) assets carry their own trademarks; `weblogos.xml` (178 brand
   logos) is pure trademark material. **Ship vendor sets as separable data packs**, each with its own
   LICENSE and provenance manifest, and keep the core build to basic/flowchart/UML/BPMN/ER and similar.
5. **Trademark:** do not use the draw.io name or logo (README "Trademark": EU TM #018062448).
6. **Other:** `js/libavoid-js` is **LGPL-2.1**; do not vendor it. Native libavoid is also LGPL, so link
   it dynamically.
