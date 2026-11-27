# Packages: what `cmake --install` and `cpack` produce (BUILDING.md, "Packages").
#
#   Windows  Qt's deployment API (windeployqt) fills the install tree; CPack builds an NSIS installer and a portable ZIP.
#   macOS    Qt's deployment API (macdeployqt) makes Qraw.app self-contained and an ad-hoc signature is applied;
#            CPack builds a .dmg (DragNDrop).
#   Linux    Nothing is bundled: `cmake --install` installs for distribution packagers and the Flatpak. The TGZ
#            generator only archives that install tree.
#
# Included from the root CMakeLists.txt after src/, so that the install rules below run after the targets are installed.

# --- Install tree ---------------------------------------------------------------------------------------------

if(WIN32 OR APPLE)
    # Runs windeployqt / macdeployqt. SVG and JPEG images (imageformats) and SVG icons (iconengines) are loaded at
    # run time and not found by looking at the link line. Qt >= 6.10 lets the caller name plugin types
    # (INCLUDE_PLUGIN_TYPES); older versions reject the argument ("Unexpected arguments", seen with 6.8.3), and
    # windeployqt deploys these plugin types with Qt6Gui and Qt6Svg on its own. Either way the install fails
    # below when one of the files is missing.
    set(qraw_deploy_plugin_args "")
    if(Qt6_VERSION VERSION_GREATER_EQUAL 6.10)
        set(qraw_deploy_plugin_args INCLUDE_PLUGIN_TYPES imageformats iconengines)
    endif()
    qt_generate_deploy_app_script(
        TARGET qraw
        OUTPUT_SCRIPT qraw_deploy_script
        ${qraw_deploy_plugin_args}
        NO_UNSUPPORTED_PLATFORM_ERROR)
    install(SCRIPT "${qraw_deploy_script}")
endif()

if(WIN32)
    # zlib is the one dependency the Qt deployment tool does not know about. With vcpkg the DLL sits in
    # <triplet>/bin, next to the include directory. Its file name changes between zlib releases (zlib1.dll up to
    # 1.3, another name with 1.4), so the install asks the executables what they import instead of guessing names.
    # QRAW_ZLIB_DLL_DIR overrides the directory searched.
    get_filename_component(qraw_zlib_prefix "${ZLIB_INCLUDE_DIR}/.." ABSOLUTE)
    set(QRAW_ZLIB_DLL_DIR "${qraw_zlib_prefix}/bin" CACHE PATH "Directory holding the zlib DLL to ship")
    install(CODE "set(zlib_dll_dir [[${QRAW_ZLIB_DLL_DIR}]])")
    install(CODE "set(bin_dir [[${CMAKE_INSTALL_BINDIR}]])")
    install(CODE [[
        set(bin "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${bin_dir}")
        # Every DLL the executables import, resolved against the install tree (windeployqt has filled it), the
        # vcpkg bin directory and the system directories. API sets (api-ms-*, ext-ms-*) are virtual and never resolve.
        file(GET_RUNTIME_DEPENDENCIES
            EXECUTABLES "${bin}/qraw.exe" "${bin}/qraw-cli.exe"
            DIRECTORIES "${bin}" "${zlib_dll_dir}"
            PRE_EXCLUDE_REGEXES "^api-ms-" "^ext-ms-"
            RESOLVED_DEPENDENCIES_VAR resolved
            UNRESOLVED_DEPENDENCIES_VAR unresolved)
        # The search follows the imports of Windows' own DLLs too, and some of those are optional components that
        # are not installed everywhere (HvsiFileTrust.dll, wpaxholder.dll), so an unresolved name is logged, not an
        # error. Whether the package runs is checked by running it (tools/ci/smoke_package.sh in CI).
        if(unresolved)
            message(STATUS "Imports not found (Windows components are expected here): ${unresolved}")
        endif()
        # Ship what came from the vcpkg directory (zlib, under whatever name its release gives the DLL). None means
        # zlib is linked statically.
        file(TO_CMAKE_PATH "${zlib_dll_dir}" vcpkg_dir)
        string(TOLOWER "${vcpkg_dir}/" vcpkg_dir)
        set(shipped "")
        foreach(dll IN LISTS resolved)
            file(TO_CMAKE_PATH "${dll}" dll_path)
            string(TOLOWER "${dll_path}" dll_lower)
            string(FIND "${dll_lower}" "${vcpkg_dir}" at)
            if(at EQUAL 0)
                list(APPEND shipped "${dll_path}")
            endif()
        endforeach()
        message(STATUS "DLLs shipped from ${zlib_dll_dir}: ${shipped}")
        if(shipped)
            file(INSTALL ${shipped} DESTINATION "${bin}")
        endif()
    ]])
endif()

if(APPLE)
    # qraw-cli renders on the offscreen platform (there may be no window server), which macdeployqt leaves out
    # when it does not copy it with the other platform plugins: add it, next to libqcocoa.dylib.
    install(CODE "set(bundle_dir [[${QRAW_MACOS_BUNDLE_DIR}]])")
    install(CODE "set(qt_platform_plugins [[${QT6_INSTALL_PREFIX}/${QT6_INSTALL_PLUGINS}/platforms]])")
    install(CODE [[
        set(bundle "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${bundle_dir}")
        set(deployed "${bundle}/Contents/PlugIns/platforms")
        if(NOT EXISTS "${deployed}/libqoffscreen.dylib")
            file(COPY "${qt_platform_plugins}/libqoffscreen.dylib" DESTINATION "${deployed}")
            # Like the plugins macdeployqt copies: resolve the Qt frameworks inside the bundle.
            execute_process(COMMAND install_name_tool -add_rpath @loader_path/../../Frameworks
                "${deployed}/libqoffscreen.dylib" ERROR_QUIET)
        endif()
    ]])

    # An ad-hoc signature ("-") is what Apple silicon needs to run the app at all. It is not a developer
    # signature and not notarised: Gatekeeper still asks on first start. The helper first, then the bundle.
    install(CODE [[
        foreach(target "${bundle}/Contents/MacOS/qraw-cli" "${bundle}")
            execute_process(COMMAND codesign --force --deep --sign - "${target}" RESULT_VARIABLE result)
            if(result)
                message(FATAL_ERROR "codesign of ${target} failed: ${result}")
            endif()
        endforeach()
    ]])
endif()

# Fails the install, and so the package, when the deployment tool left something out that Qraw loads at run
# time. The names are searched anywhere below the prefix, because windeployqt and macdeployqt differ in layout.
if(WIN32)
    set(qraw_required_files qraw.exe qraw-cli.exe Qt6Core.dll Qt6Gui.dll Qt6Widgets.dll Qt6Svg.dll
        Qt6PrintSupport.dll qwindows.dll qoffscreen.dll "q*style.dll" qsvg.dll qjpeg.dll qsvgicon.dll
        windowsprintersupport.dll qt.conf)
elseif(APPLE)
    set(qraw_required_files Qraw qraw-cli QtCore QtGui QtWidgets QtSvg QtPrintSupport libqcocoa.dylib
        libqoffscreen.dylib "libq*style.dylib" libqsvg.dylib libqjpeg.dylib libqsvgicon.dylib)
endif()
if(qraw_required_files)
    install(CODE "set(required_files \"${qraw_required_files}\")")
    install(CODE [[
        set(missing "")
        foreach(name IN LISTS required_files)
            file(GLOB_RECURSE found "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/${name}")
            if(NOT found)
                list(APPEND missing "${name}")
            endif()
        endforeach()
        if(missing)
            message(FATAL_ERROR "The install tree lacks: ${missing}")
        endif()
    ]])
endif()

# --- CPack ------------------------------------------------------------------------------------------------------

set(CPACK_PACKAGE_NAME "Qraw")
set(CPACK_PACKAGE_VENDOR "Qraw contributors")
set(CPACK_PACKAGE_VERSION "${PROJECT_VERSION}")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY "${PROJECT_DESCRIPTION}")
set(CPACK_PACKAGE_HOMEPAGE_URL "https://github.com/newmca/qraw")
set(CPACK_PACKAGE_INSTALL_DIRECTORY "Qraw")
set(CPACK_RESOURCE_FILE_LICENSE "${PROJECT_SOURCE_DIR}/LICENSE")
set(CPACK_PACKAGE_CHECKSUM SHA256)
string(TOLOWER "${CMAKE_SYSTEM_NAME}" qraw_package_system)
string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" qraw_package_arch)
if(qraw_package_arch STREQUAL "amd64")
    set(qraw_package_arch "x64")
endif()
set(CPACK_PACKAGE_FILE_NAME "Qraw-${PROJECT_VERSION}-${qraw_package_system}-${qraw_package_arch}")

if(WIN32)
    set(CPACK_GENERATOR "NSIS;ZIP")
    set(CPACK_PACKAGE_EXECUTABLES "qraw" "Qraw") # Start Menu shortcut (bin/qraw.exe)

    # Per machine: the CPack NSIS template always asks for administrator rights (RequestExecutionLevel admin) and
    # installs below Program Files with the uninstall entry and the file association for all users. The ZIP is
    # the package for installing without administrator rights.
    set(CPACK_NSIS_INSTALL_ROOT "$PROGRAMFILES64")
    set(CPACK_NSIS_DISPLAY_NAME "Qraw")
    set(CPACK_NSIS_PACKAGE_NAME "Qraw")
    set(CPACK_NSIS_URL_INFO_ABOUT "${CPACK_PACKAGE_HOMEPAGE_URL}")
    file(TO_NATIVE_PATH "${PROJECT_SOURCE_DIR}/resources/icons/qraw.ico" qraw_ico)
    set(CPACK_NSIS_MUI_ICON "${qraw_ico}")
    set(CPACK_NSIS_MUI_UNIICON "${qraw_ico}")
    set(CPACK_NSIS_INSTALLED_ICON_NAME "bin\\qraw.exe") # the icon of the "Apps" list entry

    # The .drawio file association: a ProgID with an icon and an open command. A handler another program already
    # registered as the default stays (Qraw is then offered under "Open with"); the uninstaller removes
    # exactly what this one wrote.
    set(qraw_progid "${QRAW_APP_ID}.drawio")
    set(CPACK_NSIS_EXTRA_INSTALL_COMMANDS "
        WriteRegStr HKCR '${qraw_progid}' '' 'draw.io Diagram'
        WriteRegStr HKCR '${qraw_progid}\\DefaultIcon' '' '$INSTDIR\\bin\\qraw.exe,0'
        WriteRegStr HKCR '${qraw_progid}\\shell\\open\\command' '' '\"$INSTDIR\\bin\\qraw.exe\" \"%1\"'
        WriteRegStr HKCR '.drawio\\OpenWithProgids' '${qraw_progid}' ''
        ReadRegStr $R0 HKCR '.drawio' ''
        StrCmp $R0 '' 0 +2
        WriteRegStr HKCR '.drawio' '' '${qraw_progid}'
        System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    ")
    set(CPACK_NSIS_EXTRA_UNINSTALL_COMMANDS "
        DeleteRegKey HKCR '${qraw_progid}'
        DeleteRegValue HKCR '.drawio\\OpenWithProgids' '${qraw_progid}'
        ReadRegStr $R0 HKCR '.drawio' ''
        StrCmp $R0 '${qraw_progid}' 0 +2
        DeleteRegValue HKCR '.drawio' ''
        System::Call 'shell32::SHChangeNotify(i 0x08000000, i 0, p 0, p 0)'
    ")
elseif(APPLE)
    set(CPACK_GENERATOR "DragNDrop")
    set(CPACK_DMG_FORMAT "UDZO")
    set(CPACK_DMG_VOLUME_NAME "Qraw")
else()
    set(CPACK_GENERATOR "TGZ")
endif()

include(CPack)
