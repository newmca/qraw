# Every tst_*.cpp here becomes the test tst_diag_<name> (D-014): logging, crash report text and
# session markers. The crash handler itself is exercised through the real binary in tests/app.
qraw_add_module_tests(diag LIBRARIES qraw_diag)
