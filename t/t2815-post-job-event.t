#!/bin/sh

test_description='Test flux post-job-event command'

. $(dirname $0)/sharness.sh

test_under_flux 1

test_expect_success 'run a test job' '
	JOBID=$(flux submit --wait-event=start sleep inf)
'
test_expect_success 'flux-post-job-event --help works' '
	flux post-job-event --help >help.out &&
	test_debug "cat help.out" &&
	grep "name of event to post" help.out
'
test_expect_success 'flux-post-job-event can post a simple event' '
	flux post-job-event $JOBID test &&
	flux job wait-event -t15 $JOBID test
'
test_expect_success 'flux-post-job-event can post an event with context' '
	flux post-job-event $JOBID test note=testing  &&
	flux job wait-event -m note=testing -t15 $JOBID test 
'
test_expect_success 'flux-post-job-event can post multiple context keys' '
	flux post-job-event $JOBID test note=test2 status=0  &&
	flux job wait-event -m note=test2 -t15 $JOBID test  &&
	flux job wait-event -m status=0 -t15 $JOBID test 
'
test_expect_success 'flux-post-job-event fails for invalid job' '
	test_must_fail flux post-job-event f123 test note="test event"
'
test_expect_success 'flux-post-job-event fails for inactive job' '
	flux cancel $JOBID &&
	flux job wait-event $JOBID clean &&
	test_must_fail flux post-job-event $JOBID test note="test event"
'
test_expect_success 'flux-post-job-event fails with invalid id' '
	test_must_fail flux post-job-event baz test
'
test_done
