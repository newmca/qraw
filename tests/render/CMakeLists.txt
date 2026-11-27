# Every tst_*.cpp here becomes the test tst_render_<name> (D-014). Shared
# helpers live in support/, fixtures in data/.
qraw_add_module_tests(render LIBRARIES qraw_render qraw_cli TIMEOUT 600)

# tst_render_font_dpi exports with child processes of the command-line tool.
if(TARGET tst_render_font_dpi)
    target_compile_definitions(tst_render_font_dpi PRIVATE QRAW_CLI_PATH="$<TARGET_FILE:qraw-cli>")
    add_dependencies(tst_render_font_dpi qraw-cli)
endif()
