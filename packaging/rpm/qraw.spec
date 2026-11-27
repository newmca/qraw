%{!?qraw_version:%global qraw_version 0.1.0}
%global app_id io.github.newmca.qraw

# Each compiler process of this C++ build takes about 0.5 GB: never more than 6 at a time.
%global _smp_ncpus_max 6

# Fedora's default link-time optimisation (-flto=auto) stays on: the vendored data is compiled in as Qt big
# resources, whose object file rcc patches in a second pass, so cmake/QrawTargets.cmake compiles that one
# object without LTO (with it the resources came out empty and 22 tests failed). The ~160 test programs are
# linked without LTO and without debug information (QRAW_LEAN_TEST_BINARIES): as LTO links they filled the
# disk of a hosted runner. The installed programs keep both.

# %%check builds and runs the whole test suite (about 160 test programs); rpmbuild --without check skips it.
%bcond_without check

Name:           qraw
Version:        %{qraw_version}
Release:        1%{?dist}
Summary:        Native Qt diagram editor compatible with draw.io files
# Qraw is Apache-2.0, like the vendored draw.io data compiled into the binary (default stylesheet, stencil
# libraries); the draw.io templates are also CC-BY-4.0. See NOTICE and THIRD_PARTY.md.
License:        Apache-2.0 AND CC-BY-4.0
URL:            https://github.com/newmca/qraw
Source0:        %{name}-%{version}.tar.gz

BuildRequires:  cmake >= 3.25
BuildRequires:  ninja-build
BuildRequires:  gcc-c++
BuildRequires:  qt6-qtbase-devel >= 6.5
BuildRequires:  qt6-qtsvg-devel
# Qt6LinguistTools: lrelease for the translations.
BuildRequires:  qt6-qttools-devel
BuildRequires:  zlib-devel
BuildRequires:  desktop-file-utils
BuildRequires:  appstream
BuildRequires:  libxml2
%if %{with check}
# The tests measure text and render on Qt's offscreen platform, which needs a font.
BuildRequires:  fontconfig
BuildRequires:  dejavu-sans-fonts
BuildRequires:  dejavu-serif-fonts
BuildRequires:  dejavu-sans-mono-fonts
%endif

# Image plugins (SVG, JPEG) and the icon engine come with these; the xcb and offscreen platform
# plugins with qt6-qtbase-gui. The Qt libraries themselves are found by rpm's dependency generator.
Requires:       qt6-qtsvg
Requires:       hicolor-icon-theme
Requires:       shared-mime-info
Recommends:     qt6-qtwayland

%description
Qraw is a native Qt 6 Widgets editor for draw.io diagrams. It opens and
saves .drawio files (compressed or not), draws draw.io's shape libraries,
exports PNG, JPEG, SVG and PDF, and has a command line tool, qraw-cli, to
validate and export diagrams without a display.

%prep
%autosetup -n %{name}-%{version}

%build
# RelWithDebInfo comes from Fedora's %%cmake flags; warnings are not errors, so that a newer compiler
# cannot fail a package build.
%cmake -G Ninja \
    -DCMAKE_BUILD_TYPE=RelWithDebInfo \
%if %{with check}
    -DBUILD_TESTING=ON \
    -DQRAW_LEAN_TEST_BINARIES=ON \
%else
    -DBUILD_TESTING=OFF \
%endif
    -DQRAW_WARNINGS_AS_ERRORS=OFF
%cmake_build

%install
%cmake_install

%check
desktop-file-validate %{buildroot}%{_datadir}/applications/%{app_id}.desktop
appstreamcli validate --no-net --pedantic %{buildroot}%{_metainfodir}/%{app_id}.metainfo.xml
xmllint --noout %{buildroot}%{_datadir}/mime/packages/%{app_id}.xml
%if %{with check}
# The whole suite, on the offscreen platform, four at a time. Two settings keep it honest in a build root
# (as in CI): bench_paint's wall-clock budgets are not checked (a shared builder is too noisy; its exact
# work-counter ceilings still are), and HOME points at a scratch directory.
export QT_QPA_PLATFORM=offscreen QRAW_BENCH_TIME_BUDGETS=off HOME=%{_builddir}/home
mkdir -p "$HOME"
%ctest -j4 --output-on-failure
%endif

%files
%license LICENSE NOTICE THIRD_PARTY.md
%doc README.md CHANGELOG.md
%{_bindir}/qraw
%{_bindir}/qraw-cli
%{_datadir}/applications/%{app_id}.desktop
%{_metainfodir}/%{app_id}.metainfo.xml
%{_datadir}/mime/packages/%{app_id}.xml
%{_datadir}/icons/hicolor/*/apps/%{app_id}.*

%changelog
* Thu Nov 26 2026 newmca <noreply@users.noreply.github.com> - 0.1.0-1
- First package
