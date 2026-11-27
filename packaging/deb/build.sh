#!/usr/bin/env bash
# Build the DEB into dist/ inside a Debian container (host-side entry point).
set -euo pipefail
. "$(dirname "$0")/../common.sh"

"$ROOT/packaging/make-source.sh" >/dev/null
log "DEB build in $DEB_IMAGE"
container_run "$DEB_IMAGE" packaging/deb/build-in-container.sh
ls -l "$DIST"/*.deb
