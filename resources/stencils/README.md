# Vendored draw.io stencils

| File | Content |
|---|---|
| `stencils.bin` | The 204 stencil sets of upstream `js/stencils.min.js`, base64-decoded but otherwise unchanged (raw-DEFLATE of draw.io's packed DOM). 9,090 `<shape>` elements. |
| `index.tsv` | `key<TAB>set path` per stencil, sorted by key, after a `# source ...` comment line. 9,089 keys (one duplicate key; the first set in path order wins). |
| `LICENSE` | Byte-exact copy of upstream `src/main/webapp/stencils/LICENSE`. |

## Provenance

- Source: `jgraph/drawio`, commit `c72c9a98` (2026-11-04), file `src/main/webapp/js/stencils.min.js`, lines of the form `f['<set>.xml'] = '<base64>';`.
- Upstream license: Apache-2.0 for the project, plus the field-of-use restriction in `LICENSE` (this directory) for the stencil libraries and their derivatives.
- The JavaScript is read as text only. It is never executed and no JavaScript is vendored.

## Field of use (read `LICENSE`)

These stencil libraries and any derivatives (including this packed copy and the decoded shapes) may not be used as software assets in, distributed for use with, or incorporated into Atlassian products or products distributed through the Atlassian marketplace or plugin ecosystem without explicit written permission. The restriction does not apply to end-user diagram output (exported images or documents). Keep this directory's `LICENSE` with every copy of the data.

## Format of `stencils.bin`

`QDSTPK01` magic, `u32be count`, then per set sorted by path: `u16be pathLen`, path (UTF-8), `u32be blobLen`, blob. All integers are big-endian. The runtime inflates a blob with raw DEFLATE and decodes it with `decodePackedStencilSet` (`src/render/stencil_pack.cpp`). See `docs/design/render.md` section 7.

## Regeneration

```sh
python3 -I tools/stencils/vendor_stencils.py <upstream>/src/main/webapp
```

The output is deterministic: running it twice gives byte-identical files. See `tools/stencils/README.md`.
