# Shared helpers for the packaging scripts. Source this file; do not run it.
# shellcheck shell=bash

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
DIST="${DIST:-$ROOT/dist}"
APP_ID="io.github.newmca.qraw"
# The project version is the single source of truth (CMakeLists.txt).
VERSION="$(sed -n 's/^ *VERSION \([0-9][0-9.]*\)$/\1/p' "$ROOT/CMakeLists.txt" | head -1)"

# Container images used for builds and install tests.
RPM_IMAGE="${RPM_IMAGE:-fedora:44}"
DEB_IMAGE="${DEB_IMAGE:-debian:trixie}"
# DEB install tests. Ubuntu 24.04 ships Qt 6.4 (Qraw needs 6.5), so it is not listed;
# test-install.sh checks that the DEB refuses to install there (DEB_UNSUPPORTED_IMAGES).
DEB_TEST_IMAGES="${DEB_TEST_IMAGES:-debian:trixie ubuntu:25.10 ubuntu:26.04 ubuntu:26.10}"

ENGINE="${CONTAINER_ENGINE:-$(command -v docker || command -v podman || true)}"

log() { printf '\n==> %s\n' "$*" >&2; }
die() { printf 'error: %s\n' "$*" >&2; exit 1; }

require_engine() {
    [ -n "$ENGINE" ] || die "neither docker nor podman found (set CONTAINER_ENGINE)"
}

# container_run IMAGE SCRIPT [ARGS...]
# Runs a script from the repo (relative to $ROOT) inside IMAGE. The repo is mounted
# read-only at /src, $DIST read-write at /out. Files written to /out are handed back
# to the invoking user (docker runs as root; rootless podman already maps to us).
# JOBS (default 6) is the parallelism the build may use.
container_run() {
    local image="$1" script="$2"
    shift 2
    require_engine
    mkdir -p "$DIST"
    local owner=()
    [ "$(basename "$ENGINE")" = docker ] && owner=(-e "HOST_UID=$(id -u)" -e "HOST_GID=$(id -g)")
    "$ENGINE" run --rm --security-opt label=disable \
        -v "$ROOT:/src:ro" -v "$DIST:/out" \
        -e "APP_VERSION=$VERSION" -e "APP_ID=$APP_ID" -e "JOBS=${JOBS:-6}" "${owner[@]}" \
        "$image" bash -euo pipefail "/src/$script" "$@"
}

# Inside a container: give /out files back to the host user.
hand_back_out() {
    if [ -n "${HOST_UID:-}" ]; then chown -R "$HOST_UID:$HOST_GID" /out; fi
}
