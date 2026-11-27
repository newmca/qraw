# Upstream draw.io templates

Byte-exact copies of the diagram templates shipped with draw.io, used as real-world
regression input for the `.drawio` codec.

- Source: https://github.com/jgraph/drawio, branch `dev` (commit `c72c9a98b7a6ce392556ed04c05e0ff19f43f5c3`)
- Path in source: `src/main/webapp/templates/**/*.xml` (`index.xml` is a template list, not a diagram, and is not copied)
- draw.io VERSION file: `32.6.10`
- Project licence: Apache-2.0, see `LICENSE-APACHE-2.0.txt` (copy of the upstream `LICENSE`).
- The upstream `templates/` directory additionally carries its own `LICENSE`
  (Creative Commons Attribution 4.0 International). It is reproduced as
  `LICENSE-CC-BY-4.0.txt` because it is the licence file that sits next to these files upstream.

The files are not modified. Do not reformat them: the corpus tests rely on exact bytes.
Refresh with `python3 -I tools/corpus/generate_corpus.py --sync-upstream <drawio checkout>`.
