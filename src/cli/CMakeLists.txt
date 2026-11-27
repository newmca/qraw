# Headless command-line tools, shared by qraw-cli and the qraw GUI binary.
qraw_glob_sources(cli_sources ${CMAKE_CURRENT_SOURCE_DIR})
list(REMOVE_ITEM cli_sources ${CMAKE_CURRENT_SOURCE_DIR}/main.cpp)
add_library(qraw_cli STATIC ${cli_sources})
add_library(qraw::cli ALIAS qraw_cli)
target_include_directories(qraw_cli PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(qraw_cli PUBLIC qraw_diag qraw_format qraw_render Qt6::Core Qt6::Gui)
qraw_configure_target(qraw_cli)

add_executable(qraw-cli main.cpp)
target_compile_definitions(qraw-cli PRIVATE QRAW_VERSION="${PROJECT_VERSION}")
target_link_libraries(qraw-cli PRIVATE qraw_cli)
qraw_configure_target(qraw-cli)
if(APPLE)
    # Next to the GUI executable inside the app bundle: Contents/MacOS/qraw-cli, linked against
    # the Frameworks that macdeployqt copies there (cmake/Packaging.cmake).
    set_target_properties(qraw-cli PROPERTIES INSTALL_RPATH "@executable_path/../Frameworks")
    install(TARGETS qraw-cli RUNTIME DESTINATION "${QRAW_MACOS_BUNDLE_DIR}/Contents/MacOS")
else()
    install(TARGETS qraw-cli)
endif()
