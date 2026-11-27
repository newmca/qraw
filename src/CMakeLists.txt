add_subdirectory(diag)
add_subdirectory(core)
add_subdirectory(commands)
add_subdirectory(format)
add_subdirectory(render)
add_subdirectory(ui)
add_subdirectory(cli)
add_subdirectory(app)

# Architecture contract: the model layers are plain C++.
qraw_forbid_qt(qraw_core)
qraw_forbid_qt(qraw_commands)
