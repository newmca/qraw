#!/usr/bin/env bash
# Runs inside the Debian container. See build.sh.
. /src/packaging/common.sh

export DEBIAN_FRONTEND=noninteractive
apt-get update -qq
apt-get install -y -qq --no-install-recommends build-essential debhelper dpkg-dev fakeroot \
    devscripts equivs >/dev/null

work=/tmp/build
rm -rf "$work"
mkdir -p "$work"
tar -C "$work" -xzf "/out/src/qraw-$APP_VERSION.tar.gz"
cd "$work/qraw-$APP_VERSION"
cp -r /src/packaging/deb/debian debian
sed -i "s/@VERSION@/$APP_VERSION/" debian/changelog

# Installs the Build-Depends from debian/control.
mk-build-deps --install --remove --root-cmd '' \
    --tool 'apt-get -y -qq --no-install-recommends' debian/control >/dev/null
# Each compiler process takes about 0.5 GB: parallelism is capped (JOBS, default 6).
DEB_BUILD_OPTIONS="${DEB_BUILD_OPTIONS:+$DEB_BUILD_OPTIONS }parallel=$JOBS" dpkg-buildpackage -us -uc -b

rm -f /out/qraw_*.deb /out/qraw-dbgsym_*
cp "$work"/qraw_*.deb /out/
hand_back_out
