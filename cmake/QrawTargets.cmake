# Shared helpers for Qraw targets.

# Applies the project's warning flags and include layout to a library target
# whose public headers live in include/qraw/<module>/.
function(qraw_configure_target target)
    if(MSVC)
        target_compile_options(${target} PRIVATE /W4 /permissive- /utf-8)
        if(QRAW_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE /WX)
        endif()
    else()
        target_compile_options(${target} PRIVATE
            -Wall -Wextra -Wpedantic -Wshadow -Wnon-virtual-dtor -Woverloaded-virtual)
        if(QRAW_WARNINGS_AS_ERRORS)
            target_compile_options(${target} PRIVATE -Werror)
        endif()
    endif()
endfunction()

# Makes a test-only executable cheap to build when QRAW_LEAN_TEST_BINARIES is ON (the RPM and DEB builds,
# which run the whole suite): no link-time optimisation, which the distributions switch on for everything
# (each of the ~160 test programs would be a code generation pass over all of Qraw, in a temporary
# directory of its own: the RPM build ran out of disk space linking qraw-bench), and no debug
# information, which the libraries' objects would otherwise bring into every program. The libraries and
# the installed programs are not touched. The objects of the libraries are "fat" (they carry the machine
# code next to the LTO form, -ffat-lto-objects in both distributions' flags), which a non-LTO link uses.
function(qraw_lean_test_binary target)
    if(QRAW_LEAN_TEST_BINARIES AND CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang" AND UNIX AND NOT APPLE)
        target_compile_options(${target} PRIVATE -fno-lto -g0)
        target_link_options(${target} PRIVATE -fno-lto -Wl,--strip-debug)
    endif()
endfunction()

# Compiles `FILES` into `target` as Qt big resources (rcc writes the data straight into an object file, no
# generated C++ array: the vendored stencils are tens of megabytes).
#   qraw_add_big_resources(<target> <resourcename> PREFIX <prefix> BASE <dir> FILES <files...>)
# rcc fills the object file in a second pass by patching the machine code the compiler produced in the first.
# An object compiled with link-time optimisation also carries the compiler's intermediate form, which is what
# the linker uses: the patched data is ignored and the resource is empty (the Fedora and Debian default
# `-flto` did exactly that). The object of the first pass is therefore compiled without LTO.
function(qraw_add_big_resources target name)
    cmake_parse_arguments(ARG "" "PREFIX;BASE" "FILES" ${ARGN})
    qt_add_resources(${target} ${name}
        PREFIX "${ARG_PREFIX}"
        BASE "${ARG_BASE}"
        BIG_RESOURCES
        FILES ${ARG_FILES})
    # Qt compiles the first pass in an object library of this name (OUTPUT_TARGETS does not return it).
    if(NOT MSVC AND TARGET ${target}_${name}_obj)
        target_compile_options(${target}_${name}_obj PRIVATE -fno-lto)
    endif()
endfunction()

# Fails configuration if `target` links any Qt library, directly or through
# its dependencies. core and commands must stay plain C++ (see ARCHITECTURE.md).
function(qraw_forbid_qt target)
    set(pending ${target})
    set(seen "")
    while(pending)
        list(POP_FRONT pending current)
        if(current IN_LIST seen OR NOT TARGET ${current})
            continue()
        endif()
        list(APPEND seen ${current})
        get_target_property(libs ${current} LINK_LIBRARIES)
        get_target_property(ilibs ${current} INTERFACE_LINK_LIBRARIES)
        foreach(lib IN LISTS libs ilibs)
            if(lib MATCHES "^Qt[0-9]*::")
                message(FATAL_ERROR "${target} must not depend on Qt, but ${current} links ${lib}")
            endif()
            if(lib AND NOT lib MATCHES "-NOTFOUND$")
                list(APPEND pending ${lib})
            endif()
        endforeach()
    endwhile()
endfunction()

# The environment of every CTest test. The tests run on Qt's offscreen platform,
# which has no native font database: on Linux it uses fontconfig, but on Windows
# it loads the font files found in QT_QPA_FONTDIR (default <Qt>/lib/fonts, which
# Qt no longer ships, so the database is empty and every glyph is a box). Text
# measurements then differ from every real platform (CI run 45896338083:
# tst_render_display_list, tst_render_level_of_detail, tst_render_text_layout_cache
# and tst_ui_arrange_actions failed on Windows only). Point it at the system fonts.
set(QRAW_TEST_ENVIRONMENT "QT_QPA_PLATFORM=offscreen")
if(WIN32)
    file(TO_CMAKE_PATH "$ENV{SystemRoot}" _qraw_system_root)
    if(NOT _qraw_system_root)
        set(_qraw_system_root "C:/Windows")
    endif()
    list(APPEND QRAW_TEST_ENVIRONMENT "QT_QPA_FONTDIR=${_qraw_system_root}/Fonts")
endif()

# Adds a Qt Test executable registered with CTest.
#   qraw_add_test(<name> SOURCES <files...> LIBRARIES <targets...>)
function(qraw_add_test name)
    cmake_parse_arguments(ARG "" "" "SOURCES;LIBRARIES" ${ARGN})
    add_executable(${name} ${ARG_SOURCES})
    target_link_libraries(${name} PRIVATE Qt6::Test ${ARG_LIBRARIES})
    qraw_configure_target(${name})
    qraw_lean_test_binary(${name})
    add_test(NAME ${name} COMMAND ${name})
    set_tests_properties(${name} PROPERTIES ENVIRONMENT "${QRAW_TEST_ENVIRONMENT}")
endfunction()

# Collects the C++ sources directly inside `dir` (not recursive) into `out`.
# Modules glob their sources so that new files need no edit of a shared
# CMakeLists.txt (DECISIONS.md D-014).
function(qraw_glob_sources out dir)
    file(GLOB sources CONFIGURE_DEPENDS "${dir}/*.cpp")
    list(SORT sources)
    set(${out} ${sources} PARENT_SCOPE)
endfunction()

# Sanitizer builds run several times slower. Every explicit test TIMEOUT (and
# the fuzzer's --hang-ms) and the time budgets of timing tests (tst_perf_bulk)
# are multiplied by this factor; the asan preset sets 3.
set(QRAW_TEST_TIMEOUT_SCALE 1 CACHE STRING
    "Multiplier applied to per-test TIMEOUT values and the fuzzer hang limit")

# Scales a timeout in seconds (or milliseconds) by QRAW_TEST_TIMEOUT_SCALE.
function(qraw_scaled_timeout out value)
    math(EXPR scaled "${value} * ${QRAW_TEST_TIMEOUT_SCALE}")
    set(${out} ${scaled} PARENT_SCOPE)
endfunction()

# Registers every tst_<name>.cpp in the current source directory as the Qt
# Test executable and CTest test `tst_<module>_<name>` (D-014).
#   qraw_add_module_tests(<module> LIBRARIES <targets...> [TIMEOUT <seconds>])
# Each test gets QRAW_SOURCE_DIR (repository root) and QRAW_TEST_DATA_DIR
# (<current dir>/data) as string macros, QRAW_TEST_TIMEOUT_SCALE as an integer, and the current directory on its
# include path for shared test helpers.
function(qraw_add_module_tests module)
    cmake_parse_arguments(ARG "" "TIMEOUT" "LIBRARIES" ${ARGN})
    file(GLOB tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tst_*.cpp")
    list(SORT tests)
    foreach(source IN LISTS tests)
        get_filename_component(stem "${source}" NAME_WE)
        string(REGEX REPLACE "^tst_" "" stem "${stem}")
        set(name "tst_${module}_${stem}")
        qraw_add_test(${name} SOURCES "${source}" LIBRARIES ${ARG_LIBRARIES})
        target_compile_definitions(${name} PRIVATE
            QRAW_SOURCE_DIR="${PROJECT_SOURCE_DIR}"
            QRAW_TEST_DATA_DIR="${CMAKE_CURRENT_SOURCE_DIR}/data"
            QRAW_TEST_TIMEOUT_SCALE=${QRAW_TEST_TIMEOUT_SCALE})
        target_include_directories(${name} PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
        if(ARG_TIMEOUT)
            qraw_scaled_timeout(timeout ${ARG_TIMEOUT})
            set_tests_properties(${name} PROPERTIES TIMEOUT ${timeout})
        endif()
    endforeach()
endfunction()

# Registers tests that exist already a second and third time under a scaled display, as
# <test>_dpr125 (QT_SCALE_FACTOR=1.25, the common fractional Plasma scale) and <test>_dpr2
# (QT_SCALE_FACTOR=2), both labelled "hidpi" (`ctest -L hidpi`). On the offscreen platform the
# factor sets the device pixel ratio of every window, so the tests that look at pixels or at
# decisions made in device pixels (tiles, overlays, thumbnails, level of detail) run through the
# fractional and the integer scaled paths in every preset and in CI, in seconds. QT_FONT_DPI is
# pinned to the offscreen platform's own 96: the ratios would otherwise multiply with one set in
# the caller's environment (120 and a factor of 2 make 2.5). Whole suites: BUILDING.md, "Scaled
# displays".
#   qraw_add_hidpi_variants(<existing test names...>)
function(qraw_add_hidpi_variants)
    set(variants "dpr125=1.25" "dpr2=2")
    foreach(test IN LISTS ARGN)
        get_test_property(${test} TIMEOUT timeout)
        foreach(variant IN LISTS variants)
            string(REPLACE "=" ";" parts "${variant}")
            list(GET parts 0 suffix)
            list(GET parts 1 factor)
            add_test(NAME ${test}_${suffix} COMMAND ${test})
            set_tests_properties(${test}_${suffix} PROPERTIES
                ENVIRONMENT "${QRAW_TEST_ENVIRONMENT};QT_FONT_DPI=96;QT_SCALE_FACTOR=${factor}"
                LABELS hidpi)
            if(timeout)
                set_tests_properties(${test}_${suffix} PROPERTIES TIMEOUT ${timeout})
            endif()
        endforeach()
    endforeach()
endfunction()
