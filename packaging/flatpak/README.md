# Flatpak

`io.github.newmca.qraw.yml` builds Qraw from the working tree this directory is in. Qraw is not on Flathub; the manifest is Flathub-shaped so that a submission needs the source swapped for a tag.

## Build and smoke test

Needs `flatpak`, `org.flatpak.Builder`, and the KDE runtime and SDK 6.11 (`flatpak install --user flathub org.kde.Platform//6.11 org.kde.Sdk//6.11`). Run from the repository root; keep the directories under your home, not in `/tmp` (the builder is itself a Flatpak with a private `/tmp`, and a state directory there fails):

```sh
M=packaging/flatpak/io.github.newmca.qraw.yml
flatpak run org.flatpak.Builder --user --force-clean --disable-rofiles-fuse --jobs=8 \
    --repo=build/flatpak/repo --state-dir=build/flatpak/state build/flatpak/app $M

# Without installing: run inside the build directory (a folder for files is given with --filesystem).
flatpak run org.flatpak.Builder --run build/flatpak/app $M qraw-cli --version
flatpak run org.flatpak.Builder --run --filesystem=$PWD/build/flatpak/smoke build/flatpak/app $M \
    qraw-cli --export build/flatpak/smoke/in.drawio --output build/flatpak/smoke/out.png

# Lint (the flathub linter).
flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest $M
flatpak run --command=flatpak-builder-lint org.flatpak.Builder repo build/flatpak/repo
```

`packaging/flatpak/build.sh` (or `packaging/build-all.sh flatpak`) does the build above with `--jobs=6` and then makes the single-file bundle `dist/Qraw-<version>.flatpak` (`flatpak build-bundle --runtime-repo=<flathub>`, branch `stable`); `packaging/test-install.sh flatpak` installs it with `--user`, runs `tools/ci/smoke_package.sh` through the sandbox and uninstalls it. CI builds and uploads it (`linux-packages`).

`build/` and `dist/` are ignored by git. Measured on the development machine: build about 75 s with 8 jobs.

## Choices

- **Runtime.** `org.kde.Platform` and `org.kde.Sdk` 6.11, the newest stable KDE runtime on Flathub (6.10 and 5.15-25.08 are the other branches there); it ships Qt 6.11 (Qraw needs Qt 6.5 or newer). The runtime provides zlib, the xdg-desktop-portal and KDE platform themes and the Qt plugins; nothing else is built.
- **Module.** `cmake-ninja`, `RelWithDebInfo`, tests off, `QRAW_WARNINGS_AS_ERRORS=OFF` (a newer compiler must not break a package build). The install is the normal `cmake --install`: `qraw`, `qraw-cli`, the desktop file, the metainfo, the MIME package and the icons, all named after the application id.
- **Permissions** (each line is justified in the manifest): `--share=ipc`, `--socket=wayland`, `--socket=fallback-x11`, `--device=dri`, `--socket=cups`. There is no `--filesystem` and no network. File dialogs: inside a sandbox Qt's xdg-desktop-portal platform theme and the KDE platform theme use the FileChooser portal (both plugins carry `org.freedesktop.portal.FileChooser`), which hands the app only the files the user picked; Open Link uses the OpenURI portal. Printing: Qt 6.11 has no client for the print portal, so printing needs the host's CUPS socket (`--socket=cups`); PDF export and Print Preview work without it. Revisit when Qt gains a print portal client.
- **User data.** Settings and autosave live in `~/.var/app/io.github.newmca.qraw/` (a new place; an unsandboxed Qraw keeps its own).

## Flathub form

For a submission the `dir` source becomes the `git` source that is commented in the manifest (URL, `tag`, and the `commit` the tag points to; Flathub requires both). No tag exists yet, so that block is a template. The manifest must be named after the application id in the Flathub repository.

## Lint results

`flatpak-builder-lint manifest`: only `appid-url-not-reachable` (the linter fetches https://github.com/newmca/qraw; it got a 404 while the repository was private). `flatpak-builder-lint repo`: the same, plus `appstream-missing-screenshots` and `appstream-screenshots-not-mirrored-in-ostree`: the metainfo screenshots are raw GitHub URLs, which the builder mirrors only when it can download them (the repository was private when this was run, and they are on `main`, not on a tag). The first should now be gone and the others should disappear once a tag exists; none can be fixed in the files.
