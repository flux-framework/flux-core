#!/bin/bash
#
#  Post-process testsuite logs and outputs after a failure
#
#  Uses GH Workflow commands for GH Actions
#
error() {
    printf "::error::$@\n"
}
catfile() {
    if test -f $1; then
        printf "::group::$1\n"
        cat $1
        printf "::endgroup::\n"
    fi
}
catfile_error() {
    error "Found $1"
    catfile $1
}
annotate_test_log() {
    #
    #  Look through test logfiles for various failure indicators and
    #   emit an annotation '::error::' to the logfile if found:
    #
    local test=$1

    #  Emit an annotation for each failed test ('not ok')
    grep 'not ok' ${test}.log | while read line; do
        printf "::error file=${test}.t::%s\n" "${line}"
    done

    #  Emit an annotation for TAP ERROR lines:
    grep '^ERROR: ' ${test}.log | while read line; do
        printf "::error file=${test}.t::%s\n" "${line}"
    done

    #  Emit an annotation for chain-lint errors:
    grep '^error: bug in the test script' ${test}.log | while read line; do
        printf "::error file=${test}.t::%s\n" "${line}"
    done

    #  Emit an annotation for anything that looks like an ASan error:
    sed -n 's/==[0-9][0-9]*==ERROR: //p' ${test}.log | while read line; do
        printf "::error file=${test}.t::%s\n" "${line}"
    done
}

#
#  Check all testsuite *.trs files and check for results that
#   were not 'SKIP' or 'PASS':
#
logfile=/tmp/check-errors.$$
cat /dev/null >$logfile

errors=0
total=0
for trs in $(find . -name *.trs); do
    : $((total++))
    result=$(sed -n 's/^.*global-test-result: *//p' ${trs})
    if test "$result" != "PASS" -a "$result" != "SKIP"; then
        testbase=${trs//.trs}
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
find . -name *.asan.* | xargs '-I{}' bash -c 'catfile_error {}'


#
#  Check for any expected tests that were not run:
#
ls -1 t/*.t | sort >/tmp/expected
ls -1 t/*.trs | sed 's/rs$//' | sort >/tmp/actual
comm -23 /tmp/expected /tmp/actual > missing
if test -s missing; then
    error "Detected $(wc -l missing) missing tests:"
    for f in $(cat missing); do
        printf "$f\n"
        file=${f//.t}
        test -f ${file}.log && catfile ${file}.log
        test -f ${file}.output && catfile ${file}.output
    done
else
    printf "No missing test runs detected\n"
fi

