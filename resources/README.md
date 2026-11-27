# Vendored data

Files under `resources/` are compiled into `qraw_render` as Qt resources under `:/qraw/<path>` (see `src/render/CMakeLists.txt`). Every subdirectory carries the license of its upstream source. `THIRD_PARTY.md` lists each item with its upstream commit.

| Path | Source | License |
|---|---|---|
| `styles/default.xml` | jgraph/drawio `src/main/webapp/styles/default.xml`, commit `c72c9a98`, byte-exact | Apache-2.0 (`styles/LICENSE`) |
| `icons/` | original Qraw artwork: `qraw.svg` (master), `qraw-small.svg` (16 to 24 px), generated `hicolor/`, `qraw.ico`, `qraw.icns` | Apache-2.0 (project license) |
| `linux/` | `.desktop`, AppStream metainfo and shared-mime-info package, named by the application id; installed by `cmake --install` | Apache-2.0; metainfo CC0-1.0 |
| `stencils/` | generated from jgraph/drawio `src/main/webapp/js/stencils.min.js` by `tools/stencils/vendor_stencils.py` (see `stencils/README.md`) | Apache-2.0 plus the field-of-use terms in `stencils/LICENSE` |

## Regenerating the icons

`icons/hicolor/`, `qraw.ico` and `qraw.icns` are generated from the two SVGs and committed. `tools/icons/render_icons.cpp` rasterises them with `QSvgRenderer` (sizes up to 24 px use `qraw-small.svg`); `tools/icons/pack_icons.py` (Python standard library) packs the PNGs into the ICO and ICNS containers.

```sh
cmake --preset dev -DQRAW_BUILD_TOOLS=ON
cmake --build --preset dev --target regenerate_icons
```

`icons/hicolor/*/apps/*.png` are also compiled into `qraw_ui` as `:/qraw/icons/...`, the window icon's fallback when the icon theme does not have the application id. The application id is `QRAW_APP_ID` in the root `CMakeLists.txt`; changing it means renaming the files in `linux/` and `icons/hicolor/` and regenerating.
