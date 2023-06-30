#!/bin/sh

test_description='Test flux job exec job cleanup via SIGKILL'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

test_expect_success 'job-exec: reload module with short kill-timeout' '
	flux module reload job-exec kill-timeout=0.1s
'
test_expect_success 'job-exec: run test program that blocks SIGTERM' '
	id=$(flux submit --wait-event=start  -n 1 -o trap.out \
	    sh -c "trap \"echo got SIGTERM\" 15; \
	           flux kvs put pid=\$\$; \
	           sleep inf; sleep inf") &&
	ns=$(flux job namespace $id) &&
	pid=$(flux kvs get -WN ${ns} ${dir}.pid) &&
	test_debug "echo script running as pid=$pid"
'
test_expect_success 'job-exec: ensure cancellation kills job' '
	test_debug "echo Canceling $id" &&
	flux cancel $id &&
	test_debug "flux job attach -vEX $id || :" &&
	test_expect_code 137 flux job status $id &&
	test_must_fail ps -q $pid
'
test_done
