#!/bin/sh
#

test_description='Test Caliper profiling support

Test Caliper support.'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. `dirname $0`/sharness.sh

# Check for built in caliper tests

if ! flux start --noexec --caliper-profile=thread-trace ; then
    skip_all='skipping Caliper tests, Flux not built with Caliper support'
    test_done
fi

test_expect_success '--caliper-profile works' '
	flux start --caliper-profile=thread-trace true
'

CALIPER_OUTPUT=$(echo *.cali)
test_expect_success 'caliper output file exists' '
	test -f "$CALIPER_OUTPUT"
'

which cali-query >/dev/null 2>&1 && test_set_prereq HAVE_CALI_QUERY
test_expect_success HAVE_CALI_QUERY 'caliper output file is readable by cali-query' '
	cali-query "$CALIPER_OUTPUT"
'

test_done
