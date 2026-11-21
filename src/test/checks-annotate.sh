#!/bin/bash
#
#  Post-process testsuite logs and outputs after a failure
#
#  Uses GH Workflow commands for GH Actions
#
#  If src/test/checks_run.sh recorded the tests this build was expected
#  to run in ci-expected-tests, also report expected tests that did not
#  run.  Builds that run a subset of the tests (e.g. --shard, --system)
#  or none at all (e.g. --unit-test-only) depend on this to avoid
#  reporting tests they never meant to run.
#
expected_tests=ci-expected-tests

error() {
    printf "::error::%s\n" "$*"
}
catfile() {
    if test -f "$1"; then
        printf "::group::%s\n" "$1"
        cat "$1"
        printf "::endgroup::\n"
    fi
}
catfile_error() {
    error "Found $1"
    catfile "$1"
}

#  Print the test script for test basename $1, e.g. ./t/t0001-basic.t
#   for ./t/t0001-basic, or ./t/python/t0001-handle.py for
#   ./t/python/t0001-handle.
test_script() {
    local ext
    for ext in t py; do
        if test -f "$1.$ext"; then
            echo "$1.$ext"
            return
        fi
    done
    echo "$1.t"
}

annotate_test_log() {
    #
    #  Look through test logfiles for various failure indicators and
    #   emit an annotation '::error::' to the logfile if found:
    #
    local test=$1
    local script=$(test_script $test)

    #  Emit an annotation for each failed test ('not ok')
    grep 'not ok' ${test}.log | while read line; do
        printf "::error file=%s::%s\n" "${script}" "${line}"
    done

    #  Emit an annotation for TAP ERROR lines:
    grep '^ERROR: ' ${test}.log | while read line; do
        printf "::error file=%s::%s\n" "${script}" "${line}"
    done

    #  Emit an annotation for chain-lint errors:
    grep '^error: bug in the test script' ${test}.log | while read line; do
        printf "::error file=%s::%s\n" "${script}" "${line}"
    done

    #  Emit an annotation for anything that looks like an ASan error:
    sed -n 's/==[0-9][0-9]*==ERROR: //p' ${test}.log | while read line; do
        printf "::error file=%s::%s\n" "${script}" "${line}"
    done
}

#
#  Check all testsuite *.trs files and check for results that
#   were not 'SKIP' or 'PASS':
#
logfile=$(mktemp)

errors=0
total=0
for trs in $(find . -name '*.trs'); do
    : $((total++))
    result=$(sed -n 's/^.*global-test-result: *//p' ${trs})
    if test "$result" != "PASS" -a "$result" != "SKIP"; then
        testbase=${trs%.trs}
        annotate_test_log $testbase >> $logfile
        catfile ${testbase}.output >> $logfile
        catfile ${testbase}.log >> $logfile
        : $((errors++))
    fi
done
if test $errors -gt 0; then
    printf "::warning::"
fi
printf "Found ${errors} errors from ${total} tests in testsuite\n"
cat $logfile
rm $logfile

#
#  Find and emit all *.asan.* files from test:
#
export -f catfile_error
export -f catfile
export -f error
find . -name '*.asan.*' -exec bash -c 'catfile_error "$1"' bash {} \;

#
#  Check for tests that left a .log or .output file but no .trs.
#   These were running when the job was interrupted, e.g. by a
#   timeout, or were run outside of make check, e.g. by inception.
#   The test scripts are found by their basename so that tests in
#   the src tree and in subdirectories of ./t are included.
#
incomplete=0
while read testbase; do
    test -f ${testbase}.trs && continue
    test -f ${testbase}.t -o -f ${testbase}.py || continue
    printf "::error file=%s::%s\n" "$(test_script $testbase)" \
        "test output found but no result (test failed or did not finish)"
    catfile ${testbase}.output
    catfile ${testbase}.log
    : $((incomplete++))
done < <(find . \( -name '*.log' -o -name '*.output' \) \
              -not -path '*/trash directory.*' \
         | sed 's/\.[^.]*$//' \
         | sort -u)
printf "Found ${incomplete} tests with output but no result\n"

#
#  Check for any expected tests that were not run:
#
if test -f $expected_tests; then
    expected=0
    missing=()
    while read script; do
        : $((expected++))
        testbase=${script%.*}
        test -f ${testbase}.trs \
            -o -f ${testbase}.log \
            -o -f ${testbase}.output \
            || missing+=($script)
    done <$expected_tests
    if test ${#missing[@]} -gt 0; then
        error "Detected ${#missing[@]} of ${expected} expected tests not run"
        printf "::group::tests not run\n"
        printf "%s\n" "${missing[@]}"
        printf "::endgroup::\n"
    else
        printf "No missing test runs detected\n"
    fi
else
    printf "No $expected_tests, skipping check for missing test runs\n"
fi

#  Do not fail this step: the job has already failed, and an error
#   here only adds a misleading annotation.
exit 0
