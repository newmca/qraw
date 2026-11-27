# Diagram templates

Templates offered by the Welcome dialog (File > New from template). Compiled into the binary under
`:/qraw/templates/<category>/<name>.xml`.

- Source: https://github.com/jgraph/drawio, branch `dev` (commit `c72c9a98b7a6ce392556ed04c05e0ff19f43f5c3`),
  path `src/main/webapp/templates/**/*.xml`, copied from `tests/corpus/upstream-templates/`.
- Licence: the upstream `templates/` directory is licensed under Creative Commons Attribution 4.0
  International (`LICENSE-CC-BY-4.0.txt`). The draw.io project licence is Apache-2.0
  (`LICENSE-APACHE-2.0.txt`).
- Changes: this is a subset. `network/fortinet.xml` (3.4 MB) and `cloud/gcp/` were left out to keep the
  binary small. The copied files themselves are unmodified.
- The category is the directory name, the title is the file name with `_` replaced by spaces.
