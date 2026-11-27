# Document model. C++20 standard library only: no Qt (enforced in src/CMakeLists.txt).
add_library(qraw_core STATIC
    attributes.cpp
    cell.cpp
    diagnostic.cpp
    document.cpp
    geometry.cpp
    number.cpp
    page.cpp
    shared_value.cpp
    style.cpp
    xml_node.cpp
)
add_library(qraw::core ALIAS qraw_core)
set_target_properties(qraw_core PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(qraw_core PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
qraw_configure_target(qraw_core)
