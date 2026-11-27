#!/usr/bin/env bash
# Smoke test of an unpacked or installed package: the executables start, and the plugins the package
# must carry are there (the check cmake/Packaging.cmake makes at install time is by file name; this one
# loads them). Nothing needs a display: everything runs on the offscreen platform.
#   usage: smoke_package.sh <qraw executable> <qraw-cli executable> <diagram> <work dir>
set -eu
gui="${1:?usage: smoke_package.sh <qraw> <qraw-cli> <diagram> <work dir>}"
cli="${2:?}"
diagram="${3:?}"
work="${4:?}"
mkdir -p "$work"
export QT_QPA_PLATFORM=offscreen
export QT_FORCE_STDERR_LOGGING=1
# Qt's offscreen platform finds no fonts on Windows runners without this (tools/ci, ctest environment).
if [ -n "${SYSTEMROOT:-}" ]; then
    export QT_QPA_FONTDIR="$SYSTEMROOT/Fonts"
fi

fail() { echo "smoke test failed: $*" >&2; exit 1; }
nonempty() { [ -s "$1" ] || fail "$1 was not written"; }

"$cli" --version || fail "qraw-cli does not start"
"$cli" --validate "$diagram" || fail "--validate"
"$cli" --export "$diagram" --output "$work/out.png" --format png || fail "PNG export"
"$cli" --export "$diagram" --output "$work/out.jpg" --format jpg || fail "JPEG export (imageformats/qjpeg)"
"$cli" --export "$diagram" --output "$work/out.svg" --format svg || fail "SVG export"
"$cli" --export "$diagram" --output "$work/out.pdf" --format pdf || fail "PDF export"
for file in out.png out.jpg out.svg out.pdf; do nonempty "$work/$file"; done

"$gui" --screenshot "$work/window.png" "$diagram" || fail "qraw --screenshot (platforms, styles, icons)"
nonempty "$work/window.png"
echo "smoke test passed: $gui"
