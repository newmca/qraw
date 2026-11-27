set(QRAW_ICON_DIR "${PROJECT_SOURCE_DIR}/resources/icons")
set(QRAW_LINUX_DIR "${PROJECT_SOURCE_DIR}/resources/linux")

set(app_sources main.cpp)
if(WIN32)
    # The .ico becomes the executable's icon (Explorer, taskbar).
    configure_file(qraw.rc.in "${CMAKE_CURRENT_BINARY_DIR}/qraw.rc" @ONLY)
    list(APPEND app_sources "${CMAKE_CURRENT_BINARY_DIR}/qraw.rc")
elseif(APPLE)
    set(app_icns "${QRAW_ICON_DIR}/qraw.icns")
    set_source_files_properties("${app_icns}" PROPERTIES MACOSX_PACKAGE_LOCATION Resources)
    list(APPEND app_sources "${app_icns}")
endif()

qt_add_executable(qraw WIN32 MACOSX_BUNDLE ${app_sources})
target_compile_definitions(qraw PRIVATE QRAW_VERSION="${PROJECT_VERSION}")
target_link_libraries(qraw PRIVATE qraw_ui qraw_cli qraw_diag Qt6::Widgets)
qraw_configure_target(qraw)

if(APPLE)
    # The bundle is Qraw.app; qraw-cli goes into it too (src/cli, cmake/Packaging.cmake).
    set(QRAW_MACOS_MINIMUM_VERSION "${CMAKE_OSX_DEPLOYMENT_TARGET}") # Qt's own minimum unless overridden
    if(NOT QRAW_MACOS_MINIMUM_VERSION)
        set(QRAW_MACOS_MINIMUM_VERSION "12.0")
    endif()
    set(QRAW_COPYRIGHT "Copyright (c) 2026 Qraw contributors. Apache License 2.0.")
    configure_file("${PROJECT_SOURCE_DIR}/cmake/MacOSXBundleInfo.plist.in"
        "${CMAKE_CURRENT_BINARY_DIR}/Info.plist" @ONLY)
    set_target_properties(qraw PROPERTIES
        OUTPUT_NAME Qraw
        MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_BINARY_DIR}/Info.plist"
        MACOSX_BUNDLE_ICON_FILE qraw.icns
        MACOSX_BUNDLE_GUI_IDENTIFIER "${QRAW_APP_ID}"
        MACOSX_BUNDLE_BUNDLE_NAME Qraw
        MACOSX_BUNDLE_SHORT_VERSION_STRING "${PROJECT_VERSION}"
        MACOSX_BUNDLE_BUNDLE_VERSION "${PROJECT_VERSION}")
endif()

install(TARGETS qraw BUNDLE DESTINATION . RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR})

# Linux desktop integration (docs/research/kde-best-practices.md sections 4 and 5).
if(UNIX AND NOT APPLE)
    install(FILES "${QRAW_LINUX_DIR}/${QRAW_APP_ID}.desktop"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/applications)
    install(FILES "${QRAW_LINUX_DIR}/${QRAW_APP_ID}.metainfo.xml"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/metainfo)
    install(FILES "${QRAW_LINUX_DIR}/${QRAW_APP_ID}.xml"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/mime/packages)
    install(DIRECTORY "${QRAW_ICON_DIR}/hicolor"
        DESTINATION ${CMAKE_INSTALL_DATADIR}/icons
        FILES_MATCHING PATTERN "*.png" PATTERN "*.svg")

    # The metainfo release list is maintained by hand: fail when it lags the project version.
    file(READ "${QRAW_LINUX_DIR}/${QRAW_APP_ID}.metainfo.xml" metainfo_text)
    if(NOT metainfo_text MATCHES "<release version=\"${PROJECT_VERSION}\"")
        message(FATAL_ERROR "resources/linux/${QRAW_APP_ID}.metainfo.xml has no <release> for ${PROJECT_VERSION}")
    endif()
endif()
