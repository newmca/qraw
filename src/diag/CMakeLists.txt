# Process-level diagnostics: logging categories, the log file, the crash handler and the session
# markers behind "Qraw closed unexpectedly". QtCore only, no widgets: the command-line tools use it
# too (D-049).
qraw_glob_sources(diag_sources ${CMAKE_CURRENT_SOURCE_DIR})
if(WIN32)
    list(FILTER diag_sources EXCLUDE REGEX "crash_handler_posix\\.cpp$")
else()
    list(FILTER diag_sources EXCLUDE REGEX "crash_handler_windows\\.cpp$")
endif()
add_library(qraw_diag STATIC ${diag_sources})
add_library(qraw::diag ALIAS qraw_diag)
set_target_properties(qraw_diag PROPERTIES AUTOMOC OFF)
target_include_directories(qraw_diag PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(qraw_diag PUBLIC Qt6::Core)
if(WIN32)
    target_link_libraries(qraw_diag PRIVATE dbghelp) # MiniDumpWriteDump (a system library)
endif()
qraw_configure_target(qraw_diag)

# The commit that is written into crash reports. Taken when CMake configures (a build from an
# exported tarball has none and says "unknown"); only build_info.cpp depends on it.
set(QRAW_GIT_COMMIT "unknown")
find_package(Git QUIET)
if(GIT_FOUND)
    execute_process(COMMAND "${GIT_EXECUTABLE}" rev-parse --short=12 HEAD
        WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
        OUTPUT_VARIABLE git_commit OUTPUT_STRIP_TRAILING_WHITESPACE
        RESULT_VARIABLE git_result ERROR_QUIET)
    if(git_result EQUAL 0 AND git_commit)
        set(QRAW_GIT_COMMIT "${git_commit}")
    endif()
endif()
set_source_files_properties(build_info.cpp PROPERTIES COMPILE_DEFINITIONS
    "QRAW_VERSION=\"${PROJECT_VERSION}\";QRAW_GIT_COMMIT=\"${QRAW_GIT_COMMIT}\"")
