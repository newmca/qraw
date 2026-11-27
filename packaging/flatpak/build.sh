#!/usr/bin/env bash
# Build dist/Qraw-<version>.flatpak, a single-file bundle (host-side entry point). No root needed.
#
# Needs the org.flatpak.Builder flatpak (or flatpak-builder) and the KDE 6.11 runtime and SDK; the
# missing ones are installed from flathub with --user. The manifest builds this working tree
# (packaging/flatpak/README.md).
set -euo pipefail
. "$(dirname "$0")/../common.sh"

manifest="$ROOT/packaging/flatpak/$APP_ID.yml"
bundle="$DIST/Qraw-$VERSION.flatpak"
# Under the home directory: the builder is itself a Flatpak with a private /tmp.
work="$ROOT/build/flatpak"
remote=https://dl.flathub.org/repo/flathub.flatpakrepo

if command -v flatpak-builder >/dev/null; then
    builder=(flatpak-builder)
elif flatpak info --user org.flatpak.Builder >/dev/null 2>&1 || flatpak info org.flatpak.Builder >/dev/null 2>&1; then
    builder=(flatpak run org.flatpak.Builder)
else
    die "flatpak-builder not found (install it, or: flatpak install --user flathub org.flatpak.Builder)"
fi

flatpak remote-add --user --if-not-exists flathub "$remote"

mkdir -p "$DIST" "$work"
log "flatpak-builder"
cd "$ROOT"
# --disable-rofiles-fuse: no FUSE needed (works in CI and in containers).
"${builder[@]}" --user --force-clean --disable-rofiles-fuse --jobs="${JOBS:-6}" \
    --install-deps-from=flathub --default-branch=stable \
    --repo="$work/repo" --state-dir="$work/state" "$work/app" "$manifest"

log "Bundling"
rm -f "$bundle"
flatpak build-bundle --runtime-repo="$remote" "$work/repo" "$bundle" "$APP_ID" stable
ls -l "$bundle"
