#!/bin/sh

test_description='Test job begin-time dependencies'

. $(dirname $0)/sharness.sh

test_under_flux 1 job

flux setattr log-stderr-level 1

test_expect_success 'begin-time: submit a job with begin-time +1h' '
	DELAYED=$(flux mini submit --begin-time=+1h hostname) &&
	flux job wait-event -vt 15 $DELAYED dependency-add
'
test_expect_success 'begin-time: flux mini --begin-time=invalid fails' '
	test_expect_code 1 flux mini submit --begin-time=foo hostname &&
	test_expect_code 1 flux mini submit --begin-time=+a hostname
'
test_expect_success 'begin-time: rejects invalid timestamp' '
	test_expect_code 1 flux mini submit \
		--dependency=begin-time:-1.0 \
		hostname
'
test_expect_success HAVE_JQ 'begin-time rejects missing timestamp' '
	flux mini run --dry-run hostname | \
	  jq ".attributes.system.dependencies[0].scheme = \"begin-time\"" \
	  > invalid.json &&
	test_expect_code 1 flux job submit invalid.json
'
test_expect_success HAVE_JQ '--begin-time sets dependency in jobspec' '
	flux mini run --dry-run --begin-time=+1h hostname | \
	    jq -e ".attributes.system.dependencies[0].scheme == \"begin-time\""
'
test_expect_success 'begin-time: elapsed begin-time releases job immediately' '
	jobid=$(flux mini submit --begin-time=now hostname) &&
	flux job wait-event -vt 15 $jobid dependency-remove &&
	flux job wait-event -vt 15 $jobid clean
'
test_expect_success 'begin-time: job with begin-time works' '
	flux mini run -vvv --begin=time=+1s hostname
'
test_expect_success 'begin-time: job with begin-time=+1h is still in depend' '
	flux jobs &&
	test $(flux jobs -no {state} $DELAYED) = "DEPEND"
'
test_expect_success 'begin-time: job with begin-time can be safely canceled' '
	flux job cancel $DELAYED &&
	flux job wait-event -vt 15 $DELAYED clean
'
test_done
