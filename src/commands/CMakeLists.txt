# Commands, transactions and undo. Plain C++: no Qt (enforced in src/CMakeLists.txt).
# Sources are globbed (D-014) so that new files need no edit of this list.
qraw_glob_sources(commands_sources ${CMAKE_CURRENT_SOURCE_DIR})
add_library(qraw_commands STATIC ${commands_sources})
add_library(qraw::commands ALIAS qraw_commands)
set_target_properties(qraw_commands PROPERTIES AUTOMOC OFF AUTOUIC OFF AUTORCC OFF)
target_include_directories(qraw_commands PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
target_link_libraries(qraw_commands PUBLIC qraw_core)
qraw_configure_target(qraw_commands)
