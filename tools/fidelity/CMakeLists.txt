# Fidelity diff tooling (see tools/fidelity/README.md). Developer tools, built only
# with -DQRAW_BUILD_TOOLS=ON and never installed.
find_package(Qt6 REQUIRED COMPONENTS Test)

add_library(qraw-fidelity-score STATIC diff/fidelity_score.cpp)
target_include_directories(qraw-fidelity-score PUBLIC diff)
target_link_libraries(qraw-fidelity-score PUBLIC Qt6::Gui)
qraw_configure_target(qraw-fidelity-score)

add_executable(qraw-fidelity-diff diff/main.cpp)
target_link_libraries(qraw-fidelity-diff PRIVATE qraw-fidelity-score)
qraw_configure_target(qraw-fidelity-diff)

if(BUILD_TESTING)
    # Registered with CTest only when the tools are built, so the default test set is unchanged.
    qraw_add_test(tst_tools_fidelity_score SOURCES diff/selftest.cpp LIBRARIES qraw-fidelity-score)
endif()
