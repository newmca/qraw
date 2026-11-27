# qraw-bench: timings of the main operations on generated documents.
# See docs/BENCHMARKS.md. A plain executable, not a Qt Test; the CTest entry
# bench_smoke keeps it working and asserts no time budgets.
add_executable(qraw-bench main.cpp generator.cpp image_fixture.cpp operations.cpp paint_bench.cpp paint_report.cpp)
target_link_libraries(qraw-bench PRIVATE qraw_ui)
# The input helpers shared with the UI tests.
target_include_directories(qraw-bench PRIVATE "${PROJECT_SOURCE_DIR}/tests/ui/support")
qraw_configure_target(qraw-bench)
qraw_lean_test_binary(qraw-bench)

# The build type and commit are recorded in the output; the commit is the one
# at configure time.
execute_process(COMMAND git rev-parse --short HEAD
    WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
    OUTPUT_VARIABLE QRAW_BENCH_GIT_COMMIT
    OUTPUT_STRIP_TRAILING_WHITESPACE
    ERROR_QUIET)
if(NOT QRAW_BENCH_GIT_COMMIT)
    set(QRAW_BENCH_GIT_COMMIT unknown)
endif()
if(CMAKE_BUILD_TYPE)
    set(QRAW_BENCH_BUILD_TYPE "${CMAKE_BUILD_TYPE}")
else()
    set(QRAW_BENCH_BUILD_TYPE "$<CONFIG>")
endif()
target_compile_definitions(qraw-bench PRIVATE
    QRAW_BENCH_GIT_COMMIT="${QRAW_BENCH_GIT_COMMIT}"
    QRAW_BENCH_BUILD_TYPE="${QRAW_BENCH_BUILD_TYPE}"
    QRAW_TEST_TIMEOUT_SCALE=${QRAW_TEST_TIMEOUT_SCALE})

qraw_scaled_timeout(QRAW_BENCH_TIMEOUT 300)
add_test(NAME bench_smoke COMMAND qraw-bench --sizes 1000 --repeat 1)
set_tests_properties(bench_smoke PROPERTIES
    LABELS bench
    ENVIRONMENT "${QRAW_TEST_ENVIRONMENT}"
    TIMEOUT ${QRAW_BENCH_TIMEOUT})

# The paint benchmark as a gate: exact work-counter ceilings and loose p95
# budgets (paint_budgets.h; QRAW_BENCH_TIME_BUDGETS=off skips the p95 budgets,
# as ci.yml does on hosted runners). Reduced frame counts keep it under about 20 s in
# dev; the numbers it checks are the same per frame.
add_test(NAME bench_paint
    COMMAND qraw-bench --paint-synthetic --repeat 1 --no-warmup --frame-scale 0.25 --check-budgets)
set_tests_properties(bench_paint PROPERTIES
    LABELS bench
    ENVIRONMENT "${QRAW_TEST_ENVIRONMENT}"
    TIMEOUT ${QRAW_BENCH_TIMEOUT})
