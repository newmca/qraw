# tools/stencils

Python 3 standard library only. Run with `python3 -I` from the repository root.

| Script | Purpose |
|---|---|
| `vendor_stencils.py <webapp dir> [--out resources/stencils]` | Reads `js/stencils.min.js` as text (never executed), writes `stencils.bin`, `index.tsv` and copies the upstream `stencils/LICENSE`. Prints the key and duplicate counts (duplicates to stderr). |
| `dump_shape.py <set.xml> [shape index]` | Prints a decoded set or one shape of `resources/stencils/stencils.bin` as XML. Generates the test fixtures in `tests/render/data/stencils/` and helps debugging. |
| `stencil_pack.py` | The reference decoder of the packed format (port of `decode()` in upstream `js/stencils.min.js:278`). The C++ decoder in `src/render/stencil_pack.cpp` must produce the same trees. |

```sh
python3 -I tools/stencils/vendor_stencils.py /path/to/drawio/src/main/webapp
python3 -I tools/stencils/dump_shape.py flowchart.xml 0 > tests/render/data/stencils/flowchart_annotation_1.xml
```

Format and licensing: `resources/stencils/README.md`, `docs/design/render.md` section 7, `DECISIONS.md` D-022.
