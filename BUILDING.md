# Building Qraw

This file is the build guide: dependencies per platform, build and test, options, packages, and the developer tools. For the layout of the code and the rules for changes see [CONTRIBUTING.md](CONTRIBUTING.md).

## Dependencies

You need CMake ≥ 3.25, Ninja, a C++20 compiler (GCC ≥ 11, Clang ≥ 14, MSVC 2022), Qt ≥ 6.5 (Core, Gui, Widgets, Svg, PrintSupport, Test, LinguistTools) and zlib.

| Platform | Command |
|---|---|
| Fedora 40+ | `sudo dnf install cmake ninja-build gcc-c++ qt6-qtbase-devel qt6-qtsvg-devel qt6-qttools-devel zlib-ng-compat-devel` |
| Ubuntu 25.04+ | `sudo apt install cmake ninja-build g++ qt6-base-dev qt6-svg-dev qt6-tools-dev qt6-l10n-tools zlib1g-dev libgl-dev` |
| Ubuntu 24.04 | Its packaged Qt is 6.4, which is too old. Install the build tools with `sudo apt install cmake ninja-build g++ zlib1g-dev libgl-dev libxkbcommon-dev`, then get Qt 6.8 with `pip install aqtinstall && aqt install-qt linux desktop 6.8.3 linux_gcc_64 -O ~/Qt` and pass `-DCMAKE_PREFIX_PATH=~/Qt/6.8.3/gcc_64` to the configure step. |
| macOS | `brew install cmake ninja qt`, then pass `-DCMAKE_PREFIX_PATH=$(brew --prefix qt)` to the configure step. zlib ships with the SDK. |
| Windows | Visual Studio 2022 (C++ workload), Qt 6.8 for MSVC 2022 64-bit from the Qt online installer, and zlib via `vcpkg install zlib:x64-windows`. Run the steps below from an "x64 Native Tools Command Prompt" and pass `-DCMAKE_PREFIX_PATH=C:\Qt\6.8.3\msvc2022_64 -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake` to the configure step. |

## Build and test

```sh
git clone https://github.com/newmca/qraw.git && cd qraw
cmake --preset dev          # Debug build in build/dev (warnings are errors)
cmake --build --preset dev
ctest --preset dev          # unit tests + the corpus round-trip test
```

Use `release` instead of `dev` for an optimized build in `build/release`. Use `asan` for AddressSanitizer and UBSan (GCC/Clang).

Measured on a 24-core, 31 GB Fedora 44 machine (GCC 16, Qt 6.11, `CMAKE_BUILD_PARALLEL_LEVEL=8`, no ccache), starting from a fresh clone and using the `release` preset: configure 1 s, build about 105 s, `ctest --preset release -j4` 20 s (the `dev` preset's `ctest -j4` takes about 50 s, and a serial run several minutes). Fetching and installing the dependencies is the part that takes time; expect longer on a laptop with 4 cores.

If the build is killed ("Killed signal terminated program cc1plus", or the machine freezes), the parallel compile ran out of memory: Ninja starts one job per core and a Qt-heavy translation unit needs a lot of memory. Limit the jobs with `cmake --build --preset dev -j4` (or `CMAKE_BUILD_PARALLEL_LEVEL=4`) and run it again; finished objects are kept. `-j8` was fine on 31 GB, higher was killed. The `asan` preset needs more memory per job; use `-j6` there.

## Try it

Run these from the repository root. The editor needs a display; on a machine without one, put `QT_QPA_PLATFORM=offscreen` in front of the `qraw` commands (the window is created but never shown).

```sh
build/dev/src/cli/qraw-cli --info tests/compat/fixtures/inline_two_pages.drawio
build/dev/src/cli/qraw-cli --validate tests/compat/fixtures/inline_two_pages.drawio
build/dev/src/cli/qraw-cli --convert tests/compat/fixtures/compressed_unicode.drawio --output plain.drawio --uncompressed
build/dev/src/app/qraw                      # the editor (welcome dialog)
build/dev/src/app/qraw tests/compat/fixtures/inline_two_pages.drawio   # open a file
build/dev/src/app/qraw --screenshot shot.png tests/compat/fixtures/inline_two_pages.drawio --select start   # offscreen picture
```

Replace the fixture path with your own `.drawio` file to work on it; `--select` takes a cell id.

Using the editor: pick a shape in the left palette (click to insert, drag to place, or search), click a shape to select it (drag to move, drag a handle to resize), hover a shape and drag one of its blue arrows to another shape to connect them, double click to edit a label, change fill, line and text in the Format panel on the right, and use Ctrl+Z / Ctrl+Y to undo and redo. File > Export As writes PNG, JPEG, SVG or PDF; "Include a copy of my diagram" keeps the file editable. Pages are the tabs below the canvas; Layers, Outline and Find are in the View and Edit menus.

Install with `cmake --install build/release --prefix /usr/local` (see [Install layout and icons](#install-layout-and-icons)). On Linux this adds the menu entry, the icon, AppStream metadata and the `.drawio` file association (MIME type `application/vnd.jgraph.mxfile`); run `update-mime-database <prefix>/share/mime` if your system does not do so by itself.

`qraw` accepts the same flags as `qraw-cli`. `--validate` exits with 0 when the file loads, 1 when it has errors, and 2 on bad usage. `--export` renders a diagram headlessly (see [FEATURES.md](FEATURES.md)).

## Presets

| Preset | Build type | Directory | Notes |
|---|---|---|---|
| `dev` | Debug | `build/dev` | `QRAW_WARNINGS_AS_ERRORS=ON` |
| `release` | Release | `build/release` | used by CI |
| `asan` | Debug | `build/asan` | `-fsanitize=address,undefined` (GCC/Clang only), `QRAW_TEST_TIMEOUT_SCALE=3`; see [Sanitizers](#sanitizers) |

Each preset has a matching configure, build and test preset:

```sh
cmake --preset <name> && cmake --build --preset <name> && ctest --preset <name>
```

The test presets set `QT_QPA_PLATFORM=offscreen`, so the tests run without a display. `ctest --preset dev` runs the tests one at a time (about 160 s on a 24-core machine); add `-j4` to run them in parallel (about 45 s).

Parallel builds can run out of memory: Ninja starts one compile job per core. If the build is killed, rerun it with fewer jobs (`cmake --build --preset dev -j4`, or `CMAKE_BUILD_PARALLEL_LEVEL=4`). `-j8` worked on 31 GB; the `asan` preset needs more memory per job (see [Sanitizers](#sanitizers)).

## Options

| Option | Default | Meaning |
|---|---|---|
| `QRAW_WARNINGS_AS_ERRORS` | `OFF` (`ON` in `dev`) | `-Werror` / `/WX` on project targets |
| `BUILD_TESTING` | `ON` | build `tests/` (standard CTest option) |
| `QRAW_BUILD_TOOLS` | `OFF` | build the developer tools that are not installed: `tools/icons/qraw-render-icons` with the `regenerate_icons` target, and `tools/fidelity/qraw-fidelity-diff` with its self-test `tst_tools_fidelity_score` (registered with CTest only when this is `ON`) |
| `QRAW_LEAN_TEST_BINARIES` | `OFF` (`ON` in the RPM and DEB builds) | link the test programs without LTO and without debug information (Linux, GCC/Clang), so that building and running the whole suite in a package build does not exhaust a hosted runner's disk; installed programs are unchanged |
| `QRAW_TEST_TIMEOUT_SCALE` | `1` (`3` in `asan`) | multiplier for every explicit test `TIMEOUT`, the fuzzer `--hang-ms` and the time budgets of `tst_ui_perf_bulk` |

## Layout and rules

- Every module is `src/<module>/` with public headers in `src/<module>/include/qraw/<module>/`, in the namespace `qraw::<module>`.
- `qraw_core` and `qraw_commands` must not link Qt. `qraw_forbid_qt()` in `cmake/QrawTargets.cmake` fails the configure step if either does.
- `qraw_diag` (`src/diag`) is the one module besides `format` that the programs and the UI share without rendering: logging categories, the log file, crash reports and session markers (QtCore only).
- New tests use `qraw_add_test(<name> SOURCES ... LIBRARIES ...)`, which builds a Qt Test executable and registers it with CTest.

## Install layout and icons

`cmake --install build/release --prefix <dir>` installs `bin/qraw` and `bin/qraw-cli`. On Linux (UNIX but not macOS) it also installs, all named after the application id `io.github.newmca.qraw` (`QRAW_APP_ID` in the root `CMakeLists.txt`):

| File | Destination |
|---|---|
| `resources/linux/<id>.desktop` | `share/applications/` |
| `resources/linux/<id>.metainfo.xml` | `share/metainfo/` (configure fails when it has no `<release>` for the project version) |
| `resources/linux/<id>.xml` (MIME type `application/vnd.jgraph.mxfile`) | `share/mime/packages/` |
| `resources/icons/hicolor/` (PNG 16 to 512, scalable SVG) | `share/icons/hicolor/` |

Packagers run `update-mime-database` and `gtk-update-icon-cache` (distribution triggers do this). Check the files with `desktop-file-validate`, `appstreamcli validate --no-net --pedantic` and an XML parser. The id is all lowercase (D-046). `tst_ui_app_identity` checks that the variable, the file names, the metainfo `<id>` and `<launchable>`, the desktop file's `Icon=` and `QGuiApplication::desktopFileName()` agree. If you installed files named `io.github.newmca.Qraw.*` by hand from an earlier checkout, remove them.

The Windows build embeds `resources/icons/qraw.ico` through `src/app/qraw.rc.in`; the macOS bundle carries `qraw.icns` and uses the application id as its bundle identifier.

The PNG, ICO and ICNS files are committed. After editing `resources/icons/qraw.svg` or `qraw-small.svg` regenerate them:

```sh
cmake --preset dev -DQRAW_BUILD_TOOLS=ON && cmake --build --preset dev --target regenerate_icons
```

## Packages

CI builds the Windows and macOS packages from the same `release` build that ran the tests and uploads them as workflow artifacts (`qraw-windows-<commit>`, `qraw-macos-<commit>`, kept 14 days, nothing is published). The packaging logic is `cmake/Packaging.cmake`; the version is the project version.

| Platform | Result | How |
|---|---|---|
| Windows | `Qraw-<version>-windows-x64.exe` (NSIS installer) and `.zip` (portable), each with a `.sha256` | `windeployqt` through Qt's deployment API (`qt_generate_deploy_app_script`, run by `cmake --install`) into `bin/` and `plugins/`, `qt.conf` in `bin/`, vcpkg's `zlib1.dll` and the MSVC runtime beside `qraw.exe`; CPack NSIS and ZIP |
| macOS | `Qraw-<version>-darwin-<arch>.dmg` with `.sha256` | `macdeployqt` the same way into `Qraw.app` (Qt frameworks and plugins inside), ad-hoc `codesign --force --deep --sign -`; CPack DragNDrop |
| Linux | `.rpm`, `.deb`, `.flatpak` | built from the source archive in containers by `packaging/` (see [Linux packages](#linux-packages)), not by CPack; `cpack -G TGZ` only archives the install tree (used to check it) |

`qraw-cli` is in the package on both platforms: Windows `bin\qraw-cli.exe` next to `qraw.exe`; macOS `Qraw.app/Contents/MacOS/qraw-cli` (it links the bundle's frameworks through `@executable_path/../Frameworks`). `Qraw.app/Contents/MacOS/Qraw` accepts the same flags as well, because it is one binary. The CLI renders on Qt's offscreen platform, so the packages carry that platform plugin too.

Build one yourself from a `release` build (any platform that has the tools; Qt's `bin` directory with `windeployqt` or `macdeployqt` must belong to the Qt you configured with):

```sh
cmake --preset release            # Windows: add -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%\scripts\buildsystems\vcpkg.cmake
cmake --build --preset release
cpack --config build/release/CPackConfig.cmake -B build/packages
```

Windows needs [NSIS](https://nsis.sourceforge.io/) on the PATH for the installer (preinstalled on GitHub's `windows-2022` image); the ZIP needs nothing. macOS needs the Xcode command line tools (`codesign`, `hdiutil`). `cmake --install` fails, and so does the package, when the deployment left out a plugin Qraw loads (`platforms`, `styles`, `imageformats` with SVG and JPEG, `iconengines`, `printsupport`, zlib on Windows): the list is in `cmake/Packaging.cmake`. `tools/ci/smoke_package.sh` runs the CLI exports and the offscreen `--screenshot` from an unpacked package; CI runs it on the ZIP, on the installed program (silent install, registry check, uninstall) and on the mounted `.dmg`.

**Installer (D-048).** Per machine: CPack's NSIS template always asks for administrator rights and installs to `Program Files\Qraw` with the uninstall entry in the Apps list, a Start Menu shortcut, the licence page and the icon from `resources/icons/qraw.ico`. The portable ZIP is the route without administrator rights. It registers the ProgID `io.github.newmca.qraw.drawio` (icon, `open` command `"...\qraw.exe" "%1"`) and offers it under "Open with" for `.drawio`; it becomes the default handler only when no program is registered for the extension. The uninstaller removes what the installer wrote.

**macOS bundle.** `cmake/MacOSXBundleInfo.plist.in` (bundle identifier = the application id, `NSHighResolutionCapable`, the `.drawio` document type, an imported type declaration conforming to `public.xml`, `LSMinimumSystemVersion` = Qt's deployment target). Finder passes documents as `QFileOpenEvent`; `ui::FileOpenHandler` opens them in the window, also when the event arrives before the window exists (`tst_ui_file_open`).

**Unsigned.** There is no Windows code-signing certificate and no Apple developer account. Windows SmartScreen shows "Windows protected your PC": choose More info, then Run anyway. The `.dmg` carries an ad-hoc signature (Apple silicon refuses unsigned code) and is not notarised: Gatekeeper says the app cannot be checked for malicious software; right-click `Qraw.app`, choose Open, and confirm once (or System Settings, Privacy & Security, Open Anyway). Compare the `.sha256` file with `sha256sum` / `shasum -a 256`. There is no AppImage, by choice (see [Linux packages](#linux-packages)).

Checked on Linux only: `cpack -G TGZ` (bin/qraw, bin/qraw-cli, desktop files), the NSIS registry commands expanded by CMake, the smoke script against a release build, `tst_ui_file_open`. Everything platform-specific (the deployment tools, the NSIS installer, the `.dmg`, the plist) is checked by CI.

## Linux packages

RPM, DEB and a Flatpak bundle, built by scripts in `packaging/` (modelled on the author's other projects) into `dist/`, which git ignores:

```sh
packaging/build-all.sh [rpm] [deb] [flatpak]      # default: all three; each has packaging/<format>/build.sh
packaging/test-install.sh [rpm] [deb] [flatpak]   # install and smoke-test from dist/ in clean environments
JOBS=4 packaging/build-all.sh rpm                  # build parallelism (default and cap: 6)
```

Needs `docker` or `podman` (set `CONTAINER_ENGINE` to choose) for RPM and DEB, and `flatpak` with `org.flatpak.Builder` for the bundle; nothing is installed on the host with root (the Flatpak steps use `--user`). Every container build mounts the repository read-only at `/src` and writes to `dist/` (files are handed back to your user with docker).

| Format | Result in `dist/` | How |
|---|---|---|
| RPM | `qraw-<version>-1.fc44.x86_64.rpm`, `qraw-<version>-1.fc44.src.rpm` | `packaging/rpm/qraw.spec` in `fedora:44` (`rpmbuild -ba`, build requirements from `dnf builddep`). `%check` validates the desktop file, the AppStream metainfo (`--pedantic`) and the MIME XML, then builds with tests and runs the CTest suite on the offscreen platform (`-j4`, `QRAW_BENCH_TIME_BUDGETS=off` as in CI: wall-clock budgets are noise on a shared builder, the work-counter ceilings are still checked). `rpmbuild --without check` skips it. |
| DEB | `qraw_<version>_amd64.deb` | `packaging/deb/debian/` (debhelper 13, `dh --buildsystem=cmake+ninja`, DEP-5 `copyright` with the third-party licences of `THIRD_PARTY.md`) in `debian:trixie`; `DEB_BUILD_OPTIONS=nocheck` skips the tests. The RPM is built with Fedora's default LTO (the Qt big resources are compiled without it, `qraw_add_big_resources`). Runtime dependencies come from `${shlibs:Depends}` (Qt 6.8 on trixie). |
| Flatpak | `Qraw-<version>.flatpak` | the manifest `packaging/flatpak/io.github.newmca.qraw.yml` through `org.flatpak.Builder --user`, then `flatpak build-bundle` with the Flathub runtime repository recorded, so the install pulls the KDE 6.11 runtime. Details: `packaging/flatpak/README.md`. |

The RPM and DEB are built from `dist/src/qraw-<version>.tar.gz`, which `packaging/make-source.sh` makes with `git archive` of `HEAD` (`QRAW_SOURCE_REF` selects another commit), so **commit before building** to package a change to the application; the spec and `debian/` come from the working tree. The Flatpak builds the working tree.

`packaging/test-install.sh` installs the RPM in `fedora:44` and the DEB in `debian:trixie`, `ubuntu:25.10`, `ubuntu:26.04` and `ubuntu:26.10` (`DEB_TEST_IMAGES`), each time checking the installed files (desktop file, metainfo, MIME registration, icons, no unresolved libraries) and running `tools/ci/smoke_package.sh` (CLI exports, `qraw --screenshot`). It also checks that the DEB refuses to install on `ubuntu:24.04` (Qt 6.4). The Flatpak is installed with `flatpak install --user`, run through wrappers with `--filesystem` for a work directory under `build/`, and uninstalled again (runtimes stay).

**No AppImage, by choice.** The three formats above cover Linux; an AppImage needs a bundled Qt (the distributions' Qt is too old on the usual old-glibc base) and a further tool chain to keep current.

## Flatpak

`packaging/flatpak/io.github.newmca.qraw.yml` builds the working tree against the KDE 6.11 runtime and SDK (`flatpak install --user flathub org.kde.Platform//6.11 org.kde.Sdk//6.11`; `org.flatpak.Builder` is the builder). It takes about 75 s with 8 jobs:

```sh
flatpak run org.flatpak.Builder --user --force-clean --disable-rofiles-fuse --jobs=8 \
    --repo=build/flatpak/repo --state-dir=build/flatpak/state build/flatpak/app \
    packaging/flatpak/io.github.newmca.qraw.yml
```

Use directories under your home: the Flatpak builder runs in its own sandbox with a private `/tmp` and a state directory there fails. Lint with `flatpak run --command=flatpak-builder-lint org.flatpak.Builder manifest <manifest>` and `... repo <repo>`. Smoke tests without installing: `flatpak run org.flatpak.Builder --run build/flatpak/app <manifest> qraw-cli --version` (add `--filesystem=<dir>` before the build directory to give the sandbox a folder for input and output files). Details, permissions and the Flathub form: `packaging/flatpak/README.md`.

## Fidelity tools

`tools/fidelity/` measures the rendering against draw.io Desktop itself (results: [docs/FIDELITY.md](docs/FIDELITY.md)). It downloads the official AppImage into a directory you choose (not the repository), exports a sample of the corpus with both programs and scores the pairs:

```sh
tools/fidelity/fetch_drawio.sh ~/drawio-reference            # once; verifies the published checksums
tools/fidelity/run_fidelity.sh ~/drawio-reference build/fidelity   # about 3 min the first time, 10 s afterwards
```

The run builds the `release` preset with `-DQRAW_BUILD_TOOLS=ON`; the scoring tool's self-test is `ctest --preset dev -R tst_tools_fidelity_score` after `cmake --preset dev -DQRAW_BUILD_TOOLS=ON`. Details: [tools/fidelity/README.md](tools/fidelity/README.md). Reference renders and the draw.io binary are never committed.

## Corpus test

`tst_compat_corpus` finds every `*.drawio` and `*.xml` under `tests/corpus/` and `tests/compat/fixtures/` when it runs, so new corpus files need no reconfigure. Each file must load, save semantically equal to the original, reload semantically equal, and save a second time byte-identically. The test prints a PASS/FAIL table at the end. To run only this test:

```sh
build/dev/tests/compat/tst_compat_corpus            # all files
build/dev/tests/compat/tst_compat_corpus roundTrip:tests/compat/fixtures/inline_two_pages.drawio
```

## Scaled displays

Qraw is used on screens scaled to 125 % and 150 % (Plasma, fractional Wayland scaling), and on X11 with "Force font DPI". On the offscreen platform that the tests run on, two environment variables reproduce both:

- `QT_SCALE_FACTOR=1.25` (or 1.5, 2) scales every window: the device pixel ratio is the factor.
- `QT_FONT_DPI=120` sets the logical DPI of the screen. Qt derives a device pixel ratio of DPI / 96 from it (1.25 for 120, 1.5 for 144), so a run with it exercises the fractional-ratio code and not only bigger fonts: the screenshot harness writes 1125 x 750 pixels for `--size 900x600`.

Run the whole suite, or the UI part, under either:

```sh
QT_FONT_DPI=120 ctest --preset dev -j4 -R tst_ui_
QT_SCALE_FACTOR=1.5 ctest --preset dev -j4
QT_SCALE_FACTOR=2 ctest --preset dev -j4 -R tst_ui_
```

(the `qraw-bench` tool sets the factor to 1 itself: its counter ceilings are for a ratio of 1.) The tests that depend on the ratio are also registered twice in every preset, as `<test>_dpr125` and `<test>_dpr2` with the label `hidpi` (`qraw_add_hidpi_variants` in `cmake/QrawTargets.cmake`, called from `tests/ui/CMakeLists.txt`): `ctest --preset dev -L hidpi` runs them in 4 seconds, and CI runs them with everything else. They set `QT_FONT_DPI=96` so that a value in your environment does not multiply with the factor. For a picture of the window at a ratio: `QT_SCALE_FACTOR=2 QT_QPA_PLATFORM=offscreen build/dev/src/app/qraw --screenshot out.png FILE --size 900x600` writes 1800 x 1200 pixels (`--size` is in logical pixels).

When a test compares pixels, compare in device pixels (a grabbed widget is `size x devicePixelRatioF()` large; `devicePixel()` in `tests/ui/support/canvas_input.h` maps a scene point to the grabbed image) and see D-045 for the one place a rounding tolerance is allowed and why.

## Logging, crash reports and recovery

Code: `src/diag` (`qraw_diag`, QtCore only, D-049); the recovery dialog is `ui::RecoveryDialog`.

**Logging.** Every Qt-using module has `QLoggingCategory`s named `qraw.<module>[.<sub>]`, declared in its `logging.h`: `qraw.format`, `qraw.render`, `qraw.render.tiles`, `qraw.ui`, `qraw.ui.canvas`, `qraw.ui.session`, `qraw.cli`, and for the programs themselves `qraw.app`, `qraw.app.log`, `qraw.app.crash`, `qraw.app.session`. `qraw_core` and `qraw_commands` have no Qt and return diagnostics instead. New messages use `qCDebug`, `qCInfo`, `qCWarning` of the category of their module, never `qDebug()`. Information and above is on by default (stderr, and the log file in the GUI); debug output is off. Switch it on with Qt's logging rules:

```sh
QT_LOGGING_RULES="qraw.*.debug=true" build/dev/src/app/qraw           # everything
QT_LOGGING_RULES="qraw.ui.session.debug=true" build/dev/src/app/qraw  # autosave, recovery, file loading
```

Some distributions switch debug output off for all categories in their own `qtlogging.ini`; the rule above overrides that.

**Where things are written** (the data folder is `<GenericDataLocation>/Qraw/Qraw`, which is `QStandardPaths::AppLocalDataLocation` of the GUI; a Flatpak redirects it into `~/.var/app/io.github.newmca.qraw/`):

| What | Linux | macOS | Windows |
|---|---|---|---|
| Log file `logs/qraw.log` (the run before: `qraw.log.1`) | `~/.local/share/Qraw/Qraw/` | `~/Library/Application Support/Qraw/Qraw/` | `%LOCALAPPDATA%\Qraw\Qraw\` |
| Crash reports `crashes/crash-<UTC time>-<pid>.txt` (Windows also `.dmp`) | same folder | same | same |
| Session markers `sessions/<pid>.lock`, autosave copies `recovery/` | same folder | same | same |

The log file is written by the GUI only (`qraw-cli` keeps stdout and stderr as documented). Lines look like `2026-11-26T12:34:56.789Z W qraw.ui.session: autosave failed: ...` (UTC, severity D I W C F, category, message); it is flushed per line, written from any thread, kept to 2 MiB per run (at the cap the file is emptied and says so) and the previous run's file stays next to it as `qraw.log.1`. The messages still reach stderr. **Help > Open Log Folder** opens the folder in the file manager.

**Crash reports.** A plain signal handler, no KCrash and no library (D-049). On POSIX it catches SIGSEGV, SIGBUS, SIGFPE, SIGILL and SIGABRT on an alternate stack and uses only async-signal-safe calls plus a primed `backtrace()`; `std::set_terminate` reports uncaught exceptions; on Windows an unhandled-exception filter writes the exception and a minidump. The report holds the reason (signal, fault address or exception), version, git commit (taken when CMake configures), Qt version, operating system, platform plugin, time, process id and the backtrace, and no document content. Then the default action is restored and the signal raised again, so systemd-coredump, DrKonqi, ABRT or Windows Error Reporting still see the crash. Resolve a backtrace line `qraw(+0x2a3f1)` with `addr2line -f -C -e <the same build of qraw> 0x2a3f1`. `qraw-cli` writes reports too (`Process: cli`); they never trigger the dialog.

**Provoking a crash** (for tests, and to see what a report looks like): `QRAW_DEBUG_CRASH=segv`, `abort` or `exception` in the environment of `qraw` or `qraw-cli` crashes it a moment after start-up. There is no menu entry or button. Example on a throw-away profile:

```sh
export XDG_DATA_HOME=$(mktemp -d) XDG_CONFIG_HOME=$(mktemp -d)
QRAW_DEBUG_CRASH=segv QT_QPA_PLATFORM=offscreen build/release/src/app/qraw    # dies from SIGSEGV
cat "$XDG_DATA_HOME"/Qraw/Qraw/crashes/*.txt
build/release/src/app/qraw                                                      # offers recovery
```

**Recovery on the next start.** Each GUI process holds a `QLockFile` `sessions/<pid>.lock` from start to normal exit. A marker whose process is gone means the session ended unexpectedly (crash, kill, power loss); a second instance that runs at the same time holds a live lock and is ignored. A GUI crash report newer than the last one shown counts too. In either case, or when autosaved copies are waiting, the start shows **Qraw Closed Unexpectedly**: the autosaved documents (ticked), **Recover** (opens each as an unsaved, modified document with its original path; the first takes the empty window, the others open in windows of their own), **Discard** (deletes the ticked copies), **Later** (nothing is deleted, the question comes back) and **Show Crash Reports**. Autosave copies are never deleted unless the user chooses Discard. The autosave interval is set in Extras > Preferences. `qraw --screenshot out.png --dialog recovery` renders the dialog with sample entries ([screenshot](docs/screenshots/recovery-dialog.png)).

Tests: `tst_diag_*` (text building, log file, session markers, crash report header), `tst_ui_recovery_dialog` (dialog, start-up decision, several windows, Open Log Folder) and `tst_app_crash_process`, which starts the real `qraw` and `qraw-cli` with each `QRAW_DEBUG_CRASH` value on a redirected data folder (`XDG_DATA_HOME`, `HOME`) and checks the report, the signal exit and the stale marker. The process test skips itself under AddressSanitizer (ASan installs its own handlers for these signals) and on Windows (the data folder cannot be redirected there, and a test must not write crash reports into the profile of whoever runs it); it also skips where the redirect has no effect.

## Fuzzing

`tests/fuzz/` holds `qraw-fuzz`, a deterministic mutation fuzzer (D-028: GCC only, so no libFuzzer). It mutates seeds taken from `tests/corpus/`, `tests/compat/fixtures/` and `tests/render/data/golden/` (bit and byte flips, chunk insert / delete / duplicate / swap, truncation, splicing two seeds, interesting numbers, per-target dictionaries including deep nesting runs) and checks the invariants below. The same `--seed` gives the same inputs on every platform.

| Target | Entry points | Invariants |
|---|---|---|
| `codec` | `format::readDocument`, `parseLibrary` | no crash or hang; when the read succeeds: write, read, `semanticEqual` with the first document, and a second write byte-identical to the first |
| `compression` | `decompressDiagram`, `inflateBytes`, `decodeUriComponent` | no crash or hang; `decompressDiagram(compressDiagram(x)) == x` for any decoded `x`; `decodeUriComponent(encodeUriComponent(x)) == x`; inflate(deflate(x)) == x. Inputs: the base64 of stored pages, and mutated inflated pages re-compressed so the mutation reaches the inner decoder |
| `style` | `core::Style` | `Style(s).toString() == s`; `set`, `remove`, `addName`, `removeName` never crash; `set(k, v)` then `value(k) == v`, also after re-parsing |
| `stencil` | `Stencil::fromXml` + paint at two sizes and `direction=south`, `decodePackedStencilSet`, `StencilRegistry::findInline` | no crash or hang |
| `html` | `sanitizeHtml`, `makeRichText` | no crash or hang; `sanitizeHtml` is idempotent; its output parses as XML |

Run it:

```sh
ctest --preset release -L fuzz                  # fuzz_regressions + fuzz_short
build/release/tests/fuzz/qraw-fuzz --target all --seconds 120 --seed $RANDOM --crash-dir build/fuzz-crashes   # long, opt-in
build/asan/tests/fuzz/qraw-fuzz --target all --iterations 3000                                           # with ASan/UBSan
```

- `fuzz_regressions` replays every file in `tests/fuzz/regressions/<target>/` once. It fails on any invariant violation, crash or hang.
- `fuzz_short` runs all five targets for a fixed seed (1) and a fixed number of iterations each (10000, `QRAW_FUZZ_SHORT_ITERATIONS`). Measured on the 24-core development machine: about 5 s in release and 25 s in dev (budget: 30 s and 120 s); `fuzz_regressions` takes well under a second.
- The long mode is the executable itself: `--target codec|compression|style|stencil|html|all`, `--seed N`, `--iterations N` or `--seconds S` (per target), `--max-len BYTES` (default 65536), `--hang-ms MS` (default 5000; the CTest entries use 10000 in release and 30000 in Debug builds). A failing run exits 1; a hang exits 3; a crash ends with the signal.

Reproducing a finding. Every report names the target, seed and iteration:

```
FUZZ-FAIL target=html seed=202 iteration=3112516: sanitizeHtml is not idempotent
FUZZ-CRASH target=codec seed=7 iteration=812 signal=11
FUZZ-HANG target=stencil seed=7 iteration=90
```

```sh
qraw-fuzz --target html --seed 202 --dump html:3112516 --out input.bin   # the exact input of that iteration
qraw-fuzz --target html --check input.bin                               # run it once (exit 0 or 1; crash or hang as above)
qraw-fuzz --target html --minimize input.bin --out small.bin            # shrink it, keeping the failure
```

With `--crash-dir DIR` the input of a failure, crash or hang is saved there. Nothing is written per iteration: a handler for SIGSEGV, SIGABRT, SIGFPE and SIGILL (and a watchdog thread for hangs) reports the iteration and saves the input only when the process is about to die. `--minimize` runs invariant failures in process and crashes in a child process per candidate, so it is slow for large crashing inputs.

Fixing a finding: fix the bug in `src/`, add the minimized input as `tests/fuzz/regressions/<target>/<name>.<ext>` (small; the file is the input as the target receives it: for `stencil` it starts with the line `xml`, `packed` or `inline`), add one line to the README.md of that directory naming the bug and the fix, and a focused unit test in the owning module where that is natural. An input that cannot be fixed now goes to `tests/fuzz/known/` (not replayed) and into KNOWN_ISSUES.md.

## Sanitizers

The `asan` preset builds Debug with AddressSanitizer, LeakSanitizer and UBSan (GCC or Clang) and runs the complete suite, `fuzz_regressions` and `fuzz_short` included:

```sh
cmake --preset asan
cmake --build --preset asan -j6          # never above -j8: ASan objects need more memory
ctest --preset asan -j4 --output-on-failure
```

Wall time on the development machine (8 cores): about 1.5 min to build, about 2.5 min for the suite (the slowest are `tst_ui_perf_bulk`, `tst_commands_ops_pages_layers_corpus`, `tst_ui_edit_corpus` and `fuzz_short`, 90 to 130 s each). Nothing is excluded from the run.

- Timeouts: the preset sets `QRAW_TEST_TIMEOUT_SCALE=3`. It multiplies the CTest `TIMEOUT` of the module tests and of the fuzz tests, the fuzzer's `--hang-ms`, and the time budgets in `tst_ui_perf_bulk`. Change it with `-DQRAW_TEST_TIMEOUT_SCALE=<n>` if a slower machine needs more.
- The test preset sets `UBSAN_OPTIONS=print_stacktrace=1:halt_on_error=1` (a UBSan report aborts the test) and `LSAN_OPTIONS=suppressions=tools/sanitizers/lsan.supp`.
- Suppression policy: `tools/sanitizers/lsan.supp` may list only frames of third-party libraries (Qt, fontconfig, glib, the offscreen platform plugin), each entry with a comment naming the library and why the leak is not ours. A report whose allocation stack passes through `qraw` code is a finding: fix the ownership, never suppress it. The file is empty today: the suite is clean without any suppression.
- Reading a report: the first stack is where the error happened (ASan), the `allocated by` stack is where the memory came from; for LeakSanitizer read the `Direct leak` stacks from the top and stop at the first `src/` frame. A UBSan report is a `runtime error:` line followed by the stack. A clean log has none of `ERROR: AddressSanitizer`, `runtime error:` or `ERROR: LeakSanitizer`: `ctest --preset asan -j4 --output-on-failure | grep -E "ERROR: (Address|Leak)Sanitizer|runtime error:"` prints nothing.
- Long fuzz run under the sanitizers: `build/asan/tests/fuzz/qraw-fuzz --target all --seconds 120 --seed 7 --crash-dir build/asan-fuzz-crashes` (see [Fuzzing](#fuzzing)).
- CI: there is no ASan job. A Debug build with sanitizers on a 4-core hosted runner would add a Qt install, a build of about ten minutes and a suite of several minutes to every push, and it cannot be checked without a run. Run the commands above before a release instead.

## Benchmarks

`tests/bench/qraw-bench` times the main operations on generated 1k, 10k and 50k cell documents; use the `release` preset, the dev build is Debug and the tool says so. `ctest --preset dev -L bench` runs the 1k smoke test. Options, what is timed and the recorded results are in [docs/BENCHMARKS.md](docs/BENCHMARKS.md).

```sh
QT_QPA_PLATFORM=offscreen build/release/tests/bench/qraw-bench --sizes 1000,10000,50000 --repeat 3 --markdown
```

### Canvas paint benchmark

`qraw-bench --paint FILE --page N` (or `--paint-synthetic`, a generated image-heavy document of about 37 MB) opens the page in a real `CanvasView` and reports frame times (avg, p50, p95, p99, max, share over 16.7 ms and 8.3 ms, worst second) and deterministic work counters (cells painted, images decoded, page view builds, ...) for open, first paint, full repaint, pan, zoom, hover, selection, drag, commit and edit. `ctest --preset dev -R bench_paint` runs it on the synthetic document with reduced frame counts and checks the counter ceilings and loose time budgets of `tests/bench/paint_budgets.h`.

```sh
QT_QPA_PLATFORM=offscreen build/release/tests/bench/qraw-bench --paint-synthetic --markdown
QT_QPA_PLATFORM=offscreen build/release/tests/bench/qraw-bench --paint my.drawio --page 2 --fit --timers --json run.json
```

Options: `--viewport WxH`, `--zoom Z` or `--fit`, `--at X,Y`, `--repeat N`, `--scenarios a,b`, `--frame-scale F`, `--no-warmup`, `--json OUT`, `--check-budgets`; `--write DIR` with `--paint-synthetic` also writes `DIR/bench-images.drawio`. Environment variables of the profiling hooks, which work in any build and in the app itself:

| Variable | Effect |
|---|---|
| `QRAW_PAINT_TIMERS=1` | enable the scope timers of the paint path (wall and thread CPU time per scope; `--timers` of the benchmark does the same and prints the attribution table) |
| `QRAW_IMAGE_CACHE_MB=N` | byte budget of the decoded-image cache in MiB (default 256; the cache never exceeds twice the budget, and keeps images used in the current frame within it) |
| `QRAW_RASTER_THREADS=N` | number of tile rasterisation threads (default `clamp(idealThreadCount / 2, 2, 8)`; the canvas rasterises its tiles on them, ARCHITECTURE.md "Tiles on worker threads") |
| `QRAW_TILE_CACHE_MB=N` | byte budget of the tile cache in MiB (default 192; the tiles of other zoom buckets are capped at a third of it) |
| `QRAW_TRACE=out.json` | also record every scope as a Chrome Trace Event and write the file at exit; open it in <https://ui.perfetto.dev> or `chrome://tracing` |

Full description, metrics and recorded results: [docs/BENCHMARKS.md](docs/BENCHMARKS.md#canvas-paint-benchmark).

## CI

`.github/workflows/ci.yml` builds and tests the `release` preset on three runners; the Linux job first runs `desktop-file-validate` and `appstreamcli validate --no-net --pedantic` on `resources/linux/` (Ubuntu 24.04's appstream is older than Fedora's 1.1). Qt comes from `jurplel/install-qt-action` (aqtinstall).

| Job | Runner | Qt | Notes |
|---|---|---|---|
| `linux` | `ubuntu-24.04` | 6.8.* | |
| `macos` | `macos-latest` (arm64) | 6.10.* | Qt 6.8's `WrapOpenGL` links the AGL framework, which the macOS 26 SDK on current images no longer ships (`ld: framework 'AGL' not found`). Qt 6.9 and later do not link AGL, so the job uses a newer Qt instead of pinning an old Xcode image or patching Qt's targets. |
| `windows` | `windows-2022` (MSVC) | 6.8.* | zlib comes from vcpkg. |

The `linux-packages` job builds the RPM, DEB and Flatpak with `packaging/build-all.sh`, runs `packaging/test-install.sh` on them and uploads them as workflow artifacts (`qraw-linux-<format>-<commit>`, 14 days; nothing is published).

All tests run on every test job; none is excluded. Two settings keep the hosted runners honest:

- **Fonts.** The tests use Qt's `offscreen` platform. On Windows it has no font database of its own and loads the font files found in `QT_QPA_FONTDIR` (default `<Qt>/lib/fonts`, which Qt does not ship), so without it every glyph is a box and every text measurement is wrong. The CTest environment (`QRAW_TEST_ENVIRONMENT` in `cmake/QrawTargets.cmake`) sets it to `%SystemRoot%\Fonts`; Linux uses fontconfig. A test that fails on one platform only with a text size or bounds is a font problem until shown otherwise: run it with `FONTCONFIG_FILE` pointing at a configuration without font directories to see what a missing database does.
- **Time budgets.** `ci.yml` sets `QRAW_BENCH_TIME_BUDGETS=off`: `bench_paint` checks the exact work-counter ceilings on the runners but not the wall-clock p95 budgets, which are noisy on shared machines (docs/BENCHMARKS.md, "The `bench_paint` gate").

GitHub Actions has only been run on the history of this repository up to the first public release; the Windows and macOS jobs and the packages they build have had far less verification than Linux (see [KNOWN_ISSUES.md](KNOWN_ISSUES.md)).
