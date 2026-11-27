#!/usr/bin/env bash
# Download the official draw.io Desktop Linux AppImage from the GitHub releases of
# jgraph/drawio-desktop, verify the checksums the release publishes, and extract it.
#
#   tools/fidelity/fetch_drawio.sh <target-dir> [<version-tag>]
#
# <target-dir> must be outside the repository (it receives ~1 GB). The script is
# idempotent: if <target-dir>/app/drawio exists and VERSION matches, it does nothing.
# Result: <target-dir>/VERSION, <target-dir>/DIGESTS.txt, <target-dir>/app/ (extracted).
set -euo pipefail

target="${1:?usage: fetch_drawio.sh <target-dir> [<version-tag>]}"
tag="${2:-}"
repo_root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
target="$(mkdir -p "$target" && cd "$target" && pwd)"
case "$target/" in "$repo_root"/*) echo "refusing to download into the repository" >&2; exit 2;; esac
command -v gh >/dev/null || { echo "gh is required (read-only release lookup)" >&2; exit 2; }

if [[ -z "$tag" ]]; then
    tag="$(gh release view --repo jgraph/drawio-desktop --json tagName --jq .tagName)"
fi
version="${tag#v}"
asset="drawio-x86_64-${version}.AppImage"
base="https://github.com/jgraph/drawio-desktop/releases/download/${tag}"

if [[ -x "$target/app/drawio" && "$(cat "$target/VERSION" 2>/dev/null)" == "$version" ]]; then
    echo "draw.io $version already present in $target"
    exit 0
fi

dl="$target/download-$version"
rm -rf "$dl" "$target/app"; mkdir -p "$dl"
cd "$dl"
curl -fsSL -o "$asset" "$base/$asset"
curl -fsSL -o latest-linux.yml "$base/latest-linux.yml"
curl -fsSL -o Files-SHA256-Hashes.txt "$base/Files-SHA256-Hashes.txt"

# electron-builder metadata: base64 sha512 of the AppImage; the release also lists SHA-256.
want512="$(python3 -I - "$asset" <<'PY'
import re, sys
name = sys.argv[1]
text = open("latest-linux.yml").read()
m = re.search(r"-\s+url:\s*" + re.escape(name) + r"\s*\n\s+sha512:\s*(\S+)", text)
print(m.group(1) if m else "")
PY
)"
got512="$(openssl dgst -sha512 -binary "$asset" | base64 -w0)"
[[ -n "$want512" && "$want512" == "$got512" ]] || { echo "sha512 mismatch for $asset" >&2; exit 1; }
want256="$(grep -F "$asset" Files-SHA256-Hashes.txt | awk '{print tolower($1)}' | head -1)"
got256="$(sha256sum "$asset" | awk '{print $1}')"
if [[ -n "$want256" && "$want256" != "$got256" ]]; then echo "sha256 mismatch for $asset" >&2; exit 1; fi
{
    echo "version: $version"
    echo "file: $asset"
    echo "sha256: $got256 (release list: ${want256:-not listed})"
    echo "sha512(base64): $got512 (latest-linux.yml: verified)"
} | tee "$target/DIGESTS.txt"

chmod +x "$asset"
mkdir -p "$target/extract" && cd "$target/extract"   # new empty directory; squashfs-root lands here
"$dl/$asset" --appimage-extract >/dev/null
mv squashfs-root "$target/app"
cd "$target" && rmdir extract
echo "$version" > "$target/VERSION"
echo "draw.io $version extracted to $target/app"
