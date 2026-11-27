# Every tst_*.cpp here becomes the test tst_app_<name> (D-014). They run the built binaries.
qraw_add_module_tests(app LIBRARIES qraw_diag TIMEOUT 120)

# The crash test starts the real qraw and qraw-cli and provokes a crash with QRAW_DEBUG_CRASH.
add_dependencies(tst_app_crash_process qraw qraw-cli)
target_compile_definitions(tst_app_crash_process PRIVATE
    QRAW_APP_EXECUTABLE="$<TARGET_FILE:qraw>"
    QRAW_CLI_EXECUTABLE="$<TARGET_FILE:qraw-cli>")
