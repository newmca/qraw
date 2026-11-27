#!/usr/bin/env bash
# CI failure diagnostics: re-runs the tests that failed in the last ctest run
# and prints everything they say. Qt Test output can be lost when it is
# captured by ctest (observed on Windows: tests failed with an empty log), so
# every failed test is also run directly, with the command and environment
# CTest has for it (Qt Test executables write their report to a file that is
# then printed).
#   usage: tools/ci/diagnose_failed_tests.sh <build-dir>
set -u
build_dir="${1:?usage: diagnose_failed_tests.sh <build-dir>}"
export QT_QPA_PLATFORM="${QT_QPA_PLATFORM:-offscreen}"
# Qt on Windows sends its messages to the debugger, not to a piped stderr, unless told otherwise.
export QT_FORCE_STDERR_LOGGING=1

failed_list="$build_dir/Testing/Temporary/LastTestsFailed.log"
if [ ! -f "$failed_list" ]; then
    echo "no $failed_list: ctest did not run (the failure is in configure or build)"
    exit 0
fi

# The re-run below rewrites LastTestsFailed.log (to the tests that still fail): keep the original list.
failed_names="$(cut -d: -f2 "$failed_list" | tr -d '\r')"

echo "::group::ctest --rerun-failed -V"
(cd "$build_dir" && ctest --rerun-failed --output-on-failure -V)
echo "::endgroup::"

# What CTest runs for each test (the Qt Test executable of tst_*, but also
# qraw-bench for bench_paint and qraw-fuzz for fuzz_*, which are not named
# like the test), read from CTest itself instead of guessing from the name.
json="$build_dir/diagnose-ctest-tests.json"
(cd "$build_dir" && ctest --show-only=json-v1) > "$json" 2>/dev/null
helper="$(dirname "$0")/ctest_test_command.cmake"

# LastTestsFailed.log lines look like "47:tst_ui_dialogs_settings".
# (tr above: the log has CRLF line ends on Windows.)
echo "$failed_names" | while read -r name; do
    [ -n "$name" ] || continue
    spec="$build_dir/diagnose-$name.cmd"
    if ! cmake -DJSON="$json" -DTEST="$name" -DOUT="$spec" -P "$helper"; then
        echo "$name: CTest gave no command for it"
        continue
    fi
    # $spec: the command, one element per line, then "--env" and NAME=VALUE lines, then "--cwd" and a directory.
    cmd=()
    envs=()
    cwd=""
    section=cmd
    while IFS= read -r line || [ -n "$line" ]; do
        line="${line%$'\r'}"
        case "$line" in
            --env) section=env ;;
            --cwd) section=cwd ;;
            *) case "$section" in
                cmd) cmd+=("$line") ;;
                env) envs+=("$line") ;;
                cwd) cwd="$line" ;;
            esac ;;
        esac
    done < "$spec"
    rm -f "$spec"
    [ -n "$cwd" ] && [ -d "$cwd" ] || cwd="$(dirname "${cmd[0]}")"
    exe="${cmd[0]}"
    base="$(basename "$exe")"; base="${base%.exe}"
    echo "::group::$name (direct run: ${cmd[*]})"
    if [ "${#cmd[@]}" -eq 1 ] && [ "$base" = "$name" ]; then
        # A Qt Test executable: -v2 and its report in a file, because ctest can lose its output.
        report="$cwd/$name.report.txt"
        rm -f "$report"
        (cd "$cwd" && env "${envs[@]}" "$exe" -v2 -o "$report,txt")
        echo "exit code: $?"
        cat "$report" 2>/dev/null || echo "(no report written: the process died before Qt Test started)"
    else
        # qraw-bench, qraw-fuzz, ...: run as CTest runs it; their output is the report.
        (cd "$cwd" && env "${envs[@]}" "${cmd[@]}")
        echo "exit code: $?"
    fi
    echo "::endgroup::"
done
rm -f "$json"
exit 0
