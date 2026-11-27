#!/usr/bin/env bash
# Build the RPM and SRPM into dist/ inside a Fedora container (host-side entry point).
set -euo pipefail
. "$(dirname "$0")/../common.sh"

"$ROOT/packaging/make-source.sh" >/dev/null
log "RPM build in $RPM_IMAGE"
container_run "$RPM_IMAGE" packaging/rpm/build-in-container.sh
ls -l "$DIST"/*.rpm
