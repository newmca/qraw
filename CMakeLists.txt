cmake_minimum_required(VERSION 3.25)

project(qraw
    VERSION 0.1.0
    DESCRIPTION "Native Qt 6 diagram editor compatible with draw.io files"
    LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

option(QRAW_WARNINGS_AS_ERRORS "Treat compiler warnings as errors" OFF)
option(QRAW_LEAN_TEST_BINARIES "Link the test programs without LTO and without debug information (package builds: 160 of them, each an LTO link of the whole program, filled the disk of a hosted runner)" OFF)
option(QRAW_BUILD_TOOLS "Build developer tools that are not installed (tools/icons, tools/fidelity)" OFF)

# The application id: Wayland app_id, desktop file, AppStream id, macOS bundle
# identifier. Installed file names derive from it (resources/linux/).
set(QRAW_APP_ID "io.github.newmca.qraw")
# The macOS app bundle (the executable is Qraw inside it).
set(QRAW_MACOS_BUNDLE_DIR "Qraw.app")

find_package(Qt6 6.5 REQUIRED COMPONENTS Core Gui Widgets Svg PrintSupport Test LinguistTools)
find_package(ZLIB REQUIRED)

qt_standard_project_setup(REQUIRES 6.5)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(GNUInstallDirs)
include(QrawTargets)

include(CTest)

add_subdirectory(src)
include(Packaging) # after src/: its install rules run after the targets are installed

if(QRAW_BUILD_TOOLS)
    add_subdirectory(tools/icons)
    add_subdirectory(tools/fidelity)
endif()

if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
