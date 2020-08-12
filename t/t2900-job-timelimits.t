#!/bin/sh

test_description='Test job time limit functionality'

. $(dirname $0)/sharness.sh

test_under_flux 1

# Set CLIMain log level to logging.DEBUG (10), to enable stack traces
export FLUX_PYCLI_LOGLEVEL=10

# Default timeout, extend under valgrind or ASAN
TIMEOUT=0.25
test -n "$FLUX_TEST_VALGRIND" && TIMEOUT=3
test_have_prereq ASAN && TIMEOUT=3

test_expect_success 'job time limits are enforced' '
	test_expect_code 142 \
		flux mini run --time-limit=${TIMEOUT} sleep 30 2>limit1.err &&
	grep "resource allocation expired" limit1.err
'
test_expect_success 'job may exit before time limit' '
        flux mini run --time-limit=5m sleep 0.25
'
test_expect_success 'result for expired jobs is TIMEOUT' '
	test_debug "flux jobs -a" &&
	flux jobs -ano {result} | grep -q TIMEOUT
'
sigalrm_sigkill_test() {
	timeout=$1
	kill_timeout=$2
	test_debug "echo testing with timeout=$timeout kill_timeout=$kill_timeout" &&
	flux module reload job-exec kill-timeout=$kill_timeout &&
	        test_expect_code 137 \
            flux mini run -vvv --time-limit=${timeout} bash -c \
               "trap \"echo got SIGALRM>>trap.out\" SIGALRM;sleep 10;sleep 15" \
                   > trap.out 2> trap.err &&
        test_debug "grep . trap.*" &&
        grep "resource allocation expired" trap.err &&
        grep "got SIGALRM" trap.out
}
test_expect_success 'expired jobs are sent SIGALRM, then SIGKILL' '
	kill_timeout=$(test -z "$FLUX_TEST_VALGRIND" && echo 0.25 || echo 3) &&
        for scale in 1 2 4 8; do
	    sigalrm_sigkill_test \
	      $(perl -E "say $TIMEOUT*$scale") \
	      $(perl -E "say $kill_timeout*$scale") && break
	done
'
expired_cancel_test() {
	id=$(flux mini submit --time-limit=$1 bash -c \
            "trap \"echo got SIGALRM>>trap2.out\" SIGALRM;sleep 60;sleep 60" ) &&
	flux job wait-event --timeout=30 $id exception &&
	flux job cancel $id &&
	test_expect_code 143 run_timeout 30 flux job attach $id

}
test_expect_success 'expired job can also be canceled' '
	flux module reload job-exec kill-timeout=120 &&
	for scale in 1 4 8 16; do
	    expired_cancel_test $(perl -E "say $TIMEOUT*$scale") && break
	done
'
test_done
