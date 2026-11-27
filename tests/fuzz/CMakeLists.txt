# Deterministic mutation fuzzer (D-028). A plain executable, not a Qt Test:
# see BUILDING.md "Fuzzing" for the CLI and the two CTest entries.
add_executable(qraw-fuzz main.cpp mutator.cpp seeds.cpp targets.cpp)
target_link_libraries(qraw-fuzz PRIVATE qraw_render Qt6::Gui)
target_compile_definitions(qraw-fuzz PRIVATE QRAW_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
# RecordingCanvas lives with the render tests.
target_include_directories(qraw-fuzz PRIVATE "${PROJECT_SOURCE_DIR}/tests/render")
qraw_configure_target(qraw-fuzz)
qraw_lean_test_binary(qraw-fuzz)

# Debug builds (dev, asan) are several times slower: scale the hang limit.
# Sanitizer builds additionally apply QRAW_TEST_TIMEOUT_SCALE.
if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    set(QRAW_FUZZ_HANG_MS 30000)
else()
    set(QRAW_FUZZ_HANG_MS 10000)
endif()
qraw_scaled_timeout(QRAW_FUZZ_HANG_MS ${QRAW_FUZZ_HANG_MS})
qraw_scaled_timeout(QRAW_FUZZ_TIMEOUT 600)

# Iteration counts per target, fixed so the run is the same everywhere; sized
# to finish well inside the budgets in BUILDING.md (release <= 30 s, dev <= 120 s).
set(QRAW_FUZZ_SHORT_ITERATIONS 10000)

add_test(NAME fuzz_regressions
    COMMAND qraw-fuzz --replay "${CMAKE_CURRENT_SOURCE_DIR}/regressions" --hang-ms ${QRAW_FUZZ_HANG_MS})
add_test(NAME fuzz_short
    COMMAND qraw-fuzz --target all --seed 1 --iterations ${QRAW_FUZZ_SHORT_ITERATIONS} --hang-ms ${QRAW_FUZZ_HANG_MS})
set_tests_properties(fuzz_regressions fuzz_short PROPERTIES
    LABELS fuzz
    ENVIRONMENT "${QRAW_TEST_ENVIRONMENT}"
    TIMEOUT ${QRAW_FUZZ_TIMEOUT})
