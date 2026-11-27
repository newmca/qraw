#!/usr/bin/env bash
# Regenerates the example crops of docs/FIDELITY.md from a finished run:
#   tools/fidelity/make_crops.sh <out-dir> [<crop-dir>]    (default crop-dir: docs/fidelity)
# crops.tsv lists name, file id, the region as fractions of the aligned canvas and the panel width.
set -euo pipefail
out_dir="${1:?usage: make_crops.sh <out-dir> [<crop-dir>]}"
here="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
repo="$(cd "$here/../.." && pwd)"
dest="${2:-$repo/docs/fidelity}"
tool="$repo/build/release/tools/fidelity/qraw-fidelity-diff"
mkdir -p "$dest"
tail -n +2 "$here/crops.tsv" | while IFS=$'\t' read -r name id frac width; do
    QT_QPA_PLATFORM=offscreen "$tool" --example "$id" --frac "$frac" --panel-width "$width" \
        --reference "$out_dir/drawio" --candidate "$out_dir/qraw" --crop "$dest/$name.png"
done
echo "crops in $dest: $(du -sh "$dest" | cut -f1)"
