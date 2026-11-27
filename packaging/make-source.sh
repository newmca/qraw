#!/usr/bin/env bash
# Create dist/src/qraw-<version>.tar.gz, the source tree RPM and DEB build from.
# It is `git archive` of the committed tree (HEAD, or QRAW_SOURCE_REF): uncommitted changes to the
# application are not in the package. The packaging files themselves (spec, debian/) are taken from the
# working tree by the build scripts.
set -euo pipefail
. "$(dirname "$0")/common.sh"

[ -n "$VERSION" ] || die "cannot read the version from CMakeLists.txt"
ref="${QRAW_SOURCE_REF:-HEAD}"
if [ "$ref" = HEAD ] && [ -n "$(git -C "$ROOT" status --porcelain --untracked-files=no -- . ':!packaging' ':!.github' ':!*.md')" ]; then
    echo "warning: the working tree has uncommitted changes; the source archive holds $ref only" >&2
fi

out="$DIST/src/qraw-$VERSION.tar.gz"
mkdir -p "$DIST/src"
# -n: no timestamp in the gzip header, so the same commit gives the same file.
git -C "$ROOT" archive --format=tar --prefix="qraw-$VERSION/" "$ref" | gzip -9n >"$out"
echo "$out"
