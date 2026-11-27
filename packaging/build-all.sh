#!/usr/bin/env bash
# Rebuild every package into dist/.   packaging/build-all.sh [rpm] [deb] [flatpak]
set -euo pipefail
here="$(cd "$(dirname "$0")" && pwd)"
targets=("$@")
[ ${#targets[@]} -gt 0 ] || targets=(rpm deb flatpak)
for t in "${targets[@]}"; do
    case "$t" in
        rpm|deb|flatpak) "$here/$t/build.sh" ;;
        *) echo "unknown target: $t (rpm, deb, flatpak)" >&2; exit 1 ;;
    esac
done
