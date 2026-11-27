# Shape palettes

Generated palette data for the editor's shape sidebar (`PaletteLibrary`, `PaletteDock`).

| | |
|---|---|
| Source | `jgraph/drawio` commit `c72c9a98`, `src/main/webapp/js/grapheditor/Sidebar.js`, `js/diagramly/sidebar/Sidebar-*.js`, stencil set `arrows.xml` from `js/stencils.min.js` |
| Generator | `tools/palette/extract_palettes.py` (Python 3 stdlib; sidebar scripts are interpreted as text by `tools/palette/jsmini.py`, no JavaScript engine runs) |
| Schema | version 1, `docs/design/ui.md` section 8 |
| Report | `REPORT.md`: entries written and skipped per palette, with upstream `file:line` and reason |
| License | `LICENSE.md` |

## Regeneration

```sh
python3 -I tools/palette/extract_palettes.py <upstream>/src/main/webapp resources/palettes
python3 -I tools/palette/extract_palettes.py <upstream>/src/main/webapp resources/palettes --check   # CI: diff + resolved ratio
```

The output is deterministic (sorted keys, stable order). `README.md` and `LICENSE.md` are maintained by hand.

## Files

One file per family: `general`, `misc`, `advanced`, `basic`, `arrows` (arrows + arrows2), `flowchart`, `uml`, `er`, `bpmn`, `network`, `kubernetes`, `aws4`, `azure`, `gcp`, `mockups`, `floorplan`, plus the derived `tables` and `containers` (copies of the General/Misc/Advanced entries whose style is a table or a container). Multi-library families hold one palette per sub-library (`aws4Compute`, `azure2Analytics`, ...; ids are the draw.io ids without spaces).

Entries: `kind` is `vertex`, `edge` or `cells`. `showLabel` is written only when it is `false` (the loader's default is `true`); `value` only when non-empty. Tags are lower-case search words; the title is searched separately.

## Known deviations from draw.io

- `Sidebar.createVertexTemplateFromCells` adds `labelPerimeter=1` to shapes whose label sits outside on the side; the data does not carry that style addition.
- The `azure` entries are `image=img/lib/azure2/...svg` shapes. Qraw does not ship draw.io's `img/` directory yet, so these insert but draw as empty images (REPORT.md counts them as written).
- Palettes not listed above (clipart, Cisco, SAP, Bootstrap, ...) are not extracted yet; the extractor supports adding a family by adding one line to `FAMILIES`.
