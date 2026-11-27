# .drawio / .xml codec: lossless XML I/O, page compression, semantic comparison,
# diagrams embedded in PNG/SVG/PDF files.
qraw_glob_sources(format_sources ${CMAKE_CURRENT_SOURCE_DIR})
add_library(qraw_format STATIC ${format_sources})
add_library(qraw::format ALIAS qraw_format)
set_target_properties(qraw_format PROPERTIES AUTOMOC OFF)
target_include_directories(qraw_format PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(qraw_format
    PUBLIC qraw_core Qt6::Core
    PRIVATE ZLIB::ZLIB)
qraw_configure_target(qraw_format)
