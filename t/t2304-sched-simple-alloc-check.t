#!/bin/sh

test_description='check that sched-simple never double books resources'

# Append --logfile option if FLUX_TESTS_LOGFILE is set in environment:
test -n "$FLUX_TESTS_LOGFILE" && set -- "$@" --logfile
. $(dirname $0)/sharness.sh

test_under_flux 1

# Verify that alloc-check plugin works using alloc-bypass
test_expect_success 'load alloc-bypass and alloc-check plugins' '
	flux jobtap load alloc-bypass.so &&
	flux jobtap load alloc-check.so
'
test_expect_success 'run an alloc-bypass sleep job' '
	flux submit \
	    -vvv \
	    --wait-event=start \
	    --setattr=alloc-bypass.R="$(flux R encode -r 0)" \
	    -n 1 \
	    sleep inf
'
test_expect_success 'a regular job fails with an alloc-check exception' '
	test_expect_code 1 \
	    run_timeout 30 flux submit --flags=waitable -vvv \
	    --wait-event=exception \
	    -N1 /bin/true >bypass.jobid
'
test_expect_success 'flux job wait says the job failed' '
	test_must_fail flux job wait -v $(cat bypass.jobid)
'
test_expect_success 'flux job attach says the job failed' '
	test_must_fail flux job attach -vE $(cat bypass.jobid)
'
test_expect_success 'flux job status says the job failed' '
	test_must_fail flux job status -v $(cat bypass.jobid)
'
test_expect_success 'flux jobs says the job failed' '
	flux job list-ids --wait-state=inactive $(cat bypass.jobid) >/dev/null &&
	flux jobs -no {result} $(cat bypass.jobid) > bypass.result &&
	test_debug "cat bypass.result" &&
	test "$(cat bypass.result)" = "FAILED"
'
test_expect_success 'clean up jobs' '
	flux cancel --all &&
	flux queue drain
'
test_expect_success 'unload plugins' '
	flux jobtap remove alloc-check.so &&
	flux jobtap remove alloc-bypass.so
'

# Check that sched-simple doesn't suffer from time limit issue like
#   flux-framework/flux-sched#1043
#
test_expect_success 'configure epilog with 2s delay' '
	flux config load <<-EOT &&
	[job-manager.epilog]
	per-rank = true
	command = [ "sleep", "2" ]
	EOT
	flux jobtap load perilog.so
'
test_expect_success 'load alloc-check plugin' '
	flux jobtap load alloc-check.so
'
test_expect_success 'submit consecutive jobs that exceed their time limit' '
	(for i in $(seq 3); \
	    do flux run -N1 -x -t1s sleep 30 || true; \
	done) 2>joberr
'
test_expect_success 'some jobs received timeout exception' '
	grep "job.exception type=timeout" joberr
'
test_expect_success 'no jobs received alloc-check exception' '
	test_must_fail grep "job.exception type=alloc-check" joberr
'
test_expect_success 'clean up jobs' '
	flux cancel --all &&
	flux queue drain
'
test_expect_success 'remove alloc-check plugin' '
	flux jobtap remove alloc-check.so
'
test_expect_success 'undo epilog config' '
	flux jobtap remove perilog.so &&
	flux config load </dev/null
'

test_done
