#!/usr/bin/env bash
# Export the sample (tools/fidelity/sample.tsv) with draw.io and with Qraw at the same
# settings, score every pair and write the summary.
#
#   tools/fidelity/run_fidelity.sh <drawio-dir> <out-dir>
#
# <drawio-dir>: what tools/fidelity/fetch_drawio.sh produced (needs <drawio-dir>/app/drawio).
# <out-dir>:    everything lands here, outside the repository (suggested: build/fidelity):
#   inputs/       copies of the sample files, named <id>.<ext>
#   drawio/       draw.io PNGs, <id>.png (kept between runs: delete to re-export)
#   qraw/       Qraw PNGs (always re-exported)
#   results.csv, summary.md, composites/<id>.png (draw.io | Qraw | heat map)
# Environment: QRAW_FIDELITY_BORDER (default 10), QRAW_FIDELITY_SCALE (default 1),
#              QRAW_FIDELITY_JOBS (default 4).
# draw.io runs as a private headless Electron process: DISPLAY and WAYLAND_DISPLAY are
# unset and Electron's headless Ozone backend is used, so the desktop session is untouched.
set -euo pipefail

drawio_dir="${1:?usage: run_fidelity.sh <drawio-dir> <out-dir>}"
out_dir="${2:?usage: run_fidelity.sh <drawio-dir> <out-dir>}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
border="${QRAW_FIDELITY_BORDER:-10}"
scale="${QRAW_FIDELITY_SCALE:-1}"
jobs="${QRAW_FIDELITY_JOBS:-4}"
drawio="$drawio_dir/app/drawio"
[[ -x "$drawio" ]] || { echo "no $drawio: run tools/fidelity/fetch_drawio.sh first" >&2; exit 2; }
mkdir -p "$out_dir"; out_dir="$(cd "$out_dir" && pwd)"
case "$out_dir/" in "$repo"/build/*|"$repo"/docs/*) ;; "$repo"/*) echo "out-dir inside the repository must be under build/" >&2; exit 2;; esac
start=$SECONDS

# 1. Build the release CLI and the diff tool.
t=$SECONDS
cmake --preset release -DQRAW_BUILD_TOOLS=ON >/dev/null
cmake --build --preset release -j8 --target qraw-cli qraw-fidelity-diff >/dev/null
cli="$repo/build/release/src/cli/qraw-cli"
diff_tool="$repo/build/release/tools/fidelity/qraw-fidelity-diff"
echo "build: $((SECONDS - t)) s"

# 2. Stage the sample under unique names: <path without extension, '/' -> '__'>.<ext>
mkdir -p "$out_dir/inputs" "$out_dir/drawio" "$out_dir/qraw"
ids=()
while IFS=$'\t' read -r file _rest; do
    [[ "$file" == "file" || -z "$file" ]] && continue
    id="${file%.*}"; id="${id//\//__}"
    ids+=("$id")
    cp -f "$repo/$file" "$out_dir/inputs/$id.${file##*.}"
done < "$here/sample.tsv"
echo "sample: ${#ids[@]} files"

# 3. draw.io: one Electron start for the files that have no PNG yet (first page, opaque white).
t=$SECONDS
todo="$out_dir/inputs-todo"; rm -rf "$todo"; mkdir -p "$todo"
for f in "$out_dir"/inputs/*; do
    b="$(basename "$f")"; id="${b%.*}"
    [[ -s "$out_dir/drawio/$id.png" ]] || cp "$f" "$todo/$b"
done
if [[ -n "$(ls -A "$todo")" ]]; then
    echo "draw.io $(cat "$drawio_dir/VERSION" 2>/dev/null): exporting $(ls "$todo" | wc -l) files"
    # Headless Ozone works: no compositor and no --no-sandbox are needed.
    # Page numbers are 1-based since draw.io 27.0.2; for images the first page is the default.
    (unset DISPLAY WAYLAND_DISPLAY
     "$drawio" --ozone-platform=headless --disable-gpu -x -f png -s "$scale" -b "$border" --timeout 120 \
         -o "$out_dir/drawio" "$todo" > "$out_dir/drawio-export.log" 2>&1) || echo "draw.io exited non-zero (see $out_dir/drawio-export.log)"
fi
rm -rf "$todo"
cp "$drawio_dir/VERSION" "$out_dir/drawio/VERSION" 2>/dev/null || true
echo "draw.io export: $((SECONDS - t)) s"

# 4. Qraw: same scale, border, page 1, opaque white.
t=$SECONDS
rm -f "$out_dir"/qraw/*.png
printf '%s\n' "${ids[@]}" | xargs -P "$jobs" -I{} sh -c '
    f=$(ls "$1"/inputs/"$2".* | head -1)
    "$3" --export "$f" --output "$1/qraw/$2.png" --scale "$4" --border "$5" >/dev/null 2>>"$1/qraw-export.log" ||
        echo "Qraw export failed: $2" >&2' _ "$out_dir" {} "$cli" "$scale" "$border"
echo "Qraw export: $((SECONDS - t)) s"

# 5. Fonts both sides resolve through (fontconfig).
{ for fam in Helvetica Arial "Times New Roman" "Courier New" Verdana Georgia sans-serif; do
      printf '%s -> %s\n' "$fam" "$(fc-match "$fam" family style file 2>/dev/null | head -1)"; done; } > "$out_dir/fonts.txt"

# 5b. Uncompressed copies for the class counts (classify.py greps styles and labels).
mkdir -p "$out_dir/plain"
for f in "$out_dir"/inputs/*; do
    b="$(basename "$f")"; "$cli" --convert "$f" --output "$out_dir/plain/${b%.*}.drawio" --uncompressed >/dev/null 2>&1 || true
done

# 6. Diff.
t=$SECONDS
QT_QPA_PLATFORM=offscreen "$diff_tool" --reference "$out_dir/drawio" --candidate "$out_dir/qraw" \
    --sample "$here/sample.tsv" --out "$out_dir"
echo "diff: $((SECONDS - t)) s"
python3 -I "$here/classify.py" "$out_dir"
echo "total: $((SECONDS - start)) s; results in $out_dir (summary.md, results.csv, composites/)"
