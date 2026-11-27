#!/usr/bin/env bash
# Install each artifact from dist/ in a clean environment and check it.
#   packaging/test-install.sh [rpm] [deb] [flatpak]    (default: all three)
# rpm and deb run in throwaway containers; flatpak installs --user on this host and uninstalls the app
# again afterwards (runtimes are left alone).
set -euo pipefail
. "$(dirname "$0")/common.sh"

# Ubuntu 24.04 ships Qt 6.4 (Qraw needs 6.5); the DEB must refuse to install there.
DEB_UNSUPPORTED_IMAGES="${DEB_UNSUPPORTED_IMAGES:-ubuntu:24.04}"

test_rpm() {
    log "RPM on $RPM_IMAGE"
    container_run "$RPM_IMAGE" packaging/test/native-in-container.sh rpm
}

test_deb() {
    local image
    for image in $DEB_TEST_IMAGES; do
        log "DEB on $image"
        container_run "$image" packaging/test/native-in-container.sh deb
    done
    for image in $DEB_UNSUPPORTED_IMAGES; do
        log "DEB on $image (expected to be uninstallable)"
        container_run "$image" packaging/test/deb-unsupported.sh
    done
}

test_flatpak() {
    local bundle="$DIST/Qraw-$VERSION.flatpak"
    [ -f "$bundle" ] || die "$bundle not found"
    # The sandbox sees only what it is given: the work directory must be under $HOME, not /tmp.
    local work
    mkdir -p "$ROOT/build"
    work="$(mktemp -d "$ROOT/build/flatpak-smoke.XXXXXX")"
    local was_installed=0
    flatpak info --user "$APP_ID" >/dev/null 2>&1 && was_installed=1
    log "Flatpak (user install on this host)"
    flatpak install --user -y --noninteractive "$bundle" >/dev/null
    cleanup() {
        rm -rf "$work"
        if [ "$was_installed" = 0 ]; then
            flatpak uninstall --user -y --noninteractive "$APP_ID" >/dev/null
        fi
    }
    trap cleanup RETURN
    # Wrappers with the executable interface tools/ci/smoke_package.sh expects.
    local name
    for name in qraw qraw-cli; do
        printf '#!/bin/sh\nexec flatpak run --user --env=QT_QPA_PLATFORM=offscreen --filesystem=%s --command=%s %s "$@"\n' \
            "$work" "$name" "$APP_ID" >"$work/$name"
        chmod +x "$work/$name"
    done
    cp "$ROOT/tests/compat/fixtures/inline_two_pages.drawio" "$work/"
    local out
    out="$("$work/qraw-cli" --version)"
    case "$out" in *"$VERSION"*) ;; *) die "unexpected --version output: $out" ;; esac
    echo "  ok    qraw-cli --version: $out"
    "$ROOT/tools/ci/smoke_package.sh" "$work/qraw" "$work/qraw-cli" "$work/inline_two_pages.drawio" "$work/out"
    echo "  ok    CLI exports (PNG, JPEG, SVG, PDF) and qraw --screenshot"
}

targets=("$@")
[ ${#targets[@]} -gt 0 ] || targets=(rpm deb flatpak)
for t in "${targets[@]}"; do
    case "$t" in
        rpm|deb|flatpak) "test_$t" ;;
        *) die "unknown target: $t (rpm, deb, flatpak)" ;;
    esac
done
log "all requested install tests passed: ${targets[*]}"
