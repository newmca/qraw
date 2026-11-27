# QRAW_CORPUS_DIRS is a "|"-separated list of directories.
# Round-trip every file under tests/corpus/ and tests/compat/fixtures/.
# Files are discovered at test run time, so adding corpus files needs no
# reconfigure.
qraw_add_test(tst_compat_corpus SOURCES tst_corpus.cpp LIBRARIES qraw_format)
target_compile_definitions(tst_compat_corpus PRIVATE
    QRAW_CORPUS_DIRS="${PROJECT_SOURCE_DIR}/tests/corpus|${CMAKE_CURRENT_SOURCE_DIR}/fixtures"
    QRAW_SOURCE_DIR="${PROJECT_SOURCE_DIR}")
