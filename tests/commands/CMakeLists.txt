# Every tst_*.cpp here becomes the test tst_commands_<name> (D-014). Tests may
# use format (Qt) to load and save corpus files; the library itself stays
# std-only. QRAW_CORPUS_DIRS is a "|"-separated list of directories.
qraw_add_module_tests(commands LIBRARIES qraw_commands qraw_format TIMEOUT 600)
file(GLOB commands_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tst_*.cpp")
foreach(source IN LISTS commands_tests)
    get_filename_component(stem "${source}" NAME_WE)
    string(REGEX REPLACE "^tst_" "" stem "${stem}")
    target_compile_definitions(tst_commands_${stem} PRIVATE
        QRAW_CORPUS_DIRS="${PROJECT_SOURCE_DIR}/tests/corpus|${PROJECT_SOURCE_DIR}/tests/compat/fixtures")
endforeach()
