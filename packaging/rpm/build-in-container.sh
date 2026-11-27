#!/usr/bin/env bash
# Runs inside the Fedora container. See build.sh.
. /src/packaging/common.sh

dnf -y -q install rpm-build dnf-plugins-core >/dev/null

top=/tmp/rpmbuild
mkdir -p "$top"/{SOURCES,SPECS}
cp "/out/src/qraw-$APP_VERSION.tar.gz" "$top/SOURCES/"
cp /src/packaging/rpm/qraw.spec "$top/SPECS/"

defs=(--define "_topdir $top" --define "qraw_version $APP_VERSION"
      --define "_smp_ncpus_max $JOBS")
spec="$top/SPECS/qraw.spec"

dnf -y -q builddep "${defs[@]}" "$spec" >/dev/null 2>&1 || dnf -y builddep "${defs[@]}" "$spec"
rpmbuild -ba "${defs[@]}" "$spec"

rm -f /out/qraw-*.rpm
cp "$top"/RPMS/*/qraw-*.rpm "$top"/SRPMS/*.src.rpm /out/
rm -f /out/qraw-debuginfo-* /out/qraw-debugsource-*
hand_back_out
