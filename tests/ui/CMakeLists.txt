# Every tst_*.cpp here becomes the test tst_ui_<name> (D-014). Shared helpers
# live in support/ (ui_test_support.h belongs to the design; packages add
# support/<package>_*.h), fixtures in data/<package>/. Tests run on the
# offscreen platform (qraw_add_test sets QT_QPA_PLATFORM).
qraw_add_module_tests(ui LIBRARIES qraw_ui TIMEOUT 300)

# The tests that depend on the device pixel ratio run again under QT_SCALE_FACTOR 1.25 and 2.
qraw_add_hidpi_variants(
    tst_ui_accessibility
    tst_ui_canvas_tiles
    tst_ui_canvas_tiles_threaded
    tst_ui_interaction_layer
    tst_ui_canvas_projection
    tst_ui_level_of_detail
    tst_ui_palette_thumbnail
    tst_ui_palette_dock
    tst_ui_shell_app
    tst_ui_select_resize)

# The id and the files that carry it must agree (tst_ui_app_identity reads them from the source tree).
target_compile_definitions(tst_ui_app_identity PRIVATE
    QRAW_APP_ID="${QRAW_APP_ID}"
    QRAW_LINUX_RESOURCE_DIR="${PROJECT_SOURCE_DIR}/resources/linux"
    QRAW_ICON_RESOURCE_DIR="${PROJECT_SOURCE_DIR}/resources/icons")
