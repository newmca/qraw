#!/usr/bin/env bash
# Install /out/<package> in a clean container and check it. usage: native-in-container.sh rpm|deb
. /src/packaging/common.sh
kind="$1"
export LANG=C.UTF-8
fixture=/src/tests/compat/fixtures/inline_two_pages.drawio

# Run a noisy package-manager command, showing its output only if it fails.
quiet() { local log; log="$("$@" 2>&1)" || { echo "$log" >&2; return 1; }; }

echo "container: $(. /etc/os-release && echo "$PRETTY_NAME")"
# Validators, plus a font: a desktop always has one, a bare container does not and Qt's offscreen
# platform draws nothing without it.
case "$kind" in
rpm)
    quiet dnf -y install /out/qraw-"$APP_VERSION"-*.x86_64.rpm libxml2 desktop-file-utils appstream \
        dejavu-sans-fonts
    ;;
deb)
    export DEBIAN_FRONTEND=noninteractive
    quiet apt-get update
    quiet apt-get install -y --no-install-recommends /out/qraw_"$APP_VERSION"_*.deb \
        libxml2-utils desktop-file-utils appstream fonts-dejavu-core
    ;;
esac
echo "installed $kind"

for exe in /usr/bin/qraw /usr/bin/qraw-cli; do
    [ -x "$exe" ] || { echo "$exe is not installed" >&2; exit 1; }
done
printf '  ok    /usr/bin/qraw and /usr/bin/qraw-cli are installed\n'

desktop-file-validate "/usr/share/applications/$APP_ID.desktop"
appstreamcli validate --no-net "/usr/share/metainfo/$APP_ID.metainfo.xml" >/dev/null
xmllint --noout "/usr/share/mime/packages/$APP_ID.xml"
test -s "/usr/share/icons/hicolor/scalable/apps/$APP_ID.svg"
test -s "/usr/share/icons/hicolor/256x256/apps/$APP_ID.png"
printf '  ok    desktop file, metainfo, MIME package and icons are installed and valid\n'

# The distribution's triggers must have registered the MIME type (update-mime-database).
grep -q 'application/vnd.jgraph.mxfile' /usr/share/mime/types || { echo "MIME type not registered" >&2; exit 1; }
printf '  ok    application/vnd.jgraph.mxfile is registered in the MIME database\n'

# The installed programs must have found every Qt library (no missing shared object).
for exe in /usr/bin/qraw /usr/bin/qraw-cli; do
    if ldd "$exe" | grep -q 'not found'; then ldd "$exe" >&2; echo "$exe has unresolved libraries" >&2; exit 1; fi
done
printf '  ok    no unresolved shared libraries\n'

out="$(qraw-cli --version)"
case "$out" in *"$APP_VERSION"*) ;; *) echo "unexpected --version output: $out" >&2; exit 1 ;; esac
printf '  ok    qraw-cli --version: %s\n' "$out"

# Exports, then the window rendered offscreen (tools/ci/smoke_package.sh, the script CI runs on the
# Windows and macOS packages).
/src/tools/ci/smoke_package.sh /usr/bin/qraw /usr/bin/qraw-cli "$fixture" /tmp/smoke
printf '  ok    CLI exports (PNG, JPEG, SVG, PDF) and qraw --screenshot\n'
