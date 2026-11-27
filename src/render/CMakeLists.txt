# Qt-free geometry shared by rendering and the M4 canvas: paths, perimeters,
# edge routing, label layout (D-020). Plain C++, enforced below.
qraw_glob_sources(geometry_sources ${CMAKE_CURRENT_SOURCE_DIR}/geometry)
add_library(qraw_geometry STATIC ${geometry_sources})
add_library(qraw::geometry ALIAS qraw_geometry)
set_target_properties(qraw_geometry PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(qraw_geometry PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/geometry/include)
target_link_libraries(qraw_geometry PUBLIC qraw_core)
qraw_configure_target(qraw_geometry)
qraw_forbid_qt(qraw_geometry)

# Rendering: style resolution, canvases (QPainter, SVG), shapes, stencils,
# labels, scene projection and export (docs/design/render.md).
qraw_glob_sources(render_sources ${CMAKE_CURRENT_SOURCE_DIR})
add_library(qraw_render STATIC ${render_sources})
add_library(qraw::render ALIAS qraw_render)
set_target_properties(qraw_render PROPERTIES AUTOMOC OFF)
target_include_directories(qraw_render PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(qraw_render
    PUBLIC qraw_core qraw_geometry qraw_format Qt6::Gui Qt6::Svg Qt6::Widgets)
qraw_configure_target(qraw_render)

# Vendored draw.io data (resources/, see THIRD_PARTY.md), compiled in under
# :/qraw/<path relative to resources/>. Globbed so data packages need no edit.
set(QRAW_RESOURCE_DIR "${PROJECT_SOURCE_DIR}/resources")
file(GLOB render_resources CONFIGURE_DEPENDS
    "${QRAW_RESOURCE_DIR}/styles/*.xml"
    "${QRAW_RESOURCE_DIR}/stencils/*.bin"
    "${QRAW_RESOURCE_DIR}/stencils/*.tsv")
list(SORT render_resources)
if(render_resources)
    qraw_add_big_resources(qraw_render qraw_render_data
        PREFIX "/qraw"
        BASE "${QRAW_RESOURCE_DIR}"
        FILES ${render_resources})
endif()
