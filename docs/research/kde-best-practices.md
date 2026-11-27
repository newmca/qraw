# KDE Applications best practices for Qraw (Qt 6 Widgets, no KDE Frameworks)

Date: 2026-11-26. Scope: what a native Qt 6 Widgets diagram editor (C++20, CMake, Qt >= 6.5) should do to be a good citizen on KDE Plasma, and which of those practices need KDE Frameworks (KF6) versus plain Qt. Not a plan; no code was changed.

Tags: `[MUST]` / `[SHOULD]` / `[NICE]` are priorities for Qraw as a non-KDE-Frameworks app that wants to feel native on Plasma and be distributable (Flathub, distro packages). `Plain Qt` = doable without KF6; `KF6` = needs a KDE Framework (or is much easier with one).

## Sources (fetched 2026-11-26 unless noted)

- KDE HIG: https://develop.kde.org/hig/ (sub-pages: layout_and_nav, text_and_labels, getting_input, simple_by_default, powerful_when_needed, accessibility, icons/colorful/application, kde_app_design)
- KDE developer docs: https://develop.kde.org/docs/ (KXmlGui, packaging/flatpak/publishing, getting-started/add-project/review)
- KDE Review checklist: https://develop.kde.org/docs/getting-started/add-project/review/
- Application lifecycle: https://community.kde.org/Policies/Application_Lifecycle
- Suggested review criteria (older, partly dated): https://techbase.kde.org/Policies/Suggested_Review_Criteria
- KDE Licensing Policy: https://community.kde.org/Policies/Licensing_Policy
- KDE HowToAppStream: https://community.kde.org/HowToAppStream
- AppStream metadata spec: https://www.freedesktop.org/software/appstream/docs/chap-Metadata.html
- Flathub requirements: https://docs.flathub.org/docs/for-app-authors/requirements ; metainfo guidelines: https://docs.flathub.org/docs/for-app-authors/metainfo-guidelines
- Desktop Entry spec: https://specifications.freedesktop.org/desktop-entry/latest/ (recognized-keys.html)
- Icon Theme spec: https://specifications.freedesktop.org/icon-theme/latest/
- Shared MIME-info spec: http://specifications.freedesktop.org/shared-mime-info/latest/ar01s02.html
- Qt docs: https://doc.qt.io/qt-6/qstylehints.html , /qicon.html , /highdpi.html , /accessible.html , /accessible-qwidget.html , /qloggingcategory.html , /qstandardpaths.html
- KF6 API: https://api.kde.org/kstandardaction.html , /kcolorschememanager.html , /kcrash.html , /ki18n-index.html , /kconfig-index.html , /kconfigwidgets-index.html
- KDE Goals (search results only, not fetched primary): https://kde.org/goals/ ; coverage e.g. https://www.phoronix.com/news/KDE-Goals-To-2028
- KDE Wayland porting notes (search snippet only): https://community.kde.org/Wayland/Porting_Notes

Verification caveat: several of the pages consulted (HIG "kde_app_design", KXmlGui tutorial, KConfigWidgets index) did not state what was needed. Items marked (unverified) rest on prior knowledge or search snippets and should be checked before being relied on.

---

## 1. HIG / UX

| Practice | Why | Tag | Needs |
|---|---|---|---|
| Menubar for a medium/large app (a diagram editor qualifies); hamburger menu only if <= ~15 items. Disable, don't hide, irrelevant items. Quit/Minimize do not go in a hamburger menu. (HIG layout_and_nav) | Predictable discoverability; HIG says a menubar "shows all of the app's actions" | [MUST] | Plain Qt (`KHamburgerMenu` is the KF6 helper that toggles menubar/hamburger) |
| Keep primary functions visible by default; shortcuts/context menus are accelerators, never the only route. (HIG powerful_when_needed, simple_by_default) | New users miss hidden actions | [MUST] | Plain Qt |
| Every action gets a shortcut, or shortcuts are user-assignable. No bare function-key shortcuts; no Meta key; use modifier+alphanumeric. (HIG layout_and_nav, powerful_when_needed) | Laptop keyboards; Meta is reserved for global shortcuts | [SHOULD] (configurable UI is [NICE]) | Plain Qt for assigning; a shortcut editor dialog is KXmlGui (`KShortcutsDialog`) or self-built |
| Undo for every non-trivial removal; undo prompts non-modal; prefer trash over delete. (HIG simple_by_default) | Safety without confirmation dialogs | [MUST] (Qraw already has a command stack) | Plain Qt (`QUndoStack`) |
| Show progress for tasks > 1 s; determinate when known. (HIG simple_by_default) | Responsiveness | [SHOULD] (export/import of large files) | Plain Qt |
| Minimise dialogs; never open a dialog from a dialog; one at a time; validate input inline and disable confirm until valid. (HIG getting_input) | Non-blocking flow | [SHOULD] | Plain Qt |
| Settings: apply immediately where possible, no restart required; avoid "Advanced" pages (use progressive disclosure); do not add toggles that just enable/disable a feature; do not override platform settings (fonts, colours) in-app. (HIG powerful_when_needed) | Avoid dialog bloat and conflicts with System Settings | [SHOULD] | Plain Qt |
| Switch/instant-apply vs CheckBox/OK-Apply distinction. (HIG getting_input; written for Kirigami, principle applies) | Consistent expectation of when a change takes effect | [NICE] | Plain Qt |
| Text: Title Case for buttons/menu items, sentence case for tooltips/labels/placeholders/status; imperative verbs ("Show Info"); "…" only when the action always opens a dialog; real typographic characters (… → × curly quotes); "Delete" only for removing from disk. (HIG text_and_labels) | Consistency across KDE apps | [SHOULD] | Plain Qt |
| Window titles: short, content-describing, no vendor name or version; avoid full paths. (HIG text_and_labels) | Task switcher legibility | [SHOULD] | Plain Qt (`setWindowFilePath`, `[*]` modified marker) |
| Icon-only buttons: accessible name + tooltip; tooltip must also show on keyboard focus. (HIG text_and_labels) | Screen readers, keyboard users | [MUST] | Plain Qt (`setAccessibleName`, `setToolTip`; focus tooltip needs a small event filter) |
| Allow strings to grow ~50% in translation; layouts must not truncate. (HIG text_and_labels) | German/Finnish etc. | [SHOULD] | Plain Qt |
| Remember window size/position (position only honoured on X11; Wayland compositors decide) and, for a project-type app, open files/view state. Don't stay in a tray after window close. (HIG simple_by_default) | Expected behaviour | [SHOULD] | Plain Qt (`QSettings` + `saveGeometry`/`saveState`); KXmlGui automates it (`KConfigGroup`) |
| Empty-state guidance (placeholder in empty view with a first action). (HIG simple_by_default) | Onboarding | [NICE] | Plain Qt |
| Use platform file dialogs, don't hand-roll one. (HIG getting_input; says Qt Quick FileDialog, same intent) | Portals/KIO dialogs get Plasma look and remote locations | [MUST] | Plain Qt (`QFileDialog` uses the xdg-desktop-portal or KDE platform theme when available) |
| KDE app naming: single catchy word, not a generic word, not "K"+word. (HIG text_and_labels) | Branding | [NICE] (a single, non-generic word that does not contain the Qt mark; see the trademark section below) | n/a |

HIG framing worth knowing: the HIG states KDE "recommends QtQuick with Kirigami for new applications" but that QtWidgets suits "complex traditional desktop applications" (https://develop.kde.org/hig/kde_app_design/). A dense-canvas diagram editor is squarely the Widgets case. Kirigami is not relevant.

## 2. Menus, shortcuts and standard actions

| Practice | Why | Tag | Needs |
|---|---|---|---|
| Use standard actions with standard text, icon and shortcut for New/Open/Save/Save As/Close/Quit/Undo/Redo/Cut/Copy/Paste/Select All/Find/Zoom/Print/Preferences/Full Screen. KDE does this via `KStandardAction` + `KStandardShortcut` ("should be used instead of hardcoding menubar and toolbar items", https://api.kde.org/kstandardaction.html). | Muscle memory across apps, free HIG compliance | [MUST] | Plain Qt via `QKeySequence::StandardKey` (covers most; `QKeySequence::keyBindings()` returns platform alternates) and `QIcon::fromTheme` freedesktop names. KF6 only for the exact KDE set. |
| Standard menu order: File, Edit, View, (app-specific), Settings, Help; Preferences under Settings > Configure Qraw… with `QKeySequence::Preferences`; Help menu with About, Report a Bug, Handbook. (KDE convention; HIG page did not state menu order, unverified) | Familiarity | [SHOULD] | Plain Qt (`KHelpMenu` is the KF6 version) |
| Mark Quit, About, Preferences with `QAction::setMenuRole` so macOS moves them to the app menu. | Needed for the macOS build | [SHOULD] | Plain Qt |
| Context-sensitive actions disabled, not removed. Group related actions with separators (undo/redo, cut/copy/paste). (HIG layout_and_nav) | Layout stability | [MUST] | Plain Qt |
| User-configurable toolbars and shortcuts (KXmlGui: `KActionCollection`, `.rc` UI files, `KShortcutsDialog`, `KEditToolBar`). (Capabilities from prior knowledge; the fetched KXmlGui page did not list them.) | KDE users expect "Configure Shortcuts…" and "Configure Toolbars…" | [NICE] | KF6 (KXmlGui), or ~200 lines of plain Qt for a shortcut table persisted in settings |
| Do not bind Meta-modified or function-key-only shortcuts; avoid 3/4-finger gestures. (HIG) | Conflicts with global shortcuts | [MUST] | Plain Qt |
| Recent Files submenu ("Open Recent" with Clear List), plus register opened files with the desktop's recent-documents list (HIG mentions `KRecentDocument`). | Expected for file-based apps | [SHOULD] | Plain Qt for the menu (`QSettings` list); `KRecentFilesAction`/`KRecentDocument` in KF6 (unverified which framework). Writes to `recently-used.xbel` possible in plain Qt but not trivial. |

## 3. Settings and persistence

| Practice | Why | Tag | Needs |
|---|---|---|---|
| Store settings in the platform's standard location. KDE writes `~/.config/<app>rc` (KConfig INI). Plain `QSettings(QSettings::NativeFormat)` on Linux writes `~/.config/<org>/<app>.conf`, which is also XDG-compliant. (`QStandardPaths` doc: AppConfigLocation `~/.config/<APPNAME>`.) | Backup/dotfile tooling, Flatpak redirects XDG dirs | [MUST] | Plain Qt |
| Set `QCoreApplication::setOrganizationName/Domain/ApplicationName` once so `QSettings`/`QStandardPaths` are stable. Keep config key names stable and add migration code (KDE has `kconf_update` for this). | Prevents orphaned settings | [MUST] | Plain Qt; `kconf_update` is KF6 |
| Declarative settings schema (KConfigXT: XML `.kcfg` -> generated class + `KConfigDialog` auto-binding widgets <-> settings). | Less boilerplate; default values in one place | [NICE] | KF6 (KConfig + KConfigWidgets) |
| Honour system-wide defaults/lockdown (KConfig can layer `/etc/xdg` under user config and lock keys; relevant to the KDE-for-Enterprise goal). | Managed deployments | [NICE] | KF6; `QSettings` system scope covers part of it |
| Respect `XDG_CONFIG_HOME` / Flatpak sandbox paths; never hard-code `~/.qraw`. | Flatpak, XDG compliance | [MUST] | Plain Qt (`QStandardPaths`) |
| Configuration dialog follows HIG (pages left, instant or OK/Apply/Cancel, Defaults). | Familiarity | [SHOULD] | Plain Qt |

## 4. Icons and branding

| Practice | Why | Tag | Needs |
|---|---|---|---|
| Use freedesktop-named theme icons (`document-open`, `edit-undo`, `zoom-in`, ...) via `QIcon::fromTheme(name, fallbackIcon)` so Breeze (or any theme) supplies them on Plasma. Qt 6.7 adds `QIcon::ThemeIcon` enum with 150 standard names. (https://doc.qt.io/qt-6/qicon.html) | Native look, automatic dark/light/HiDPI | [MUST] | Plain Qt |
| Bundle fallback icons (resources) and set a fallback theme before creating `QGuiApplication`; Qt's theme support is freedesktop-only (macOS/Windows native icon library only since 6.7). (qicon doc) | Windows/macOS builds have no Breeze unless shipped | [MUST] for non-Linux builds | Plain Qt (`QIcon::setFallbackThemeName`, `setFallbackSearchPaths`, `:/icons`) |
| Prefer monochrome SVG action icons that recolour with the palette (Breeze convention: symbolic/monochrome icons coloured by the colour scheme). (HIG icons/monochrome; recolouring behaviour is a Breeze/KIconThemes feature, unverified for plain Qt) | Dark-mode legibility | [SHOULD] | Plain Qt gets Breeze recolouring only when the active icon theme + platform theme provide it; bundled icons need manual recolour or both variants |
| Application icon: colourful style, 48 px baseline, distinct silhouette/colour, metaphor that explains the app, not derived solely from another app's icon. (https://develop.kde.org/hig/icons/colorful/application/) Provide SVG (scalable/apps) plus optionally PNG sizes. Flathub requires SVG or >= 256x256 PNG. | Recognisability; store listing | [MUST] | Plain Qt/CMake |
| MIME type icon for `.drawio` (colourful MIME style, HIG icons/colorful/mimetype) installed as `application-x-drawio` or via `<generic-icon>`. | File manager shows an icon | [SHOULD] | Plain |
| Trademark: the draw.io/diagrams.net name and logo must not be implied; Flathub: "Names and icons must be distinct and must not imply official affiliation". (Flathub requirements.) | Rejection risk | [MUST] | n/a |
| KDE app naming guidance (see section 1). KDE's Review/Flathub accept non-KDE names; `org.kde.*` is reserved for KDE Invent projects (Flathub rule). | App ID choice | [SHOULD] choose own domain/hosting-based ID | n/a |

## 5. Desktop integration and packaging

| Practice | Why | Tag | Needs |
|---|---|---|---|
| `.desktop` file named by app ID (reverse-DNS), e.g. `io.github.<owner>.qraw.desktop`: `Type=Application`, `Name`, `Exec` (with `%f`/`%F`/`%u`/`%U` field code so "Open with" passes files), `Icon`, `Categories` (e.g. `Graphics;2DGraphics;VectorGraphics;Qt;`), `MimeType=...;`, `Keywords`, `GenericName`. `StartupNotify`/`StartupWMClass` optional. Validate with `desktop-file-validate`. (Desktop Entry spec) | Menu entry, file association, launcher search | [MUST] | Plain |
| Call `QGuiApplication::setDesktopFileName("<desktop file basename without .desktop>")`. On Wayland this becomes the xdg `app_id`; mismatch gives a generic icon in the task manager (KDE bugs 450180, 483690). | Correct icon/grouping on Wayland | [MUST] | Plain Qt |
| AppStream metainfo `<app-id>.metainfo.xml` in `share/metainfo/` (`.appdata.xml` accepted for desktop apps but new files should use `.metainfo.xml`). Required by Flathub, by the KDE Review checklist, and by software centres (Discover). Required tags (Flathub): id, name, summary, description, developer (with `id`), launchable `desktop-id`, screenshots, releases, content_rating (OARS 1.1), url homepage, `metadata_license` (CC0-1.0/FSFAP) and `project_license` (SPDX); `branding` recommended. Validate with `appstreamcli validate --pedantic` and `validate-tree`. Flathub treats warnings as errors. (AppStream spec, Flathub metainfo guidelines, HowToAppStream) | Discoverability; gating requirement | [MUST] | Plain (CMake install; KDEInstallDirs only provides `KDE_INSTALL_METAINFODIR`, plain `GNUInstallDirs` equivalent `share/metainfo`) |
| Don't hand-add localised metainfo strings if ever hosted on KDE Invent: Scripty merges translations. (HowToAppStream) | Avoid overwrite | [NICE] | n/a |
| Screenshots: stable direct URLs pointing to a tag/commit not a branch (Flathub); KDE hosts them in `product-screenshots` repo. (Flathub metainfo; Review checklist item 7) | Store listing | [MUST] for store, [NICE] otherwise | Plain; repo already has `docs/screenshots` |
| MIME registration: ship `share/mime/packages/<app-id>.xml` defining e.g. `application/vnd.jgraph.mxfile` (the registered draw.io type; verify in shared-mime-info: upstream may already define it, check `xdg-mime query filetype` on target distros, unverified) with `<glob pattern="*.drawio"/>`, optionally `<glob pattern="*.dio"/>`, `<magic>` for `<mxfile`, `<sub-class-of type="application/xml"/>`, a `<comment>`, translated comments. Run `update-mime-database` in post-install (distro/Flatpak triggers do it automatically). List the type in `MimeType=` of the .desktop file and in metainfo `<provides><mediatype>`. (Shared MIME-info spec: root `mime-info`, glob `weight` default 50, `Override.xml` is for users.) | Double-click opens Qraw; file manager previews/icons | [MUST] | Plain |
| Icons in hicolor: `share/icons/hicolor/scalable/apps/<app-id>.svg` (+ optional `48x48`, `256x256` PNG; hicolor is the mandatory fallback theme). Symbolic variant `<app-id>-symbolic.svg` optional (GNOME/Flathub). (Icon Theme spec) | Works under any theme | [MUST] | Plain |
| Flatpak/Flathub: manifest `<app-id>.yaml` against the `org.kde.Platform` Qt 6 runtime; use XDG portals instead of broad `--filesystem` (portal use is mandatory where one exists); `--socket=wayland` + `--socket=fallback-x11`; minimal `finish-args`; build from a tagged release tarball, no network at build time; metainfo + desktop file + SVG icon upstream-integrated. (Flathub requirements; KDE Flatpak docs: https://develop.kde.org/docs/packaging/flatpak/) | Primary Linux distribution channel for KDE users | [SHOULD] | Plain Qt works (Qt apps run on org.kde.Platform without KF6 usage) |
| App ID rules for Flathub: 3-5 components, domain you control or code-host prefix (`io.github.<user>.qraw`; `org.kde.` and `org.gnome.` prefixes protected); ID must equal metainfo `<id>`. Flathub also requires English localisation complete, no nightly builds, no AI-generated manifests/PRs (as stated on the requirements page). | Avoid rejection | [MUST] if targeting Flathub | n/a |
| CMake install rules for all of the above via `GNUInstallDirs` (`share/applications`, `share/metainfo`, `share/icons/hicolor`, `share/mime/packages`). ECM's `KDEInstallDirs`/`ECMInstallIcons`/`ECMAddAppIcon` are KDE helpers (not verified in this session: ECM module list page was empty). | Packagers expect `make install` to do everything | [MUST] | Plain CMake |
| CI check: `desktop-file-validate`, `appstreamcli validate`, `xmllint` on MIME XML. | Catches Flathub/KDE gating failures early | [SHOULD] | Plain |
| Other channels (Review checklist item 13): AppImage, Snap, Windows (Craft/installer), macOS bundle with `Info.plist` `CFBundleDocumentTypes` + UTI for `.drawio`. | Non-KDE users | [NICE] | Plain Qt (macdeployqt/windeployqt) |

## 6. i18n

| Practice | Why | Tag | Needs |
|---|---|---|---|
| Every user-visible string goes through `tr()` with a disambiguation context for short or ambiguous strings; avoid string concatenation; use `%n`/`%1` placeholders; no word puzzles. (HIG text_and_labels, Suggested Review Criteria) | Translatable | [MUST] | Plain Qt (`QObject::tr`, `lupdate`/`lrelease`; project already links `LinguistTools`) |
| Ship `.ts` -> `.qm` in resources; load `QTranslator` for `QLocale::system()` plus Qt's own `qtbase_*` translations. Fall back to English. | Localised dialogs (file dialog, buttons) | [MUST] | Plain Qt |
| KDE's stack is KI18n (gettext-based `i18n()`, `i18nc()`, `i18np()`, KUIT markup) and the Review checklist requires a `Messages.sh` extracting `i18n()` strings. (https://api.kde.org/ki18n-index.html; Review checklist item 5.) Not relevant unless hosted on KDE Invent. | KDE translator workflow (Scripty/Lokalize) | [NICE] | KF6 (KI18n); gettext `.po` pipeline |
| Locale-aware formatting for numbers/dates; units spelled out where space allows (HIG). | Correctness | [SHOULD] | Plain Qt (`QLocale`) |
| RTL layouts: use layouts, mirrored icons where needed. | RTL users | [NICE] | Plain Qt |
| Translated desktop file/metainfo strings (`Name[de]=...`, `<name xml:lang>`). Flathub: English must be complete; machine-generated translations not allowed. | Store listing | [NICE] | Plain |

## 7. Accessibility

HIG accessibility checklist (https://develop.kde.org/hig/accessibility/): keyboard only; pointer only; touch only; switch colour scheme; set system font size 14; mute audio; Orca with screen off; disable animations.

| Practice | Why | Tag | Needs |
|---|---|---|---|
| Whole UI reachable via keyboard, sane focus order and visible focus; tooltips not the sole carrier of info; distinctive text first in item names. | HIG | [MUST] | Plain Qt |
| Accessible name/description on every control, especially icon-only buttons; use `QLabel::setBuddy` for form labels; no duplicate labels in a window. | Orca reads "Create New Folder, Button" | [MUST] | Plain Qt |
| Canvas (`QGraphicsView`/custom widget) is opaque to AT-SPI unless you expose it. Implement `QAccessibleInterface` (subclass `QAccessibleWidget`/`QAccessibleObject`) via `QAccessible::installFactory`, expose selected shape names/types/counts, send `QAccessible::updateAccessibility()` events. (Qt accessible-qwidget doc; that QGraphicsItems get no automatic interface is prior knowledge, unverified.) Minimum viable: shape list/outline tree panel that is accessible and mirrors the canvas, plus keyboard shape traversal (Tab / arrow keys to select next shape, arrow to move). | A purely pointer-driven canvas excludes keyboard and screen-reader users | [SHOULD] (outline panel + keyboard nav), [NICE] (full custom accessible interface) | Plain Qt |
| Follow system font size and scale; no fixed pixel sizes for chrome; no text clipping at font size 14. | HIG | [MUST] | Plain Qt (layouts) |
| Respect colour scheme; never rely on colour alone (e.g. selection state needs shape/handle as well). | HIG | [MUST] | Plain Qt |
| Honour "reduce animations" (global setting); no blinking elements other than text caret. | HIG | [SHOULD] | Plain Qt (`QStyleHints`/own flag; Plasma's animation speed setting is read by KDE style, how plain Qt apps read it is unverified) |
| No information by sound alone. | HIG | [MUST] | n/a |
| Test with Orca on Wayland and AT-SPI inspector (Accerciser). | Validation | [SHOULD] | Plain |
| KDE goal context: "KDE For All: Boosting Accessibility" was in the 2022-2024 cycle; the current 2026-2028 goals are Enterprise & Deployments, Better Documentation and Next Generation Styling (Ocean design system, Union style engine) per search results (not primary-source verified; fetching kde.org/goals returned the three current goals). | Direction of travel | info | n/a |

## 8. HiDPI, Wayland, theming

| Practice | Why | Tag | Needs |
|---|---|---|---|
| Rely on Qt 6 automatic high-DPI scaling (device-independent pixels; Windows is Per-Monitor V2 by default). Do not use deprecated `AA_EnableHighDpiScaling`. Fractional scaling: default rounding policy is `PassThrough`, which Plasma uses; test at 125%/150%. (Qt highdpi doc) | Crisp UI | [MUST] | Plain Qt |
| Canvas rendering: draw via `QPainter` in logical coordinates; for cached pixmaps/tiles allocate at `devicePixelRatio()` and call `setDevicePixelRatio`; avoid 1-px "hairlines" without cosmetic-pen handling; ship icons as SVG or with `@2x` variants. (Qt highdpi doc says pixmaps are raw pixels and need `@2x` naming) | Blurry diagrams on 2x/1.5x screens | [MUST] | Plain Qt |
| Export PNG with explicit scale (1x/2x/3x) independent of screen DPR. | Export correctness | [SHOULD] | Plain Qt |
| Colours from `QPalette`, not hard-coded, for UI chrome. Plasma's colour scheme reaches Qt apps through the platform theme (plasma-integration) as palette; a Qt app using standard widgets and palette roles follows it, including live switching. | Light/dark/custom Plasma schemes | [MUST] | Plain Qt (unverified: that plasma-integration is the delivery mechanism is prior knowledge) |
| React to colour-scheme change: `QStyleHints::colorScheme()` / `colorSchemeChanged` (Qt 6.5+, so within Qraw's minimum). `setColorScheme()` / `unsetColorScheme()` arrive in Qt 6.8 (optional in-app Light/Dark/System override). Repaint canvas, regenerate any theme-dependent cached pixmaps/icons on `QEvent::PaletteChange`/`ApplicationPaletteChange`. | Dark mode correctness | [MUST] | Plain Qt |
| Diagram canvas decisions: page/canvas background and default shape colours belong to the document (draw.io has its own "dark" setting and `adaptiveColors`); chrome follows the system, canvas follows document + a dark-canvas preference. | Don't invert user diagrams | [SHOULD] | Plain |
| App-specific colour scheme picker (`KColorSchemeManager`, can offer custom schemes and follow system; https://api.kde.org/kcolorschememanager.html) | Extra choice for photo-type apps | [NICE] (HIG says do not override platform settings) | KF6 |
| Wayland: window position cannot be set or restored by the client; do not rely on `move()`/restoring position; use `saveGeometry` only for size on Wayland (HIG: position remembered "on X11"). Set desktop file name (section 5). Use `QGuiApplication::platformName()` only for diagnostics. Native Wayland is Qt's default on Plasma; test `QT_QPA_PLATFORM=wayland` and `xcb`. | Correct behaviour on Plasma 6 | [MUST] | Plain Qt |
| Drag-and-drop and clipboard: use standard MIME types (`image/svg+xml`, `image/png`, `application/vnd.jgraph.mxfile` or `text/plain` with the XML); show a drag preview; support drop of files onto the window to open. | Wayland-friendly, HIG getting_input | [SHOULD] | Plain Qt |
| Print via `QPrintDialog`; platform theme provides CUPS dialog. | Native | [NICE] | Plain Qt |
| Style: do not force `Fusion` or a custom stylesheet; let Breeze (via Plasma) be the style. Avoid `setStyleSheet` on top-level chrome since it disables native styling. | Native look | [MUST] | Plain Qt |

## 9. Crash handling and logging

| Practice | Why | Tag | Needs |
|---|---|---|---|
| Use `QLoggingCategory` with dotted names per subsystem (`qraw.format`, `qraw.render`, ...); `Q_DECLARE_LOGGING_CATEGORY` in header and `Q_LOGGING_CATEGORY(cat, "qraw.format", QtInfoMsg)` in one TU; never use `qt*` prefix (reserved for Qt). Users enable with `QT_LOGGING_RULES="qraw.*.debug=true"`. (Qt doc.) KDE's convention is `org.kde.<app>[.<sub>]` (via ECM `ecm_qt_declare_logging_category`; unverified here). | Debuggable on user machines without recompiling | [SHOULD] | Plain Qt |
| Never `qDebug()` in release paths; use category-gated `qCDebug`. | Noise, privacy | [SHOULD] | Plain Qt |
| Crash reporting: KDE uses `KCrash` + DrKonqi (`KCrash::initialize()`; handles SIGSEGV/SIGBUS/SIGFPE/SIGILL/SIGABRT, optional emergency-save function; https://api.kde.org/kcrash.html). On Plasma, DrKonqi is also triggered by systemd-coredump/`coredumpctl`, and Fedora/ABRT-style handlers exist, so a plain Qt app still gets a crash report. | Users can report with backtraces | [NICE] | KF6 (KCrash); DrKonqi is only on Plasma; no verified evidence KCrash works on Windows/macOS (page covers Linux/Android only) |
| Plain-Qt equivalent: emergency save/autosave journal (below) rather than a custom signal handler; do not write a SIGSEGV handler that does non-async-signal-safe work. | Data safety | [SHOULD] | Plain |
| Ship debug symbols (or `-g1`) in Flatpak/dev builds so reports are useful; set `CMAKE_BUILD_TYPE=RelWithDebInfo` for packagers. | Triage | [NICE] | Plain |
| Optional `--version` and "About" dialog showing Qt version, platform plugin, build ID. | Bug reports | [SHOULD] | Plain Qt (`QCommandLineParser`) |

## 10. File handling

| Practice | Why | Tag | Needs |
|---|---|---|---|
| Open from command line, from `%F` args, and from the desktop's "Open with"; single-instance behaviour (second launch forwards files to the running instance) is optional but KDE apps commonly do this via D-Bus (`KDBusService`). Note: on macOS use `QFileOpenEvent`. | Integration | [SHOULD] open from args; [NICE] single instance | Plain Qt (`QLocalServer` or `QDBus`); `KDBusService` is KF6 (KDBusAddons) |
| Atomic save: write to a temp file in the same directory, `fsync`, rename over the target; preserve file permissions; handle read-only paths with a clear message. | No corruption on crash/full disk | [MUST] | Plain Qt (`QSaveFile`) |
| Autosave / recovery: periodic backup to `QStandardPaths::AppDataLocation` (or `~/.cache`), offer restore after crash; delete on clean save/close. HIG pages fetched do not address autosave; KDE apps commonly use `~name.kra`-style backups (unverified). | Crash/power loss | [SHOULD] | Plain Qt |
| Modified indicator in title (`[*]`) and prompt on close with Save/Discard/Cancel (standard buttons). | Standard | [MUST] | Plain Qt |
| Warn on external change of the open file (`QFileSystemWatcher`), offer reload. | Expected | [NICE] | Plain Qt |
| Remote/URL files: KDE apps use KIO for `smb://`, `sftp://`, `mtp://`, `gdrive:`. Plain Qt's `QFileDialog` returns local paths via portal/KDE platform dialog; Plasma's KIO FUSE exposes remote locations to non-KIO apps under `~/.cache/kio-fuse`. (kio-fuse behaviour is prior knowledge, unverified.) Recommendation: do not implement KIO; accept local paths only. | Complexity vs value | [NICE] to ignore | KIO is KF6 |
| Recent files (section 2) and remember last-used directory in settings. | Expected | [SHOULD] | Plain Qt |
| Treat opened `.drawio` content as untrusted: size limits, XML entity expansion off, decompression bomb limits, no launching external programs/URLs without consent. (Review criteria: "every place that launches external programs is checked"; HTML special chars encoded.) | Security gate in review | [MUST] | Plain |

## 11. Release and review requirements

KDE Review checklist (https://develop.kde.org/docs/getting-started/add-project/review/), items and how Qraw would fare **only if** it were to be hosted in KDE:

1. Incubation completed if external. 2. Android ID rules (n/a). 3. **REUSE 3.0 compliance, SPDX in every file.** 4. CI passes REUSE lint (+JSON/XML/YAML lint). 5. `Messages.sh` extracting `i18n()` strings (implies KI18n). 6. **metainfo.xml per AppStream guidelines.** 7. Screenshot in `product-screenshots` repo. 8. clazy/clang-tidy checked. 9. User-facing docs (README for apps). 10. bugs.kde.org product. 11. GitLab CI build passes. 12. KDE neon build passes. 13. Packages where appropriate (Flatpak, Snap, AppImage, Windows via Craft).

Also (Application Lifecycle, https://community.kde.org/Policies/Application_Lifecycle): stages Personal -> Playground -> Incubator (for outside projects) -> KDE Review (>= 2 weeks) -> released; all projects with user-visible strings must have translations; release cadence for Gear/Plasma/Extragear; depend only on stable released software.
Older "Suggested Review Criteria" (techbase, partially dated: mentions Krazy/Solaris): prefer KDE classes over Qt equivalents where more suitable (e.g. KIO over QHttp), portability to Windows/macOS/BSD, KUIT markup, security (escape HTML, check launching external programs).

| Practice | Why | Tag | Needs |
|---|---|---|---|
| Treat the KDE Review checklist as a quality bar even if not applying to KDE: REUSE, AppStream, screenshots, docs, static analysis, CI, packages. | Free, widely understood bar | [SHOULD] | Plain |
| Joining KDE (Invent, bugs.kde.org, Scripty, release team) requires KDE Frameworks (item 5 -> KI18n) and `org.kde.*` ID. Not necessary for being distributed on Flathub/distros. | Decision gate, see section 13 | decision | KF6 |
| Semantic versioning, tagged releases with signed tarballs and a `releases` entry in metainfo per release; Flathub requires tagged releases and no nightlies. | Packaging | [MUST] for Flathub | Plain |
| CHANGELOG feeds metainfo `<release><description>` so Discover shows release notes. | Store UX | [NICE] | Plain |

## 12. Licensing (REUSE / SPDX)

| Practice | Why | Tag | Needs |
|---|---|---|---|
| Follow REUSE Specification 3.0: `LICENSES/<SPDX-ID>.txt` per used license (use `reuse download --all`), every file carries `SPDX-FileCopyrightText` + `SPDX-License-Identifier` (or `REUSE.toml`/`.reuse/dep5` for generated/binary assets). Run `reuse lint` in CI. (KDE Licensing Policy; Review checklist items 3-4.) Repo currently has a single `LICENSE` file and `THIRD_PARTY.md` (seen at repo root). | Required by KDE Review, valued by distros, makes third-party attribution machine-readable | [SHOULD] (MUST for KDE hosting) | Plain |
| App code under GPL-2.0-only OR GPL-3.0-only OR LicenseRef-KDE-Accepted-GPL (KDE policy) or GPL-2.0-or-later/3.0-or-later; libraries LGPL; CMake BSD-2/3; docs/media CC-BY-SA-4.0; Breeze/Oxygen icons LGPL-3.0-only-or-KDE-accepted. Qraw's chosen license should be confirmed compatible (Qt Widgets is LGPL-3/GPL; Qt dynamic linking fine). Metainfo `project_license` must be a valid SPDX expression; `metadata_license` CC0-1.0 or FSFAP. | Compatibility, metainfo | [MUST] pick and declare consistently | Plain |
| Bundled third-party data: draw.io shape stencils/libraries and icons must have redistributable licences recorded per file (Flathub: "All hosted content must allow legal redistribution"; non-redistributable -> `extra-data`). Existing `THIRD_PARTY.md` should map onto REUSE annotations. | Legal; Flathub review | [MUST] | Plain |
| Manifests/build files for Flathub should be permissive (MIT/0BSD/Unlicense) per Flathub requirements. | Flathub rule | [NICE] | n/a |

## 13. Adopt KDE Frameworks or not?

What Qraw gains from KF6 (each bullet is Plasma-friendly polish, not a functional necessity):

- **KXmlGui** (`KXmlGuiWindow`, `KActionCollection`, `.rc` UI, `KShortcutsDialog`, `KEditToolBar`): user-configurable shortcuts/toolbars, automatic window-state save, standard Help menu. Biggest single UX gain, also biggest structural change (main window + action creation pattern changes). (Tutorial exists at https://develop.kde.org/docs/getting-started/kxmlgui/; did not verify feature list from fetch.)
- **KStandardAction/KStandardShortcut**: exact KDE text/icons/shortcuts; ~90% reproducible with `QKeySequence::StandardKey` + `QIcon::fromTheme`.
- **KConfig/KConfigXT/KConfigDialog**: declarative settings, system/lockdown layering. Replaces `QSettings` (no migration helper from QSettings; you'd write one).
- **KI18n**: needed only for KDE hosting/translator infrastructure; Qt's `.ts` pipeline is adequate otherwise.
- **KCrash/DrKonqi**: nice crash UX on Plasma only; no verified Windows/macOS support.
- **KColorScheme/KColorSchemeManager**: in-app scheme chooser; HIG says not to override platform settings, so low value.
- **KNewStuff** (Get Hot New Stuff downloader for shape libraries): plausible fit for downloadable shape/template libraries, but requires a KDE Store (store.kde.org) hosted provider and has the HIG caveat about warning that third-party content comes with no guarantees. (Not researched in depth; unverified that it still fits our needs.)
- **KIO/KRecentDocument/KDBusAddons**: remote files, recent list, single instance.
- **Kirigami**: not relevant for a Widgets desktop editor; HIG recommends Kirigami for *new* apps in general but names Widgets for complex traditional desktop apps.
- **Gains in KDE process**: Invent hosting, Scripty translations, CI/neon, bugs.kde.org, Craft packages (requires `org.kde.*`-style identity and KDE license policy).

Costs:

- **Portability**: KF6 targets Linux, Windows, macOS, Android, but support is per-framework and described as "partial" for non-Linux platforms in secondary sources; D-Bus is disabled on Windows/macOS in places. The project already ships on Linux, macOS and Windows CI (per recent commits). Every KF6 dependency adds Craft/vcpkg/Homebrew build work and installer size on those platforms. Windows/macOS tooling for KDE is Craft (https://develop.kde.org/docs/packaging/ lists Windows packaging only; no macOS page seen).
- **Dependency weight**: KF6 modules pull in each other (ConfigWidgets, XmlGui -> WidgetsAddons, ConfigCore, CoreAddons, I18n, IconThemes, ItemViews, TextWidgets, Service, ...), plus ECM and gettext. Flatpak on `org.kde.Platform` already contains KF6, so cost there is ~0; AppImage/Windows/macOS bundles grow noticeably.
- **Build and CI complexity** and Qt version pinning: KF6 requires a recent Qt 6 release series; Qraw currently requires Qt 6.5.
- **Design coupling**: KXmlGui wants `KXmlGuiWindow` as base and `.rc` XML; this competes with an existing architecture (see `ARCHITECTURE.md`).

Recommendation (a judgment, not a source claim):

1. **Stay plain Qt for core.** Get ~90% of the Plasma-native feel from plain Qt: standard shortcuts, theme icons, palette-driven colours, `QFileDialog`, desktop-file name, HiDPI, accessibility, MIME, AppStream, Flatpak. Every `[MUST]` item in this document is doable in plain Qt.
2. **Isolate behind small interfaces** (`ShortcutProvider`, `RecentFiles`, `SettingsStore`, `CrashReporter`) so a KF6-backed implementation can be switched on with a CMake option (`QRAW_WITH_KF6`, default OFF) if someone wants it for Linux packages. Keep KF6 optional, never required.
3. **Reconsider** full KDE adoption only if Qraw wants to be hosted in KDE (Invent/Gear/Extragear) or if user-configurable toolbars/shortcuts become a top request. Then KXmlGui + KI18n + KConfig are the three worth taking; KCrash, KColorScheme, KNewStuff last.

## 14. Highest-value, cheapest items (prioritised shortlist)

1. `.desktop` + AppStream metainfo + hicolor SVG icon + MIME XML for `.drawio`, installed by CMake, validated in CI; `setDesktopFileName` for Wayland.
2. `QStyleHints::colorSchemeChanged` + palette-only chrome + dark-aware canvas; no `setStyleSheet` on chrome; Qt 6 HiDPI-correct canvas caches.
3. Standard actions (`QKeySequence::StandardKey`), theme icons with bundled fallback, HIG text conventions (Title Case, "…" rules).
4. Accessibility: accessible names on all controls, keyboard shape navigation, accessible outline panel, `QAccessibleInterface` for canvas later.
5. REUSE/SPDX compliance + `reuse lint` in CI; record bundled stencil/icon licences per file.
6. `QSaveFile` atomic saves, autosave/recovery, `QLoggingCategory` per module.
7. Flathub submission prep: reverse-DNS ID on a controlled domain or `io.github.*`, permissive manifest, tagged release.
8. Keep KF6 optional.

## Unverified / could not confirm

- HIG pages did not state menu order, standard shortcut table, or autosave guidance; KStandardAction page did not list the standard action set.
- KXmlGui feature list (configurable toolbars/shortcuts), KRecentFilesAction framework, KNewStuff fit, ECM module behaviours, kio-fuse behaviour, KDE logging-category naming convention, plasma-integration palette delivery: prior knowledge/not fetched.
- KDE Goals 2026-2028: from search results and kde.org/goals fetch listing three goals; Akademy 2026 announcement not read directly.
- Whether `application/vnd.jgraph.mxfile` already exists in the installed shared-mime-info database: not checked.
- KCrash platform support on Windows/macOS: API docs only cover Linux/Android and say nothing for desktop OSs.
- Whether `api.kde.org/ecm/` ECM modules and `KHamburgerMenu` are in KConfigWidgets: HIG states `KHamburgerMenu` exists for QtWidgets apps; its home framework not confirmed.

## Name and the Qt trademark

Researched 2026-11-26 from The Qt Company's own pages only.

- Trademark page: https://www.qt.io/trademark/. Fair use covers an application created with Qt "when the Qt name is not being used in a way which suggests sponsorship or endorsement" (1.2), the unmodified "Built with Qt" logo (1.3) and tools made for Qt (1.6, state the Qt version when it is one). Prohibited without express written permission: the Qt trademark "in conjunction with your company name or another name" (2.3), as part of a company name or logo (2.6), as whole or part of a domain name (2.4), and any use that suggests endorsement (2.7). Qt logos always need a licence.
- Acknowledgement: use the registered mark symbol at the first use of the mark in text and the legend "Qt is a registered trademark of The Qt Company Ltd. and its subsidiaries." (other trademarks: "all other trademarks are the property of their respective owners"). The page has no effective date; changes need 30 days' written notice. Questions and licences: legal@qt-project.org.
- Clarification post (2014): https://www.qt.io/blog/2014/03/18/clarifications-to-qt-trademark-fair-use-policy. A product made with or for use with Qt: "the recommendation is to make the trademark license with us". Fair use does not override the prohibited list.
- https://doc.qt.io/qt-6/trademarks.html only names the owner and the registered marks; it asks for no acknowledgement.

Finding: a product whose name contains "Qt" is the "in conjunction with ... another name" case of the prohibited list; the guidelines do not say that fair use extends to it, and an application "created with Qt" is covered only while the name does not suggest endorsement. The pages are not clear enough to settle it, and no other source was used. Options: keep the name with a disclaimer, ask The Qt Company, rename. The project was renamed before its first public release; D-047 records the decision.
