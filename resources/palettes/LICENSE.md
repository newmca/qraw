# License of the palette data

The palette definitions (style strings, sizes, titles, search tags and the template cells) are derived from the sidebar definitions of draw.io, `jgraph/drawio` commit `c72c9a98`:

- Copyright (c) 2006-2025 JGraph Holdings Ltd
- Copyright (c) 2006-2025 draw.io AG
- Licensed under the Apache License, Version 2.0 (https://www.apache.org/licenses/LICENSE-2.0). The data files are generated reformulations (JSON) of those definitions; no draw.io code is compiled into Qraw.

The shapes referenced by `shape=mxgraph.*` styles are the stencil libraries in `resources/stencils/`, which carry an additional field-of-use restriction: they and their derivatives may not be used in Atlassian products or products distributed through the Atlassian marketplace or plugin ecosystem without explicit written permission. The restriction does not apply to end-user diagram output. See `resources/stencils/LICENSE`, which must stay with every copy of this data.

Vendor icon sets referenced by some entries (AWS, Azure, GCP) have their own usage terms; see `docs/research/drawio-rendering-data.md` section 8.
