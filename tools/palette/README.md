# tools/palette

Offline extractor for the shape palettes (`resources/palettes/`, see its README).

```sh
python3 -I tools/palette/extract_palettes.py <upstream>/src/main/webapp resources/palettes [--check]
```

| File | Role |
|---|---|
| `extract_palettes.py` | CLI: loads the sidebar scripts, runs the palette functions, writes JSON + `REPORT.md`; `--check` diffs and enforces the 90 % resolved ratio of the core families |
| `jsmini.py` | A small JavaScript-subset interpreter (tokenizer, parser, evaluator). It is the only thing that "runs" upstream code, and it can only reach the host objects handed to it: no I/O, no `eval`, no JavaScript engine |
| `sidebar_host.py` | Recording stand-ins for `Sidebar`, `mxCell`, `mxGeometry`, `mxUtils`, ... (ports of `createVertexTemplateEntry`, `addEntry`, `addStencilPalette`, ...) and the cell -> `<mxGraphModel>` serialiser |

The stencil set of the `arrows` palette is read through `tools/stencils/` (`vendor_stencils.read_entries`, `stencil_pack`).

To add a family (clipart, Cisco, SAP, ...): add its `Sidebar-<Name>.js` and entry function to `FAMILIES` in `extract_palettes.py`, regenerate, and look at `REPORT.md` for entries that need more of the interpreter (an unresolved identifier or unsupported method is reported with `file:line`).
