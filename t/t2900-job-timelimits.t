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
		flux mini run --time-limit=${TIMEOUT}s sleep 30 2>limit1.err &&
	grep "resource allocation expired" limit1.err
'
test_expect_success HAVE_JQ 'job timelimits are propagated' '
	cat <<-EOF >limit.sh &&
	#!/bin/sh -e
	round() { printf "%.0f" \$1; }

	expiration=\$(flux kvs get resource.R | jq .execution.expiration)
	echo "expiration is \$expiration"

	id1=\$(flux mini submit --wait-event=start sleep 300)
	flux jobs -no "{id.f58} {expiration}"
	exp1=\$(flux jobs -no {expiration} \$id1)
	test "\$(round \$exp1)" = "\$(round \${expiration})"
	flux job cancelall -f

	id2=\$(flux mini submit --wait-event=start -t 1m sleep 300)
	flux jobs -no "{id.f58} {expiration}"
	exp2=\$(flux jobs -no {expiration} \$id2)
	test \$(round \${exp2}) -lt \$(round \${expiration})
        flux job cancelall -f
	EOF
	chmod +x limit.sh &&
	flux mini run --time-limit=10m flux start ./limit.sh
'
test_expect_success 'job may exit before time limit' '
        flux mini run --time-limit=5m sleep 0.25
'
test_expect_success 'result for expired jobs is TIMEOUT' '
	test_debug "flux jobs -a" &&
	flux jobs -ano {result} | grep -q TIMEOUT
'
sigalrm_sigkill_test() {
	scale=$1
	timeout=$2
	kill_timeout=$3
	test_debug "echo testing with timeout=$timeout kill_timeout=$kill_timeout" &&
	flux module reload job-exec kill-timeout=$kill_timeout &&
	ofile=trap.$scale.out &&
	test_must_fail_or_be_terminated \
           flux mini run -vvv --time-limit=${timeout}s bash -xc \
               "trap \"echo got SIGALRM>>$ofile\" SIGALRM;sleep 10;sleep 15" \
                   > $ofile 2> trap.$scale.err &&
        test_debug "grep . trap.*" &&
        grep "resource allocation expired" trap.$scale.err &&
        grep "got SIGALRM" $ofile
}
test_expect_success 'expired jobs are sent SIGALRM, then SIGKILL' '
	kill_timeout=$(test -z "$FLUX_TEST_VALGRIND" && echo 0.25 || echo 3) &&
        for scale in 1 2 4 8; do
	    sigalrm_sigkill_test \
	      $scale \
	      $(perl -E "say $TIMEOUT*$scale") \
	      $(perl -E "say $kill_timeout*$scale") && break
	done
'
expired_cancel_test() {
	id=$(flux mini submit --time-limit=${1}s bash -c \
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
