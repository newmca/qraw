find_package(Python3 REQUIRED COMPONENTS Interpreter)
# Developer tool, built only with -DQRAW_BUILD_TOOLS=ON and never installed:
# rasterises resources/icons/qraw.svg into the hicolor PNGs. Regeneration is
# documented in resources/README.md.
add_executable(qraw-render-icons render_icons.cpp)
target_link_libraries(qraw-render-icons PRIVATE Qt6::Gui Qt6::Svg)
qraw_configure_target(qraw-render-icons)

add_custom_target(regenerate_icons
    COMMAND qraw-render-icons "${PROJECT_SOURCE_DIR}/resources/icons" "${QRAW_APP_ID}"
    COMMAND Python3::Interpreter "${CMAKE_CURRENT_SOURCE_DIR}/pack_icons.py"
            "${PROJECT_SOURCE_DIR}/resources/icons" "${QRAW_APP_ID}"
    COMMENT "Regenerating resources/icons/hicolor, qraw.ico and qraw.icns"
    VERBATIM)
